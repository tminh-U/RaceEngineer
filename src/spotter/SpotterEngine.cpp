#include "spotter/SpotterEngine.h"

#include <cmath>

namespace raceengineer {
namespace {

// GT3 / touring-car sized overlap zones, measured between car centres.
constexpr double kEngageLongitudinalOverlap = 4.0;
constexpr double kEngageLateralMin = 1.0;
constexpr double kEngageLateralMax = 3.8;
constexpr double kClearLongitudinalOverlap = 5.4;
constexpr double kClearLateralMin = 0.6;
constexpr double kClearLateralMax = 4.6;
constexpr double kMaxDistanceSquared = 400.0;
constexpr double kMinSpeedKmh = 15.0;

// Require several 10 Hz telemetry publications before changing state.
constexpr auto kEngageDuration = std::chrono::milliseconds{180};
constexpr auto kClearHoldDuration = std::chrono::milliseconds{1500};
constexpr auto kRepeatCooldown = std::chrono::seconds{8};
constexpr auto kMinCalloutInterval = std::chrono::milliseconds{2500};

bool validPoint(const std::array<double, 3>& point)
{
    return std::isfinite(point[0]) && std::isfinite(point[1]) && std::isfinite(point[2]);
}

bool elapsed(const std::chrono::steady_clock::time_point now,
    const std::chrono::steady_clock::time_point since,
    const std::chrono::steady_clock::duration duration)
{
    return since != std::chrono::steady_clock::time_point{} && now - since >= duration;
}

} // namespace

SpotterEngine::SpotterEngine() = default;

void SpotterEngine::reset()
{
    leftEngaged_ = false;
    rightEngaged_ = false;
    leftActiveSince_ = {};
    rightActiveSince_ = {};
    leftClearSince_ = {};
    rightClearSince_ = {};
    lastCallout_ = {};
    lastLeftCallout_ = {};
    lastRightCallout_ = {};
    lastThreeWideCallout_ = {};
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
    if (!state.connected) {
        reset();
        return {};
    }
    if (!state.worldPosition || !state.heading || !validPoint(*state.worldPosition)
        || !std::isfinite(*state.heading)) {
        return {};
    }
    if (state.opponents.empty() && !leftEngaged_ && !rightEngaged_) {
        return {};
    }
    if (state.speedKmh && *state.speedKmh < kMinSpeedKmh && !leftEngaged_ && !rightEngaged_) {
        return {};
    }

    bool activeLeft = false;
    bool activeRight = false;
    for (const auto& opponent : state.opponents) {
        if (!opponent.worldPosition || !validPoint(*opponent.worldPosition)) continue;

        const auto relative = computeRelative(*state.worldPosition, *state.heading,
            *opponent.worldPosition);
        if (relative.distanceSquared > kMaxDistanceSquared) continue;

        const double longitudinal = std::abs(relative.longitudinal);
        const double lateral = std::abs(relative.lateral);
        const bool left = relative.lateral < 0.0;
        const bool engaged = left ? leftEngaged_ : rightEngaged_;
        const bool alongside = longitudinal <= (engaged
            ? kClearLongitudinalOverlap : kEngageLongitudinalOverlap)
            && lateral >= (engaged ? kClearLateralMin : kEngageLateralMin)
            && lateral <= (engaged ? kClearLateralMax : kEngageLateralMax);

        if (left) activeLeft = activeLeft || alongside;
        else if (relative.lateral > 0.0) activeRight = activeRight || alongside;
    }

    if (activeLeft) {
        leftClearSince_ = {};
        if (!leftEngaged_ && leftActiveSince_ == std::chrono::steady_clock::time_point{}) {
            leftActiveSince_ = now;
        }
    } else {
        leftActiveSince_ = {};
        if (leftEngaged_ && leftClearSince_ == std::chrono::steady_clock::time_point{}) {
            leftClearSince_ = now;
        }
    }

    if (activeRight) {
        rightClearSince_ = {};
        if (!rightEngaged_ && rightActiveSince_ == std::chrono::steady_clock::time_point{}) {
            rightActiveSince_ = now;
        }
    } else {
        rightActiveSince_ = {};
        if (rightEngaged_ && rightClearSince_ == std::chrono::steady_clock::time_point{}) {
            rightClearSince_ = now;
        }
    }

    const bool previousLeft = leftEngaged_;
    const bool previousRight = rightEngaged_;
    if (!leftEngaged_ && elapsed(now, leftActiveSince_, kEngageDuration)) {
        leftEngaged_ = true;
        leftActiveSince_ = {};
    } else if (leftEngaged_ && elapsed(now, leftClearSince_, kClearHoldDuration)) {
        leftEngaged_ = false;
        leftClearSince_ = {};
    }
    if (!rightEngaged_ && elapsed(now, rightActiveSince_, kEngageDuration)) {
        rightEngaged_ = true;
        rightActiveSince_ = {};
    } else if (rightEngaged_ && elapsed(now, rightClearSince_, kClearHoldDuration)) {
        rightEngaged_ = false;
        rightClearSince_ = {};
    }

    const auto canCall = [now](const auto last, const auto cooldown) {
        return last == std::chrono::steady_clock::time_point{} || now - last >= cooldown;
    };
    const bool cadenceReady = canCall(lastCallout_, kMinCalloutInterval);
    std::vector<RaceEvent> events;

    // Escalation to two-sided overlap gets one unambiguous callout.
    if (events.empty() && leftEngaged_ && rightEngaged_ && (!previousLeft || !previousRight)
        && canCall(lastThreeWideCallout_, kRepeatCooldown)) {
        events.push_back({EventType::ThreeWide, EventPriority::Spotter,
            "Kẹp ba, giữ làn.", now});
        lastThreeWideCallout_ = now;
        lastCallout_ = now;
    } else if (events.empty() && leftEngaged_ && !rightEngaged_ && !previousLeft
        && cadenceReady && canCall(lastLeftCallout_, kRepeatCooldown)) {
        events.push_back({EventType::CarLeft, EventPriority::Spotter,
            "Có xe bên trái.", now});
        lastLeftCallout_ = now;
        lastCallout_ = now;
    } else if (events.empty() && !leftEngaged_ && rightEngaged_ && !previousRight
        && cadenceReady && canCall(lastRightCallout_, kRepeatCooldown)) {
        events.push_back({EventType::CarRight, EventPriority::Spotter,
            "Có xe bên phải.", now});
        lastRightCallout_ = now;
        lastCallout_ = now;
    }

    return events;
}

} // namespace raceengineer
