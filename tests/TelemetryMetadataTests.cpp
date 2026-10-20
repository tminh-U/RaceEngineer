#include "telemetry/ac/ACTelemetryProvider.h"
#include "telemetry/acc/ACCTelemetryProvider.h"

#include "structed_file_AC.h"
#include "structed_file_ACC.h"

#include <QCoreApplication>
#include <QStandardPaths>

#include <windows.h>

#include <chrono>
#include <cstddef>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

using namespace std::chrono_literals;

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

void requireMappingAbsent(const wchar_t* name)
{
    HANDLE existing = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
    if (existing != nullptr) {
        CloseHandle(existing);
        throw std::runtime_error("Refusing to touch an existing simulator mapping.");
    }
    const DWORD error = GetLastError();
    if (error != ERROR_FILE_NOT_FOUND) {
        throw std::runtime_error("Cannot safely check a simulator mapping; refusing to continue. Win32 error="
            + std::to_string(error));
    }
}

template <typename T>
class WritableMapping final {
public:
    explicit WritableMapping(const wchar_t* name)
    {
        requireMappingAbsent(name);
        SetLastError(ERROR_SUCCESS);
        handle_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
            static_cast<DWORD>(sizeof(T)), name);
        if (handle_ == nullptr) {
            throw std::runtime_error("CreateFileMappingW failed; Win32 error="
                + std::to_string(GetLastError()));
        }
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            CloseHandle(handle_);
            handle_ = nullptr;
            throw std::runtime_error("A simulator mapping appeared during setup; refusing to touch it.");
        }
        view_ = MapViewOfFile(handle_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(T));
        if (view_ == nullptr) {
            const DWORD error = GetLastError();
            CloseHandle(handle_);
            handle_ = nullptr;
            throw std::runtime_error("MapViewOfFile failed; Win32 error=" + std::to_string(error));
        }
        clear();
    }

    ~WritableMapping()
    {
        if (view_ != nullptr) UnmapViewOfFile(view_);
        if (handle_ != nullptr) CloseHandle(handle_);
    }

    WritableMapping(const WritableMapping&) = delete;
    WritableMapping& operator=(const WritableMapping&) = delete;

    T& value() { return *static_cast<T*>(view_); }
    void clear() { std::memset(view_, 0, sizeof(T)); }

private:
    HANDLE handle_{nullptr};
    void* view_{nullptr};
};

void requireAllSimulatorMapsAbsent()
{
    for (const wchar_t* name : {L"Local\\acpmf_physics", L"Local\\acpmf_graphics",
             L"Local\\acpmf_static"}) {
        requireMappingAbsent(name);
    }
}

template <std::size_t N>
void setWide(wchar_t (&target)[N], const wchar_t* value)
{
    std::wmemset(target, L'\0', N);
    std::size_t length = 0;
    while (length + 1 < N && value[length] != L'\0') {
        target[length] = value[length];
        ++length;
    }
}

std::string ascii(const wchar_t* value)
{
    std::string result;
    while (*value != L'\0') {
        require(*value >= 0 && *value <= 0x7f, "Test car identifiers must stay ASCII.");
        result.push_back(static_cast<char>(*value++));
    }
    return result;
}

template <typename StaticPage>
void setMetadata(StaticPage& page, const wchar_t* track, const wchar_t* car)
{
    std::memset(&page, 0, sizeof(page));
    setWide(page.track, track);
    setWide(page.carModel, car);
    setWide(page.playerName, L"Metadata");
    setWide(page.playerSurname, L"Test");
    page.maxFuel = 98.0F;
}

void waitForMetadataRefresh()
{
    // Providers intentionally refresh static/late graphics mappings once a second.
    std::this_thread::sleep_for(1100ms);
}

template <typename Provider, typename PhysicsPage, typename GraphicPage, typename StaticPage>
void exerciseProvider(const char* label, const wchar_t* modelKnown,
    const wchar_t* modelChanged, const wchar_t* modelUnknown)
{
    requireAllSimulatorMapsAbsent();

    // Physics is the only required initial mapping, and starts all-zero. Static
    // and graphics are deliberately absent so the provider must reopen them later.
    WritableMapping<PhysicsPage> physics(L"Local\\acpmf_physics");
    Provider provider;
    require(provider.start(), "Provider failed to connect from its zeroed physics mapping.");
    auto state = provider.getCurrentState();
    require(provider.isConnected() && state.connected, "Zeroed physics should still signal a connection.");
    require(state.speedKmh && *state.speedKmh == 0.0, "Initial zero physics was not preserved.");
    require(!state.track && !state.carModel, "Metadata appeared before either late mapping existed.");

    physics.value().packetId = 1;
    physics.value().speedKmh = 142.5F;
    physics.value().gear = 5;
    require(provider.update(), "Provider rejected live physics before metadata arrived.");
    state = provider.getCurrentState();
    require(state.speedKmh && *state.speedKmh == 142.5, "Live speed was lost while metadata was absent.");
    require(state.gear && *state.gear == 4, "Live gear was lost while metadata was absent.");

    WritableMapping<GraphicPage> graphics(L"Local\\acpmf_graphics");
    WritableMapping<StaticPage> metadata(L"Local\\acpmf_static");
    waitForMetadataRefresh();
    require(provider.update(), "Provider rejected zeroed late static/graphics mappings.");
    state = provider.getCurrentState();
    require(!state.track && !state.carModel && !state.carSubclass,
        "Zeroed late static mapping should remain empty.");

    graphics.value().position = 7;
    graphics.value().completedLaps = 3;
    graphics.value().iLastTime = 91000;
    require(provider.update(), "Provider rejected a populated late graphics mapping.");
    state = provider.getCurrentState();
    require(state.position && *state.position == 7, "Provider did not reopen/read delayed graphics mapping.");
    require(state.currentLap && *state.currentLap == 4, "Delayed graphics lap data was not read.");
    require(state.previousLapTimeSeconds && *state.previousLapTimeSeconds == 91.0,
        "Delayed graphics lap time was not read.");

    setMetadata(metadata.value(), L"ks_barcelona", modelKnown);
    waitForMetadataRefresh();
    require(provider.update(), "Provider rejected first populated static metadata.");
    state = provider.getCurrentState();
    require(state.track && *state.track == "ks_barcelona", "Late track metadata was not adopted.");
    require(state.carModel && *state.carModel == ascii(modelKnown),
        "Late car metadata was not adopted.");
    require(state.carSubclass && !state.carSubclass->empty(), "Known car subclass was not classified.");
    require(state.driverName && *state.driverName == "Metadata Test", "Late driver metadata was not adopted.");
    require(state.fuelCapacityLiters && *state.fuelCapacityLiters == 98.0,
        "Late fuel-capacity metadata was not adopted.");

    setMetadata(metadata.value(), L"ks_silverstone", modelChanged);
    waitForMetadataRefresh();
    require(provider.update(), "Provider rejected changed static metadata.");
    state = provider.getCurrentState();
    require(state.track && *state.track == "ks_silverstone", "Changed track metadata stayed stale.");
    require(state.carModel && *state.carModel == ascii(modelChanged),
        "Changed car metadata stayed stale.");

    setMetadata(metadata.value(), L"ks_silverstone", modelUnknown);
    waitForMetadataRefresh();
    require(provider.update(), "Provider rejected unknown car metadata.");
    state = provider.getCurrentState();
    require(state.carModel && *state.carModel == ascii(modelUnknown),
        "Unknown changed car metadata was not adopted.");
    require(!state.carSubclass, "Subclass from the previous car leaked into an unknown car.");

    metadata.clear();
    waitForMetadataRefresh();
    require(provider.update(), "Provider rejected a cleared static mapping.");
    state = provider.getCurrentState();
    require(!state.track && !state.carModel && !state.carCategory && !state.carSubclass
            && !state.driverName && !state.fuelCapacityLiters,
        "Empty static snapshot did not clear stale metadata.");

    setMetadata(metadata.value(), L"ks_monza", modelKnown);
    waitForMetadataRefresh();
    require(provider.update(), "Provider rejected metadata before reconnect.");
    require(provider.getCurrentState().track && *provider.getCurrentState().track == "ks_monza",
        "Metadata setup before reconnect failed.");
    provider.stop();
    require(!provider.isConnected(), "stop() did not clear the connection flag.");
    metadata.clear();
    physics.clear();
    physics.value().packetId = 2;
    physics.value().speedKmh = 51.0F;
    require(provider.start(), "Provider failed to reconnect from physics after stop().");
    state = provider.getCurrentState();
    require(state.connected && state.speedKmh && *state.speedKmh == 51.0,
        "Reconnect did not publish the new physics state.");
    require(!state.track && !state.carModel && !state.carCategory && !state.carSubclass
            && !state.driverName && !state.fuelCapacityLiters,
        "Metadata from the previous connection survived reconnect with an empty static page.");
    provider.stop();

    std::cout << "PASS " << label << " metadata refresh/reconnect\n";
}

} // namespace

int main(int argc, char* argv[])
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("RaceEngineer-TelemetryMetadataTests"));
    QCoreApplication::setOrganizationName(QStringLiteral("RaceEngineerTests"));
    try {
        exerciseProvider<raceengineer::ACTelemetryProvider, SPageFilePhysics,
            SPageFileGraphic, SPageFileStatic>("AC", L"ks_ferrari_488_gt3",
                L"ks_ferrari_488_gtb", L"raceengineer_unknown_car");
        exerciseProvider<raceengineer::ACCTelemetryProvider, acc::SPageFilePhysics,
            acc::SPageFileGraphic, acc::SPageFileStatic>("ACC", L"ferrari_296_gt3",
                L"porsche_992_gt3_r", L"raceengineer_unknown_car");
    } catch (const std::exception& error) {
        std::cerr << "FAIL TelemetryMetadataTests: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
