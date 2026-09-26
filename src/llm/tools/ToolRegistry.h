#pragma once

#include "race/RaceHistory.h"
#include "telemetry/common/RaceState.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace raceengineer {

class ToolRegistry final {
public:
    [[nodiscard]] QJsonArray definitions() const;
    [[nodiscard]] QJsonObject execute(const QString& name, const RaceState& state,
        const RaceHistory& history, const QJsonObject& arguments = {},
        const QJsonObject& pitStrategy = {}) const;

private:
    static QJsonObject unavailable();
};

} // namespace raceengineer
