#include "telemetry/mock/MockTelemetryProvider.h"

#include <algorithm>
#include <cmath>

namespace raceengineer {

bool MockTelemetryProvider::start()
{
    startedAt_ = std::chrono::steady_clock::now();
    state_ = {};
    state_.simulator = Simulator::Mock;
    state_.connected = true;
    state_.track = "Spa-Francorchamps (Mock)";
    state_.driverName = "Mock Driver";
    state_.sessionType = SessionType::Race;
    state_.totalLaps = 12;
    state_.fuelCapacityLiters = 110.0;
    state_.opponentAhead = "Mock Driver A";
    state_.opponentBehind = "Mock Driver B";
    connected_ = true;
    return update();
}

void MockTelemetryProvider::stop() noexcept
{
    connected_ = false;
    state_.connected = false;
}

bool MockTelemetryProvider::update()
{
    if (!connected_) {
        return false;
    }

    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - startedAt_).count();
    constexpr double lapDuration = 90.0;
    const int lap = std::min(12, 1 + static_cast<int>(elapsed / lapDuration));
    const double lapTime = std::fmod(elapsed, lapDuration);
    const double wave = std::sin(elapsed * 0.7);

    state_.capturedAt = now;
    state_.sessionTimeSeconds = elapsed;
    state_.timeRemainingSeconds = std::max(0.0, 12.0 * lapDuration - elapsed);
    state_.currentLap = lap;
    state_.lapsRemaining = std::max(0, 12 - lap);
    state_.position = 4;
    state_.speedKmh = std::max(0.0, 182.0 + 38.0 * wave);
    state_.rpm = static_cast<int>(6200.0 + 1300.0 * std::abs(wave));
    state_.gear = std::clamp(4 + static_cast<int>(wave * 2.0), 1, 6);
    state_.throttle = clampUnit(0.78 + wave * 0.2);
    state_.brake = clampUnit(-wave * 0.55);
    state_.clutch = 0.0;
    state_.steering = std::sin(elapsed * 0.35) * 18.0;
    state_.currentLapTimeSeconds = lapTime;
    state_.previousLapTimeSeconds = lap > 1 ? std::optional<double>(90.43) : std::nullopt;
    state_.bestLapTimeSeconds = lap > 1 ? std::optional<double>(89.91) : std::nullopt;
    state_.currentDeltaSeconds = 0.13 * std::sin(elapsed * 0.12);
    state_.sectorTimesSeconds = std::array<double, 3>{30.12, 31.08, 28.71};
    state_.fuelLiters = std::max(0.0, 64.0 - elapsed * (2.58 / lapDuration));
    state_.tyreTemperaturesCelsius = WheelValues{84.2 + wave, 85.1 + wave, 81.8, 82.4};
    state_.tyrePressuresPsi = WheelValues{27.3, 27.4, 27.1, 27.2};
    state_.tyreWear = WheelValues{0.96, 0.95, 0.97, 0.97};
    state_.brakeTemperaturesCelsius = WheelValues{512.0, 526.0, 448.0, 453.0};
    state_.waterTemperatureCelsius = 91.0;
    state_.damage = std::array<double, 5>{0.0, 0.0, 0.0, 0.0, 0.0};
    state_.pitLimiter = std::fmod(elapsed, 180.0) > 170.0;
    state_.tractionControl = 3.0;
    state_.abs = 4.0;
    state_.flag = (std::fmod(elapsed, 75.0) > 70.0) ? FlagState::Yellow : FlagState::Green;
    state_.gapAheadSeconds = 1.42 + std::sin(elapsed * 0.08) * 0.2;
    state_.gapBehindSeconds = 2.83 + std::cos(elapsed * 0.06) * 0.25;
    state_.pitState = state_.pitLimiter.value() ? PitState::PitLane : PitState::Track;
    return true;
}

} // namespace raceengineer
