#include "spotter/SpotterEngine.h"

#include <algorithm>
#include <cmath>

namespace raceengineer {
namespace {

using Vec3 = WorldPoint;

constexpr auto kEngageDuration = std::chrono::milliseconds{100};
constexpr auto kClearHoldDuration = std::chrono::milliseconds{250};
constexpr double kMaximumDistance = 20.0;
constexpr double kMaximumHeightDifference = 1.5;
constexpr double kWidthMargin = 0.20;
constexpr double kLengthMargin = 0.65;
constexpr double kLongitudinalWarningExpansion = 0.50;
constexpr double kExitHysteresis = 0.25;
constexpr double kMinimumHalfWidth = 0.50;
constexpr double kMaximumHalfWidth = 1.20;
constexpr double kMinimumHalfLength = 1.20;
constexpr double kMaximumHalfLength = 3.40;

Vec3 subtract(const Vec3& a, const Vec3& b)
{
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

Vec3 add(const Vec3& a, const Vec3& b)
{
    return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
}

Vec3 scale(const Vec3& value, const double factor)
{
    return {value[0] * factor, value[1] * factor, value[2] * factor};
}

double dot(const Vec3& a, const Vec3& b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0]};
}

double magnitude(const Vec3& value)
{
    return std::sqrt(dot(value, value));
}

bool finitePoint(const Vec3& value)
{
    return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
}

bool normalize(Vec3& value)
{
    const double length = magnitude(value);
    if (!std::isfinite(length) || length < 0.25) return false;
    value = scale(value, 1.0 / length);
    return true;
}

bool elapsed(const std::chrono::steady_clock::time_point now,
    const std::chrono::steady_clock::time_point since,
    const std::chrono::steady_clock::duration duration)
{
    return since != std::chrono::steady_clock::time_point{} && now - since >= duration;
}

struct CarShape {
    Vec3 position;
    Vec3 forward;
    Vec3 right;
    Vec3 up;
    double halfWidth;
    double halfLength;
};

bool makeCarShape(const Vec3& position, const WheelContactPoints& points, CarShape& result)
{
    if (!finitePoint(position)) return false;
    for (const auto& point : points) {
        if (!finitePoint(point)) return false;
    }

    const Vec3 front = scale(add(points[0], points[1]), 0.5);
    const Vec3 rear = scale(add(points[2], points[3]), 0.5);
    const Vec3 left = scale(add(points[0], points[2]), 0.5);
    const Vec3 right = scale(add(points[1], points[3]), 0.5);
    Vec3 forward = subtract(front, rear);
    Vec3 lateral = subtract(right, left);
    if (!normalize(forward) || !normalize(lateral)) return false;
    lateral = subtract(lateral, scale(forward, dot(lateral, forward)));
    if (!normalize(lateral)) return false;

    Vec3 up = cross(forward, lateral);
    if (!normalize(up)) return false;
    if (up[1] < 0.0) {
        up = scale(up, -1.0);
    }

    const double halfWidth = (magnitude(subtract(points[1], points[0]))
        + magnitude(subtract(points[3], points[2]))) * 0.25 + kWidthMargin;
    const double halfLength = magnitude(subtract(front, rear)) * 0.5 + kLengthMargin;
    if (halfWidth < kMinimumHalfWidth || halfWidth > kMaximumHalfWidth
        || halfLength < kMinimumHalfLength || halfLength > kMaximumHalfLength) return false;

    result = {position, forward, lateral, up, halfWidth, halfLength};
    return true;
}

bool overlapsAlongside(const CarShape& player, const CarShape& opponent,
    const double lateralWarningGap, const bool leftEngaged, const bool rightEngaged,
    bool& isRight)
{
    const Vec3 relative = subtract(opponent.position, player.position);
    if (dot(relative, relative) > kMaximumDistance * kMaximumDistance
        || std::abs(dot(relative, player.up)) > kMaximumHeightDifference
        || std::abs(dot(relative, opponent.up)) > kMaximumHeightDifference) return false;

    const double lateral = dot(relative, player.right);
    if (std::abs(lateral) < 0.25) return false;
    const double lateralExpansion = lateralWarningGap
        + ((lateral > 0.0 ? rightEngaged : leftEngaged) ? kExitHysteresis : 0.0);

    const auto overlapsOnAxis = [&](const Vec3& axis) {
        const double playerRadius = (player.halfLength + kLongitudinalWarningExpansion)
                * std::abs(dot(player.forward, axis))
            + (player.halfWidth + lateralExpansion) * std::abs(dot(player.right, axis));
        const double opponentRadius = opponent.halfLength * std::abs(dot(opponent.forward, axis))
            + opponent.halfWidth * std::abs(dot(opponent.right, axis));
        return std::abs(dot(relative, axis)) <= playerRadius + opponentRadius;
    };
    if (!overlapsOnAxis(player.forward) || !overlapsOnAxis(player.right)
        || !overlapsOnAxis(opponent.forward) || !overlapsOnAxis(opponent.right)) return false;

    isRight = lateral > 0.0;
    return true;
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
    usableOpponentCount_ = 0;
    hasInvalidGeometry_ = false;
}

void SpotterEngine::setLateralWarningGap(const double meters) noexcept
{
    lateralWarningGapMeters_ = std::clamp(meters, 0.5, 2.5);
}

std::vector<RaceEvent> SpotterEngine::process(const RaceState& state,
    const std::chrono::steady_clock::time_point now)
{
    usableOpponentCount_ = 0;
    hasInvalidGeometry_ = false;
    if (!state.connected || !state.spotterGeometryFresh || !state.spotterWorldPosition
        || !state.spotterWheelContactPoints
        || (state.pitState && *state.pitState != PitState::Track)) {
        reset();
        return {};
    }

    CarShape player;
    if (!makeCarShape(*state.spotterWorldPosition, *state.spotterWheelContactPoints, player)) {
        reset();
        hasInvalidGeometry_ = true;
        return {};
    }

    bool activeLeft = false;
    bool activeRight = false;
    for (const auto& opponent : state.spotterOpponents) {
        if (opponent.inPitLane || !opponent.worldPosition || !opponent.spotterWheelContactPoints) continue;
        CarShape other;
        if (!makeCarShape(*opponent.worldPosition, *opponent.spotterWheelContactPoints, other)) {
            hasInvalidGeometry_ = true;
            continue;
        }
        ++usableOpponentCount_;

        bool isRight = false;
        if (!overlapsAlongside(player, other,
                lateralWarningGapMeters_, leftEngaged_, rightEngaged_, isRight)) continue;
        if (isRight) activeRight = true;
        else activeLeft = true;
    }

    if (activeLeft) leftClearSince_ = {};
    else if (leftEngaged_ && leftClearSince_ == std::chrono::steady_clock::time_point{}) leftClearSince_ = now;
    if (activeRight) rightClearSince_ = {};
    else if (rightEngaged_ && rightClearSince_ == std::chrono::steady_clock::time_point{}) rightClearSince_ = now;
    if (activeLeft && !leftEngaged_ && leftActiveSince_ == std::chrono::steady_clock::time_point{}) leftActiveSince_ = now;
    else if (!activeLeft) leftActiveSince_ = {};
    if (activeRight && !rightEngaged_ && rightActiveSince_ == std::chrono::steady_clock::time_point{}) rightActiveSince_ = now;
    else if (!activeRight) rightActiveSince_ = {};

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

    std::vector<RaceEvent> events;
    if (leftEngaged_ && !previousLeft) {
        events.push_back({EventType::CarLeft, EventPriority::Spotter, "Có xe bên trái.", now});
    }
    if (rightEngaged_ && !previousRight) {
        events.push_back({EventType::CarRight, EventPriority::Spotter, "Có xe bên phải.", now});
    }
    return events;
}

} // namespace raceengineer
