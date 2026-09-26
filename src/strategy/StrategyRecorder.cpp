#include "strategy/StrategyRecorder.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QDebug>
#include <QScopeGuard>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QSet>
#include <QTimer>
#include <QTimeZone>
#include <QUrl>
#include <QUuid>

#include <cmath>
#include <algorithm>

namespace raceengineer {
namespace {
void addNumber(QJsonObject& row, const char* name, const std::optional<double>& value)
{
    if (value && std::isfinite(*value)) row.insert(QString::fromLatin1(name), *value);
}
}

StrategyRecorder::StrategyRecorder()
{
    pool_.setMaxThreadCount(1);
    pool_.setExpiryTimeout(-1);
    pool_.setThreadPriority(QThread::LowPriority);
    QFile previous(QDir(storagePath()).filePath(QStringLiteral("training/last_result.json")));
    if (previous.open(QIODevice::ReadOnly)) {
        const auto message = QJsonDocument::fromJson(previous.read(65536)).object().value(QStringLiteral("message")).toString();
        if (!message.isEmpty()) status_ = message;
    }
    connect(&process_, &QProcess::readyReadStandardOutput, this, &StrategyRecorder::readJobOutput);
    connect(&process_, &QProcess::readyReadStandardError, this, [this] {
        errorOutput_ = (errorOutput_ + QString::fromUtf8(process_.readAllStandardError())).right(4096);
    });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        busy_ = false;
        status_ = QStringLiteral("Không chạy được Python: %1").arg(process_.errorString());
        emit changed();
    });
    connect(&process_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus exit) {
        readJobOutput();
        busy_ = false;
        if (cancelled_) status_ = QStringLiteral("Đã dừng xử lý / train. Dữ liệu gốc vẫn được giữ.");
        else if (exit != QProcess::NormalExit || code != 0)
            status_ = QStringLiteral("Không hoàn tất: %1").arg(errorOutput_.isEmpty() ? status_ : errorOutput_);
        emit changed();
    });
}

StrategyRecorder::~StrategyRecorder()
{
    if (uploadReply_) {
        uploadReply_->disconnect(this);
        uploadReply_->abort();
    }
    process_.disconnect(this);
    if (process_.state() != QProcess::NotRunning) {
        process_.kill();
        process_.waitForFinished(3000);
    }
    pool_.waitForDone();
    if (uploadFile_) {
        uploadFile_->close();
        delete uploadFile_;
    }
}

QString StrategyRecorder::storagePath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
}

void StrategyRecorder::setEnabled(bool enabled)
{
    if (enabled_ == enabled) return;
    enabled_ = enabled;
    emit enabledChanged(enabled);
    emit changed();
}

void StrategyRecorder::setSharingEnabled(bool enabled)
{
    if (sharingEnabled_ == enabled) {
        if (!enabled) clearPendingShares();
        return;
    }
    sharingEnabled_ = enabled;
    ++sharingRevision_;
    sharingStatus_ = !enabled ? QStringLiteral("Chia sẻ dữ liệu đang tắt.")
        : sharingConfigured() ? QStringLiteral("Đã đồng ý chia sẻ sau phiên đua.")
                              : QStringLiteral("Chưa có nơi nhận dữ liệu; tính năng tạm chưa sẵn sàng.");
    emit sharingEnabledChanged(enabled);
    emit changed();
    if (!enabled) {
        if (uploadReply_) uploadReply_->abort();
        else clearPendingShares();
    } else {
        QTimer::singleShot(1500, this, &StrategyRecorder::sendNextShare);
    }
}

void StrategyRecorder::setSharingConfig(const QString& endpoint, const QString& token)
{
    const bool configChanged = sharingEndpoint_ != endpoint || shareToken_ != token;
    sharingEndpoint_ = endpoint;
    shareToken_ = token;
    if (configChanged && uploadReply_) uploadReply_->abort();
    if (sharingEnabled_ && sharingConfigured())
        sharingStatus_ = QStringLiteral("Đã cấu hình máy chủ chia sẻ.");
    emit changed();
    sendNextShare();
}

void StrategyRecorder::retrySharing()
{
    if (!sharingEnabled_) sharingStatus_ = QStringLiteral("Bật đồng ý chia sẻ trước khi gửi.");
    else if (!sharingConfigured()) sharingStatus_ = QStringLiteral("Chưa có nơi nhận dữ liệu; tính năng tạm chưa sẵn sàng.");
    else sharingStatus_ = QStringLiteral("Đang kiểm tra dữ liệu chờ gửi…");
    emit changed();
    sendNextShare();
}

void StrategyRecorder::clearPendingShares()
{
    QDir dir(QDir(storagePath()).filePath(QStringLiteral("share_queue")));
    for (const QString& name : dir.entryList({QStringLiteral("*.jsonl")}, QDir::Files))
        dir.remove(name);
}

void StrategyRecorder::finishSession(const QString& sessionId)
{
    if (!sharingEnabled_ || sessionId.isEmpty()) return;
    const quint64 revision = sharingRevision_.load();
    pool_.start([this, sessionId, revision] {
        if (revision != sharingRevision_.load()) return;
        const QDir root(storagePath());
        QStringList paths{root.filePath(QStringLiteral("pit_strategy_laps.jsonl"))};
        const QDir archives(root.filePath(QStringLiteral("recordings")));
        for (const QFileInfo& info : archives.entryInfoList(
                 {QStringLiteral("pit_strategy_laps-*.jsonl")}, QDir::Files, QDir::Time))
            paths.append(info.absoluteFilePath());

        QVector<QJsonObject> rows;
        bool found = false;
        qsizetype matchedBytes = 0;
        for (const QString& path : paths) {
            QFile source(path);
            if (!source.open(QIODevice::ReadOnly)) continue;
            bool foundHere = false;
            while (!source.atEnd()) {
                const QByteArray line = source.readLine();
                const auto row = QJsonDocument::fromJson(line).object();
                if (row.value(QStringLiteral("session_id")).toString() != sessionId) continue;
                matchedBytes += line.size();
                if (matchedBytes > 20 * 1024 * 1024) return;
                rows.append(row);
                foundHere = true;
            }
            if (found && !foundHere) break;
            found |= foundHere;
        }
        if (rows.isEmpty() || revision != sharingRevision_.load()) return;
        if (!std::all_of(rows.cbegin(), rows.cend(), [](const QJsonObject& row) {
                return row.value(QStringLiteral("realism_confirmed")).toBool();
            })) {
            QMetaObject::invokeMethod(this, [this, revision] {
                if (revision != sharingRevision_.load()) return;
                sharingStatus_ = QStringLiteral("Phiên chưa xác nhận cài đặt thực tế; chỉ giữ log trên máy.");
                emit changed();
            }, Qt::QueuedConnection);
            return;
        }
        std::sort(rows.begin(), rows.end(), [](const QJsonObject& left, const QJsonObject& right) {
            return left.value(QStringLiteral("captured_utc")).toString()
                 < right.value(QStringLiteral("captured_utc")).toString();
        });

        static const QSet<QString> sharedFields{
            QStringLiteral("record_type"), QStringLiteral("simulator"),
            QStringLiteral("completed_lap"), QStringLiteral("lap_time_s"),
            QStringLiteral("sample_current_lap"), QStringLiteral("in_pit_at_sample"),
            QStringLiteral("lap_excluded"), QStringLiteral("track"),
            QStringLiteral("car_model"), QStringLiteral("car_category"),
            QStringLiteral("car_subclass"), QStringLiteral("total_laps"),
            QStringLiteral("fuel_used_l"), QStringLiteral("fuel_at_sample_l"),
            QStringLiteral("gap_ahead_at_sample_s"), QStringLiteral("gap_behind_at_sample_s"),
            QStringLiteral("current_lap"), QStringLiteral("pit_state"),
            QStringLiteral("realism_confirmed"),
            QStringLiteral("tyre_wear_0_at_sample"), QStringLiteral("tyre_wear_1_at_sample"),
            QStringLiteral("tyre_wear_2_at_sample"), QStringLiteral("tyre_wear_3_at_sample"),
            QStringLiteral("tyre_temp_0_at_sample"), QStringLiteral("tyre_temp_1_at_sample"),
            QStringLiteral("tyre_temp_2_at_sample"), QStringLiteral("tyre_temp_3_at_sample")};
        const QDateTime first = QDateTime::fromString(
            rows.front().value(QStringLiteral("captured_utc")).toString(), Qt::ISODateWithMs);
        const QDateTime rebased = QDateTime::fromSecsSinceEpoch(946684800, QTimeZone::UTC);
        const QString anonymousId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString queuePath = root.filePath(QStringLiteral("share_queue"));
        if (!QDir().mkpath(queuePath)) return;
        const QString outputPath = QDir(queuePath).filePath(anonymousId + QStringLiteral(".jsonl"));
        QSaveFile output(outputPath);
        if (!output.open(QIODevice::WriteOnly)) return;
        qsizetype total = 0;
        for (qsizetype index = 0; index < rows.size(); ++index) {
            QJsonObject clean;
            for (auto it = rows[index].constBegin(); it != rows[index].constEnd(); ++it)
                if (sharedFields.contains(it.key())) clean.insert(it.key(), it.value());
            const QDateTime captured = QDateTime::fromString(
                rows[index].value(QStringLiteral("captured_utc")).toString(), Qt::ISODateWithMs);
            clean.insert(QStringLiteral("session_id"), anonymousId);
            clean.insert(QStringLiteral("captured_utc"), rebased.addMSecs(
                first.isValid() && captured.isValid() ? first.msecsTo(captured) : index * 1000)
                .toString(Qt::ISODateWithMs));
            clean.insert(QStringLiteral("share_schema_version"), 1);
            const QByteArray line = QJsonDocument(clean).toJson(QJsonDocument::Compact) + '\n';
            total += line.size();
            if (total > 20 * 1024 * 1024 || output.write(line) != line.size()) {
                output.cancelWriting();
                return;
            }
        }
        if (revision != sharingRevision_.load() || !output.commit()) return;
        if (revision != sharingRevision_.load()) {
            QFile::remove(outputPath);
            return;
        }
        QMetaObject::invokeMethod(this, [this, revision] {
            if (revision != sharingRevision_.load()) return;
            sharingStatus_ = QStringLiteral("Đã chuẩn bị %1 phiên đua để gửi.").arg(
                QDir(QDir(storagePath()).filePath(QStringLiteral("share_queue")))
                    .entryList({QStringLiteral("*.jsonl")}, QDir::Files).size());
            emit changed();
            sendNextShare();
        }, Qt::QueuedConnection);
    });
}

void StrategyRecorder::sendNextShare()
{
    if (!sharingEnabled_ || !sharingConfigured() || raceActive_ || uploadReply_) return;
    const QDir queue(QDir(storagePath()).filePath(QStringLiteral("share_queue")));
    const auto pending = queue.entryInfoList({QStringLiteral("*.jsonl")}, QDir::Files, QDir::Time | QDir::Reversed);
    if (pending.isEmpty()) return;
    bool discardedUnconfirmed = false;
    for (const QFileInfo& candidate : pending) {
        QFile queued(candidate.absoluteFilePath());
        bool confirmed = queued.open(QIODevice::ReadOnly) && !queued.atEnd();
        while (confirmed && !queued.atEnd())
            confirmed = QJsonDocument::fromJson(queued.readLine()).object()
                .value(QStringLiteral("realism_confirmed")).toBool();
        queued.close();
        if (confirmed) {
            uploadFile_ = new QFile(candidate.absoluteFilePath());
            break;
        }
        discardedUnconfirmed |= QFile::remove(candidate.absoluteFilePath());
    }
    if (!uploadFile_) {
        if (discardedUnconfirmed) {
            sharingStatus_ = QStringLiteral("Đã bỏ gói chờ gửi chưa xác nhận; log gốc vẫn trên máy.");
            emit changed();
        }
        return;
    }
    if (!uploadFile_->open(QIODevice::ReadOnly)) {
        sharingStatus_ = QStringLiteral("Không đọc được gói dữ liệu chờ gửi.");
        delete uploadFile_;
        uploadFile_ = nullptr;
        emit changed();
        return;
    }
    QNetworkRequest request{QUrl(sharingEndpoint_)};
    const bool googleScript = request.url().host() == QStringLiteral("script.google.com")
        && request.url().path().startsWith(QStringLiteral("/macros/s/"))
        && request.url().path().endsWith(QStringLiteral("/exec"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, googleScript
        ? QStringLiteral("text/plain; charset=utf-8") : QStringLiteral("application/x-ndjson"));
    if (!googleScript) request.setRawHeader("Authorization", "Bearer " + shareToken_.toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, googleScript
        ? QNetworkRequest::NoLessSafeRedirectPolicy : QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(30000);
    // Apps Script exposes POST text but not custom headers. Keep the token out of the saved JSONL.
    uploadReply_ = googleScript
        ? network_.post(request, shareToken_.toUtf8() + '\n' + uploadFile_->readAll())
        : network_.post(request, uploadFile_);
    sharingStatus_ = QStringLiteral("Đang gửi dữ liệu phiên đua…");
    emit changed();
    connect(uploadReply_, &QNetworkReply::finished, this, [this, googleScript] {
        const int http = uploadReply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto networkError = uploadReply_->error();
        const QJsonObject scriptResult = googleScript
            ? QJsonDocument::fromJson(uploadReply_->readAll()).object() : QJsonObject{};
        const bool sent = networkError == QNetworkReply::NoError && http >= 200 && http < 300
            && (!googleScript || scriptResult.value(QStringLiteral("accepted")).toBool());
        const QString path = uploadFile_->fileName();
        uploadFile_->close();
        delete uploadFile_;
        uploadFile_ = nullptr;
        uploadReply_->deleteLater();
        uploadReply_ = nullptr;
        if (!sharingEnabled_) {
            clearPendingShares();
            return;
        }
        if (sent) {
            QFile::remove(path);
            sharingStatus_ = QStringLiteral("Đã gửi dữ liệu phiên đua.");
            emit changed();
            sendNextShare();
        } else {
            if (raceActive_) sharingStatus_ = QStringLiteral("Tạm dừng gửi khi đang đua; sẽ thử lại sau phiên.");
            else if (http == 401 || scriptResult.value(QStringLiteral("error")).toString() == QStringLiteral("unauthorized"))
                sharingStatus_ = QStringLiteral("Mã truy cập máy chủ không đúng; dữ liệu vẫn ở trên máy.");
            else if (http == 413 || scriptResult.value(QStringLiteral("error")).toString() == QStringLiteral("too_large"))
                sharingStatus_ = QStringLiteral("Gói dữ liệu vượt giới hạn máy chủ; dữ liệu vẫn ở trên máy.");
            else sharingStatus_ = QStringLiteral("Chưa gửi được; dữ liệu vẫn ở trên máy. Bấm Gửi lại sau.");
            emit changed();
            if (networkError == QNetworkReply::OperationCanceledError && !raceActive_)
                sendNextShare();
        }
    });
}

void StrategyRecorder::setSimulatorConnected(bool connected)
{
    if (simulatorConnected_ == connected) return;
    simulatorConnected_ = connected;
    if (connected && busy_) cancelJob();
    emit changed();
}

void StrategyRecorder::setRaceActive(bool racing)
{
    if (raceActive_ == racing) return;
    if (racing) sessionRealisticSetupConfirmed_ = realisticSetupConfirmed_;
    raceActive_ = racing;
    if (!racing) realisticSetupConfirmed_ = false;
    if (racing && uploadReply_) uploadReply_->abort();
    if (!racing) sendNextShare();
    emit changed();
}

void StrategyRecorder::setRealisticSetupConfirmed(bool confirmed)
{
    if (raceActive_ || realisticSetupConfirmed_ == confirmed) return;
    realisticSetupConfirmed_ = confirmed;
    emit changed();
}

void StrategyRecorder::openStorage()
{
    if (QDir().mkpath(storagePath())) QDesktopServices::openUrl(QUrl::fromLocalFile(storagePath()));
}

void StrategyRecorder::processData() { startJob(QStringLiteral("process")); }
void StrategyRecorder::trainModel() { startJob(QStringLiteral("train")); }
void StrategyRecorder::trainTyreModels() { startJob(QStringLiteral("train-tyres")); }

void StrategyRecorder::startJob(const QString& action)
{
    if (busy_) return;
    if (simulatorConnected_) {
        status_ = QStringLiteral("Ngắt kết nối simulator trước khi xử lý / train để dành CPU cho đua.");
        emit changed();
        return;
    }
    busy_ = true;
    ++jobRevision_;
    cancelled_ = false;
    output_.clear();
    errorOutput_.clear();
    status_ = QStringLiteral("Đang chuẩn bị dữ liệu đã lưu…");
    emit changed();
    launchJob(action);
}

void StrategyRecorder::launchJob(const QString& action)
{
    if (!busy_ || cancelled_) return;
    if (pending_.load() > 0) {
        const auto revision = jobRevision_;
        QTimer::singleShot(50, this, [this, action, revision] {
            if (revision == jobRevision_) launchJob(action);
        });
        return;
    }
    const QString appDir = QCoreApplication::applicationDirPath();
    QString script = QDir(appDir).filePath(QStringLiteral("training/pit_strategy/local_training.py"));
    if (!QFileInfo::exists(script))
        script = QDir(appDir).absoluteFilePath(QStringLiteral("../training/pit_strategy/local_training.py"));
    QString python = QDir(appDir).filePath(QStringLiteral("training/python/python.exe"));
    if (!QFileInfo::exists(python)) python = QStandardPaths::findExecutable(QStringLiteral("python"));
    if (python.isEmpty() || !QFileInfo::exists(script)) {
        busy_ = false;
        status_ = QStringLiteral("Cần Python trong PATH và bộ training đi kèm app. Train cần numpy và xgboost.");
        emit changed();
        return;
    }
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));
    environment.insert(QStringLiteral("OMP_NUM_THREADS"), QStringLiteral("1"));
    environment.insert(QStringLiteral("OPENBLAS_NUM_THREADS"), QStringLiteral("1"));
    environment.insert(QStringLiteral("MKL_NUM_THREADS"), QStringLiteral("1"));
    process_.setProcessEnvironment(environment);
    process_.setProgram(python);
    process_.setArguments({QStringLiteral("-u"), script, QStringLiteral("--action"), action,
        QStringLiteral("--data-dir"), storagePath(), QStringLiteral("--output-dir"),
        QDir(storagePath()).filePath(QStringLiteral("training"))});
    status_ = action == QStringLiteral("train") ? QStringLiteral("Đang train XGBoost pace…")
        : action == QStringLiteral("train-tyres") ? QStringLiteral("Đang train XGBoost lốp…")
        : QStringLiteral("Đang xử lý dữ liệu…");
    emit changed();
    process_.start();
}

void StrategyRecorder::cancelJob()
{
    if (!busy_) return;
    cancelled_ = true;
    if (process_.state() != QProcess::NotRunning) process_.kill();
    else {
        busy_ = false;
        status_ = QStringLiteral("Đã hủy tác vụ.");
        emit changed();
    }
}

void StrategyRecorder::readJobOutput()
{
    output_ += process_.readAllStandardOutput();
    qsizetype newline;
    while ((newline = output_.indexOf('\n')) >= 0) {
        const auto row = QJsonDocument::fromJson(output_.left(newline)).object();
        output_.remove(0, newline + 1);
        const auto message = row.value(QStringLiteral("message")).toString();
        if (!message.isEmpty()) { status_ = message; emit changed(); }
    }
    if (output_.size() > 16384) output_.clear();
}

void StrategyRecorder::reportWrite(const QString& error)
{
    QMetaObject::invokeMethod(this, [this, error] {
        if (error.isEmpty()) ++savedRecords_;
        else status_ = error;
        emit changed();
    }, Qt::QueuedConnection);
}

void StrategyRecorder::record(const QString& sessionId, const RaceState& state, const LapRecord& lap, bool lapExcluded)
{
    if (!state.connected || state.simulator == Simulator::Mock || lap.lapNumber < 1
        || !std::isfinite(lap.lapTimeSeconds) || lap.lapTimeSeconds <= 0) return;
    QJsonObject row{
        {QStringLiteral("record_type"), QStringLiteral("lap")},
        {QStringLiteral("session_id"), sessionId},
        {QStringLiteral("captured_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("simulator"), state.simulator == Simulator::AssettoCorsaCompetizione
            ? QStringLiteral("sim_acc") : QStringLiteral("sim_ac")},
        {QStringLiteral("completed_lap"), lap.lapNumber},
        {QStringLiteral("lap_time_s"), lap.lapTimeSeconds},
        {QStringLiteral("sample_current_lap"), state.currentLap.value_or(0)},
        {QStringLiteral("in_pit_at_sample"), state.pitState && *state.pitState != PitState::Track},
        {QStringLiteral("lap_excluded"), lapExcluded},
        {QStringLiteral("realism_confirmed"), sessionRealisticSetupConfirmed_},
    };
    if (state.track) row.insert(QStringLiteral("track"), QString::fromStdString(*state.track));
    if (state.carModel) row.insert(QStringLiteral("car_model"), QString::fromStdString(*state.carModel));
    if (state.carCategory) row.insert(QStringLiteral("car_category"), QString::fromStdString(*state.carCategory));
    if (state.carSubclass) row.insert(QStringLiteral("car_subclass"), QString::fromStdString(*state.carSubclass));
    if (state.totalLaps) row.insert(QStringLiteral("total_laps"), *state.totalLaps);
    addNumber(row, "fuel_used_l", lap.fuelUsedLiters);
    addNumber(row, "fuel_at_sample_l", state.fuelLiters);
    addNumber(row, "fuel_capacity_l", state.fuelCapacityLiters);
    addNumber(row, "gap_ahead_at_sample_s", state.gapAheadSeconds);
    addNumber(row, "gap_behind_at_sample_s", state.gapBehindSeconds);
    if (state.tyreWear) {
        for (int i = 0; i < 4; ++i) {
            const double wear = (*state.tyreWear)[i];
            if (std::isfinite(wear)) row.insert(QStringLiteral("tyre_wear_%1_at_sample").arg(i), wear);
        }
    }
    if (state.tyreTemperaturesCelsius) {
        for (int i = 0; i < 4; ++i) {
            const double temperature = (*state.tyreTemperaturesCelsius)[i];
            if (std::isfinite(temperature))
                row.insert(QStringLiteral("tyre_temp_%1_at_sample").arg(i), temperature);
        }
    }
    enqueue(row);
}

void StrategyRecorder::recordPitEvent(const QString& sessionId, const RaceState& state, const bool entering)
{
    recordPitEvent(sessionId, state, entering ? QStringLiteral("pit_enter") : QStringLiteral("pit_exit"));
}

void StrategyRecorder::recordPitBoxEvent(const QString& sessionId, const RaceState& state, const bool entering)
{
    recordPitEvent(sessionId, state,
        entering ? QStringLiteral("pit_box_enter") : QStringLiteral("pit_box_exit"));
}

void StrategyRecorder::recordPitEvent(const QString& sessionId, const RaceState& state,
    const QString& recordType)
{
    if (!state.connected || state.simulator == Simulator::Mock) return;
    QJsonObject row{
        {QStringLiteral("record_type"), recordType},
        {QStringLiteral("session_id"), sessionId},
        {QStringLiteral("captured_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("simulator"), state.simulator == Simulator::AssettoCorsaCompetizione
            ? QStringLiteral("sim_acc") : QStringLiteral("sim_ac")},
        {QStringLiteral("current_lap"), state.currentLap.value_or(0)},
        {QStringLiteral("realism_confirmed"), sessionRealisticSetupConfirmed_},
    };
    if (state.track) row.insert(QStringLiteral("track"), QString::fromStdString(*state.track));
    if (state.carModel) row.insert(QStringLiteral("car_model"), QString::fromStdString(*state.carModel));
    if (state.carCategory) row.insert(QStringLiteral("car_category"), QString::fromStdString(*state.carCategory));
    if (state.carSubclass) row.insert(QStringLiteral("car_subclass"), QString::fromStdString(*state.carSubclass));
    if (state.totalLaps) row.insert(QStringLiteral("total_laps"), *state.totalLaps);
    if (state.pitState) {
        row.insert(QStringLiteral("pit_state"), QString::fromLatin1(pitStateName(*state.pitState))
            .toLower().replace(' ', '_'));
    }
    addNumber(row, "fuel_at_sample_l", state.fuelLiters);
    addNumber(row, "fuel_capacity_l", state.fuelCapacityLiters);
    if (state.tyreWear) {
        for (int i = 0; i < 4; ++i) {
            const double wear = (*state.tyreWear)[i];
            if (std::isfinite(wear)) row.insert(QStringLiteral("tyre_wear_%1_at_sample").arg(i), wear);
        }
    }
    if (state.tyreTemperaturesCelsius) {
        for (int i = 0; i < 4; ++i) {
            const double temperature = (*state.tyreTemperaturesCelsius)[i];
            if (std::isfinite(temperature)) row.insert(QStringLiteral("tyre_temp_%1_at_sample").arg(i), temperature);
        }
    }
    enqueue(row);
}

void StrategyRecorder::enqueue(const QJsonObject& row)
{
    if (!enabled_ || busy_) return;
    if (pending_.fetch_add(1) >= 32) {
        pending_.fetch_sub(1);
        qWarning() << "Pit strategy recorder queue full; dropped sample";
        status_ = QStringLiteral("Hàng đợi ghi đầy; một bản ghi đã bị bỏ qua.");
        emit changed();
        return;
    }
    const QByteArray line = QJsonDocument(row).toJson(QJsonDocument::Compact) + '\n';
    pool_.start([this, line] {
        const auto done = qScopeGuard([this] { pending_.fetch_sub(1); });
        const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        if (!QDir().mkpath(directory)) {
            reportWrite(QStringLiteral("Không tạo được thư mục ghi dữ liệu: %1").arg(directory));
            return;
        }
        const QString path = QDir(directory).filePath(QStringLiteral("pit_strategy_laps.jsonl"));
        QFile file(path);
        if (file.exists() && file.size() + line.size() > 20 * 1024 * 1024) {
            const QString archives = QDir(directory).filePath(QStringLiteral("recordings"));
            if (!QDir().mkpath(archives)) {
                reportWrite(QStringLiteral("Không tạo được thư mục lưu trữ log."));
                return;
            }
            const QString previous = QDir(archives).filePath(QStringLiteral("pit_strategy_laps-%1-%2.jsonl")
                .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmsszzz")),
                    QUuid::createUuid().toString(QUuid::WithoutBraces)));
            if (!QFile::rename(path, previous)) {
                reportWrite(QStringLiteral("Không lưu trữ được log cũ; đã dừng ghi để giữ dữ liệu."));
                return;
            }
        }
        if (!file.open(QIODevice::WriteOnly | QIODevice::Append) || file.write(line) != line.size() || !file.flush())
            reportWrite(QStringLiteral("Không lưu được dữ liệu: %1").arg(file.errorString()));
        else reportWrite({});
    });
}

} // namespace raceengineer
