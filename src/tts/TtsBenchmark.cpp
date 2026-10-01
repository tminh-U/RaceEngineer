#include "tts/TtsBenchmark.h"
#include "utils/Logging.h"
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <algorithm>
#include <cmath>

namespace raceengineer {
int chooseTtsThreads(const QJsonArray& results)
{
    if (results.size() != 3) return 0;
    double best = INFINITY;
    for (int i = 0; i < 3; ++i) {
        const auto row = results[i].toObject();
        const double p95 = row.value("p95_ms").toDouble(-1);
        if (row.value("threads").toInt() != i + 2 || row.value("samples").toInt() != 23
            || row.value("short_samples").toInt() != 20 || !std::isfinite(p95) || p95 < 0)
            return 0;
        best = std::min(best, p95);
    }
    for (const auto& value : results) {
        const auto row = value.toObject();
        if (row.value("p95_ms").toDouble() <= 1000) return row.value("threads").toInt();
    }
    for (const auto& value : results) {
        const auto row = value.toObject();
        if (row.value("p95_ms").toDouble() <= best * 1.05) return row.value("threads").toInt();
    }
    return 0;
}

QString ttsBenchmarkFingerprint(const QString& modelDir, const QString& voice, const QString& device)
{
    QStringList artifacts;
    const auto append = [&artifacts](const QString& path) {
        const QFileInfo info(path);
        artifacts << info.absoluteFilePath() + ':' + QString::number(info.size()) + ':'
            + QString::number(info.lastModified().toMSecsSinceEpoch());
    };
    QDirIterator files(modelDir, QDir::Files, QDirIterator::Subdirectories);
    while (files.hasNext()) append(files.next());
    const QDir runtimeDir(QCoreApplication::applicationDirPath());
    QDirIterator runtime(runtimeDir.path(), {"*.dll"}, QDir::Files);
    while (runtime.hasNext()) append(runtime.next());
    append(runtimeDir.filePath("RaceEngineer.exe"));
    append(runtimeDir.filePath("RaceEngineerTtsBench.exe"));
    artifacts.sort();
    return QString::fromLatin1(QCryptographicHash::hash((artifacts.join('\n') + '\n' + voice
        + '\n' + device + '\n' + QString::number(ttsBenchmarkVersion)).toUtf8(),
        QCryptographicHash::Sha256).toHex());
}

TtsBenchmark::TtsBenchmark(QObject* parent) : QObject(parent)
{
    timeout_.setSingleShot(true);
    connect(&timeout_, &QTimer::timeout, this, [this] { fail(QStringLiteral("Benchmark timeout; giữ cấu hình cũ.")); });
}
TtsBenchmark::~TtsBenchmark()
{
    if (process_ && process_->state() != QProcess::NotRunning) {
        process_->disconnect(this);
        process_->kill();
        process_->waitForFinished(3000);
    }
}
void TtsBenchmark::prepare()
{
    if (running_) return;
    running_ = true;
    results_ = {};
    level_ = 2;
    progress_ = 0;
    failure_.clear();
    status_ = QStringLiteral("Đang chờ worker giải phóng VieNeu…");
    timeout_.start(120000);
    emit changed();
}
void TtsBenchmark::start(const QString& executable, const QStringList& arguments, int timeoutMs)
{
    if (!running_) prepare();
    if (process_ || !failure_.isEmpty()) return;
    executable_ = executable;
    arguments_ = arguments;
    timeoutMs_ = timeoutMs;
    launch();
}
void TtsBenchmark::launch()
{
    buffer_.clear();
    pendingResult_ = {};
    completed_ = 0;
    auto* child = new QProcess(this);
    process_ = child;
    auto env = QProcessEnvironment::systemEnvironment();
    // Parent's owned OMP value must not become an external override in the child.
    env.remove(QStringLiteral("OMP_NUM_THREADS"));
    child->setProcessEnvironment(env);
    connect(child, &QProcess::readyReadStandardOutput, this, [this, child] {
        if (process_ == child) readOutput();
    });
    connect(child, &QProcess::readyReadStandardError, this, [child] { child->readAllStandardError(); });
    connect(child, &QProcess::errorOccurred, this, [this, child](QProcess::ProcessError error) {
        if (process_ == child && error == QProcess::FailedToStart) {
            failure_ = QStringLiteral("Không khởi chạy được harness: %1").arg(child->errorString());
            finish();
        }
    });
    connect(child, &QProcess::finished, this, [this, child](int code, QProcess::ExitStatus exit) {
        if (process_ != child) return;
        readOutput();
        if (process_ != child) return;
        timeout_.stop();
        if (failure_.isEmpty() && (code != 0 || exit != QProcess::NormalExit || !buffer_.isEmpty()
                || pendingResult_.isEmpty() || completed_ != ttsBenchmarkSamples))
            failure_ = QStringLiteral("Kết quả chưa đầy đủ hoặc harness lỗi; giữ cấu hình cũ.");
        process_ = nullptr;
        child->deleteLater();
        if (!failure_.isEmpty()) { finish(); return; }
        if (!results_.isEmpty() && results_[0].toObject().value("runtime") != pendingResult_.value("runtime")) {
            failure_ = QStringLiteral("Runtime khác nhau giữa các mức; không tự chọn.");
            finish(); return;
        }
        results_.append(pendingResult_);
        if (++level_ <= 4) { launch(); return; }
        finish();
    });
    status_ = QStringLiteral("Đang đo %1 luồng…").arg(level_);
    timeout_.start(timeoutMs_);
    emit changed();
    child->start(executable_, arguments_ + QStringList{"--threads", QString::number(level_)});
    qCInfo(logTts) << "Started TTS benchmark harness after worker release; threads" << level_;
}
void TtsBenchmark::readOutput()
{
    if (!process_) return;
    buffer_ += process_->readAllStandardOutput();
    if (buffer_.size() > 65536) { fail(QStringLiteral("Dữ liệu benchmark vượt giới hạn.")); return; }
    int end;
    while ((end = buffer_.indexOf('\n')) >= 0) {
        const auto line = buffer_.left(end);
        buffer_.remove(0, end + 1);
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject() || !consume(doc.object())) {
            fail(QStringLiteral("JSON benchmark không hợp lệ; giữ cấu hình cũ.")); return;
        }
    }
}
bool TtsBenchmark::consume(const QJsonObject& event)
{
    if (!failure_.isEmpty() || event.value("version").toInt() != ttsBenchmarkVersion
        || event.value("threads").toInt() != level_) return false;
    const auto type = event.value("type").toString();
    if (type == "progress") {
        const int count = event.value("completed").toInt(-1);
        if (!pendingResult_.isEmpty() || count != completed_ + 1 || count > ttsBenchmarkSamples) return false;
        completed_ = count;
        progress_ = ((level_ - 2) * ttsBenchmarkSamples + completed_) / double(3 * ttsBenchmarkSamples);
        emit changed();
        return true;
    }
    if (type != "result" || !pendingResult_.isEmpty() || completed_ != ttsBenchmarkSamples
        || event.value("samples").toInt() != ttsBenchmarkSamples
        || event.value("short_samples").toInt() != 20 || event.value("runtime").toString().isEmpty()) return false;
    for (const auto* key : {"p50_ms", "p95_ms", "cpu_ms", "peak_working_set", "peak_private_bytes", "init_ms"}) {
        const auto value = event.value(QLatin1String(key));
        if (!value.isDouble() || !std::isfinite(value.toDouble()) || value.toDouble() < 0) return false;
    }
    if (event.value("p50_ms").toDouble() > event.value("p95_ms").toDouble()) return false;
    pendingResult_ = event;
    return true;
}
void TtsBenchmark::fail(const QString& reason)
{
    if (!running_ || !failure_.isEmpty()) return;
    failure_ = reason;
    timeout_.stop();
    if (process_ && process_->state() != QProcess::NotRunning) process_->kill();
    else finish();
}
void TtsBenchmark::cancel(const QString& reason) { fail(reason); }
void TtsBenchmark::finish()
{
    timeout_.stop();
    if (process_) { auto* child = process_; process_ = nullptr; child->deleteLater(); }
    const int selected = failure_.isEmpty() ? chooseTtsThreads(results_) : 0;
    running_ = false;
    status_ = !failure_.isEmpty() ? failure_ : selected == 0 ? QStringLiteral("Kết quả chưa đầy đủ.")
        : results_[selected - 2].toObject().value("p95_ms").toDouble() <= 1000
            ? QStringLiteral("Đã áp dụng %1 luồng.").arg(selected)
            : QStringLiteral("Đã áp dụng %1 luồng. Chưa đạt mục tiêu 1 giây.").arg(selected);
    emit changed();
    emit finished(selected);
}
}
