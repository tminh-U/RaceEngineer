#include "stt/WhisperRecognizer.h"

#include "utils/Logging.h"

#include <whisper.h>

#include <QElapsedTimer>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <thread>
#include <vector>

namespace raceengineer {

WhisperRecognizer::WhisperRecognizer(QString modelPath, QObject* const parent)
    : QObject(parent)
    , modelPath_(std::move(modelPath))
{
}

WhisperRecognizer::~WhisperRecognizer()
{
    if (context_ != nullptr) {
        whisper_free(context_);
    }
}

void WhisperRecognizer::warmUp()
{
    ensureModelLoaded();
}

void WhisperRecognizer::transcribe(const QByteArray& pcm16k, const QString& language)
{
    cancelRequested_ = false;
    emit recognitionStarted();
    if (!ensureModelLoaded()) {
        emit recognitionFinished();
        return;
    }
    if (pcm16k.size() < 3200 || pcm16k.size() % 2 != 0) {
        emit recognitionError(QStringLiteral("Đoạn nói quá ngắn. Hãy giữ nút PTT trong lúc nói."));
        emit recognitionFinished();
        return;
    }

    const auto* input = reinterpret_cast<const std::int16_t*>(pcm16k.constData());
    const auto sampleCount = static_cast<std::size_t>(pcm16k.size() / 2);
    std::vector<float> samples(sampleCount);
    const double mean = std::accumulate(input, input + sampleCount, 0.0)
        / static_cast<double>(sampleCount) / 32768.0;
    float peak = 0.0F;
    for (std::size_t index = 0; index < sampleCount; ++index) {
        samples[index] = static_cast<float>(input[index] / 32768.0 - mean);
        peak = std::max(peak, std::abs(samples[index]));
    }
    const float gain = peak > 0.001F && peak < 0.2F
        ? std::min(4.0F, 0.2F / peak) : 1.0F;
    if (gain > 1.0F) {
        for (float& sample : samples) sample = std::clamp(sample * gain, -1.0F, 1.0F);
    }

    auto parameters = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    parameters.n_threads = std::clamp(static_cast<int>(std::thread::hardware_concurrency() / 2), 4, 8);
    parameters.translate = false;
    parameters.no_context = true;
    parameters.no_timestamps = true;
    parameters.single_segment = true;
    parameters.print_special = false;
    parameters.print_progress = false;
    parameters.print_realtime = false;
    parameters.print_timestamps = false;
    parameters.max_tokens = 64;
    // Half-context substantially reduces CPU latency for short PTT utterances
    // without the clear Vietnamese accuracy regression seen at 512 frames.
    parameters.audio_ctx = 768;
    parameters.temperature_inc = 0.0F;
    parameters.greedy.best_of = 1;
    const QByteArray languageUtf8 = language.toUtf8();
    parameters.language = languageUtf8.constData();
    parameters.detect_language = language.compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0;
    parameters.initial_prompt = "Vietnamese race engineer. Preserve English racing terms exactly: "
                                "gap ahead, full push, box this lap, tyre, fuel, sector, DRS, ERS, ABS, TC.";
    parameters.abort_callback = [](void* userData) {
        return static_cast<WhisperRecognizer*>(userData)->cancelRequested_.load();
    };
    parameters.abort_callback_user_data = this;

    QElapsedTimer timer;
    timer.start();
    const int result = whisper_full(context_, parameters, samples.data(), static_cast<int>(samples.size()));
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
    qCInfo(logStt) << "Whisper completed in" << timer.elapsed() << "ms; language" << detected;
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
    modelPath_ = modelPath;
}

void WhisperRecognizer::cancel() noexcept
{
    cancelRequested_ = true;
}

bool WhisperRecognizer::ensureModelLoaded()
{
    if (context_ != nullptr) {
        return true;
    }
    if (!QFileInfo::exists(modelPath_)) {
        emit recognitionError(QStringLiteral("Không tìm thấy mô hình Whisper: %1").arg(modelPath_));
        return false;
    }
    auto parameters = whisper_context_default_params();
    const QByteArray path = QFileInfo(modelPath_).absoluteFilePath().toUtf8();
    context_ = whisper_init_from_file_with_params(path.constData(), parameters);
    if (context_ == nullptr) {
        emit recognitionError(QStringLiteral("Không thể nạp mô hình Whisper."));
        return false;
    }
    qCInfo(logStt) << "PhoWhisper-medium Q5 model loaded:" << modelPath_;
    return true;
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
