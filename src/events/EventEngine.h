#pragma once

#include "telemetry/common/RaceState.h"

#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace raceengineer {

enum class EventType {
    FuelLow,
    FuelCritical,
    EngineHot,
    EngineCritical,
    YellowFlag,
    BlueFlag,
    GreenFlag,
    RedFlag,
    BlackFlag,
    WhiteFlag,
    ChequeredFlag,
    PitLimiterOn,
    PitLimiterOff,
    SessionStarted,
    NewBestLap,
    CarLeft,
    CarRight,
    ThreeWide,
    DamageDetected
};

enum class EventPriority { Conversation, Engineer, Important, Spotter, Critical };

struct RaceEvent final {
    EventType type;
    EventPriority priority;
    std::string message;
    std::chrono::steady_clock::time_point createdAt;
};

class EventEngine final {
public:
    EventEngine();

    [[nodiscard]] std::vector<RaceEvent> process(const RaceState& state,
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());
    void reset();
    void setCooldown(EventType type, std::chrono::milliseconds cooldown);

private:
    enum class FuelLevel { Unknown, Normal, Low, Critical };
    enum class EngineLevel { Unknown, Normal, Hot, Critical };

    void emitIfReady(std::vector<RaceEvent>& output, EventType type, EventPriority priority,
        std::string message, std::chrono::steady_clock::time_point now);

    FuelLevel fuelLevel_{FuelLevel::Unknown};
    EngineLevel engineLevel_{EngineLevel::Unknown};
    std::optional<FlagState> flag_;
    std::optional<bool> pitLimiter_;
    std::optional<bool> connected_;
    std::optional<double> bestLap_;
    std::optional<std::array<double, 5>> previousDamage_;
    std::optional<WheelValues> previousSuspensionDamage_;
    std::unordered_map<EventType, std::chrono::milliseconds> cooldowns_;
    std::unordered_map<EventType, std::chrono::steady_clock::time_point> lastEmitted_;
};

} // namespace raceengineer
