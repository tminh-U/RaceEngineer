#include "stt/WhisperRecognizer.h"

#include "utils/Logging.h"

#include <whisper.h>

#include <QElapsedTimer>
#include <QFileInfo>
#include <QRegularExpression>
#include <QtGlobal>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace raceengineer {

WhisperRecognizer::WhisperRecognizer(QString modelPath, QObject* const parent)
    : QObject(parent)
    , modelPath_(std::move(modelPath))
{
    samples_.reserve(30 * 16'000);
    const int requestedThreads = qEnvironmentVariableIntValue("RACEENGINEER_STT_THREADS");
    if (requestedThreads == 4 || requestedThreads == 6 || requestedThreads == 8) {
        threadCount_ = requestedThreads;
    }
    const int requestedMaxTokens = qEnvironmentVariableIntValue("RACEENGINEER_STT_MAX_TOKENS");
    if (requestedMaxTokens >= 16 && requestedMaxTokens <= 64) {
        maxTokens_ = requestedMaxTokens;
    }
    useGpu_ = qgetenv("GGML_DISABLE_VULKAN") != "1";
    const QByteArray flashEnvironment = qgetenv("RACEENGINEER_STT_FLASH_ATTN");
    flashAttention_ = flashEnvironment.isEmpty() || flashEnvironment != "0";
}

WhisperRecognizer::~WhisperRecognizer()
{
    if (context_ != nullptr) {
        whisper_free(context_);
    }
}

void WhisperRecognizer::warmUp()
{
    QString error;
    if (!ensureModelLoaded(&error)
        || (!warmupComplete_ && !warmUpInference(&error))) {
        qCWarning(logStt).noquote() << "STT startup warm-up failed:" << error;
        emit warmUpFinished(false, error);
        return;
    }
    emit warmUpFinished(true, {});
}

void WhisperRecognizer::transcribe(const QByteArray& pcm16k, const QString& language)
{
    QElapsedTimer totalTimer;
    totalTimer.start();
    cancelRequested_ = false;
    emit recognitionStarted();
    QString loadError;
    if (!ensureModelLoaded(&loadError)) {
        emit recognitionError(loadError);
        emit recognitionFinished();
        return;
    }
    if (pcm16k.size() < 3200 || pcm16k.size() % 2 != 0) {
        emit recognitionError(QStringLiteral("Đoạn nói quá ngắn. Hãy giữ nút PTT trong lúc nói."));
        emit recognitionFinished();
        return;
    }

    QElapsedTimer preprocessTimer;
    preprocessTimer.start();
    const auto* input = reinterpret_cast<const std::int16_t*>(pcm16k.constData());
    const auto sampleCount = static_cast<std::size_t>(pcm16k.size() / 2);
    samples_.resize(sampleCount);
    double sum = 0.0;
    for (std::size_t index = 0; index < sampleCount; ++index) {
        sum += input[index];
    }
    const double mean = sum / static_cast<double>(sampleCount) / 32768.0;
    float peak = 0.0F;
    for (std::size_t index = 0; index < sampleCount; ++index) {
        samples_[index] = static_cast<float>(input[index] / 32768.0 - mean);
        peak = std::max(peak, std::abs(samples_[index]));
    }
    const float gain = peak > 0.001F && peak < 0.2F
        ? std::min(4.0F, 0.2F / peak) : 1.0F;
    if (gain > 1.0F) {
        for (float& sample : samples_) sample = std::clamp(sample * gain, -1.0F, 1.0F);
    }
    const double preprocessMs = preprocessTimer.nsecsElapsed() / 1'000'000.0;

    auto parameters = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    parameters.n_threads = threadCount_;
    parameters.translate = false;
    parameters.no_context = true;
    parameters.no_timestamps = true;
    parameters.single_segment = true;
    parameters.print_special = false;
    parameters.print_progress = false;
    parameters.print_realtime = false;
    parameters.print_timestamps = false;
    parameters.max_tokens = maxTokens_;
    // Half-context substantially reduces CPU latency for short PTT utterances
    // without the clear Vietnamese accuracy regression seen at 512 frames.
    parameters.audio_ctx = 768;
    parameters.temperature_inc = 0.0F;
    parameters.greedy.best_of = 1;
    Q_UNUSED(language);
    parameters.language = "vi";
    parameters.detect_language = false;
    parameters.initial_prompt = "Vietnamese race engineer. Preserve English racing terms exactly: "
                                "gap ahead, full push, box this lap, tyre, tire, pit, fuel, sector, delta, "
                                "brake bias, understeer, oversteer, front left, front right, rear left, "
                                "rear right, DRS, ERS, ABS, TC.";
    parameters.abort_callback = [](void* userData) {
        return static_cast<WhisperRecognizer*>(userData)->cancelRequested_.load();
    };
    parameters.abort_callback_user_data = this;

    whisper_reset_timings(context_);
    QElapsedTimer inferenceTimer;
    inferenceTimer.start();
    const int result = whisper_full(context_, parameters, samples_.data(), static_cast<int>(samples_.size()));
    const double inferenceMs = inferenceTimer.nsecsElapsed() / 1'000'000.0;
    if (result != 0 || cancelRequested_.load()) {
        if (!cancelRequested_.load()) {
            emit recognitionError(QStringLiteral("Whisper không thể nhận dạng đoạn nói."));
        }
        emit recognitionFinished();
        return;
    }

    QString text;
    const int segments = whisper_full_n_segments(context_);
    for (int index = 0; index < segments; ++index) {
        text += QString::fromUtf8(whisper_full_get_segment_text(context_, index));
    }
    text = normalizeRacingTerms(text.simplified());
    const int languageId = whisper_full_lang_id(context_);
    const char* const languageName = whisper_lang_str(languageId);
    const QString detected = languageName == nullptr ? QStringLiteral("unknown")
                                                     : QString::fromLatin1(languageName);
    const whisper_timings* const timings = whisper_get_timings(context_);
    double sampleMs = 0.0;
    double encoderMs = 0.0;
    double decoderMs = 0.0;
    double batchDecoderMs = 0.0;
    double promptMs = 0.0;
    if (timings != nullptr) {
        sampleMs = timings->sample_ms;
        encoderMs = timings->encode_ms;
        decoderMs = timings->decode_ms;
        batchDecoderMs = timings->batchd_ms;
        promptMs = timings->prompt_ms;
        delete timings;
    }
    // whisper.cpp does not expose a separate Vulkan fence/copy counter.  The
    // residual is the closest observable estimate and is labelled as such.
    const double backendOverheadMs = std::max(0.0,
        inferenceMs - sampleMs - encoderMs - decoderMs - batchDecoderMs - promptMs);
    qCInfo(logStt) << "STT timing total_ms=" << totalTimer.nsecsElapsed() / 1'000'000.0
                   << "preprocess_ms=" << preprocessMs
                   << "sample_ms=" << sampleMs
                   << "encoder_ms=" << encoderMs
                   << "decoder_ms=" << decoderMs
                   << "batch_decoder_ms=" << batchDecoderMs
                   << "prompt_ms=" << promptMs
                   << "gpu_sync_transfer_estimate_ms=" << backendOverheadMs
                   << "threads=" << threadCount_
                   << "max_tokens=" << maxTokens_
                   << "gpu_request=" << useGpu_
                   << "flash_attn=" << flashAttention_
                   << "language=" << detected;
    if (text.isEmpty()) {
        emit recognitionError(QStringLiteral("Không nhận dạng được lời nói. Hãy kiểm tra microphone và thử lại."));
    } else {
        emit transcriptionReady(text, detected);
    }
    emit recognitionFinished();
}

void WhisperRecognizer::setModelPath(const QString& modelPath)
{
    if (modelPath_ == modelPath) {
        return;
    }
    if (context_ != nullptr) {
        whisper_free(context_);
        context_ = nullptr;
    }
    warmupComplete_ = false;
    modelPath_ = modelPath;
}

void WhisperRecognizer::cancel() noexcept
{
    cancelRequested_ = true;
}

bool WhisperRecognizer::ensureModelLoaded(QString* const error)
{
    if (context_ != nullptr) {
        return true;
    }
    if (!QFileInfo::exists(modelPath_)) {
        if (error) *error = QStringLiteral("Không tìm thấy mô hình Whisper: %1").arg(modelPath_);
        return false;
    }
    auto parameters = whisper_context_default_params();
    parameters.use_gpu = useGpu_;
    parameters.flash_attn = flashAttention_;
    parameters.gpu_device = 0;
    const QByteArray path = QFileInfo(modelPath_).absoluteFilePath().toUtf8();
    QElapsedTimer loadTimer;
    loadTimer.start();
    context_ = whisper_init_from_file_with_params(path.constData(), parameters);
    if (context_ == nullptr) {
        if (error) *error = QStringLiteral("Không thể nạp mô hình Whisper.");
        return false;
    }
    qCInfo(logStt) << "PhoWhisper-small Q5_1 model loaded:" << modelPath_
                   << "model_load_ms=" << loadTimer.nsecsElapsed() / 1'000'000.0
                   << "threads=" << threadCount_
                   << "flash_attn=" << flashAttention_;
    const char* const systemInfo = whisper_print_system_info();
    if (systemInfo != nullptr) {
        qCInfo(logStt).noquote() << "whisper.cpp system:" << systemInfo;
    }
    return true;
}

bool WhisperRecognizer::warmUpInference(QString* const error)
{
    std::array<float, 16'000> silence{};
    auto parameters = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    parameters.n_threads = threadCount_;
    parameters.translate = false;
    parameters.no_context = true;
    parameters.no_timestamps = true;
    parameters.single_segment = true;
    parameters.print_special = false;
    parameters.print_progress = false;
    parameters.print_realtime = false;
    parameters.print_timestamps = false;
    parameters.max_tokens = 8;
    parameters.audio_ctx = 768;
    parameters.temperature_inc = 0.0F;
    parameters.greedy.best_of = 1;
    parameters.language = "vi";
    parameters.detect_language = false;
    parameters.initial_prompt = "Vietnamese race engineer.";

    whisper_reset_timings(context_);
    QElapsedTimer timer;
    timer.start();
    const int result = whisper_full(context_, parameters, silence.data(), static_cast<int>(silence.size()));
    warmupComplete_ = result == 0;
    qCInfo(logStt) << "STT warmup result=" << result
                   << "warmup_ms=" << timer.nsecsElapsed() / 1'000'000.0;
    if (!warmupComplete_ && error) {
        *error = QStringLiteral("PhoWhisper inference warm-up failed (code %1).").arg(result);
    }
    return warmupComplete_;
}

QString WhisperRecognizer::normalizeRacingTerms(QString text)
{
    text.replace(QRegularExpression(QStringLiteral("\\bnhị[pt] độ lốp\\b"),
                     QRegularExpression::CaseInsensitiveOption),
        QStringLiteral("nhiệt độ lốp"));
    text.replace(QRegularExpression(QStringLiteral("\\báp xuất\\b"),
                     QRegularExpression::CaseInsensitiveOption),
        QStringLiteral("áp suất"));
    const QStringList terms = {QStringLiteral("DRS"), QStringLiteral("ERS"), QStringLiteral("ABS"),
        QStringLiteral("TC"), QStringLiteral("T1"), QStringLiteral("T2"), QStringLiteral("T3")};
    for (const auto& term : terms) {
        text.replace(QRegularExpression(QStringLiteral("\\b%1\\b").arg(term),
                         QRegularExpression::CaseInsensitiveOption), term);
    }
    return text;
}

} // namespace raceengineer
