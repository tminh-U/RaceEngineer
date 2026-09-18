#include "events/EventEngine.h"

#include <algorithm>
#include <utility>

namespace raceengineer {

EventEngine::EventEngine()
{
    cooldowns_[EventType::FuelLow] = std::chrono::seconds(30);
    cooldowns_[EventType::FuelCritical] = std::chrono::seconds(15);
    cooldowns_[EventType::EngineHot] = std::chrono::seconds(30);
    cooldowns_[EventType::EngineCritical] = std::chrono::seconds(10);
    cooldowns_[EventType::YellowFlag] = std::chrono::seconds(5);
    cooldowns_[EventType::BlueFlag] = std::chrono::seconds(10);
}

std::vector<RaceEvent> EventEngine::process(const RaceState& state,
    const std::chrono::steady_clock::time_point now)
{
    std::vector<RaceEvent> events;
    if (!connected_) {
        connected_ = state.connected;
    } else if (*connected_ != state.connected) {
        emitIfReady(events, state.connected ? EventType::SessionStarted : EventType::SessionEnded,
            EventPriority::Engineer,
            state.connected ? "Đã kết nối với game." : "Đã ngắt kết nối game.", now);
        connected_ = state.connected;
    }
    if (!state.connected) return events;

    FuelLevel nextFuel = FuelLevel::Unknown;
    if (state.fuelLiters && state.fuelCapacityLiters && *state.fuelCapacityLiters > 0.0) {
        const double fraction = *state.fuelLiters / *state.fuelCapacityLiters;
        nextFuel = fraction <= 0.07 ? FuelLevel::Critical
            : fraction <= 0.15 ? FuelLevel::Low : FuelLevel::Normal;
    }
    if (nextFuel != FuelLevel::Unknown && nextFuel != fuelLevel_) {
        if (nextFuel == FuelLevel::Low) {
            emitIfReady(events, EventType::FuelLow, EventPriority::Important,
                "Nhiên liệu sắp hết.", now);
        } else if (nextFuel == FuelLevel::Critical) {
            emitIfReady(events, EventType::FuelCritical, EventPriority::Critical,
                "Nhiên liệu nguy cấp.", now);
        }
        fuelLevel_ = nextFuel;
    }

    const auto temperature = state.engineTemperatureCelsius
        ? state.engineTemperatureCelsius : state.waterTemperatureCelsius;
    EngineLevel nextEngine = EngineLevel::Unknown;
    if (temperature) {
        nextEngine = *temperature >= 120.0 ? EngineLevel::Critical
            : *temperature >= 110.0 ? EngineLevel::Hot : EngineLevel::Normal;
    }
    if (nextEngine != EngineLevel::Unknown && nextEngine != engineLevel_) {
        if (nextEngine == EngineLevel::Hot) {
            emitIfReady(events, EventType::EngineHot, EventPriority::Important,
                "Nhiệt độ động cơ cao.", now);
        } else if (nextEngine == EngineLevel::Critical) {
            emitIfReady(events, EventType::EngineCritical, EventPriority::Critical,
                "Động cơ quá nhiệt.", now);
        }
        engineLevel_ = nextEngine;
    }

    if (state.flag && (!flag_ || *flag_ != *state.flag)) {
        if (*state.flag == FlagState::Yellow) {
            emitIfReady(events, EventType::YellowFlag, EventPriority::Critical, "Cờ vàng.", now);
        } else if (*state.flag == FlagState::Blue) {
            emitIfReady(events, EventType::BlueFlag, EventPriority::Important, "Cờ xanh dương.", now);
        }
        flag_ = state.flag;
    }

    if (state.pitLimiter) {
        if (!pitLimiter_) {
            pitLimiter_ = state.pitLimiter;
        } else if (*pitLimiter_ != *state.pitLimiter) {
            emitIfReady(events, *state.pitLimiter ? EventType::PitLimiterOn : EventType::PitLimiterOff,
                EventPriority::Important,
                *state.pitLimiter ? "Đã bật giới hạn tốc độ pit."
                                  : "Đã tắt giới hạn tốc độ pit.", now);
            pitLimiter_ = state.pitLimiter;
        }
    }

    if (state.bestLapTimeSeconds && *state.bestLapTimeSeconds > 0.0) {
        if (bestLap_ && *state.bestLapTimeSeconds < *bestLap_ - 0.001) {
            emitIfReady(events, EventType::NewBestLap, EventPriority::Engineer,
                "Vòng chạy nhanh nhất mới.", now);
        }
        if (!bestLap_ || *state.bestLapTimeSeconds < *bestLap_) bestLap_ = state.bestLapTimeSeconds;
    }
    return events;
}

void EventEngine::reset()
{
    fuelLevel_ = FuelLevel::Unknown;
    engineLevel_ = EngineLevel::Unknown;
    flag_.reset();
    pitLimiter_.reset();
    connected_.reset();
    bestLap_.reset();
    lastEmitted_.clear();
}

void EventEngine::setCooldown(const EventType type, const std::chrono::milliseconds cooldown)
{
    cooldowns_[type] = std::max(cooldown, std::chrono::milliseconds::zero());
}

void EventEngine::emitIfReady(std::vector<RaceEvent>& output, const EventType type,
    const EventPriority priority, std::string message, const std::chrono::steady_clock::time_point now)
{
    const auto last = lastEmitted_.find(type);
    const auto cooldown = cooldowns_.find(type);
    if (last != lastEmitted_.end() && cooldown != cooldowns_.end()
        && now - last->second < cooldown->second) return;
    output.push_back({type, priority, std::move(message), now});
    lastEmitted_[type] = now;
}

} // namespace raceengineer
