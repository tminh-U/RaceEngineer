#include "telemetry/acc/ACCTelemetryProvider.h"

#include "structed_file_ACC.h"

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
    case 0: return SessionType::Practice;
    case 1: return SessionType::Qualifying;
    case 2: return SessionType::Race;
    case 3: return SessionType::Hotlap;
    case 4: return SessionType::TimeAttack;
    case 5: return SessionType::Drift;
    case 6: return SessionType::Drag;
    case 7: return SessionType::Hotlap; // Hotstint
    case 8: return SessionType::Qualifying; // Superpole
    default: return SessionType::Unknown;
    }
}

FlagState flagState(const acc::SPageFileGraphic& graphics)
{
    if (graphics.globalRed != 0) return FlagState::Red;
    if (graphics.globalChequered != 0) return FlagState::Chequered;
    if (graphics.globalYellow != 0) return FlagState::Yellow;
    if (graphics.globalWhite != 0) return FlagState::White;
    if (graphics.globalGreen != 0) return FlagState::Green;
    switch (graphics.flag) {
    case 0: return FlagState::None;
    case 1: return FlagState::Blue;
    case 2: return FlagState::Yellow;
    case 3: return FlagState::Black;
    case 4: return FlagState::White;
    case 5: return FlagState::Chequered;
    case 7: return FlagState::Green;
    default: return FlagState::Unknown;
    }
}

} // namespace

bool ACCTelemetryProvider::start()
{
    stop();
    if (!physicsPage_.open(L"Local\\acpmf_physics", sizeof(acc::SPageFilePhysics))) {
        return false;
    }
    graphicsPage_.open(L"Local\\acpmf_graphics", sizeof(acc::SPageFileGraphic));
    staticPage_.open(L"Local\\acpmf_static", sizeof(acc::SPageFileStatic));
    broadcast_.start();

    state_ = {};
    state_.simulator = Simulator::AssettoCorsaCompetizione;
    state_.connected = true;
    connected_ = true;

    acc::SPageFileStatic staticData{};
    if (staticPage_.copyTo(staticData)) {
        const auto track = wideToUtf8(staticData.track);
        if (!track.empty()) {
            state_.track = track;
        }
        if (std::isfinite(staticData.maxFuel) && staticData.maxFuel > 0.0F) {
            state_.fuelCapacityLiters = staticData.maxFuel;
        }
    }
    return update();
}

void ACCTelemetryProvider::stop() noexcept
{
    physicsPage_.close();
    graphicsPage_.close();
    staticPage_.close();
    broadcast_.stop();
    playerCarId_ = -1;
    connected_ = false;
    state_.connected = false;
}

bool ACCTelemetryProvider::update()
{
    if (!connected_) {
        return false;
    }
    acc::SPageFilePhysics physics{};
    if (!copyStable(physicsPage_, physics)) {
        return false;
    }

    state_.capturedAt = std::chrono::steady_clock::now();
    state_.speedKmh = std::max(0.0F, physics.speedKmh);
    state_.rpm = std::max(0, physics.rpms);
    state_.gear = normalizeAcGear(physics.gear);
    state_.throttle = clampUnit(physics.gas);
    state_.brake = clampUnit(physics.brake);
    state_.clutch = clampUnit(physics.clutch);
    state_.steering = physics.steerAngle;
    state_.fuelLiters = std::max(0.0F, physics.fuel);
    state_.tyreTemperaturesCelsius = toWheelValues(physics.tyreCoreTemperature);
    state_.tyrePressuresPsi = toWheelValues(physics.wheelsPressure);
    state_.tyreWear = toWheelValues(physics.tyreWear);
    state_.brakeTemperaturesCelsius = toWheelValues(physics.brakeTemp);
    state_.damage = std::array<double, 5>{physics.carDamage[0], physics.carDamage[1],
        physics.carDamage[2], physics.carDamage[3], physics.carDamage[4]};
    state_.waterTemperatureCelsius = physics.waterTemp;
    state_.pitLimiter = physics.pitLimiterOn != 0;
    state_.tractionControl = physics.tc;
    state_.abs = physics.abs;

    acc::SPageFileGraphic graphics{};
    if (copyStable(graphicsPage_, graphics)) {
        playerCarId_ = graphics.playerCarID;
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
        if (graphics.iDeltaLapTime != 0) {
            const double delta = std::abs(static_cast<double>(graphics.iDeltaLapTime)) / 1000.0;
            state_.currentDeltaSeconds = graphics.isDeltaPositive != 0 ? delta : -delta;
        } else {
            state_.currentDeltaSeconds.reset();
        }
        if (graphics.gapAhead >= 0) {
            state_.gapAheadSeconds = static_cast<double>(graphics.gapAhead) / 1000.0;
        }
        if (graphics.gapBehind >= 0) {
            state_.gapBehindSeconds = static_cast<double>(graphics.gapBehind) / 1000.0;
        }
        state_.flag = flagState(graphics);
        state_.pitState = graphics.isInPit != 0 ? PitState::PitBox
            : graphics.isInPitLane != 0 ? PitState::PitLane : PitState::Track;
    }

    broadcast_.update();
    state_.opponents = broadcast_.opponents(playerCarId_);
    state_.opponentAhead.reset();
    state_.opponentBehind.reset();
    if (state_.position) {
        for (const auto& opponent : state_.opponents) {
            if (!opponent.position || opponent.driverName.empty()) continue;
            if (*opponent.position == *state_.position - 1) state_.opponentAhead = opponent.driverName;
            if (*opponent.position == *state_.position + 1) state_.opponentBehind = opponent.driverName;
        }
    }
    return true;
}

} // namespace raceengineer
