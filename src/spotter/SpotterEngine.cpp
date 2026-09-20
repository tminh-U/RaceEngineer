#include "spotter/SpotterEngine.h"

#include <cmath>

namespace raceengineer {
namespace {

constexpr double kMaxDistanceSquared = 400.0;  // 20m cutoff
constexpr double kMaxLongitudinalOverlap = 4.8; // ~GT3 car length (4.6m) + margin
constexpr double kMinLateralDistance = 1.2;     // Door rubbing distance
constexpr double kMaxLateralDistance = 4.2;     // 1 lane width alongside
constexpr int kEngageConsecutive = 2;           // ~100-200ms of consistent overlap
constexpr int kClearConsecutive = 3;            // ~150-300ms clear before declaring clear

} // namespace

SpotterEngine::SpotterEngine() = default;

void SpotterEngine::reset()
{
    leftEngaged_ = false;
    rightEngaged_ = false;
    leftConsecutive_ = 0;
    rightConsecutive_ = 0;
    leftClearConsecutive_ = 0;
    rightClearConsecutive_ = 0;
    lastCallout_ = {};
    lastType_ = EventType::ClearAll;
}

SpotterEngine::RelativeVector SpotterEngine::computeRelative(
    const std::array<double, 3>& playerPos, const double heading,
    const std::array<double, 3>& opponentPos)
{
    const double dx = opponentPos[0] - playerPos[0];
    const double dz = opponentPos[2] - playerPos[2];
    const double cosH = std::cos(heading);
    const double sinH = std::sin(heading);
    return {
        dx * cosH - dz * sinH,
        dx * sinH + dz * cosH,
        dx * dx + dz * dz
    };
}

std::vector<RaceEvent> SpotterEngine::process(const RaceState& state,
    const std::chrono::steady_clock::time_point now)
{
    if (!state.connected || !state.worldPosition || !state.heading
        || (state.opponents.empty() && !leftEngaged_ && !rightEngaged_)) {
        return {};
    }

    bool activeLeft = false;
    bool activeRight = false;

    for (const auto& opponent : state.opponents) {
        if (!opponent.worldPosition) continue;
        const auto rel = computeRelative(*state.worldPosition, *state.heading, *opponent.worldPosition);
        if (rel.distanceSquared > kMaxDistanceSquared) continue;

        if (std::abs(rel.longitudinal) <= kMaxLongitudinalOverlap) {
            if (rel.lateral <= -kMinLateralDistance && rel.lateral >= -kMaxLateralDistance) {
                activeLeft = true;
            } else if (rel.lateral >= kMinLateralDistance && rel.lateral <= kMaxLateralDistance) {
                activeRight = true;
            }
        }
    }

    if (activeLeft) {
        ++leftConsecutive_;
        leftClearConsecutive_ = 0;
    } else {
        leftConsecutive_ = 0;
        ++leftClearConsecutive_;
    }

    if (activeRight) {
        ++rightConsecutive_;
        rightClearConsecutive_ = 0;
    } else {
        rightConsecutive_ = 0;
        ++rightClearConsecutive_;
    }

    const bool shouldEngageLeft = leftConsecutive_ >= kEngageConsecutive;
    const bool shouldEngageRight = rightConsecutive_ >= kEngageConsecutive;
    const bool shouldClearLeft = leftClearConsecutive_ >= kClearConsecutive;
    const bool shouldClearRight = rightClearConsecutive_ >= kClearConsecutive;

    const bool prevLeft = leftEngaged_;
    const bool prevRight = rightEngaged_;

    if (shouldEngageLeft) leftEngaged_ = true;
    else if (shouldClearLeft) leftEngaged_ = false;

    if (shouldEngageRight) rightEngaged_ = true;
    else if (shouldClearRight) rightEngaged_ = false;

    std::vector<RaceEvent> events;

    if (leftEngaged_ && rightEngaged_) {
        if (!prevLeft || !prevRight || lastType_ != EventType::ThreeWide) {
            events.push_back(RaceEvent{
                EventType::ThreeWide,
                EventPriority::Spotter,
                "Kẹp ba, giữ làn.",
                now
            });
            lastType_ = EventType::ThreeWide;
            lastCallout_ = now;
        }
    } else if (leftEngaged_ && !rightEngaged_) {
        if (!prevLeft || (prevRight && lastType_ == EventType::ThreeWide)) {
            events.push_back(RaceEvent{
                EventType::CarLeft,
                EventPriority::Spotter,
                "Có xe bên trái.",
                now
            });
            lastType_ = EventType::CarLeft;
            lastCallout_ = now;
        }
    } else if (!leftEngaged_ && rightEngaged_) {
        if (!prevRight || (prevLeft && lastType_ == EventType::ThreeWide)) {
            events.push_back(RaceEvent{
                EventType::CarRight,
                EventPriority::Spotter,
                "Có xe bên phải.",
                now
            });
            lastType_ = EventType::CarRight;
            lastCallout_ = now;
        }
    } else if (!leftEngaged_ && !rightEngaged_) {
        lastType_ = EventType::ClearAll;
    }

    return events;
}

} // namespace raceengineer
