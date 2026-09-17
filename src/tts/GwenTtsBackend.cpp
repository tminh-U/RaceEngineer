#include "tts/GwenTtsBackend.h"

#include "utils/Logging.h"

#include <QAudioOutput>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaPlayer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>
#include <QUuid>

#include <algorithm>

#ifdef Q_OS_WIN
#include <Windows.h>
#endif

namespace raceengineer {
namespace {

constexpr quint16 serverPort = 8397;
constexpr int maximumHealthAttempts = 240;

QUrl endpoint(const QString& path)
{
    return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(serverPort).arg(path));
}

QString responseError(const QByteArray& body, const QString& fallback)
{
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (document.isObject()) {
        const QJsonValue error = document.object().value(QStringLiteral("error"));
        if (error.isObject()) {
            const QString message = error.toObject().value(QStringLiteral("message")).toString();
            if (!message.isEmpty()) return message;
        } else if (error.isString()) {
            return error.toString();
        }
    }
    return fallback;
}

} // namespace

GwenTtsBackend::GwenTtsBackend(QString executablePath, QString modelPath,
    QString codecPath, QString voiceDirectory, QObject* const parent)
    : ITtsBackend(parent)
    , executablePath_(std::move(executablePath))
    , modelPath_(std::move(modelPath))
    , codecPath_(std::move(codecPath))
    , voiceDirectory_(std::move(voiceDirectory))
    , voicePath_(QDir(voiceDirectory_).filePath(QStringLiteral("khanh_toan.wav")))
    , server_(new QProcess(this))
    , network_(new QNetworkAccessManager(this))
    , healthTimer_(new QTimer(this))
    , player_(new QMediaPlayer(this))
    , output_(new QAudioOutput(this))
{
    QFile transcript(QDir(voiceDirectory_).filePath(QStringLiteral("khanh_toan.txt")));
    if (transcript.open(QIODevice::ReadOnly | QIODevice::Text)) {
        voiceTranscript_ = QString::fromUtf8(transcript.readAll()).trimmed();
    }
    loadCachedSpotter();

    player_->setAudioOutput(output_);
    output_->setVolume(0.85F);
    healthTimer_->setInterval(250);

    connect(healthTimer_, &QTimer::timeout, this, &GwenTtsBackend::pollHealth);
    connect(server_, &QProcess::started, this, [this] {
#ifdef Q_OS_WIN
        HANDLE process = OpenProcess(PROCESS_SET_INFORMATION, FALSE,
            static_cast<DWORD>(server_->processId()));
        if (process == nullptr || !SetPriorityClass(process, BELOW_NORMAL_PRIORITY_CLASS)) {
            qCWarning(logTts) << "Could not lower Gwen-TTS process priority";
        }
        if (process != nullptr) CloseHandle(process);
#endif
        healthAttempts_ = 0;
        healthTimer_->start();
        pollHealth();
    });
    connect(server_, &QProcess::readyReadStandardError, this, [this] {
        const QString output = QString::fromUtf8(server_->readAllStandardError()).trimmed();
        if (!output.isEmpty()) qCDebug(logTts).noquote() << output;
    });
    connect(server_, &QProcess::readyReadStandardOutput, this, [this] {
        const QString output = QString::fromUtf8(server_->readAllStandardOutput()).trimmed();
        if (!output.isEmpty()) qCDebug(logTts).noquote() << output;
    });
    connect(server_, &QProcess::errorOccurred, this, [this](const QProcess::ProcessError error) {
        if (shuttingDown_) return;
        if (error != QProcess::FailedToStart) {
            qCWarning(logTts).noquote() << server_->errorString();
            return;
        }
        setServerReady(false);
        finishWithError(QStringLiteral("Không thể khởi động Gwen-TTS: %1")
                            .arg(server_->errorString()));
    });
    connect(server_, &QProcess::finished, this,
        [this](const int exitCode, const QProcess::ExitStatus status) {
            healthTimer_->stop();
            setServerReady(false);
            if (shuttingDown_) return;
            const QString detail = QString::fromUtf8(server_->readAllStandardError()).trimmed();
            const QString reason = detail.isEmpty()
                ? QStringLiteral("Gwen-TTS server đã dừng (mã %1, trạng thái %2).")
                      .arg(exitCode)
                      .arg(static_cast<int>(status))
                : detail;
            finishWithError(reason);
        });
    connect(player_, &QMediaPlayer::mediaStatusChanged, this,
        [this](const QMediaPlayer::MediaStatus status) {
            if (status != QMediaPlayer::EndOfMedia || !playbackActive_) return;
            playbackActive_ = false;
            pendingText_.clear();
            cleanTemporaryAudio();
            emit speakingFinished();
        });
    connect(player_, &QMediaPlayer::errorOccurred, this,
        [this](QMediaPlayer::Error, const QString& detail) {
            if (playbackActive_) finishWithError(detail);
        });
}

GwenTtsBackend::~GwenTtsBackend()
{
    shutdown();
}

bool GwenTtsBackend::isAvailable() const
{
    return dynamicTtsAvailable() || !cachedSpotterFiles_.isEmpty();
}

bool GwenTtsBackend::dynamicTtsAvailable() const
{
    return QFileInfo(executablePath_).isFile()
        && QFileInfo(modelPath_).isFile()
        && QFileInfo(codecPath_).isFile()
        && QFileInfo(voicePath_).isFile()
        && !voiceTranscript_.isEmpty();
}

void GwenTtsBackend::warmUp()
{
    if (shuttingDown_ || serverReady_ || server_->state() != QProcess::NotRunning) return;
    if (!dynamicTtsAvailable()) {
        emit statusChanged(QStringLiteral("Thiếu runtime/model Gwen-TTS"));
        return;
    }

    emit statusChanged(QStringLiteral("Đang nạp Gwen-TTS · Khánh Toàn…"));
    server_->setProgram(executablePath_);
    server_->setWorkingDirectory(QFileInfo(executablePath_).absolutePath());
    server_->setProcessChannelMode(QProcess::SeparateChannels);
    server_->setArguments({
        QStringLiteral("--server"),
        QStringLiteral("--backend"), QStringLiteral("qwen3-tts"),
        QStringLiteral("--model"), modelPath_,
        QStringLiteral("--codec-model"), codecPath_,
        QStringLiteral("--voice-dir"), voiceDirectory_,
        QStringLiteral("--i-have-rights"),
        QStringLiteral("--speaker-identity"), QStringLiteral("real_person"),
        QStringLiteral("--no-spoken-disclaimer"),
        QStringLiteral("--accept-marking-responsibility"),
        QStringLiteral("--no-punctuation"),
        QStringLiteral("--host"), QStringLiteral("127.0.0.1"),
        QStringLiteral("--port"), QString::number(serverPort),
        QStringLiteral("--gpu-backend"), QStringLiteral("vulkan"),
        QStringLiteral("--seed"), QStringLiteral("42"),
        QStringLiteral("--threads"), QStringLiteral("8")});
    server_->start();
}

void GwenTtsBackend::speak(const QString& text)
{
    cancelActiveRequest();
    pendingText_ = text.trimmed();
    if (pendingText_.isEmpty()) {
        emit speakingFinished();
        return;
    }
    if (playCachedSpotter(pendingText_)) return;
    if (!dynamicTtsAvailable()) {
        finishWithError(QStringLiteral("Gwen-TTS runtime, model hoặc giọng Khánh Toàn chưa được cài."));
        return;
    }

    emit speakingStarted(pendingText_);
    if (serverReady_) requestSpeech();
    else warmUp();
}

void GwenTtsBackend::loadCachedSpotter()
{
    cachedSpotterDirectory_ = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("audio/spotter"));
    QFile manifest(QDir(cachedSpotterDirectory_).filePath(QStringLiteral("manifest.json")));
    if (!manifest.open(QIODevice::ReadOnly)) return;
    const QJsonDocument document = QJsonDocument::fromJson(manifest.readAll());
    if (!document.isObject()) return;
    const QJsonArray entries = document.object().value(QStringLiteral("entries")).toArray();
    for (const QJsonValue& value : entries) {
        const QJsonObject entry = value.toObject();
        const QString text = entry.value(QStringLiteral("text")).toString().trimmed();
        QStringList files;
        for (const QJsonValue& file : entry.value(QStringLiteral("files")).toArray()) {
            const QString relativePath = file.toString();
            if (QFileInfo(QDir(cachedSpotterDirectory_).filePath(relativePath)).isFile()) {
                files.append(relativePath);
            }
        }
        if (!text.isEmpty() && !files.isEmpty()) cachedSpotterFiles_.insert(text, files);
    }
    if (!cachedSpotterFiles_.isEmpty()) {
        qCInfo(logTts) << "Loaded cached spotter phrases:" << cachedSpotterFiles_.size();
    }
}

bool GwenTtsBackend::playCachedSpotter(const QString& text)
{
    const QString key = text.trimmed();
    const auto iterator = cachedSpotterFiles_.constFind(key);
    if (iterator == cachedSpotterFiles_.cend() || iterator->isEmpty()) return false;
    const QStringList& variants = iterator.value();
    int index = QRandomGenerator::global()->bounded(variants.size());
    const int previous = lastCachedSpotterVariant_.value(key, -1);
    if (variants.size() > 1 && index == previous) index = (index + 1) % variants.size();
    lastCachedSpotterVariant_.insert(key, index);

    playbackActive_ = true;
    emit speakingStarted(key);
    emit statusChanged(QStringLiteral("Đang phát spotter đã lưu"));
    player_->setSource(QUrl::fromLocalFile(
        QDir(cachedSpotterDirectory_).filePath(variants.at(index))));
    player_->play();
    return true;
}

void GwenTtsBackend::stop()
{
    pendingText_.clear();
    cancelActiveRequest();
}

void GwenTtsBackend::shutdown()
{
    if (shuttingDown_) return;
    shuttingDown_ = true;
    healthTimer_->stop();
    if (healthReply_) {
        disconnect(healthReply_, nullptr, this, nullptr);
        healthReply_->abort();
        healthReply_->deleteLater();
        healthReply_.clear();
    }
    stop();
    if (server_->state() != QProcess::NotRunning) {
        server_->terminate();
        if (!server_->waitForFinished(2000)) {
            server_->kill();
            server_->waitForFinished(1000);
        }
    }
    setServerReady(false);
}

void GwenTtsBackend::setVolume(const float volume)
{
    output_->setVolume(std::clamp(volume, 0.0F, 1.0F));
}

void GwenTtsBackend::setSpeed(const float speed)
{
    speed_ = std::clamp(speed, 0.5F, 2.0F);
}

void GwenTtsBackend::pollHealth()
{
    if (shuttingDown_ || serverReady_ || healthReply_) return;
    if (++healthAttempts_ > maximumHealthAttempts) {
        healthTimer_->stop();
        finishWithError(QStringLiteral("Gwen-TTS mất quá 60 giây để khởi động."));
        return;
    }

    QNetworkRequest request(endpoint(QStringLiteral("/health")));
    request.setTransferTimeout(1000);
    QNetworkReply* const reply = network_->get(request);
    healthReply_ = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (healthReply_ != reply) return;
        healthReply_.clear();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());
        const bool ready = status == 200 && document.isObject()
            && document.object().value(QStringLiteral("status")).toString() == QStringLiteral("ok");
        reply->deleteLater();
        if (!ready) return;

        healthTimer_->stop();
        setServerReady(true);
        if (!pendingText_.isEmpty()) requestSpeech();
    });
}

void GwenTtsBackend::requestSpeech()
{
    if (!serverReady_ || pendingText_.isEmpty() || speechReply_) return;

    emit statusChanged(QStringLiteral("Đang tổng hợp giọng Khánh Toàn…"));

    QJsonObject payload{
        {QStringLiteral("model"), QStringLiteral("gwen-tts")},
        {QStringLiteral("input"), pendingText_},
        {QStringLiteral("voice"), QStringLiteral("khanh_toan")},
        {QStringLiteral("ref_text"), voiceTranscript_},
        {QStringLiteral("language"), QStringLiteral("vi")},
        {QStringLiteral("response_format"), QStringLiteral("wav")},
        {QStringLiteral("seed"), 42},
        {QStringLiteral("spoken_disclaimer"), false},
        {QStringLiteral("consent_attestation"),
            QStringLiteral("User requested the upstream Gwen-TTS khanh_toan preset published by G-Group AI Lab.")},
        {QStringLiteral("marking_attestation"),
            QStringLiteral("RaceEngineer visibly labels this output as AI-generated local TTS; watermark and C2PA remain enabled.")}};
    if (speed_ != 1.0F) payload.insert(QStringLiteral("speed"), speed_);

    QNetworkRequest request(endpoint(QStringLiteral("/v1/audio/speech")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(120000);
    QNetworkReply* const reply = network_->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    speechReply_ = reply;
    qCInfo(logTts) << "Gwen-TTS synthesis started with khanh_toan";
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (speechReply_ != reply) return;
        speechReply_.clear();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        const QString networkError = reply->errorString();
        reply->deleteLater();

        if (status != 200 || body.size() < 44 || !body.startsWith("RIFF")) {
            finishWithError(responseError(body,
                QStringLiteral("Gwen-TTS synthesis failed (HTTP %1): %2")
                    .arg(status)
                    .arg(networkError)));
            return;
        }

        temporaryAudioPath_ = QDir::temp().filePath(
            QStringLiteral("raceengineer-gwen-%1.wav")
                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        QSaveFile outputFile(temporaryAudioPath_);
        if (!outputFile.open(QIODevice::WriteOnly)
            || outputFile.write(body) != body.size()
            || !outputFile.commit()) {
            finishWithError(QStringLiteral("Không thể lưu âm thanh tạm thời của Gwen-TTS."));
            return;
        }

        playbackActive_ = true;
        emit statusChanged(QStringLiteral("Đang phát giọng Khánh Toàn"));
        player_->setSource(QUrl::fromLocalFile(temporaryAudioPath_));
        player_->play();
    });
}

void GwenTtsBackend::cancelActiveRequest()
{
    if (speechReply_) {
        disconnect(speechReply_, nullptr, this, nullptr);
        speechReply_->abort();
        speechReply_->deleteLater();
        speechReply_.clear();
    }
    playbackActive_ = false;
    player_->stop();
    player_->setSource({});
    cleanTemporaryAudio();
}

void GwenTtsBackend::finishWithError(const QString& error)
{
    const bool hadActiveSpeech = !pendingText_.isEmpty() || speechReply_ || playbackActive_;
    pendingText_.clear();
    cancelActiveRequest();
    if (hadActiveSpeech) emit speakingFinished();
    emit errorOccurred(error);
}

void GwenTtsBackend::cleanTemporaryAudio()
{
    if (temporaryAudioPath_.isEmpty()) return;
    const QString path = temporaryAudioPath_;
    temporaryAudioPath_.clear();
    QFile::remove(path);
}

void GwenTtsBackend::setServerReady(const bool ready)
{
    if (serverReady_ == ready) return;
    serverReady_ = ready;
    emit statusChanged(ready
            ? QStringLiteral("AI voice · Gwen-TTS · Khánh Toàn sẵn sàng")
            : QStringLiteral("AI voice · Gwen-TTS chưa sẵn sàng"));
}

} // namespace raceengineer
