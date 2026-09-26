#include "strategy/StrategyPredictor.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLibrary>
#include <QMetaObject>

#include <algorithm>
#include <cmath>
#include <limits>

namespace raceengineer {
namespace {
const char* featureNames[] = {
    "laps_remaining", "fuel_laps_remaining", "reserve_laps", "stint_laps",
    "pace_mean_s", "pace_trend_s", "gap_ahead_s", "gap_behind_s",
    "pit_loss_s", "window_open_offset", "window_close_offset", "candidate_offset"
};
constexpr int featureCount = 12;

QJsonObject readObject(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const auto document = QJsonDocument::fromJson(file.readAll());
    return document.isObject() ? document.object() : QJsonObject{};
}

bool finitePositive(const double value) { return std::isfinite(value) && value > 0; }

bool passesQualityGate(const QJsonObject& metrics)
{
    const auto number = [&metrics](const char* key) {
        const auto value = metrics.value(QString::fromLatin1(key));
        return value.isDouble() ? value.toDouble() : std::numeric_limits<double>::quiet_NaN();
    };
    const double regret = number("simulation_regret_s");
    const double baseline = number("best_baseline_simulation_regret_s");
    const double p90 = number("simulation_regret_p90_s");
    const double baselineP90 = number("best_baseline_simulation_regret_p90_s");
    return metrics.value(QStringLiteral("held_out_races")).toInt() >= 10
        && finitePositive(baseline) && std::isfinite(regret) && regret >= 0
        && regret <= baseline * 0.9
        && std::isfinite(p90) && p90 >= 0 && std::isfinite(baselineP90) && baselineP90 >= 0
        && p90 <= baselineP90
        && finitePositive(number("improvement_ci95_lower_s"))
        && number("legal_choice_rate") == 1.0;
}

double planNumber(const QJsonObject& object, const char* name)
{
    const auto value = object.value(QString::fromLatin1(name));
    return value.isDouble() ? value.toDouble() : std::numeric_limits<double>::quiet_NaN();
}

bool passesPlanGate(const QJsonObject& plan, const QJsonObject& profile)
{
    const auto metrics = plan.value(QStringLiteral("metrics")).toObject();
    const double paceMae = planNumber(metrics, "pace_mae_s");
    const double baselineMae = planNumber(metrics, "baseline_pace_mae_s");
    const double age = planNumber(plan, "stint_age_penalty_s_per_lap");
    const double fuelPenalty = planNumber(plan, "fuel_penalty_s_per_liter");
    const double uncertainty = planNumber(plan, "uncertainty_s");
    const double margin = planNumber(plan, "decision_margin_s");
    const auto sourceHash = plan.value(QStringLiteral("source_sha256")).toString().toLatin1();
    const bool sourceHashValid = sourceHash.size() == 64
        && std::all_of(sourceHash.cbegin(), sourceHash.cend(), [](const char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        });
    return plan.value(QStringLiteral("schema_version")).toInt() == 1
        && plan.value(QStringLiteral("deployment_ready")).toBool()
        && !profile.value(QStringLiteral("profile_id")).toString().isEmpty()
        && !profile.value(QStringLiteral("simulator")).toString().isEmpty()
        && !profile.value(QStringLiteral("track")).toString().isEmpty()
        && !profile.value(QStringLiteral("car_model")).toString().isEmpty()
        && profile.value(QStringLiteral("total_laps")).toInt() > 0
        && profile.value(QStringLiteral("total_laps")).toInt() <= 1000
        && sourceHashValid
        && plan.value(QStringLiteral("session_count")).toInt() >= 10
        && plan.value(QStringLiteral("profile_id")) == profile.value(QStringLiteral("profile_id"))
        && plan.value(QStringLiteral("simulator")) == profile.value(QStringLiteral("simulator"))
        && plan.value(QStringLiteral("track")) == profile.value(QStringLiteral("track"))
        && plan.value(QStringLiteral("car_model")) == profile.value(QStringLiteral("car_model"))
        && plan.value(QStringLiteral("total_laps")) == profile.value(QStringLiteral("total_laps"))
        && profile.value(QStringLiteral("race_format")).toString() == QStringLiteral("laps")
        && profile.value(QStringLiteral("mandatory_stop")).toBool()
        && profile.value(QStringLiteral("service_validated")).toBool()
        && profile.value(QStringLiteral("dry_conditions_confirmed")).toBool()
        && profile.value(QStringLiteral("start_fresh_tyres_validated")).toBool()
        && profile.value(QStringLiteral("tyre_change_validated")).toBool()
        && profile.value(QStringLiteral("fixed_pit_loss_validated")).toBool()
        && profile.value(QStringLiteral("refuel_allowed")).isBool()
        && (!profile.value(QStringLiteral("refuel_allowed")).toBool()
            || profile.value(QStringLiteral("refuel_amount_control_validated")).toBool())
        && finitePositive(planNumber(plan, "fresh_lap_s"))
        && std::isfinite(age) && age >= 0.0
        && std::isfinite(fuelPenalty) && fuelPenalty >= 0.0
        && finitePositive(planNumber(plan, "fuel_use_l_per_lap"))
        && finitePositive(planNumber(plan, "pit_loss_s"))
        && std::isfinite(uncertainty) && uncertainty >= 0.0
        && std::isfinite(margin) && margin >= 0.0
        && metrics.value(QStringLiteral("held_out_races")).toInt() >= 10
        && metrics.value(QStringLiteral("prospective_races")).toInt() >= 10
        && finitePositive(baselineMae) && std::isfinite(paceMae) && paceMae >= 0.0
        && paceMae <= baselineMae
        && planNumber(metrics, "legal_choice_rate") == 1.0
        && finitePositive(planNumber(metrics, "improvement_ci95_lower_s"));
}

bool planFuelLegal(const int currentLap, const int totalLaps, const int pitLap,
    const double fuel, const double burn, const double capacity, const bool refuel,
    const double reserveLaps)
{
    if (pitLap < currentLap || pitLap > totalLaps || !finitePositive(fuel)
        || !finitePositive(burn) || !std::isfinite(reserveLaps) || reserveLaps < 0
        || (refuel && !finitePositive(capacity))) return false;
    const double fuelAtPit = fuel - (pitLap - currentLap + 1) * burn;
    const double fuelAfterPit = refuel ? capacity : fuelAtPit;
    return fuelAtPit + 1e-6 >= reserveLaps * burn
        && fuelAfterPit + 1e-6 >= (totalLaps - pitLap + reserveLaps) * burn;
}
}

struct StrategyPredictor::CandidateBatch {
    quint64 revision{0};
    int currentLap{0};
    int totalLaps{0};
    int stintLaps{0};
    double fuelLiters{0.0};
    double fuelCapacityLiters{0.0};
    double fuelUseLitersPerLap{0.0};
    double reserveLaps{0.0};
    bool refuelAllowed{false};
    QVector<int> laps;
    QVector<float> features;
};

// Load an approved XGBoost library once, then reuse it for lap-triggered inference.
// These signatures are the stable XGBoost C API, so Python is not needed in the app.
struct StrategyPredictor::Runtime {
    using Handle = void*;
    using Size = unsigned long long;
    QLibrary library;
    Handle booster{nullptr};
    int (*create)(const Handle*, Size, Handle*){nullptr};
    int (*load)(Handle, const char*){nullptr};
    int (*setParam)(Handle, const char*, const char*){nullptr};
    int (*freeBooster)(Handle){nullptr};
    int (*createMatrix)(const float*, Size, Size, float, Handle*){nullptr};
    int (*freeMatrix)(Handle){nullptr};
    int (*setFeatureInfo)(Handle, const char*, const char**, Size){nullptr};
    int (*predict)(Handle, Handle, int, unsigned, int, Size*, const float**){nullptr};
    const char* (*lastError)(){nullptr};
    QString error;

    ~Runtime()
    {
        if (booster && freeBooster) freeBooster(booster);
    }

    template <typename T> bool symbol(T& target, const char* name)
    {
        target = reinterpret_cast<T>(library.resolve(name));
        if (!target) error = QStringLiteral("XGBoost thiếu hàm %1").arg(QString::fromLatin1(name));
        return target != nullptr;
    }

    bool open(const QString& directory)
    {
        library.setFileName(QDir(directory).filePath(QStringLiteral("xgboost.dll")));
        if (!library.load()) {
            error = QStringLiteral("Không tải được xgboost.dll: %1").arg(library.errorString());
            return false;
        }
        if (!symbol(create, "XGBoosterCreate") || !symbol(load, "XGBoosterLoadModel")
            || !symbol(setParam, "XGBoosterSetParam") || !symbol(freeBooster, "XGBoosterFree")
            || !symbol(createMatrix, "XGDMatrixCreateFromMat")
            || !symbol(freeMatrix, "XGDMatrixFree")
            || !symbol(setFeatureInfo, "XGDMatrixSetStrFeatureInfo")
            || !symbol(predict, "XGBoosterPredict") || !symbol(lastError, "XGBGetLastError")) return false;
        if (create(nullptr, 0, &booster) != 0
            || load(booster, QFile::encodeName(QDir(directory).filePath(QStringLiteral("pit_ranker.json"))).constData()) != 0
            || setParam(booster, "nthread", "1") != 0
            || setParam(booster, "device", "cpu") != 0) {
            error = QString::fromUtf8(lastError());
            return false;
        }
        const auto parity = readObject(QDir(directory).filePath(QStringLiteral("parity_vectors.json")));
        const auto rows = parity.value(QStringLiteral("rows")).toArray();
        const auto expected = parity.value(QStringLiteral("expected_scores")).toArray();
        const auto laps = parity.value(QStringLiteral("candidate_laps")).toArray();
        if (rows.isEmpty() || rows.size() != expected.size() || rows.size() != laps.size()) {
            error = QStringLiteral("Thiếu vector đối chiếu Python/C++");
            return false;
        }
        CandidateBatch batch;
        batch.currentLap = 1;
        for (int i = 0; i < rows.size(); ++i) {
            const auto row = rows[i].toArray();
            if (row.size() != featureCount || !laps[i].isDouble() || !expected[i].isDouble()) {
                error = QStringLiteral("Vector đối chiếu không đúng schema");
                return false;
            }
            batch.laps.push_back(laps[i].toInt());
            for (const auto value : row) {
                batch.features.push_back(value.isNull()
                    ? std::numeric_limits<float>::quiet_NaN() : float(value.toDouble()));
            }
        }
        Handle matrix = nullptr;
        if (createMatrix(batch.features.constData(), static_cast<Size>(batch.laps.size()),
                featureCount, std::numeric_limits<float>::quiet_NaN(), &matrix) != 0) {
            error = QString::fromUtf8(lastError());
            return false;
        }
        Size count = 0;
        const float* scores = nullptr;
        const bool success = setFeatureInfo(matrix, "feature_name", featureNames, featureCount) == 0
            && predict(booster, matrix, 0, 0, 0, &count, &scores) == 0
            && count == static_cast<Size>(batch.laps.size()) && scores;
        if (success) {
            for (int i = 0; i < rows.size(); ++i) {
                const double target = expected[i].toDouble();
                if (!std::isfinite(scores[i]) || std::abs(scores[i] - target) > 1e-6 + 1e-6 * std::abs(target)) {
                    error = QStringLiteral("Điểm XGBoost C++ lệch notebook");
                    break;
                }
            }
        } else {
            error = QString::fromUtf8(lastError());
            if (error.isEmpty()) error = QStringLiteral("Số điểm vector đối chiếu không khớp");
        }
        freeMatrix(matrix);
        if (!error.isEmpty()) return false;
        return true;
    }

    StrategyDecision run(const CandidateBatch& batch)
    {
        StrategyDecision result;
        result.revision = batch.revision;
        result.currentLap = batch.currentLap;
        Handle matrix = nullptr;
        if (createMatrix(batch.features.constData(), static_cast<Size>(batch.laps.size()),
                featureCount, std::numeric_limits<float>::quiet_NaN(), &matrix) != 0) {
            result.error = QString::fromUtf8(lastError());
            return result;
        }
        const auto release = [this, matrix] { freeMatrix(matrix); };
        Size count = 0;
        const float* scores = nullptr;
        if (setFeatureInfo(matrix, "feature_name", featureNames, featureCount) != 0
            || predict(booster, matrix, 0, 0, 0, &count, &scores) != 0) {
            result.error = QString::fromUtf8(lastError());
            release();
            return result;
        }
        if (count != static_cast<Size>(batch.laps.size()) || !scores) {
            result.error = QStringLiteral("Số điểm XGBoost không khớp ứng viên");
            release();
            return result;
        }
        int best = -1;
        for (int i = 0; i < batch.laps.size(); ++i) {
            if (!std::isfinite(scores[i])) {
                result.error = QStringLiteral("XGBoost trả về điểm không hợp lệ");
                break;
            }
            if (best < 0 || scores[i] > scores[best]) best = i;
        }
        if (best >= 0 && result.error.isEmpty()) result.pitLap = batch.laps[best];
        release();
        return result;
    }
};

StrategyPredictor::StrategyPredictor(const QString& directory, QObject* parent)
    : QObject(parent), directory_(directory)
{
    pool_.setMaxThreadCount(1);
    pool_.setExpiryTimeout(-1);
    pool_.setThreadPriority(QThread::LowPriority);
    const QDir dir(directory_);
    const auto manifest = readObject(dir.filePath(QStringLiteral("manifest.json")));
    const auto schema = readObject(dir.filePath(QStringLiteral("feature_schema.json")));
    profile_ = readObject(dir.filePath(QStringLiteral("profile.json")));
    const auto calibration = readObject(dir.filePath(QStringLiteral("plan_calibration.json")));
    if (passesPlanGate(calibration, profile_)) {
        plan_ = calibration;
        plannerAvailable_ = true;
        available_ = true;
        artifactStatus_ = QStringLiteral("Chờ telemetry cuộc đua");
        return;
    }
    const QString modelPath = dir.filePath(QStringLiteral("pit_ranker.json"));
    QFile model(modelPath);
    if (!model.open(QIODevice::ReadOnly) || manifest.isEmpty() || schema.isEmpty()) {
        artifactStatus_ = calibration.isEmpty()
            ? QStringLiteral("Chưa có hiệu chỉnh chiến thuật pit đã duyệt")
            : QStringLiteral("Hiệu chỉnh pit chưa đạt cổng duyệt");
        return;
    }
    if (manifest.value(QStringLiteral("deployment_ready")).toBool() != true
        || manifest.value(QStringLiteral("mode")).toString() != QStringLiteral("real")
        || manifest.value(QStringLiteral("label_methods")).toArray().contains(QStringLiteral("toy_simulation"))) {
        artifactStatus_ = QStringLiteral("Model chưa được duyệt để sử dụng");
        return;
    }
    if (!passesQualityGate(manifest.value(QStringLiteral("metrics")).toObject())) {
        artifactStatus_ = QStringLiteral("Model chưa đạt ngưỡng chất lượng chiến thuật pit");
        return;
    }
    QFile libraryFile(dir.filePath(QStringLiteral("xgboost.dll")));
    if (!libraryFile.open(QIODevice::ReadOnly)
        || QCryptographicHash::hash(libraryFile.readAll(), QCryptographicHash::Sha256).toHex()
            != manifest.value(QStringLiteral("xgboost_sha256")).toString().toLatin1()) {
        artifactStatus_ = QStringLiteral("Thiếu xgboost.dll đã được kiểm tra checksum");
        return;
    }
    if (QCryptographicHash::hash(model.readAll(), QCryptographicHash::Sha256).toHex()
            != manifest.value(QStringLiteral("model_sha256")).toString().toLatin1()) {
        artifactStatus_ = QStringLiteral("Sai checksum model chiến thuật");
        return;
    }
    const auto features = schema.value(QStringLiteral("features")).toArray();
    if (schema.value(QStringLiteral("version")).toInt() != 1
        || features.size() != featureCount
        || schema.value(QStringLiteral("profile_id")).toString() != manifest.value(QStringLiteral("profile_id")).toString()) {
        artifactStatus_ = QStringLiteral("Schema model chiến thuật không khớp");
        return;
    }
    for (int i = 0; i < featureCount; ++i) {
        if (features[i].toString() != QString::fromLatin1(featureNames[i])) {
            artifactStatus_ = QStringLiteral("Thứ tự feature model không khớp");
            return;
        }
    }
    if (profile_.value(QStringLiteral("profile_id")).toString() != schema.value(QStringLiteral("profile_id")).toString()
        || profile_.value(QStringLiteral("race_format")).toString() != QStringLiteral("laps")
        || !manifest.value(QStringLiteral("domains")).toArray().contains(profile_.value(QStringLiteral("simulator")))
        || profile_.value(QStringLiteral("mandatory_stop")).toBool() != true
        || profile_.value(QStringLiteral("service_validated")).toBool() != true
        || profile_.value(QStringLiteral("dry_conditions_confirmed")).toBool() != true) {
        artifactStatus_ = QStringLiteral("Thiếu race profile đã xác nhận luật và dịch vụ pit");
        return;
    }
    runtime_ = std::make_unique<Runtime>();
    if (!runtime_->open(directory_)) {
        artifactStatus_ = runtime_->error;
        runtime_.reset();
        return;
    }
    artifactStatus_ = QStringLiteral("Chờ telemetry cuộc đua");
    available_ = true;
}

StrategyPredictor::~StrategyPredictor()
{
    pool_.waitForDone();
    runtime_.reset();
}

std::optional<StrategyPredictor::CandidateBatch> StrategyPredictor::candidates(
    const RaceState& state, const RaceHistory& history, const int stintLaps,
    const quint64 revision, QString& reason) const
{
    if (!state.connected || state.simulator == Simulator::Mock) {
        reason = QStringLiteral("Đang chờ AC / ACC"); return std::nullopt;
    }
    const QString simulator = state.simulator == Simulator::AssettoCorsaCompetizione
        ? QStringLiteral("sim_acc") : QStringLiteral("sim_ac");
    if (state.sessionType != SessionType::Race || !state.totalLaps || *state.totalLaps <= 0
        || !state.currentLap || !state.lapsRemaining || *state.lapsRemaining < 1
        || *state.lapsRemaining > 1000
        || !state.track || !state.carModel
        || profile_.value(QStringLiteral("simulator")).toString() != simulator
        || profile_.value(QStringLiteral("track")).toString() != QString::fromStdString(*state.track)
        || profile_.value(QStringLiteral("car_model")).toString() != QString::fromStdString(*state.carModel)
        || profile_.value(QStringLiteral("total_laps")).toInt() != *state.totalLaps) {
        reason = QStringLiteral("Race hiện tại chưa có profile model phù hợp"); return std::nullopt;
    }
    if (plannerAvailable_ && (*state.totalLaps != *state.currentLap + *state.lapsRemaining - 1
        || (state.flag && (*state.flag == FlagState::Yellow || *state.flag == FlagState::Red
            || *state.flag == FlagState::Black || *state.flag == FlagState::Chequered)))) {
        reason = QStringLiteral("Điều kiện đua hoặc số vòng không phù hợp hiệu chỉnh pit"); return std::nullopt;
    }
    const auto fuelLaps = history.estimatedFuelLapsRemaining(state);
    const auto pace = history.averageLapTime();
    const auto trend = history.recentLapTrend();
    if (stintLaps < 1 || !fuelLaps || !pace || !trend
        || !finitePositive(*fuelLaps) || *fuelLaps > 1000
        || !finitePositive(*pace) || *pace > 10000 || !std::isfinite(*trend)) {
        reason = QStringLiteral("Chưa đủ vòng/fuel để chọn thời điểm pit"); return std::nullopt;
    }
    const double reserve = profile_.value(QStringLiteral("reserve_laps")).toDouble(-1);
    const double pitLoss = profile_.value(QStringLiteral("pit_loss_s")).toDouble(-1);
    const int openLap = profile_.value(QStringLiteral("pit_window_open_lap")).toInt(-1);
    const int closeLap = profile_.value(QStringLiteral("pit_window_close_lap")).toInt(-1);
    if (!std::isfinite(reserve) || reserve < 0 || !std::isfinite(pitLoss) || pitLoss <= 0
        || openLap < 1 || closeLap < openLap || closeLap > *state.totalLaps) {
        reason = QStringLiteral("Race profile thiếu pit window hoặc pit loss hợp lệ"); return std::nullopt;
    }
    const auto measuredFuelUse = history.averageFuelConsumption();
    const double fuelUse = plannerAvailable_ && measuredFuelUse
        ? std::max(*measuredFuelUse, planNumber(plan_, "fuel_use_l_per_lap")) : 0.0;
    if (plannerAvailable_ && (!state.fuelLiters || !finitePositive(*state.fuelLiters)
        || !finitePositive(fuelUse)
        || (profile_.value(QStringLiteral("refuel_allowed")).toBool()
            && (!state.fuelCapacityLiters || !finitePositive(*state.fuelCapacityLiters)
                || *state.fuelLiters > *state.fuelCapacityLiters + 0.01)))) {
        reason = QStringLiteral("Thiếu nhiên liệu hoặc dung tích bình để xét cả sau pit"); return std::nullopt;
    }
    const double usableFuelLaps = plannerAvailable_ ? *state.fuelLiters / fuelUse : *fuelLaps;
    const int lower = std::max(0, openLap - *state.currentLap);
    const double fuelOffset = std::floor(usableFuelLaps - reserve - 1.0);
    const int fuelUpper = fuelOffset < 0 ? -1
        : fuelOffset >= *state.lapsRemaining ? *state.lapsRemaining - 1 : static_cast<int>(fuelOffset);
    const int upper = std::min({closeLap - *state.currentLap, *state.lapsRemaining - 1,
        fuelUpper});
    if (lower > upper || upper < 0) {
        reason = QStringLiteral("Không còn vòng pit hợp lệ với nhiên liệu hiện tại"); return std::nullopt;
    }
    CandidateBatch batch;
    batch.revision = revision;
    batch.currentLap = *state.currentLap;
    if (plannerAvailable_) {
        batch.totalLaps = *state.totalLaps;
        batch.stintLaps = stintLaps;
        batch.fuelLiters = *state.fuelLiters;
        batch.fuelCapacityLiters = state.fuelCapacityLiters.value_or(0.0);
        batch.fuelUseLitersPerLap = fuelUse;
        batch.reserveLaps = reserve;
        batch.refuelAllowed = profile_.value(QStringLiteral("refuel_allowed")).toBool();
    }
    if (!plannerAvailable_) batch.features.reserve((upper - lower + 1) * featureCount);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const auto gap = [nan](const std::optional<double>& seconds) {
        return seconds && std::isfinite(*seconds) && *seconds >= 0 && *seconds < 100000
            ? float(*seconds) : nan;
    };
    for (int offset = lower; offset <= upper; ++offset) {
        const int pitLap = *state.currentLap + offset;
        if (plannerAvailable_ && offset == 0
            && (!state.currentLapTimeSeconds || *state.currentLapTimeSeconds < 0.0
                || *state.currentLapTimeSeconds > 5.0)) continue;
        if (plannerAvailable_ && !planFuelLegal(batch.currentLap, batch.totalLaps, pitLap,
            batch.fuelLiters, batch.fuelUseLitersPerLap, batch.fuelCapacityLiters,
            batch.refuelAllowed, reserve)) continue;
        batch.laps.push_back(pitLap);
        if (plannerAvailable_) continue;
        const float row[] = {float(*state.lapsRemaining), float(*fuelLaps), float(reserve),
            float(stintLaps), float(*pace), float(*trend),
            gap(state.gapAheadSeconds), gap(state.gapBehindSeconds),
            float(pitLoss), float(std::max(0, openLap - *state.currentLap)),
            float(closeLap - *state.currentLap), float(offset)};
        for (const float value : row) batch.features.push_back(value);
    }
    if (batch.laps.isEmpty()) {
        reason = QStringLiteral("Không có vòng pit hợp lệ cho cả hai chặng nhiên liệu");
        return std::nullopt;
    }
    return batch;
}

StrategyDecision StrategyPredictor::runPlan(const CandidateBatch& batch) const
{
    StrategyDecision result;
    result.revision = batch.revision;
    result.currentLap = batch.currentLap;
    result.fromPlanner = true;
    const double fresh = planNumber(plan_, "fresh_lap_s");
    const double agePenalty = planNumber(plan_, "stint_age_penalty_s_per_lap");
    const double fuelPenalty = planNumber(plan_, "fuel_penalty_s_per_liter");
    const double pitLoss = planNumber(plan_, "pit_loss_s");
    const double margin = std::max(planNumber(plan_, "uncertainty_s"),
        planNumber(plan_, "decision_margin_s"));
    double bestCost = std::numeric_limits<double>::infinity();
    double secondCost = std::numeric_limits<double>::infinity();
    double bestFutureCost = std::numeric_limits<double>::infinity();
    for (const int pitLap : batch.laps) {
        const int before = pitLap - batch.currentLap + 1;
        const int after = batch.totalLaps - pitLap;
        const double fuelAtPit = batch.fuelLiters - before * batch.fuelUseLitersPerLap;
        const double fuelAfterPit = batch.refuelAllowed
            ? std::max(fuelAtPit, (after + batch.reserveLaps) * batch.fuelUseLitersPerLap)
            : fuelAtPit;
        const double beforePairs = double(before) * (before - 1) / 2.0;
        const double afterPairs = double(after) * (after - 1) / 2.0;
        const double totalAge = double(before) * batch.stintLaps + beforePairs + afterPairs;
        const double totalFuel = double(before) * batch.fuelLiters
            - batch.fuelUseLitersPerLap * beforePairs
            + double(after) * fuelAfterPit - batch.fuelUseLitersPerLap * afterPairs;
        const double cost = double(before + after) * fresh + agePenalty * totalAge
            + fuelPenalty * totalFuel + pitLoss;
        if (!finitePositive(cost)) {
            result.error = QStringLiteral("Hiệu chỉnh pit tạo ra thời gian không hợp lệ");
            return result;
        }
        if (pitLap > batch.currentLap) bestFutureCost = std::min(bestFutureCost, cost);
        if (cost < bestCost) {
            secondCost = bestCost;
            bestCost = cost;
            result.pitLap = pitLap;
        } else {
            secondCost = std::min(secondCost, cost);
        }
    }
    if (result.pitLap == 0) {
        result.error = QStringLiteral("Không có vòng pit hợp lệ");
        return result;
    }
    result.expectedRemainingSeconds = bestCost;
    result.lastLegalLap = result.pitLap == batch.currentLap && !std::isfinite(bestFutureCost);
    if (result.pitLap == batch.currentLap && std::isfinite(bestFutureCost))
        result.gainVsWaitSeconds = bestFutureCost - bestCost;
    result.decisive = result.lastLegalLap || !std::isfinite(secondCost)
        || secondCost - bestCost > margin;
    return result;
}

QString StrategyPredictor::prepare(const RaceState& state, const RaceHistory& history,
    const int stintLaps, const quint64 revision, std::function<void(StrategyDecision)> onDecision)
{
    if (!available_) return artifactStatus_;
    if (busy_) return QStringLiteral("Đang tính chiến thuật");
    QString reason;
    auto batch = candidates(state, history, stintLaps, revision, reason);
    if (!batch) return reason;
    busy_ = true;
    pool_.start([this, batch = std::move(*batch), callback = std::move(onDecision)]() mutable {
        auto result = plannerAvailable_ ? runPlan(batch) : runtime_->run(batch);
        QMetaObject::invokeMethod(this, [this, callback = std::move(callback), result = std::move(result)]() mutable {
            busy_ = false;
            if (!result.error.isEmpty()) {
                artifactStatus_ = result.error;
                available_ = false;
            }
            callback(std::move(result));
        }, Qt::QueuedConnection);
    });
    return QStringLiteral("Đang tính chiến thuật");
}

bool StrategyPredictor::stillLegal(const RaceState& state, const RaceHistory& history,
    const int stintLaps, const int pitLap) const
{
    if (stintLaps < 1 || !state.connected || state.sessionType != SessionType::Race
        || !state.currentLap || !state.totalLaps || !state.lapsRemaining
        || !state.track || !state.carModel
        || profile_.value(QStringLiteral("track")).toString() != QString::fromStdString(*state.track)
        || profile_.value(QStringLiteral("car_model")).toString() != QString::fromStdString(*state.carModel)
        || profile_.value(QStringLiteral("total_laps")).toInt() != *state.totalLaps) return false;
    if (plannerAvailable_) {
        const QString simulator = state.simulator == Simulator::AssettoCorsaCompetizione
            ? QStringLiteral("sim_acc") : QStringLiteral("sim_ac");
        if (profile_.value(QStringLiteral("simulator")).toString() != simulator
            || *state.totalLaps != *state.currentLap + *state.lapsRemaining - 1
            || (state.flag && (*state.flag == FlagState::Yellow || *state.flag == FlagState::Red
                || *state.flag == FlagState::Black || *state.flag == FlagState::Chequered))) return false;
        const auto measured = history.averageFuelConsumption();
        const double reserve = profile_.value(QStringLiteral("reserve_laps")).toDouble(-1);
        const double burn = measured ? std::max(*measured, planNumber(plan_, "fuel_use_l_per_lap")) : 0.0;
        const bool refuel = profile_.value(QStringLiteral("refuel_allowed")).toBool();
        return state.fuelLiters && (!refuel || (state.fuelCapacityLiters
                && *state.fuelLiters <= *state.fuelCapacityLiters + 0.01))
            && (pitLap != *state.currentLap || (state.currentLapTimeSeconds
                && *state.currentLapTimeSeconds >= 0.0 && *state.currentLapTimeSeconds <= 5.0))
            && pitLap >= profile_.value(QStringLiteral("pit_window_open_lap")).toInt()
            && pitLap <= profile_.value(QStringLiteral("pit_window_close_lap")).toInt()
            && planFuelLegal(*state.currentLap, *state.totalLaps, pitLap, *state.fuelLiters,
                burn, state.fuelCapacityLiters.value_or(0.0), refuel, reserve);
    }
    const auto fuelLaps = history.estimatedFuelLapsRemaining(state);
    const double reserve = profile_.value(QStringLiteral("reserve_laps")).toDouble(-1);
    return fuelLaps && finitePositive(*fuelLaps) && reserve >= 0
        && pitLap >= std::max(*state.currentLap, profile_.value(QStringLiteral("pit_window_open_lap")).toInt())
        && pitLap <= std::min(profile_.value(QStringLiteral("pit_window_close_lap")).toInt(),
            *state.currentLap + *state.lapsRemaining - 1)
        && double(pitLap - *state.currentLap + 1) + reserve <= *fuelLaps;
}

} // namespace raceengineer
