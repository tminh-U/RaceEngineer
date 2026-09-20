#include "telemetry/ac/ACTelemetryProvider.h"

#include "structed_file_AC.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace raceengineer {
namespace {

template <typename T>
bool copyStable(const WindowsSharedMemory& page, T& output)
{
    T first{};
    T second{};
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (!page.copyTo(first) || !page.copyTo(second)) {
            return false;
        }
        if (first.packetId == second.packetId
            && std::memcmp(&first, &second, sizeof(T)) == 0) {
            output = second;
            return true;
        }
    }
    return false;
}

template <std::size_t N>
std::string wideToUtf8(const wchar_t (&source)[N])
{
    std::size_t length = 0;
    while (length < N && source[length] != L'\0') {
        ++length;
    }
#ifdef _WIN32
    if (length == 0) {
        return {};
    }
    const int required = WideCharToMultiByte(CP_UTF8, 0, source, static_cast<int>(length),
        nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, source, static_cast<int>(length), result.data(),
        required, nullptr, nullptr);
    return result;
#else
    std::string result;
    result.reserve(length);
    for (std::size_t i = 0; i < length; ++i) {
        result.push_back(static_cast<char>(source[i] & 0x7f));
    }
    return result;
#endif
}

template <typename StaticPage>
std::string playerDisplayName(const StaticPage& page)
{
    const auto first = wideToUtf8(page.playerName);
    const auto surname = wideToUtf8(page.playerSurname);
    const auto nickname = wideToUtf8(page.playerNick);
    std::string result = first;
    if (!surname.empty()) {
        if (!result.empty()) result += ' ';
        result += surname;
    }
    return result.empty() ? nickname : result;
}

template <std::size_t N>
WheelValues toWheelValues(const float (&values)[N])
{
    static_assert(N == 4);
    return {values[0], values[1], values[2], values[3]};
}

std::optional<double> millisecondsToSeconds(int value)
{
    if (value <= 0) {
        return std::nullopt;
    }
    return static_cast<double>(value) / 1000.0;
}

SessionType sessionType(int value)
{
    switch (value) {
    case AC_PRACTICE: return SessionType::Practice;
    case AC_QUALIFY: return SessionType::Qualifying;
    case AC_RACE: return SessionType::Race;
    case AC_HOTLAP: return SessionType::Hotlap;
    case AC_TIME_ATTACK: return SessionType::TimeAttack;
    case AC_DRIFT: return SessionType::Drift;
    case AC_DRAG: return SessionType::Drag;
    default: return SessionType::Unknown;
    }
}

FlagState flagState(int value)
{
    switch (value) {
    case 0: return FlagState::None;
    case 1: return FlagState::Blue;
    case 2: return FlagState::Yellow;
    case 3: return FlagState::Black;
    case 4: return FlagState::White;
    case 5: return FlagState::Chequered;
    default: return FlagState::Unknown;
    }
}

} // namespace

bool ACTelemetryProvider::start()
{
    stop();
    if (!physicsPage_.open(L"Local\\acpmf_physics", sizeof(SPageFilePhysics))) {
        return false;
    }
    graphicsPage_.open(L"Local\\acpmf_graphics", sizeof(SPageFileGraphic));
    // The static page enriches telemetry, but physics alone is sufficient to connect.
    staticPage_.open(L"Local\\acpmf_static", sizeof(SPageFileStatic));
    extensionClient_.start();

    state_ = {};
    state_.simulator = Simulator::AssettoCorsa;
    state_.connected = true;
    connected_ = true;

    SPageFileStatic staticData{};
    if (staticPage_.copyTo(staticData)) {
        const auto track = wideToUtf8(staticData.track);
        if (!track.empty()) {
            state_.track = track;
        }
        const auto driver = playerDisplayName(staticData);
        if (!driver.empty()) state_.driverName = driver;
        if (std::isfinite(staticData.maxFuel) && staticData.maxFuel > 0.0F) {
            state_.fuelCapacityLiters = staticData.maxFuel;
        }
    }
    return update();
}

void ACTelemetryProvider::stop() noexcept
{
    physicsPage_.close();
    graphicsPage_.close();
    staticPage_.close();
    extensionClient_.stop();
    connected_ = false;
    state_.connected = false;
}

bool ACTelemetryProvider::update()
{
    if (!connected_) {
        return false;
    }
    SPageFilePhysics physics{};
    if (!copyStable(physicsPage_, physics)) {
        return false;
    }

    state_.capturedAt = std::chrono::steady_clock::now();
    state_.speedKmh = std::max(0.0F, physics.speedKmh);
    state_.rpm = std::max(0, physics.rpms);
    state_.gear = normalizeAcGear(physics.gear);
    state_.throttle = clampUnit(physics.gas);
    state_.brake = clampUnit(physics.brake);
    state_.steering = physics.steerAngle;
    state_.heading = physics.heading;
    state_.fuelLiters = std::max(0.0F, physics.fuel);
    state_.tyreTemperaturesCelsius = toWheelValues(physics.tyreCoreTemperature);
    state_.tyrePressuresPsi = toWheelValues(physics.wheelsPressure);
    state_.tyreWear = toWheelValues(physics.tyreWear);
    state_.damage = std::array<double, 5>{physics.carDamage[0], physics.carDamage[1],
        physics.carDamage[2], physics.carDamage[3], physics.carDamage[4]};
    state_.pitLimiter = physics.pitLimiterOn != 0;
    state_.tractionControl = physics.tc;
    state_.abs = physics.abs;

    SPageFileGraphic graphics{};
    if (copyStable(graphicsPage_, graphics)) {
        state_.worldPosition = std::array<double, 3>{
            graphics.carCoordinates[0],
            graphics.carCoordinates[1],
            graphics.carCoordinates[2]
        };
        state_.sessionType = sessionType(graphics.session);
        state_.currentLap = std::max(1, graphics.completedLaps + 1);
        if (graphics.position > 0) {
            state_.position = graphics.position;
        }
        state_.currentLapTimeSeconds = millisecondsToSeconds(graphics.iCurrentTime);
        state_.previousLapTimeSeconds = millisecondsToSeconds(graphics.iLastTime);
        state_.bestLapTimeSeconds = millisecondsToSeconds(graphics.iBestTime);
        if (std::isfinite(graphics.sessionTimeLeft) && graphics.sessionTimeLeft >= 0.0F) {
            state_.timeRemainingSeconds = graphics.sessionTimeLeft;
        }
        if (graphics.numberOfLaps > 0) {
            state_.totalLaps = graphics.numberOfLaps;
            state_.lapsRemaining = std::max(0, graphics.numberOfLaps - graphics.completedLaps);
        }
        state_.flag = flagState(graphics.flag);
        state_.pitState = graphics.isInPit != 0 ? PitState::PitBox
            : graphics.isInPitLane != 0 ? PitState::PitLane : PitState::Track;
    }

    // Process companion Python app extended telemetry if available
    extensionClient_.update();
    if (extensionClient_.hasData()) {
        state_.opponents = extensionClient_.opponents();
        if (extensionClient_.gapAhead()) state_.gapAheadSeconds = extensionClient_.gapAhead();
        if (extensionClient_.gapBehind()) state_.gapBehindSeconds = extensionClient_.gapBehind();
        if (extensionClient_.opponentAhead()) state_.opponentAhead = extensionClient_.opponentAhead();
        if (extensionClient_.opponentBehind()) state_.opponentBehind = extensionClient_.opponentBehind();
        if (extensionClient_.playerSectors()) state_.sectorTimesSeconds = extensionClient_.playerSectors();
        if (extensionClient_.brakeTemperatures()) state_.brakeTemperaturesCelsius = extensionClient_.brakeTemperatures();
    } else {
        state_.opponents.clear();
        state_.gapAheadSeconds.reset();
        state_.gapBehindSeconds.reset();
        state_.opponentAhead.reset();
        state_.opponentBehind.reset();
    }

    return true;
}

} // namespace raceengineer
