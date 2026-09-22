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
    [[nodiscard]] bool isThreeWide() const noexcept { return leftEngaged_ && rightEngaged_; }

    [[nodiscard]] static constexpr std::string_view unavailableReason() noexcept
    {
        return "No reliable 3D overlap coordinates available in active simulator.";
    }

private:
    struct RelativeVector {
        double lateral{0.0};      // < 0 is Left, > 0 is Right
        double longitudinal{0.0}; // > 0 is Ahead, < 0 is Behind
        double distanceSquared{0.0};
    };

    [[nodiscard]] static RelativeVector computeRelative(
        const std::array<double, 3>& playerPos, double heading,
        const std::array<double, 3>& opponentPos);

    bool leftEngaged_{false};
    bool rightEngaged_{false};
    std::chrono::steady_clock::time_point leftActiveSince_{};
    std::chrono::steady_clock::time_point rightActiveSince_{};
    std::chrono::steady_clock::time_point leftClearSince_{};
    std::chrono::steady_clock::time_point rightClearSince_{};

    std::chrono::steady_clock::time_point lastCallout_{};
    std::chrono::steady_clock::time_point lastLeftCallout_{};
    std::chrono::steady_clock::time_point lastRightCallout_{};
    std::chrono::steady_clock::time_point lastThreeWideCallout_{};
};

} // namespace raceengineer
