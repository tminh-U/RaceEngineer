#pragma once

#include <optional>

namespace raceengineer {

enum class RunningSimulator { None, AssettoCorsa, AssettoCorsaCompetizione };

// Takes a single process snapshot and gives ACC precedence if both are running.
[[nodiscard]] RunningSimulator detectRunningSimulator() noexcept;
// Setup must distinguish an unknown process state from a confirmed stopped game.
[[nodiscard]] std::optional<RunningSimulator> inspectRunningSimulator() noexcept;

} // namespace raceengineer
