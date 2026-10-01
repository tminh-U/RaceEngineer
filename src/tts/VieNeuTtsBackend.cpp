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
#include <QTimer>
#include <QSet>
#include <QtEndian>
#include <cstring>
#include <QtGlobal>

#include <algorithm>
#include <chrono>

namespace raceengineer {

namespace {
struct InitialOmpOverride final {
    bool active{false};
    bool valid{false};
    int threads{0};
};

const InitialOmpOverride& initialOmpOverride()
{
    static const InitialOmpOverride value = [] {
        bool ok = false;
        const int threads = qEnvironmentVariableIntValue("OMP_NUM_THREADS", &ok);
        return InitialOmpOverride{qEnvironmentVariableIsSet("OMP_NUM_THREADS"), ok && threads > 0, threads};
    }();
    return value;
}
}

class VieNeuWorker final : public QObject {
    Q_OBJECT

public:
    explicit VieNeuWorker(QString modelDir,
        QString voice,
        LocalAiRuntimeSelection runtimeSelection,
        const int cpuThreads)
        : modelDir_(std::move(modelDir))
        , voice_(std::move(voice))
        , runtimeSelection_(std::move(runtimeSelection))
        , cpuThreads_(cpuThreads)
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

    void setCpuThreads(const int cpuThreads)
    {
        if (!ctx_) cpuThreads_ = cpuThreads;
    }

    void releaseContext(const quint64 generation)
    {
        if (ctx_) {
            vieneu_free(ctx_);
            ctx_ = nullptr;
        }
        generation_ = generation;
        qCInfo(logTts) << "VieNeu context released on worker; generation" << generation;
        emit contextReleased(generation);
    }

    void initialize(const quint64 generation)
    {
        generation_ = generation;
        if (ctx_) {
            emit initialized(generation, true, {}, runtimeBackend(), runtimeSelection_.fallbackReason);
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
        const InitialOmpOverride& ompOverride = initialOmpOverride();
        const int effectiveThreads = ompOverride.valid ? ompOverride.threads : cpuThreads_;
        init_params.n_threads = effectiveThreads;
        if (!ompOverride.active) {
            qputenv("OMP_NUM_THREADS", QByteArray::number(effectiveThreads));
        }
        qCInfo(logTts) << "VieNeu CPU threads:" << effectiveThreads
                       << "(setting:" << cpuThreads_ << ", OMP override:" << ompOverride.active << ')';

        qCInfo(logTts) << "Initializing native VieNeu-TTS from" << modelDir_;
        ctx_ = vieneu_init_v2(&init_params);
        if (!ctx_ && runtimeSelection_.useVulkan) {
            const QString gpuError = QString::fromUtf8(vieneu_last_error());
            runtimeSelection_.useVulkan = false;
            runtimeSelection_.resolvedDeviceId = QStringLiteral("cpu");
            runtimeSelection_.label = QStringLiteral("CPU");
            runtimeSelection_.fallbackToCpu = true;
            runtimeSelection_.fallbackReason =
                QStringLiteral("VieNeu Vulkan init thất bại: %1").arg(gpuError);
            qputenv("VIENEU_GPU_LAYERS", QByteArrayLiteral("0"));
            qunsetenv("VIENEU_GPU_DEVICE");
            qputenv("VIENEU_ORT_EP", QByteArrayLiteral("cpu"));
            ctx_ = vieneu_init_v2(&init_params);
        }
        if (!ctx_) {
            const QString err = QString::fromUtf8(vieneu_last_error());
            qCWarning(logTts) << "Failed to initialize native VieNeu-TTS:" << err;
            emit initialized(generation, false, err, QString(), runtimeSelection_.fallbackReason);
            return;
        }

        qCInfo(logTts) << "Native VieNeu-TTS initialized successfully";
        emit initialized(generation, true, {}, runtimeBackend(), runtimeSelection_.fallbackReason);
    }

    void synthesize(const quint64 generation, const quint64 requestId, const QString& rawText)
    {
        synthesizeText(generation, requestId, rawText, false);
    }

    void benchmarkSynthesize(const quint64 generation, const quint64 requestId, const QString& rawText)
    {
        synthesizeText(generation, requestId, rawText, true);
    }

private:
    void synthesizeText(const quint64 generation, const quint64 requestId, const QString& rawText,
        const bool measuredOnly)
    {
        const auto fail = [this, generation, requestId, measuredOnly](const QString& error) {
            if (measuredOnly) emit synthesisMeasured(generation, requestId, 0.0, false, error);
            else emit synthesisFailed(generation, requestId, error);
        };

        if (generation != generation_) return;
        if (!ctx_) {
            fail(QStringLiteral("VieNeu context not initialized"));
            return;
        }

        const QString text = RacingTextNormalizer::normalize(rawText);
        if (text.isEmpty()) {
            fail(QStringLiteral("Empty text after normalization"));
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
            fail(err);
            return;
        }

        const double durSec = static_cast<double>(audio.n_samples) / static_cast<double>(audio.sample_rate);
        const double rtf = (elapsedMs / 1000.0) / (durSec > 0.0 ? durSec : 1.0);
        qCInfo(logTts).nospace() << "VieNeu synthesis succeeded for request " << requestId << ": "
                                 << audio.n_samples << " samples (" << Qt::fixed << qSetRealNumberPrecision(2) << durSec << "s @ "
                                 << audio.sample_rate << " Hz) in " << elapsedMs << "ms (RTF " << rtf << ")";

        if (measuredOnly) {
            vieneu_audio_free(&audio);
            emit synthesisMeasured(generation, requestId, elapsedMs, true, {});
            return;
        }

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

        emit audioReady(generation, requestId, text, pcm, sampleRate, elapsedMs);
    }

signals:
    void initialized(quint64 generation, bool success, const QString& error,
        const QString& backend, const QString& fallbackReason);
    void contextReleased(quint64 generation);
    void audioReady(quint64 generation, quint64 requestId, const QString& text,
        const QByteArray& pcmData, int sampleRate, double elapsedMs);
    void synthesisFailed(quint64 generation, quint64 requestId, const QString& error);
    void synthesisMeasured(quint64 generation, quint64 requestId,
        double elapsedMs, bool success, const QString& error);

private:
    [[nodiscard]] QString runtimeBackend() const
    {
        return runtimeSelection_.useVulkan
            ? QStringLiteral("%1 backbone · CPU codec").arg(runtimeSelection_.label)
            : QStringLiteral("CPU backbone · CPU codec");
    }

    QString modelDir_;
    QString voice_{QStringLiteral("Minh Đức")};
    LocalAiRuntimeSelection runtimeSelection_;
    int cpuThreads_{4};
    quint64 generation_{1};
    struct vieneu_context* ctx_{nullptr};
};

VieNeuTtsBackend::VieNeuTtsBackend(QString modelDir,
    QString voice,
    LocalAiRuntimeSelection runtimeSelection,
    const int cpuThreads,
    QObject* parent)
    : ITtsBackend(parent)
    , modelDir_(std::move(modelDir))
    , runtimeSelection_(std::move(runtimeSelection))
    , cpuThreads_(cpuThreads == 2 || cpuThreads == 3 ? cpuThreads : 4)
    , voice_(voice.trimmed().isEmpty() ? QStringLiteral("Minh Đức") : std::move(voice))
{
    (void)initialOmpOverride();
    setAudioOutputDevice(QString());

    workerThread_ = new QThread(this);
    worker_ = new VieNeuWorker(modelDir_, voice_, runtimeSelection_, cpuThreads_);
    worker_->moveToThread(workerThread_);

    connect(workerThread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(this, &VieNeuTtsBackend::requestSynthesis, worker_, &VieNeuWorker::synthesize);
    connect(this, &VieNeuTtsBackend::requestBenchmarkSynthesisInternal,
        worker_, &VieNeuWorker::benchmarkSynthesize);
    connect(worker_, &VieNeuWorker::initialized, this, &VieNeuTtsBackend::onWorkerInitialized);
    connect(worker_, &VieNeuWorker::contextReleased, this, &VieNeuTtsBackend::onWorkerContextReleased);
    connect(worker_, &VieNeuWorker::audioReady, this, &VieNeuTtsBackend::onAudioReady);
    connect(worker_, &VieNeuWorker::synthesisFailed, this, &VieNeuTtsBackend::onSynthesisFailed);
    connect(worker_, &VieNeuWorker::synthesisMeasured,
        this, &VieNeuTtsBackend::onBenchmarkSynthesisMeasured);

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

bool VieNeuTtsBackend::hasExternalThreadOverride()
{
    return initialOmpOverride().active;
}

bool VieNeuTtsBackend::setCpuThreads(const int cpuThreads)
{
    if ((cpuThreads != 2 && cpuThreads != 3 && cpuThreads != 4)
        || !contextReleased_ || releasePending_ || initializing_ || ready_ || !worker_) {
        return false;
    }
    cpuThreads_ = cpuThreads;
    if (!hasExternalThreadOverride()) {
        qputenv("OMP_NUM_THREADS", QByteArray::number(cpuThreads));
    }
    return QMetaObject::invokeMethod(worker_, "setCpuThreads", Qt::QueuedConnection,
        Q_ARG(int, cpuThreads));
}

void VieNeuTtsBackend::warmUp()
{
    if (releasePending_) {
        warmUpRequestedAfterRelease_ = true;
        return;
    }
    if (ready_) {
        emit initializationFinished(true, QStringLiteral("VieNeu-TTS"), {});
        emit warmUpFinished(true, {});
        return;
    }
    if (initializing_) return;
    if (!isAvailable()) {
        const QString error = QStringLiteral("Thiếu mô hình VieNeu-TTS v3 Turbo.");
        emit statusChanged(QStringLiteral("Lỗi khởi tạo VieNeu-TTS"));
        emit initializationFinished(false, QStringLiteral("Unavailable"), error);
        emit warmUpFinished(false, error);
        emit errorOccurred(error);
        return;
    }
    initializing_ = true;
    contextReleased_ = false;
    emit statusChanged(QStringLiteral("Đang chuẩn bị mô hình VieNeu-TTS..."));
    QMetaObject::invokeMethod(worker_, "initialize", Qt::QueuedConnection,
        Q_ARG(quint64, workerGeneration_));
}

void VieNeuTtsBackend::onWorkerInitialized(const quint64 generation, bool success,
    const QString& error,
    const QString& backend,
    const QString& fallbackReason)
{
    if (generation != workerGeneration_ || !initializing_ || shuttingDown_) return;
    emit initializationFinished(success, backend, error);
    if (success) {
        emit runtimeBackendChanged(backend, fallbackReason);
        emit statusChanged(QStringLiteral("Đang chạy inference khởi động VieNeu-TTS..."));
        emit requestSynthesis(generation, 0, QStringLiteral("Kiểm tra giọng nói."));
    } else {
        emit runtimeBackendChanged(QStringLiteral("Không sẵn sàng"), fallbackReason);
        initializing_ = false;
        ready_ = false;
        contextReleased_ = true;
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

    requestTimer_.start();
    if (playCachedSpotter(normalized)) return;
    if (releasePending_ || activeBenchmarkRequestId_ != 0) {
        emit speakingFinished();
        return;
    }

    if (!ready_) {
        deferredText_ = normalized;
        warmUp();
        return;
    }

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

    emit requestSynthesis(workerGeneration_, reqId, normalized);
}

quint64 VieNeuTtsBackend::pauseSpeech()
{
    if (audioSink_ && audioSink_->state() == QAudio::IdleState) return 0;
    if (pausedSpeech_.size() >= 5) return 0;
    const quint64 token = nextRequestId_++;
    PausedSpeech saved{audioSink_, audioBuffer_, activeRequestId_, currentPlayingText_, {}, 0,
        synthesizing_, deferredText_};
    if (audioSink_) audioSink_->suspend();
    pausedSpeech_.insert(token, saved);
    audioSink_ = nullptr;
    audioBuffer_ = nullptr;
    playbackActive_ = false;
    activeRequestId_ = 0;
    synthesizing_ = false;
    deferredText_.clear();
    currentPlayingText_.clear();
    return token;
}

bool VieNeuTtsBackend::resumeSpeech(const quint64 token)
{
    if (!pausedSpeech_.contains(token)) return false;
    const auto saved = pausedSpeech_.take(token);
    stopPlayback();
    activeRequestId_ = saved.requestId;
    synthesizing_ = saved.synthesizing;
    if (saved.sink) {
        audioSink_ = saved.sink;
        audioBuffer_ = saved.buffer;
        playbackActive_ = true;
        currentPlayingText_ = saved.text;
        audioSink_->setVolume(volume_);
        audioSink_->resume();
    } else if (!saved.pcm.isEmpty()) {
        startPlayback(saved.text, saved.pcm, saved.sampleRate);
    } else if (!saved.deferred.isEmpty()) {
        speak(saved.deferred);
    } else if (!saved.synthesizing) {
        if (!saved.failure.isEmpty())
            QTimer::singleShot(0, this, [this, error = saved.failure] { emit errorOccurred(error); });
        return false;
    }
    return true;
}

void VieNeuTtsBackend::discardSpeech(const quint64 token)
{
    const auto saved = pausedSpeech_.take(token);
    if (saved.sink) {
        saved.sink->disconnect(this);
        saved.sink->stop();
        saved.sink->deleteLater();
    }
    if (saved.buffer) saved.buffer->deleteLater();
}

void VieNeuTtsBackend::requestBenchmarkSynthesis(const QString& text)
{
    const QString error = releasePending_ ? QStringLiteral("VieNeu context is being released")
        : !ready_ ? QStringLiteral("VieNeu context is not initialized") : QString();
    if (!error.isEmpty()) {
        emit synthesisMeasured(0.0, false, error);
        return;
    }
    if (activeBenchmarkRequestId_ != 0) {
        emit synthesisMeasured(0.0, false, QStringLiteral("Benchmark synthesis is already running"));
        return;
    }
    stop();
    activeBenchmarkRequestId_ = nextRequestId_++;
    benchmarkTimer_.start();
    emit requestBenchmarkSynthesisInternal(workerGeneration_, activeBenchmarkRequestId_, text);
}

void VieNeuTtsBackend::stop()
{
    activeRequestId_ = 0;
    synthesizing_ = false;
    if (playbackActive_) {
        stopPlayback();
        emit speakingFinished();
    }
    if (activeBenchmarkRequestId_ != 0) {
        activeBenchmarkRequestId_ = 0;
        benchmarkTimer_.invalidate();
        emit synthesisMeasured(0.0, false, QStringLiteral("Benchmark synthesis cancelled"));
    }
}

void VieNeuTtsBackend::releaseModel()
{
    for (const auto token : pausedSpeech_.keys()) discardSpeech(token);
    warmUpRequestedAfterRelease_ = false;
    stop();
    deferredText_.clear();
    initializing_ = false;
    ready_ = false;
    contextReleased_ = false;
    releasePending_ = true;
    ++workerGeneration_;
    if (worker_) {
        QMetaObject::invokeMethod(worker_, "releaseContext", Qt::QueuedConnection,
            Q_ARG(quint64, workerGeneration_));
    }
    emit statusChanged(QStringLiteral("VieNeu-TTS context release requested"));
}

void VieNeuTtsBackend::onWorkerContextReleased(const quint64 generation)
{
    if (generation != workerGeneration_ || shuttingDown_) return;
    releasePending_ = false;
    contextReleased_ = true;
    emit statusChanged(QStringLiteral("VieNeu-TTS context released"));
    const bool resumeWarmUp = warmUpRequestedAfterRelease_;
    warmUpRequestedAfterRelease_ = false;
    emit modelReleased();
    if (resumeWarmUp && !initializing_) {
        warmUp();
    }
}

void VieNeuTtsBackend::shutdown()
{
    for (const auto token : pausedSpeech_.keys()) discardSpeech(token);
    if (shuttingDown_) return;
    shuttingDown_ = true;

    stop();

    if (workerThread_) {
        workerThread_->quit();
        // Native initialization/inference cannot be interrupted safely. Never destroy a running QThread.
        workerThread_->wait();
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

void VieNeuTtsBackend::onAudioReady(const quint64 generation, const quint64 requestId,
    const QString& text, const QByteArray& pcmData, const int sampleRate, const double elapsedMs)
{
    Q_UNUSED(elapsedMs);
    if (generation != workerGeneration_) return;
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
    for (auto it = pausedSpeech_.begin(); it != pausedSpeech_.end(); ++it) {
        if (it->requestId != requestId || requestId == 0) continue;
        it->synthesizing = false;
        it->text = text;
        it->pcm = pcmData;
        it->sampleRate = sampleRate;
        return;
    }
    if (requestId != activeRequestId_ || shuttingDown_) {
        return;
    }
    synthesizing_ = false;
    startPlayback(text, pcmData, sampleRate);
}

void VieNeuTtsBackend::onSynthesisFailed(const quint64 generation, const quint64 requestId, const QString& error)
{
    if (generation != workerGeneration_) return;
    if (requestId == 0 && initializing_ && !shuttingDown_) {
        initializing_ = false;
        ready_ = false;
        emit statusChanged(QStringLiteral("Lỗi khởi chạy inference VieNeu-TTS"));
        emit warmUpFinished(false, error);
        emit errorOccurred(error);
        return;
    }
    for (auto it = pausedSpeech_.begin(); it != pausedSpeech_.end(); ++it) {
        if (it->requestId != requestId || requestId == 0) continue;
        it->synthesizing = false;
        it->failure = error;
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

void VieNeuTtsBackend::onBenchmarkSynthesisMeasured(const quint64 generation, const quint64 requestId,
    const double elapsedMs, const bool success, const QString& error)
{
    if (generation != workerGeneration_ || requestId != activeBenchmarkRequestId_ || shuttingDown_) return;
    activeBenchmarkRequestId_ = 0;
    const double totalElapsedMs = benchmarkTimer_.isValid()
        ? static_cast<double>(benchmarkTimer_.nsecsElapsed()) / 1'000'000.0
        : elapsedMs;
    benchmarkTimer_.invalidate();
    emit synthesisMeasured(totalElapsedMs, success, error);
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

    connect(audioSink_, &QAudioSink::stateChanged, this, [this, sink = audioSink_](QAudio::State state) {
        if (state == QAudio::IdleState || state == QAudio::StoppedState) {
            // Defer to avoid re-entrant crash: QAudioSink::stop() emits
            // stateChanged(StoppedState) synchronously within the same call stack,
            // which would re-enter stopPlayback() while audioSink_ is mid-teardown.
            QMetaObject::invokeMethod(this, [this, sink] {
                if (!playbackActive_ || audioSink_ != sink) return;
                stopPlayback();
                emit speakingFinished();
            }, Qt::QueuedConnection);
        }
    });

    emit speakingStarted(text);
    audioSink_->start(audioBuffer_);
    if (requestTimer_.isValid()) {
        qCInfo(logTts) << "VieNeu playback began after" << requestTimer_.elapsed() << "ms";
        requestTimer_.invalidate();
    }
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
    constexpr qint64 cacheLimit = 64 * 1024 * 1024;
    cachedSpotterDirectory_ = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("audio/spotter"));
    if (!QDir(cachedSpotterDirectory_).exists()) {
        cachedSpotterDirectory_ = QStringLiteral("assets/spotter");
    }
    QFile manifest(QDir(cachedSpotterDirectory_).filePath(QStringLiteral("manifest.json")));
    if (!manifest.open(QIODevice::ReadOnly)) {
        qCWarning(logTts) << "Spotter audio manifest is missing:" << manifest.fileName();
        return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(manifest.readAll());
    if (!document.isObject()) {
        qCWarning(logTts) << "Spotter audio manifest is invalid:" << manifest.fileName();
        return;
    }

    qint64 cachedBytes = 0;
    QSet<QString> loadedIds;
    for (const QJsonValue& value : document.object().value(QStringLiteral("entries")).toArray()) {
        const QJsonObject entry = value.toObject();
        const QString id = entry.value(QStringLiteral("id")).toString();
        const QString phraseText = entry.value(QStringLiteral("text")).toString().trimmed();
        QVector<CachedSpotterAudio> variants;
        for (const QJsonValue& file : entry.value(QStringLiteral("files")).toArray()) {
            const QString path = QDir(cachedSpotterDirectory_).filePath(file.toString());
            QFile wav(path);
            if (!wav.open(QIODevice::ReadOnly) || wav.size() < 44
                || wav.size() > cacheLimit - cachedBytes) {
                qCWarning(logTts) << "Cannot preload spotter audio within 64 MiB cache:" << path;
                continue;
            }
            const QByteArray bytes = wav.readAll();
            if (!bytes.startsWith("RIFF") || bytes.mid(8, 4) != "WAVE") continue;
            quint16 encoding = 0;
            quint16 channels = 0;
            quint16 bitsPerSample = 0;
            quint32 sampleRate = 0;
            int dataOffset = -1;
            int dataSize = 0;
            qint64 offset = 12;
            while (offset + 8 <= bytes.size()) {
                const auto chunkSize = qFromLittleEndian<quint32>(
                    reinterpret_cast<const uchar*>(bytes.constData() + offset + 4));
                if (chunkSize > static_cast<quint64>(bytes.size() - offset - 8)) break;
                const char* chunk = bytes.constData() + offset;
                if (std::memcmp(chunk, "fmt ", 4) == 0 && chunkSize >= 16) {
                    encoding = qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(chunk + 8));
                    channels = qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(chunk + 10));
                    sampleRate = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(chunk + 12));
                    bitsPerSample = qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(chunk + 22));
                } else if (std::memcmp(chunk, "data", 4) == 0) {
                    dataOffset = static_cast<int>(offset + 8);
                    dataSize = static_cast<int>(chunkSize);
                }
                offset += 8 + chunkSize + (chunkSize & 1U);
            }
            if (encoding != 1 || channels != 1 || bitsPerSample != 16 || sampleRate == 0
                || dataOffset < 0 || dataSize <= 0 || (dataSize & 1) != 0) {
                qCWarning(logTts) << "Spotter WAV must be mono PCM16:" << path;
                continue;
            }
            variants.push_back({bytes.mid(dataOffset, dataSize), static_cast<int>(sampleRate)});
            cachedBytes += wav.size();
        }
        if (!phraseText.isEmpty() && !variants.isEmpty()) {
            cachedSpotterAudio_.insert(phraseText, variants);
            loadedIds.insert(id);
        }
    }
    for (const QString& required : {QStringLiteral("car_left"), QStringLiteral("car_right"),
             QStringLiteral("red_flag"), QStringLiteral("fuel_critical"),
 QStringLiteral("engine_critical"), QStringLiteral("tyre_hot"),
 QStringLiteral("damage_detected"), QStringLiteral("tts_api_unavailable")}) {
        if (!loadedIds.contains(required)) {
            qCWarning(logTts) << "Required spotter audio phrase is missing:" << required;
        }
    }
    qCInfo(logTts) << "Preloaded spotter audio:" << cachedBytes << "bytes; phrases:"
                   << cachedSpotterAudio_.size();
}

bool VieNeuTtsBackend::canSpeakCached(const QString& text) const
{
    return cachedSpotterAudio_.contains(text.trimmed());
}

bool VieNeuTtsBackend::playCachedSpotter(const QString& text)
{
    const auto iterator = cachedSpotterAudio_.constFind(text.trimmed());
    if (iterator == cachedSpotterAudio_.cend() || iterator->isEmpty()) return false;
    const int index = QRandomGenerator::global()->bounded(iterator->size());
    const CachedSpotterAudio& audio = iterator->at(index);
    activeRequestId_ = 0;
    synthesizing_ = false;
    stopPlayback();
    emit statusChanged(QStringLiteral("Playing cached spotter audio"));
    startPlayback(text, audio.pcm, audio.sampleRate);
    return true;
}
} // namespace raceengineer

#include "VieNeuTtsBackend.moc"
