#pragma once

namespace raceengineer {

enum class RunningSimulator { None, AssettoCorsa, AssettoCorsaCompetizione };

// Takes a single process snapshot and gives ACC precedence if both are running.
[[nodiscard]] RunningSimulator detectRunningSimulator() noexcept;

} // namespace raceengineer
