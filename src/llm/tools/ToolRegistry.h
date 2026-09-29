#pragma once

#include "race/RaceHistory.h"
#include "telemetry/common/RaceState.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <functional>

namespace raceengineer {

class ToolRegistry final {
public:
    using FeatureSettingsReader = std::function<QJsonObject()>;
    using FeatureToggle = std::function<QJsonObject(const QString&, bool, quint64)>;

    void setFeatureControlHandlers(FeatureSettingsReader reader, FeatureToggle toggle);
    [[nodiscard]] QJsonArray definitions() const;
    [[nodiscard]] QJsonObject execute(const QString& name, const RaceState& state,
        const RaceHistory& history, const QJsonObject& arguments = {},
        const QJsonObject& pitStrategy = {}, quint64 featureControlRevision = 0) const;

private:
    static QJsonObject unavailable();
    FeatureSettingsReader featureSettingsReader_;
    FeatureToggle featureToggle_;
};

} // namespace raceengineer
