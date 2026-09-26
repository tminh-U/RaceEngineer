#pragma once

#include "race/RaceHistory.h"
#include "telemetry/common/RaceState.h"

#include <QJsonObject>
#include <QObject>
#include <QThreadPool>
#include <functional>
#include <memory>
#include <optional>

namespace raceengineer {

struct StrategyDecision final {
    quint64 revision{0};
    int currentLap{0};
    int pitLap{0};
    bool fromPlanner{false};
    bool decisive{false};
    bool lastLegalLap{false};
    double expectedRemainingSeconds{0.0};
    double gainVsWaitSeconds{0.0};
    QString error;
};

class StrategyPredictor final : public QObject {
public:
    explicit StrategyPredictor(const QString& directory, QObject* parent = nullptr);
    ~StrategyPredictor() override;

    [[nodiscard]] QString artifactStatus() const { return artifactStatus_; }
    [[nodiscard]] bool available() const noexcept { return available_; }
    [[nodiscard]] QString prepare(const RaceState& state, const RaceHistory& history,
        int stintLaps, quint64 revision, std::function<void(StrategyDecision)> onDecision);
    [[nodiscard]] bool busy() const noexcept { return busy_; }
    [[nodiscard]] bool stillLegal(const RaceState& state, const RaceHistory& history,
        int stintLaps, int pitLap) const;

private:
    struct Runtime;
    struct CandidateBatch;
    [[nodiscard]] std::optional<CandidateBatch> candidates(const RaceState& state,
        const RaceHistory& history, int stintLaps, quint64 revision, QString& reason) const;
    [[nodiscard]] StrategyDecision runPlan(const CandidateBatch& batch) const;

    QString directory_;
    QString artifactStatus_;
    bool available_{false};
    QJsonObject profile_;
    QJsonObject plan_;
    bool plannerAvailable_{false};
    QThreadPool pool_;
    std::unique_ptr<Runtime> runtime_;
    bool busy_{false};
};

} // namespace raceengineer
