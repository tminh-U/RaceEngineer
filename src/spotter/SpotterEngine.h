#pragma once

#include "events/EventEngine.h"
#include "telemetry/common/RaceState.h"

#include <chrono>
#include <string_view>
#include <vector>

namespace raceengineer {

class SpotterEngine final {
public:
    SpotterEngine();

    [[nodiscard]] std::vector<RaceEvent> process(const RaceState& state,
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());
    void reset();

    [[nodiscard]] bool hasLeft() const noexcept { return leftEngaged_; }
    [[nodiscard]] bool hasRight() const noexcept { return rightEngaged_; }
    [[nodiscard]] static constexpr std::string_view unavailableReason() noexcept
    {
        return "Fresh wheel-position geometry is unavailable.";
    }

private:
    bool leftEngaged_{false};
    bool rightEngaged_{false};
    std::chrono::steady_clock::time_point leftActiveSince_{};
    std::chrono::steady_clock::time_point rightActiveSince_{};
    std::chrono::steady_clock::time_point leftClearSince_{};
    std::chrono::steady_clock::time_point rightClearSince_{};
    std::chrono::steady_clock::time_point lastLeftCallout_{};
    std::chrono::steady_clock::time_point lastRightCallout_{};
};

} // namespace raceengineer
