#include "llm/tools/ToolRegistry.h"

#include <QJsonDocument>

#include <algorithm>
#include <cmath>

namespace raceengineer {
namespace {

QJsonObject definition(const QString& name, const QString& description)
{
    return {{QStringLiteral("type"), QStringLiteral("function")},
        {QStringLiteral("function"), QJsonObject{{QStringLiteral("name"), name},
            {QStringLiteral("description"), description},
            {QStringLiteral("parameters"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                {QStringLiteral("properties"), QJsonObject{}},
                {QStringLiteral("additionalProperties"), false}}}}}};
}

QJsonObject definition(const QString& name, const QString& description,
    const QJsonObject& properties, const QJsonArray& required)
{
    return {{QStringLiteral("type"), QStringLiteral("function")},
        {QStringLiteral("function"), QJsonObject{{QStringLiteral("name"), name},
            {QStringLiteral("description"), description},
            {QStringLiteral("parameters"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                {QStringLiteral("properties"), properties},
                {QStringLiteral("required"), required},
                {QStringLiteral("additionalProperties"), false}}}}}};
}

QJsonArray wheels(const WheelValues& values)
{
    return {values[0], values[1], values[2], values[3]};
}

QString tyreTemperatureStatus(const double temperature)
{
    if (temperature >= 110.0) return QStringLiteral("overheating");
    if (temperature >= 95.0) return QStringLiteral("hot");
    if (temperature < 70.0) return QStringLiteral("cold");
    return QStringLiteral("normal");
}

QString brakeTemperatureStatus(const double temperature)
{
    if (temperature >= 850.0) return QStringLiteral("critical");
    if (temperature >= 650.0) return QStringLiteral("hot");
    if (temperature < 250.0) return QStringLiteral("cold");
    return QStringLiteral("normal");
}

std::optional<double> recentGapChange(const RaceHistory& history, const bool ahead)
{
    std::optional<double> first;
    std::optional<double> last;
    int samples = 0;
    for (auto iterator = history.trends().rbegin(); iterator != history.trends().rend() && samples < 10;
         ++iterator) {
        const auto& value = ahead ? iterator->gapAheadSeconds : iterator->gapBehindSeconds;
        if (!value) continue;
        if (!last) last = value;
        first = value;
        ++samples;
    }
    return first && last && samples >= 2 ? std::optional<double>(*last - *first) : std::nullopt;
}

QString lapTrendStatus(const std::optional<double>& trend)
{
    if (!trend) return QStringLiteral("unknown");
    if (*trend < -0.1) return QStringLiteral("improving");
    if (*trend > 0.1) return QStringLiteral("slowing");
    return QStringLiteral("stable");
}

QString consistencyStatus(const std::optional<double>& deviation)
{
    if (!deviation) return QStringLiteral("unknown");
    if (*deviation <= 0.3) return QStringLiteral("excellent");
    if (*deviation <= 0.8) return QStringLiteral("good");
    return QStringLiteral("inconsistent");
}

template <typename T>
void optionalNumber(QJsonObject& object, const QString& key, const std::optional<T>& value)
{
    if (value) object.insert(key, static_cast<double>(*value));
}

QJsonObject opponentJson(const OpponentState& opponent)
{
    QJsonObject result{{QStringLiteral("car_id"), opponent.carId},
        {QStringLiteral("driver"), QString::fromStdString(opponent.driverName)},
        {QStringLiteral("team"), QString::fromStdString(opponent.teamName)},
        {QStringLiteral("in_pit_lane"), opponent.inPitLane}};
    optionalNumber(result, QStringLiteral("race_number"), opponent.raceNumber);
    optionalNumber(result, QStringLiteral("position"), opponent.position);
    optionalNumber(result, QStringLiteral("class_position"), opponent.classPosition);
    optionalNumber(result, QStringLiteral("track_position"), opponent.trackPosition);
    optionalNumber(result, QStringLiteral("completed_laps"), opponent.completedLaps);
    optionalNumber(result, QStringLiteral("speed_kmh"), opponent.speedKmh);
    optionalNumber(result, QStringLiteral("current_lap_seconds"), opponent.currentLapTimeSeconds);
    optionalNumber(result, QStringLiteral("last_lap_seconds"), opponent.previousLapTimeSeconds);
    optionalNumber(result, QStringLiteral("best_lap_seconds"), opponent.bestLapTimeSeconds);
    if (!opponent.recentLapTimesSeconds.empty()) {
        QJsonArray recent;
        double total = 0.0;
        for (const double lap : opponent.recentLapTimesSeconds) {
            recent.append(lap);
            total += lap;
        }
        result.insert(QStringLiteral("recent_laps_seconds"), recent);
        result.insert(QStringLiteral("recent_average_seconds"),
            total / static_cast<double>(opponent.recentLapTimesSeconds.size()));
    }
    return result;
}

} // namespace

QJsonArray ToolRegistry::definitions() const
{
    return {
        definition(QStringLiteral("get_session_status"), QStringLiteral("Returns authoritative current simulator, track, session and remaining race status. Never infer missing fields.")),
        definition(QStringLiteral("get_position"), QStringLiteral("Returns authoritative driver position and available nearby opponent names.")),
        definition(QStringLiteral("get_leaderboard"), QStringLiteral("Returns the live ACC leaderboard, driver positions and lap pace.")),
        definition(QStringLiteral("get_driver_pace"), QStringLiteral("Returns live position, current/last/best/recent lap pace for a named ACC driver. Use the spoken driver name as the driver argument."),
            QJsonObject{{QStringLiteral("driver"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                {QStringLiteral("description"), QStringLiteral("Driver name or unambiguous part of the name")}}}},
            QJsonArray{QStringLiteral("driver")}),
        definition(QStringLiteral("get_gap_ahead"), QStringLiteral("Returns the gap ahead and a pre-calculated trend. closing means the gap is shrinking; falling_behind means it is growing. Do not reinterpret trend.")),
        definition(QStringLiteral("get_gap_behind"), QStringLiteral("Returns the gap behind and a pre-calculated trend. under_pressure means the car behind is closing; pulling_away means the gap is growing. Do not reinterpret trend.")),
        definition(QStringLiteral("get_fuel_status"), QStringLiteral("Returns authoritative pre-calculated fuel status. Do not recalculate. enough_fuel=true and fuel_status=surplus mean enough fuel to finish; spare_laps is non-negative. fuel_status=deficit means more fuel is required; missing_laps is non-negative.")),
        definition(QStringLiteral("get_tyre_status"), QStringLiteral("Returns authoritative tyre classifications and FL/FR/RL/RR measurements. Do not derive hot, cold, normal or overheating yourself.")),
        definition(QStringLiteral("get_brake_status"), QStringLiteral("Returns authoritative front/rear brake classifications and critical flag. Do not infer danger from temperatures.")),
        definition(QStringLiteral("get_engine_status"), QStringLiteral("Returns authoritative engine thermal status, overheating and critical flags. Do not reinterpret them.")),
        definition(QStringLiteral("get_damage_status"), QStringLiteral("Returns authoritative damage status and major_damage flag plus raw supported channels.")),
        definition(QStringLiteral("get_current_lap"), QStringLiteral("Current lap number, time and delta.")),
        definition(QStringLiteral("get_lap_times"), QStringLiteral("Returns lap times plus authoritative recent trend and consistency classifications.")),
        definition(QStringLiteral("get_recent_laps"), QStringLiteral("Returns recent laps with pre-calculated average, best, trend and consistency. Do not calculate them.")),
        definition(QStringLiteral("get_best_lap"), QStringLiteral("Best recorded lap.")),
        definition(QStringLiteral("get_sector_analysis"), QStringLiteral("Returns best sectors and, when available, an authoritative largest current loss sector. Do not calculate sector loss.")),
        definition(QStringLiteral("get_pit_status"), QStringLiteral("Pit state and pit limiter status.")),
        definition(QStringLiteral("get_flag_status"), QStringLiteral("Current race flag.")),
        definition(QStringLiteral("get_race_summary"), QStringLiteral("Compact session, position, fuel and lap summary."))};
}

QJsonObject ToolRegistry::execute(const QString& name, const RaceState& state,
    const RaceHistory& history, const QJsonObject& arguments) const
{
    if (name == QStringLiteral("get_session_status")) {
        if (!state.connected) return unavailable();
        QJsonObject result{{QStringLiteral("available"), true},
            {QStringLiteral("simulator"), QString::fromLatin1(simulatorName(state.simulator))}};
        if (state.track) result.insert(QStringLiteral("track"), QString::fromStdString(*state.track));
        if (state.sessionType) result.insert(QStringLiteral("session_type"),
            QString::fromLatin1(sessionTypeName(*state.sessionType)));
        optionalNumber(result, QStringLiteral("current_lap"), state.currentLap);
        optionalNumber(result, QStringLiteral("total_laps"), state.totalLaps);
        optionalNumber(result, QStringLiteral("laps_remaining"), state.lapsRemaining);
        optionalNumber(result, QStringLiteral("time_remaining_seconds"), state.timeRemainingSeconds);
        return result;
    }
    if (name == QStringLiteral("get_position")) {
        if (!state.position) return unavailable();
        QJsonObject result{{QStringLiteral("available"), true}, {QStringLiteral("position"), *state.position}};
        std::optional<std::string> ahead = state.opponentAhead;
        std::optional<std::string> behind = state.opponentBehind;
        for (const auto& opponent : state.opponents) {
            if (!opponent.position) continue;
            if (!ahead && *opponent.position == *state.position - 1) ahead = opponent.driverName;
            if (!behind && *opponent.position == *state.position + 1) behind = opponent.driverName;
        }
        if (ahead) result.insert(QStringLiteral("opponent_ahead"), QString::fromStdString(*ahead));
        if (behind) result.insert(QStringLiteral("opponent_behind"), QString::fromStdString(*behind));
        return result;
    }
    if (name == QStringLiteral("get_leaderboard")) {
        if (state.opponents.empty()) return unavailable();
        std::vector<const OpponentState*> ordered;
        ordered.reserve(state.opponents.size());
        for (const auto& opponent : state.opponents) ordered.push_back(&opponent);
        std::sort(ordered.begin(), ordered.end(), [](const auto* left, const auto* right) {
            return left->position.value_or(9999) < right->position.value_or(9999);
        });
        QJsonArray entries;
        for (const auto* opponent : ordered) entries.append(opponentJson(*opponent));
        return {{QStringLiteral("available"), true},
            {QStringLiteral("opponents"), entries},
            {QStringLiteral("opponent_count"), static_cast<int>(ordered.size())}};
    }
    if (name == QStringLiteral("get_driver_pace")) {
        const QString query = arguments.value(QStringLiteral("driver")).toString().trimmed();
        if (query.isEmpty() || state.opponents.empty()) return unavailable();
        std::vector<const OpponentState*> matches;
        for (const auto& opponent : state.opponents) {
            const QString driver = QString::fromStdString(opponent.driverName);
            if (driver.compare(query, Qt::CaseInsensitive) == 0) {
                matches = {&opponent};
                break;
            }
            if (driver.contains(query, Qt::CaseInsensitive)) matches.push_back(&opponent);
        }
        if (matches.size() != 1) {
            QJsonObject result = unavailable();
            result.insert(QStringLiteral("reason"), matches.empty()
                    ? QStringLiteral("driver_not_found") : QStringLiteral("ambiguous_driver"));
            QJsonArray names;
            for (const auto* match : matches) names.append(QString::fromStdString(match->driverName));
            if (!names.isEmpty()) result.insert(QStringLiteral("matches"), names);
            return result;
        }
        QJsonObject result = opponentJson(*matches.front());
        result.insert(QStringLiteral("available"), true);
        if (matches.front()->bestLapTimeSeconds && state.bestLapTimeSeconds) {
            result.insert(QStringLiteral("best_lap_delta_to_player_seconds"),
                *matches.front()->bestLapTimeSeconds - *state.bestLapTimeSeconds);
        }
        return result;
    }
    if (name == QStringLiteral("get_gap_ahead") || name == QStringLiteral("get_gap_behind")) {
        const bool ahead = name.endsWith(QStringLiteral("ahead"));
        const auto& gap = ahead ? state.gapAheadSeconds : state.gapBehindSeconds;
        if (!gap) return unavailable();
        QJsonObject result{{QStringLiteral("available"), true}, {QStringLiteral("gap_seconds"), *gap}};
        const auto change = recentGapChange(history, ahead);
        if (change) {
            result.insert(QStringLiteral("recent_change_seconds"), *change);
            result.insert(QStringLiteral("trend"), ahead
                    ? (*change < -0.05 ? QStringLiteral("closing")
                       : *change > 0.05 ? QStringLiteral("falling_behind") : QStringLiteral("stable"))
                    : (*change < -0.05 ? QStringLiteral("under_pressure")
                       : *change > 0.05 ? QStringLiteral("pulling_away") : QStringLiteral("stable")));
        } else {
            result.insert(QStringLiteral("trend"), QStringLiteral("unknown"));
        }
        return result;
    }
    if (name == QStringLiteral("get_fuel_status")) {
        if (!state.fuelLiters) return unavailable();
        QJsonObject result{{QStringLiteral("available"), true}, {QStringLiteral("fuel_liters"), *state.fuelLiters}};
        optionalNumber(result, QStringLiteral("fuel_capacity_liters"), state.fuelCapacityLiters);
        const auto average = history.averageFuelConsumption();
        const auto estimated = history.estimatedFuelLapsRemaining(state);
        const auto margin = history.estimatedFuelMargin(state);
        optionalNumber(result, QStringLiteral("average_liters_per_lap"), average);
        optionalNumber(result, QStringLiteral("estimated_laps_available"), estimated);
        optionalNumber(result, QStringLiteral("race_laps_remaining"), state.lapsRemaining);
        if (margin) {
            const bool enough = *margin >= 0.0;
            result.insert(QStringLiteral("calculation_available"), true);
            result.insert(QStringLiteral("enough_fuel"), enough);
            result.insert(QStringLiteral("fuel_status"), enough ? QStringLiteral("surplus")
                                                                 : QStringLiteral("deficit"));
            result.insert(enough ? QStringLiteral("spare_laps") : QStringLiteral("missing_laps"),
                std::abs(*margin));
        } else {
            result.insert(QStringLiteral("calculation_available"), false);
            result.insert(QStringLiteral("fuel_status"), QStringLiteral("unknown"));
        }
        return result;
    }
    if (name == QStringLiteral("get_tyre_status")) {
        if (!state.tyreTemperaturesCelsius && !state.tyrePressuresPsi && !state.tyreWear) return unavailable();
        QJsonObject result{{QStringLiteral("available"), true},
            {QStringLiteral("wheel_order"), QJsonArray{QStringLiteral("FL"), QStringLiteral("FR"), QStringLiteral("RL"), QStringLiteral("RR")}}};
        if (state.tyreTemperaturesCelsius) result.insert(QStringLiteral("temperatures_celsius"), wheels(*state.tyreTemperaturesCelsius));
        if (state.tyrePressuresPsi) result.insert(QStringLiteral("pressures_psi"), wheels(*state.tyrePressuresPsi));
        if (state.tyreWear) result.insert(QStringLiteral("wear"), wheels(*state.tyreWear));
        if (state.tyreTemperaturesCelsius) {
            const auto& temperatures = *state.tyreTemperaturesCelsius;
            const double front = (temperatures[0] + temperatures[1]) / 2.0;
            const double rear = (temperatures[2] + temperatures[3]) / 2.0;
            result.insert(QStringLiteral("fl_c"), temperatures[0]);
            result.insert(QStringLiteral("fr_c"), temperatures[1]);
            result.insert(QStringLiteral("rl_c"), temperatures[2]);
            result.insert(QStringLiteral("rr_c"), temperatures[3]);
            result.insert(QStringLiteral("front_status"), tyreTemperatureStatus(front));
            result.insert(QStringLiteral("rear_status"), tyreTemperatureStatus(rear));
            result.insert(QStringLiteral("overheating"),
                *std::max_element(temperatures.begin(), temperatures.end()) >= 110.0);
            result.insert(QStringLiteral("overall_status"), front > rear + 3.0
                    ? QStringLiteral("front_hotter_than_rear")
                    : rear > front + 3.0 ? QStringLiteral("rear_hotter_than_front")
                                         : QStringLiteral("balanced"));
        }
        return result;
    }
    if (name == QStringLiteral("get_brake_status")) {
        if (!state.brakeTemperaturesCelsius) return unavailable();
        const auto& temperatures = *state.brakeTemperaturesCelsius;
        const double front = (temperatures[0] + temperatures[1]) / 2.0;
        const double rear = (temperatures[2] + temperatures[3]) / 2.0;
        return {{QStringLiteral("available"), true}, {QStringLiteral("wheel_order"),
            QJsonArray{QStringLiteral("FL"), QStringLiteral("FR"), QStringLiteral("RL"), QStringLiteral("RR")}},
            {QStringLiteral("temperatures_celsius"), wheels(temperatures)},
            {QStringLiteral("front_status"), brakeTemperatureStatus(front)},
            {QStringLiteral("rear_status"), brakeTemperatureStatus(rear)},
            {QStringLiteral("critical"), *std::max_element(temperatures.begin(), temperatures.end()) >= 850.0}};
    }
    if (name == QStringLiteral("get_engine_status")) {
        if (!state.rpm && !state.engineTemperatureCelsius && !state.waterTemperatureCelsius) return unavailable();
        QJsonObject result{{QStringLiteral("available"), true}};
        optionalNumber(result, QStringLiteral("rpm"), state.rpm);
        optionalNumber(result, QStringLiteral("engine_temperature_celsius"), state.engineTemperatureCelsius);
        optionalNumber(result, QStringLiteral("oil_temperature_celsius"), state.oilTemperatureCelsius);
        optionalNumber(result, QStringLiteral("water_temperature_celsius"), state.waterTemperatureCelsius);
        const bool critical = (state.engineTemperatureCelsius && *state.engineTemperatureCelsius >= 125.0)
            || (state.waterTemperatureCelsius && *state.waterTemperatureCelsius >= 120.0)
            || (state.oilTemperatureCelsius && *state.oilTemperatureCelsius >= 140.0);
        const bool overheating = critical
            || (state.engineTemperatureCelsius && *state.engineTemperatureCelsius >= 110.0)
            || (state.waterTemperatureCelsius && *state.waterTemperatureCelsius >= 105.0)
            || (state.oilTemperatureCelsius && *state.oilTemperatureCelsius >= 125.0);
        result.insert(QStringLiteral("status"), critical ? QStringLiteral("critical")
            : overheating ? QStringLiteral("overheating") : QStringLiteral("normal"));
        result.insert(QStringLiteral("overheating"), overheating);
        result.insert(QStringLiteral("critical"), critical);
        return result;
    }
    if (name == QStringLiteral("get_damage_status")) {
        if (!state.damage) return unavailable();
        QJsonArray values;
        double maximum = 0.0;
        for (double value : *state.damage) { values.append(value); maximum = std::max(maximum, value); }
        const bool major = maximum >= 0.5;
        return {{QStringLiteral("available"), true}, {QStringLiteral("damage_channels"), values},
            {QStringLiteral("status"), major ? QStringLiteral("major")
                : maximum > 0.0 ? QStringLiteral("minor") : QStringLiteral("none")},
            {QStringLiteral("major_damage"), major}};
    }
    if (name == QStringLiteral("get_current_lap")) {
        if (!state.currentLap && !state.currentLapTimeSeconds) return unavailable();
        QJsonObject result{{QStringLiteral("available"), true}};
        optionalNumber(result, QStringLiteral("lap"), state.currentLap);
        optionalNumber(result, QStringLiteral("lap_time_seconds"), state.currentLapTimeSeconds);
        optionalNumber(result, QStringLiteral("delta_seconds"), state.currentDeltaSeconds);
        return result;
    }
    if (name == QStringLiteral("get_lap_times")) {
        if (!state.currentLapTimeSeconds && history.laps().empty()) return unavailable();
        QJsonObject result{{QStringLiteral("available"), true}};
        optionalNumber(result, QStringLiteral("current_seconds"), state.currentLapTimeSeconds);
        optionalNumber(result, QStringLiteral("previous_seconds"), state.previousLapTimeSeconds);
        optionalNumber(result, QStringLiteral("best_seconds"), history.bestLap());
        optionalNumber(result, QStringLiteral("average_recent_seconds"), history.averageLapTime());
        optionalNumber(result, QStringLiteral("consistency_stddev_seconds"), history.lapConsistency());
        optionalNumber(result, QStringLiteral("recent_trend_seconds"), history.recentLapTrend());
        result.insert(QStringLiteral("trend"), lapTrendStatus(history.recentLapTrend()));
        result.insert(QStringLiteral("consistency"), consistencyStatus(history.lapConsistency()));
        return result;
    }
    if (name == QStringLiteral("get_recent_laps")) {
        const auto laps = history.recentLaps(5);
        if (laps.empty()) return unavailable();
        QJsonArray values;
        for (const auto& lap : laps) {
            QJsonObject item{{QStringLiteral("lap"), lap.lapNumber},
                {QStringLiteral("time_seconds"), lap.lapTimeSeconds}};
            optionalNumber(item, QStringLiteral("fuel_used_liters"), lap.fuelUsedLiters);
            values.append(item);
        }
        QJsonObject result{{QStringLiteral("available"), true}, {QStringLiteral("laps"), values},
            {QStringLiteral("trend"), lapTrendStatus(history.recentLapTrend())},
            {QStringLiteral("consistency"), consistencyStatus(history.lapConsistency())}};
        optionalNumber(result, QStringLiteral("average_lap_seconds"), history.averageLapTime());
        optionalNumber(result, QStringLiteral("best_lap_seconds"), history.bestLap());
        return result;
    }
    if (name == QStringLiteral("get_best_lap")) {
        const auto best = history.bestLap();
        return best ? QJsonObject{{QStringLiteral("available"), true}, {QStringLiteral("best_lap_seconds"), *best}}
                    : unavailable();
    }
    if (name == QStringLiteral("get_sector_analysis")) {
        QJsonArray values;
        bool available = false;
        for (std::size_t index = 0; index < 3; ++index) {
            const auto value = history.bestSector(index);
            if (value) { values.append(*value); available = true; } else values.append(QJsonValue::Null);
        }
        if (!available) return unavailable();
        QJsonObject result{{QStringLiteral("available"), true},
            {QStringLiteral("best_sectors_seconds"), values},
            {QStringLiteral("trend"), QStringLiteral("best_sectors_only")}};
        if (state.sectorTimesSeconds) {
            int largestSector = 0;
            double largestLoss = 0.0;
            for (std::size_t index = 0; index < 3; ++index) {
                const auto best = history.bestSector(index);
                if (!best) continue;
                const double loss = (*state.sectorTimesSeconds)[index] - *best;
                if (loss > largestLoss) { largestLoss = loss; largestSector = static_cast<int>(index + 1); }
            }
            if (largestSector > 0) {
                result.insert(QStringLiteral("largest_loss_sector"), largestSector);
                result.insert(QStringLiteral("loss_seconds"), largestLoss);
                result.insert(QStringLiteral("trend"), QStringLiteral("current_loss"));
            } else {
                result.insert(QStringLiteral("trend"), QStringLiteral("on_best_sector_pace"));
            }
        }
        return result;
    }
    if (name == QStringLiteral("get_pit_status")) {
        if (!state.pitState && !state.pitLimiter) return unavailable();
        QJsonObject result{{QStringLiteral("available"), true}};
        if (state.pitState) result.insert(QStringLiteral("pit_state"), QString::fromLatin1(pitStateName(*state.pitState)));
        if (state.pitLimiter) result.insert(QStringLiteral("pit_limiter"), *state.pitLimiter);
        return result;
    }
    if (name == QStringLiteral("get_flag_status")) {
        return state.flag ? QJsonObject{{QStringLiteral("available"), true},
                                {QStringLiteral("flag"), QString::fromLatin1(flagName(*state.flag))}}
                          : unavailable();
    }
    if (name == QStringLiteral("get_race_summary")) {
        if (!state.connected) return unavailable();
        QJsonObject result{{QStringLiteral("available"), true},
            {QStringLiteral("simulator"), QString::fromLatin1(simulatorName(state.simulator))}};
        optionalNumber(result, QStringLiteral("position"), state.position);
        optionalNumber(result, QStringLiteral("lap"), state.currentLap);
        optionalNumber(result, QStringLiteral("laps_remaining"), state.lapsRemaining);
        optionalNumber(result, QStringLiteral("fuel_liters"), state.fuelLiters);
        optionalNumber(result, QStringLiteral("best_lap_seconds"), history.bestLap());
        const auto fuel = execute(QStringLiteral("get_fuel_status"), state, history);
        if (fuel.value(QStringLiteral("calculation_available")).toBool()) {
            result.insert(QStringLiteral("enough_fuel"), fuel.value(QStringLiteral("enough_fuel")));
            result.insert(QStringLiteral("fuel_status"), fuel.value(QStringLiteral("fuel_status")));
            if (fuel.contains(QStringLiteral("spare_laps")))
                result.insert(QStringLiteral("spare_laps"), fuel.value(QStringLiteral("spare_laps")));
            if (fuel.contains(QStringLiteral("missing_laps")))
                result.insert(QStringLiteral("missing_laps"), fuel.value(QStringLiteral("missing_laps")));
        }
        return result;
    }
    return unavailable();
}

QJsonObject ToolRegistry::unavailable()
{
    return {{QStringLiteral("available"), false}};
}

} // namespace raceengineer
