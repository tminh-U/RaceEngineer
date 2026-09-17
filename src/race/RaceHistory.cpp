#include "race/RaceHistory.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace raceengineer {

RaceHistory::RaceHistory(const std::size_t maximumLaps, const std::size_t maximumTrendSamples)
    : maximumLaps_(std::max<std::size_t>(1, maximumLaps))
    , maximumTrendSamples_(std::max<std::size_t>(1, maximumTrendSamples))
{
}

void RaceHistory::update(const RaceState& state)
{
    if (!state.connected) {
        return;
    }
    maybeAddTrend(state);

    if (!state.currentLap) {
        return;
    }
    if (!observedLap_) {
        observedLap_ = state.currentLap;
        fuelAtLapStart_ = state.fuelLiters;
        return;
    }
    if (*state.currentLap <= *observedLap_) {
        return;
    }

    if (state.previousLapTimeSeconds && *state.previousLapTimeSeconds > 0.0) {
        LapRecord completed;
        completed.lapNumber = *state.currentLap - 1;
        completed.lapTimeSeconds = *state.previousLapTimeSeconds;
        completed.sectorsSeconds = state.sectorTimesSeconds;
        if (fuelAtLapStart_ && state.fuelLiters && *fuelAtLapStart_ >= *state.fuelLiters) {
            completed.fuelUsedLiters = *fuelAtLapStart_ - *state.fuelLiters;
        }
        addLap(completed);
    }

    observedLap_ = state.currentLap;
    fuelAtLapStart_ = state.fuelLiters;
}

void RaceHistory::reset()
{
    laps_.clear();
    trends_.clear();
    observedLap_.reset();
    fuelAtLapStart_.reset();
    lastTrendAt_.reset();
}

std::vector<LapRecord> RaceHistory::recentLaps(const std::size_t count) const
{
    const auto actual = std::min(count, laps_.size());
    return {laps_.end() - static_cast<std::ptrdiff_t>(actual), laps_.end()};
}

std::optional<double> RaceHistory::averageFuelConsumption(const std::size_t recentCount) const
{
    double total = 0.0;
    std::size_t samples = 0;
    for (auto iterator = laps_.rbegin(); iterator != laps_.rend() && samples < recentCount; ++iterator) {
        if (iterator->fuelUsedLiters && *iterator->fuelUsedLiters > 0.0) {
            total += *iterator->fuelUsedLiters;
            ++samples;
        }
    }
    return samples == 0 ? std::nullopt : std::optional<double>(total / static_cast<double>(samples));
}

std::optional<double> RaceHistory::estimatedFuelLapsRemaining(const RaceState& state) const
{
    const auto average = averageFuelConsumption();
    if (!average || !state.fuelLiters || *average <= 0.0) {
        return std::nullopt;
    }
    return *state.fuelLiters / *average;
}

std::optional<double> RaceHistory::estimatedFuelMargin(const RaceState& state) const
{
    const auto estimated = estimatedFuelLapsRemaining(state);
    if (!estimated || !state.lapsRemaining) {
        return std::nullopt;
    }
    return *estimated - static_cast<double>(*state.lapsRemaining);
}

std::optional<double> RaceHistory::averageLapTime(const std::size_t recentCount) const
{
    if (laps_.empty() || recentCount == 0) {
        return std::nullopt;
    }
    const auto count = std::min(recentCount, laps_.size());
    const auto first = laps_.end() - static_cast<std::ptrdiff_t>(count);
    const double sum = std::accumulate(first, laps_.end(), 0.0,
        [](const double value, const LapRecord& lap) { return value + lap.lapTimeSeconds; });
    return sum / static_cast<double>(count);
}

std::optional<double> RaceHistory::lapConsistency(const std::size_t recentCount) const
{
    const auto average = averageLapTime(recentCount);
    if (!average) {
        return std::nullopt;
    }
    const auto count = std::min(recentCount, laps_.size());
    const auto first = laps_.end() - static_cast<std::ptrdiff_t>(count);
    double squaredError = 0.0;
    for (auto iterator = first; iterator != laps_.end(); ++iterator) {
        const double difference = iterator->lapTimeSeconds - *average;
        squaredError += difference * difference;
    }
    return std::sqrt(squaredError / static_cast<double>(count));
}

std::optional<double> RaceHistory::bestLap() const
{
    if (laps_.empty()) {
        return std::nullopt;
    }
    return std::min_element(laps_.begin(), laps_.end(), [](const auto& left, const auto& right) {
        return left.lapTimeSeconds < right.lapTimeSeconds;
    })->lapTimeSeconds;
}

std::optional<double> RaceHistory::recentLapTrend(const std::size_t recentCount) const
{
    const auto count = std::min(recentCount, laps_.size());
    if (count < 2) {
        return std::nullopt;
    }
    const auto first = laps_.end() - static_cast<std::ptrdiff_t>(count);
    const auto middle = first + static_cast<std::ptrdiff_t>(count / 2);
    const double early = std::accumulate(first, middle, 0.0,
        [](double sum, const LapRecord& lap) { return sum + lap.lapTimeSeconds; })
        / static_cast<double>(std::distance(first, middle));
    const double recent = std::accumulate(middle, laps_.end(), 0.0,
        [](double sum, const LapRecord& lap) { return sum + lap.lapTimeSeconds; })
        / static_cast<double>(std::distance(middle, laps_.end()));
    return recent - early;
}

std::optional<double> RaceHistory::bestSector(const std::size_t sectorIndex) const
{
    if (sectorIndex >= 3) {
        return std::nullopt;
    }
    std::optional<double> best;
    for (const auto& lap : laps_) {
        if (lap.sectorsSeconds && (*lap.sectorsSeconds)[sectorIndex] > 0.0
            && (!best || (*lap.sectorsSeconds)[sectorIndex] < *best)) {
            best = (*lap.sectorsSeconds)[sectorIndex];
        }
    }
    return best;
}

void RaceHistory::addLap(const LapRecord& lap)
{
    laps_.push_back(lap);
    while (laps_.size() > maximumLaps_) {
        laps_.pop_front();
    }
}

void RaceHistory::maybeAddTrend(const RaceState& state)
{
    constexpr auto sampleInterval = std::chrono::seconds(1);
    if (lastTrendAt_ && state.capturedAt - *lastTrendAt_ < sampleInterval) {
        return;
    }
    trends_.push_back({state.capturedAt, state.position, state.gapAheadSeconds,
        state.gapBehindSeconds, state.tyreTemperaturesCelsius});
    lastTrendAt_ = state.capturedAt;
    while (trends_.size() > maximumTrendSamples_) {
        trends_.pop_front();
    }
}

} // namespace raceengineer
