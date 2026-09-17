#pragma once

#include "telemetry/common/RaceState.h"

#include <cstddef>
#include <deque>
#include <optional>
#include <vector>

namespace raceengineer {

struct LapRecord final {
    int lapNumber{0};
    double lapTimeSeconds{0.0};
    std::optional<double> fuelUsedLiters;
    std::optional<std::array<double, 3>> sectorsSeconds;
};

struct TrendSample final {
    std::chrono::steady_clock::time_point capturedAt{};
    std::optional<int> position;
    std::optional<double> gapAheadSeconds;
    std::optional<double> gapBehindSeconds;
    std::optional<WheelValues> tyreTemperaturesCelsius;
};

class RaceHistory final {
public:
    explicit RaceHistory(std::size_t maximumLaps = 100, std::size_t maximumTrendSamples = 900);

    void update(const RaceState& state);
    void reset();

    [[nodiscard]] const std::deque<LapRecord>& laps() const noexcept { return laps_; }
    [[nodiscard]] const std::deque<TrendSample>& trends() const noexcept { return trends_; }
    [[nodiscard]] std::vector<LapRecord> recentLaps(std::size_t count) const;
    [[nodiscard]] std::optional<double> averageFuelConsumption(std::size_t recentCount = 5) const;
    [[nodiscard]] std::optional<double> estimatedFuelLapsRemaining(const RaceState& state) const;
    [[nodiscard]] std::optional<double> estimatedFuelMargin(const RaceState& state) const;
    [[nodiscard]] std::optional<double> averageLapTime(std::size_t recentCount = 5) const;
    [[nodiscard]] std::optional<double> lapConsistency(std::size_t recentCount = 5) const;
    [[nodiscard]] std::optional<double> bestLap() const;
    // Negative means improving; positive means getting slower.
    [[nodiscard]] std::optional<double> recentLapTrend(std::size_t recentCount = 5) const;
    [[nodiscard]] std::optional<double> bestSector(std::size_t sectorIndex) const;

private:
    void addLap(const LapRecord& lap);
    void maybeAddTrend(const RaceState& state);

    std::deque<LapRecord> laps_;
    std::deque<TrendSample> trends_;
    std::size_t maximumLaps_;
    std::size_t maximumTrendSamples_;
    std::optional<int> observedLap_;
    std::optional<double> fuelAtLapStart_;
    std::optional<std::chrono::steady_clock::time_point> lastTrendAt_;
};

} // namespace raceengineer
