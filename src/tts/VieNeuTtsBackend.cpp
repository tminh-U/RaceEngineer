#include "tts/VieNeuTtsBackend.h"
#include "tts/RacingTextNormalizer.h"
#include "utils/Logging.h"
#include "vieneu/vieneu_tts.h"

#include <QAudioFormat>
#include <QAudioSink>
#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaDevices>
#include <QRandomGenerator>
#include <QThread>

#include <algorithm>
#include <chrono>

namespace raceengineer {

class VieNeuWorker final : public QObject {
    Q_OBJECT

public:
    explicit VieNeuWorker(QString modelDir, QString voice)
        : modelDir_(std::move(modelDir))
        , voice_(std::move(voice))
    {
    }

    ~VieNeuWorker() override
    {
        if (ctx_) {
            vieneu_free(ctx_);
            ctx_ = nullptr;
        }
    }

public slots:
    void setVoice(QString voice)
    {
        voice_ = std::move(voice);
    }

    void initialize()
    {
        if (ctx_) {
            emit initialized(true, QString());
            return;
        }

        struct vieneu_init_params_v2 init_params;
        vieneu_init_v2_default_params(&init_params);
        init_params.profile = "vieneu-v3-native";
        const QByteArray modelDirLocal = modelDir_.toLocal8Bit();
        init_params.model_dir = modelDirLocal.constData();
        const QString codecDir = QDir(modelDir_).filePath(QStringLiteral("codec"));
        const QByteArray codecDirLocal = codecDir.toLocal8Bit();
        init_params.codec_dir = codecDirLocal.constData();
        init_params.n_threads = 4;
        if (!qEnvironmentVariableIsSet("OMP_NUM_THREADS")) {
            qputenv("OMP_NUM_THREADS", QByteArrayLiteral("6"));
        }

        qCInfo(logTts) << "Initializing native VieNeu-TTS from" << modelDir_;
        ctx_ = vieneu_init_v2(&init_params);
        if (!ctx_) {
            const QString err = QString::fromUtf8(vieneu_last_error());
            qCWarning(logTts) << "Failed to initialize native VieNeu-TTS:" << err;
            emit initialized(false, err);
            return;
        }

        qCInfo(logTts) << "Native VieNeu-TTS initialized successfully";
        emit initialized(true, QString());
    }

    void synthesize(quint64 requestId, const QString& rawText)
    {
        if (!ctx_) {
            emit synthesisFailed(requestId, QStringLiteral("VieNeu context not initialized"));
            return;
        }

        const QString text = RacingTextNormalizer::normalize(rawText);
        if (text.isEmpty()) {
            emit synthesisFailed(requestId, QStringLiteral("Empty text after normalization"));
            return;
        }

        struct vieneu_tts_params_v3 synth_params;
        vieneu_tts_v3_default_params(&synth_params);

        // Hyper-parameter tuning: lower temperature and top_p eliminate acoustic token jitter,
        // preventing slurred pronunciation ("ngọng") and unwanted breaths or hesitation ("ngắt ngứ").
        synth_params.temperature = 0.40f;
        synth_params.top_p = 0.90f;
        synth_params.repetition_penalty = 1.15f;
        synth_params.max_new_frames = 300;

        const QByteArray textUtf8 = text.toUtf8();
        synth_params.text = textUtf8.constData();

        // Preset voice (Minh Đức, Minh Quân, etc. defined in voices_v3_turbo.json)
        const QString activeVoice = voice_.trimmed().isEmpty() ? QStringLiteral("Minh Đức") : voice_;
        const QByteArray voiceIdUtf8 = activeVoice.toUtf8();
        synth_params.voice_id = voiceIdUtf8.constData();
        synth_params.ref_audio_path = nullptr;
        synth_params.use_ref_codes = true; // Use precomputed studio codes
        synth_params.denoise_ref = false;  // Presets are clean studio embeddings

        QByteArray styleUtf8;
        if (activeVoice == QStringLiteral("Minh Đức") || activeVoice == QStringLiteral("Mai Anh")) {
            styleUtf8 = "tin_tuc";
        } else if (activeVoice == QStringLiteral("Thái Sơn") || activeVoice == QStringLiteral("Thanh Bình")
            || activeVoice == QStringLiteral("Ngọc Linh") || activeVoice == QStringLiteral("Thục Đoan")) {
            styleUtf8 = "doc_truyen";
        } else {
            styleUtf8 = "tu_nhien";
        }
        synth_params.style = styleUtf8.constData();

        qCInfo(logTts) << "VieNeu synthesizing request" << requestId << "(voice:" << synth_params.voice_id
                       << "style:" << synth_params.style << "temp:" << synth_params.temperature << "):" << text;
        const auto t0 = std::chrono::high_resolution_clock::now();

        struct vieneu_audio audio = {0};
        const int ret = vieneu_synthesize_v3(ctx_, &synth_params, &audio);
        const auto t1 = std::chrono::high_resolution_clock::now();
        const double elapsedMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

        if (ret != 0) {
            const QString err = QString::fromUtf8(vieneu_last_error());
            qCWarning(logTts) << "VieNeu synthesis failed for request" << requestId << ":" << err;
            emit synthesisFailed(requestId, err);
            return;
        }

        const double durSec = static_cast<double>(audio.n_samples) / static_cast<double>(audio.sample_rate);
        const double rtf = (elapsedMs / 1000.0) / (durSec > 0.0 ? durSec : 1.0);
        qCInfo(logTts).nospace() << "VieNeu synthesis succeeded for request " << requestId << ": "
                                 << audio.n_samples << " samples (" << Qt::fixed << qSetRealNumberPrecision(2) << durSec << "s @ "
                                 << audio.sample_rate << " Hz) in " << elapsedMs << "ms (RTF " << rtf << ")";

        QByteArray pcm;
        pcm.resize(audio.n_samples * static_cast<int>(sizeof(int16_t)));
        auto* pcm_ptr = reinterpret_cast<int16_t*>(pcm.data());
        // Boost digital gain by ~25% to ensure engineer voice cuts through engine audio
        constexpr float kGainBoost = 1.25F;
        for (int i = 0; i < audio.n_samples; ++i) {
            float s = audio.samples[i] * kGainBoost;
            if (s > 1.0F) s = 1.0F;
            if (s < -1.0F) s = -1.0F;
            pcm_ptr[i] = static_cast<int16_t>(s * 32767.0F);
        }

        const int sampleRate = audio.sample_rate;
        vieneu_audio_free(&audio);

        emit audioReady(requestId, text, pcm, sampleRate, elapsedMs);
    }

signals:
    void initialized(bool success, const QString& error);
    void audioReady(quint64 requestId, const QString& text, const QByteArray& pcmData, int sampleRate, double elapsedMs);
    void synthesisFailed(quint64 requestId, const QString& error);

private:
    QString modelDir_;
    QString voice_{QStringLiteral("Minh Đức")};
    struct vieneu_context* ctx_{nullptr};
};

VieNeuTtsBackend::VieNeuTtsBackend(QString modelDir, QString voice, QObject* parent)
    : ITtsBackend(parent)
    , modelDir_(std::move(modelDir))
    , voice_(voice.trimmed().isEmpty() ? QStringLiteral("Minh Đức") : std::move(voice))
{
    setAudioOutputDevice(QString());

    workerThread_ = new QThread(this);
    worker_ = new VieNeuWorker(modelDir_, voice_);
    worker_->moveToThread(workerThread_);

    connect(workerThread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(this, &VieNeuTtsBackend::requestSynthesis, worker_, &VieNeuWorker::synthesize);
    connect(worker_, &VieNeuWorker::initialized, this, &VieNeuTtsBackend::onWorkerInitialized);
    connect(worker_, &VieNeuWorker::audioReady, this, &VieNeuTtsBackend::onAudioReady);
    connect(worker_, &VieNeuWorker::synthesisFailed, this, &VieNeuTtsBackend::onSynthesisFailed);

    loadCachedSpotter();

    workerThread_->start();
}

VieNeuTtsBackend::~VieNeuTtsBackend()
{
    shutdown();
}

bool VieNeuTtsBackend::isAvailable() const
{
    if (modelDir_.isEmpty()) return false;
    const QDir dir(modelDir_);
    return dir.exists(QStringLiteral("backbone.gguf"))
        && dir.exists(QStringLiteral("config.json"))
        && dir.exists(QStringLiteral("tokenizer.json"))
        && dir.exists(QStringLiteral("vieneu_v3_heads.npz"))
        && dir.exists(QStringLiteral("acoustic/vieneu_acoustic_weights.npz"))
        && dir.exists(QStringLiteral("codec/moss_audio_tokenizer_decode_full.onnx"));
}

void VieNeuTtsBackend::warmUp()
{
    if (ready_) {
        emit warmUpFinished(true, {});
        return;
    }
    if (initializing_) return;
    if (!isAvailable()) {
        const QString error = QStringLiteral("Thiếu mô hình VieNeu-TTS v3 Turbo.");
        emit statusChanged(QStringLiteral("Lỗi khởi tạo VieNeu-TTS"));
        emit warmUpFinished(false, error);
        emit errorOccurred(error);
        return;
    }
    initializing_ = true;
    emit statusChanged(QStringLiteral("Đang chuẩn bị mô hình VieNeu-TTS..."));
    QMetaObject::invokeMethod(worker_, "initialize", Qt::QueuedConnection);
}

void VieNeuTtsBackend::onWorkerInitialized(bool success, const QString& error)
{
    if (success) {
        emit statusChanged(QStringLiteral("Đang chạy inference khởi động VieNeu-TTS..."));
        emit requestSynthesis(0, QStringLiteral("Kiểm tra giọng nói."));
    } else {
        initializing_ = false;
        ready_ = false;
        emit statusChanged(QStringLiteral("Lỗi khởi tạo VieNeu-TTS"));
        emit warmUpFinished(false, error);
        emit errorOccurred(error);
    }
}

void VieNeuTtsBackend::setVoice(const QString& voice)
{
    voice_ = voice.trimmed().isEmpty() ? QStringLiteral("Minh Đức") : voice;
    if (worker_) {
        QMetaObject::invokeMethod(worker_, "setVoice", Qt::QueuedConnection,
                                  Q_ARG(QString, voice_));
    }
}

void VieNeuTtsBackend::speak(const QString& text)
{
    const QString normalized = RacingTextNormalizer::normalize(text);
    if (normalized.isEmpty()) return;

    if (!ready_) {
        deferredText_ = normalized;
        warmUp();
        return;
    }

    if (playCachedSpotter(normalized)) return;

    if (!isAvailable()) {
        emit errorOccurred(QStringLiteral("Thiếu mô hình VieNeu-TTS v3 Turbo"));
        emit speakingFinished();
        return;
    }

    activeRequestId_ = 0;
    synthesizing_ = false;
    stopPlayback();

    const quint64 reqId = nextRequestId_++;
    activeRequestId_ = reqId;
    synthesizing_ = true;

    emit requestSynthesis(reqId, normalized);
}

void VieNeuTtsBackend::stop()
{
    activeRequestId_ = 0;
    synthesizing_ = false;
    if (playbackActive_) {
        stopPlayback();
        emit speakingFinished();
    }
}

void VieNeuTtsBackend::shutdown()
{
    if (shuttingDown_) return;
    shuttingDown_ = true;

    stop();

    if (workerThread_) {
        workerThread_->quit();
        workerThread_->wait(2000);
    }
}

void VieNeuTtsBackend::setVolume(float volume)
{
    volume_ = std::clamp(volume, 0.0F, 1.0F);
    if (audioSink_) {
        audioSink_->setVolume(volume_);
    }
}

void VieNeuTtsBackend::setSpeed(float speed)
{
    speed_ = std::clamp(speed, 0.5F, 2.0F);
}

void VieNeuTtsBackend::setAudioOutputDevice(const QString& description)
{
    if (description.trimmed().isEmpty()) {
        audioDevice_ = QMediaDevices::defaultAudioOutput();
        return;
    }
    const auto devices = QMediaDevices::audioOutputs();
    for (const auto& device : devices) {
        if (device.description().compare(description, Qt::CaseInsensitive) == 0) {
            audioDevice_ = device;
            return;
        }
    }
    audioDevice_ = QMediaDevices::defaultAudioOutput();
}

void VieNeuTtsBackend::onAudioReady(quint64 requestId, const QString& text, const QByteArray& pcmData, int sampleRate, double elapsedMs)
{
    Q_UNUSED(elapsedMs);
    if (requestId == 0 && initializing_ && !shuttingDown_) {
        initializing_ = false;
        ready_ = true;
        emit statusChanged(QStringLiteral("VieNeu-TTS sẵn sàng"));
        emit warmUpFinished(true, {});
        const QString deferred = deferredText_;
        deferredText_.clear();
        if (!deferred.isEmpty()) speak(deferred);
        return; // Discard warm-up audio; never play it to the driver.
    }
    if (requestId != activeRequestId_ || shuttingDown_) {
        return;
    }
    synthesizing_ = false;
    startPlayback(text, pcmData, sampleRate);
}

void VieNeuTtsBackend::onSynthesisFailed(quint64 requestId, const QString& error)
{
    if (requestId == 0 && initializing_ && !shuttingDown_) {
        initializing_ = false;
        ready_ = false;
        emit statusChanged(QStringLiteral("Lỗi khởi chạy inference VieNeu-TTS"));
        emit warmUpFinished(false, error);
        emit errorOccurred(error);
        return;
    }
    if (requestId != activeRequestId_ || shuttingDown_) {
        return;
    }
    synthesizing_ = false;
    activeRequestId_ = 0;
    emit errorOccurred(error);
    emit speakingFinished();
}

void VieNeuTtsBackend::startPlayback(const QString& text, const QByteArray& pcmData, int sampleRate)
{
    stopPlayback();

    currentPlayingText_ = text;
    playbackActive_ = true;

    audioBuffer_ = new QBuffer(this);
    audioBuffer_->setData(pcmData);
    if (!audioBuffer_->open(QIODevice::ReadOnly)) {
        qCWarning(logTts) << "Failed to open in-memory PCM buffer for VieNeu playback";
        stopPlayback();
        emit speakingFinished();
        return;
    }

    QAudioFormat format;
    format.setSampleRate(sampleRate > 0 ? sampleRate : 48000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    const QAudioDevice device = audioDevice_.isNull() ? QMediaDevices::defaultAudioOutput() : audioDevice_;
    audioSink_ = new QAudioSink(device, format, this);
    audioSink_->setVolume(volume_);

    connect(audioSink_, &QAudioSink::stateChanged, this, [this](QAudio::State state) {
        if (state == QAudio::IdleState || state == QAudio::StoppedState) {
            // Defer to avoid re-entrant crash: QAudioSink::stop() emits
            // stateChanged(StoppedState) synchronously within the same call stack,
            // which would re-enter stopPlayback() while audioSink_ is mid-teardown.
            QMetaObject::invokeMethod(this, [this] {
                if (!playbackActive_) return;
                stopPlayback();
                emit speakingFinished();
            }, Qt::QueuedConnection);
        }
    });

    emit speakingStarted(text);
    audioSink_->start(audioBuffer_);
}

void VieNeuTtsBackend::stopPlayback()
{
    if (!playbackActive_) return;
    playbackActive_ = false;
    currentPlayingText_.clear();

    if (audioSink_) {
        audioSink_->disconnect(this);
        audioSink_->stop();
        audioSink_->deleteLater();
        audioSink_ = nullptr;
    }
    if (audioBuffer_) {
        audioBuffer_->close();
        audioBuffer_->deleteLater();
        audioBuffer_ = nullptr;
    }
}

void VieNeuTtsBackend::loadCachedSpotter()
{
    cachedSpotterDirectory_ = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("audio/spotter"));
    if (!QDir(cachedSpotterDirectory_).exists()) {
        cachedSpotterDirectory_ = QStringLiteral("assets/spotter");
    }
    QFile manifest(QDir(cachedSpotterDirectory_).filePath(QStringLiteral("manifest.json")));
    if (!manifest.open(QIODevice::ReadOnly)) return;
    const QJsonDocument document = QJsonDocument::fromJson(manifest.readAll());
    if (!document.isObject()) return;
    for (const QJsonValue& value : document.object().value(QStringLiteral("entries")).toArray()) {
        const QJsonObject entry = value.toObject();
        const QString phraseText = entry.value(QStringLiteral("text")).toString().trimmed();
        QStringList files;
        for (const QJsonValue& file : entry.value(QStringLiteral("files")).toArray()) {
            const QString relativePath = file.toString();
            if (QFileInfo(QDir(cachedSpotterDirectory_).filePath(relativePath)).isFile()) {
                files.append(relativePath);
            }
        }
        if (!phraseText.isEmpty() && !files.isEmpty()) {
            cachedSpotterFiles_.insert(phraseText, files);
        }
    }
}

bool VieNeuTtsBackend::playCachedSpotter(const QString& text)
{
    const auto iterator = cachedSpotterFiles_.constFind(text.trimmed());
    if (iterator == cachedSpotterFiles_.cend() || iterator->isEmpty()) return false;
    const QStringList& variants = iterator.value();
    int index = QRandomGenerator::global()->bounded(variants.size());
    const int previous = lastCachedSpotterVariant_.value(text, -1);
    if (variants.size() > 1 && index == previous) index = (index + 1) % variants.size();
    lastCachedSpotterVariant_.insert(text, index);

    const QString wavPath = QDir(cachedSpotterDirectory_).filePath(variants.at(index));
    QFile wavFile(wavPath);
    if (!wavFile.open(QIODevice::ReadOnly)) return false;

    const QByteArray data = wavFile.readAll();
    if (data.size() < 44 || !data.startsWith("RIFF")) return false;

    quint32 sampleRate = *reinterpret_cast<const quint32*>(data.constData() + 24);

    int dataOffset = 12;
    int dataSize = 0;
    while (dataOffset + 8 <= data.size()) {
        const char* chunkId = data.constData() + dataOffset;
        const quint32 chunkSize = *reinterpret_cast<const quint32*>(data.constData() + dataOffset + 4);
        if (memcmp(chunkId, "data", 4) == 0) {
            dataOffset += 8;
            dataSize = static_cast<int>(chunkSize);
            break;
        }
        dataOffset += 8 + static_cast<int>(chunkSize);
    }

    if (dataSize <= 0 || dataOffset + dataSize > data.size()) {
        dataOffset = 44;
        dataSize = data.size() - 44;
    }

    const QByteArray pcmData(data.constData() + dataOffset, dataSize);

    activeRequestId_ = 0;
    synthesizing_ = false;
    stopPlayback();
    emit statusChanged(QStringLiteral("Đang phát spotter VieNeu đã lưu"));
    startPlayback(text, pcmData, sampleRate > 0 ? sampleRate : 48000);
    return true;
}

} // namespace raceengineer

#include "VieNeuTtsBackend.moc"
