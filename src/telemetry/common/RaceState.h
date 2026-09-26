#pragma once

#include <array>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace raceengineer {

enum class Simulator { None, AssettoCorsa, AssettoCorsaCompetizione, Mock };
enum class SessionType { Unknown, Practice, Qualifying, Race, Hotlap, TimeAttack, Drift, Drag };
enum class FlagState { Unknown, None, Green, Yellow, Blue, Red, Black, Chequered, White };
enum class PitState { Unknown, Track, Entering, PitLane, PitBox, Exiting };

using WheelValues = std::array<double, 4>; // FL, FR, RL, RR

struct OpponentState final {
    int carId{-1};
    std::string driverName;
    std::string teamName;
    std::optional<int> raceNumber;
    std::optional<int> position;
    std::optional<int> classPosition;
    std::optional<int> trackPosition;
    std::optional<int> completedLaps;
    std::optional<double> speedKmh;
    std::optional<double> splinePosition;
    std::optional<double> currentLapTimeSeconds;
    std::optional<double> previousLapTimeSeconds;
    std::optional<double> bestLapTimeSeconds;
    std::optional<std::array<double, 3>> sectorTimesSeconds;
    std::vector<double> recentLapTimesSeconds;
    std::optional<std::array<double, 3>> worldPosition;
    bool inPitLane{false};
};

struct RaceState final {
    Simulator simulator{Simulator::None};
    bool connected{false};
    std::chrono::steady_clock::time_point capturedAt{};

    std::optional<std::string> track;
    std::optional<std::string> carModel;
    std::optional<std::string> carCategory;
    std::optional<std::string> carSubclass;
    // Driver name from the simulator static page.  It lets tools identify the
    // player when the player is the leader or when nearby opponent names exist.
    std::optional<std::string> driverName;
    std::optional<SessionType> sessionType;
    std::optional<double> sessionTimeSeconds;
    std::optional<double> timeRemainingSeconds;
    std::optional<int> currentLap;
    std::optional<int> totalLaps;
    std::optional<int> lapsRemaining;

    std::optional<int> position;
    std::optional<double> speedKmh;
    std::optional<int> rpm;
    std::optional<int> gear;
    std::optional<double> throttle;
    std::optional<double> brake;
    std::optional<double> clutch;
    std::optional<double> steering;
    std::optional<double> heading;
    std::optional<std::array<double, 3>> worldPosition;

    std::optional<double> currentLapTimeSeconds;
    std::optional<double> previousLapTimeSeconds;
    std::optional<double> bestLapTimeSeconds;
    std::optional<double> currentDeltaSeconds;
    std::optional<std::array<double, 3>> sectorTimesSeconds;

    std::optional<double> fuelLiters;
    std::optional<double> fuelCapacityLiters;

    std::optional<WheelValues> tyreTemperaturesCelsius;
    std::optional<WheelValues> tyrePressuresPsi;
    std::optional<WheelValues> tyreWear;
    std::optional<WheelValues> brakeTemperaturesCelsius;
    std::optional<double> engineTemperatureCelsius;
    std::optional<double> oilTemperatureCelsius;
    std::optional<double> waterTemperatureCelsius;
    std::optional<std::array<double, 5>> damage;
    // ACC-only per-wheel suspension damage, ordered FL, FR, RL, RR. AC's
    // shared-memory physics page does not expose an equivalent field.
    std::optional<WheelValues> suspensionDamage;
    std::optional<bool> pitLimiter;
    std::optional<double> tractionControl;
    std::optional<double> abs;

    std::optional<FlagState> flag;
    std::optional<double> gapAheadSeconds;
    std::optional<double> gapBehindSeconds;
    std::optional<std::string> opponentAhead;
    std::optional<std::string> opponentBehind;
    std::optional<PitState> pitState;
    std::vector<OpponentState> opponents;
};

[[nodiscard]] const char* simulatorName(Simulator simulator) noexcept;
[[nodiscard]] const char* sessionTypeName(SessionType session) noexcept;
[[nodiscard]] const char* flagName(FlagState flag) noexcept;
[[nodiscard]] const char* pitStateName(PitState state) noexcept;
[[nodiscard]] int normalizeAcGear(int sharedMemoryGear) noexcept;
[[nodiscard]] double clampUnit(double value) noexcept;

} // namespace raceengineer
