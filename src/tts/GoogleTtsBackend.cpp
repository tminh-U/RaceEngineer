#include "tts/GoogleTtsBackend.h"
#include "utils/Logging.h"
#include "utils/ApiRetry.h"

#include "utils/Logging.h"

#include <QAudioFormat>
#include <QAudioSink>
#include <QBuffer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaDevices>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QtEndian>
#include <algorithm>
#include <cstring>

namespace raceengineer {
namespace {

constexpr auto kInteractionsUrl = "https://generativelanguage.googleapis.com/v1beta/interactions";
constexpr auto kVoicesUrl = "https://generativelanguage.googleapis.com/v1beta/voices";
constexpr qint64 kMaximumAudioBytes = 8 * 1024 * 1024;
constexpr qsizetype kMaximumEventBufferBytes = 1024 * 1024;
constexpr qsizetype kMaximumPendingPcmBytes = 2 * 1024 * 1024;

const QString kBonoVoiceDescription = QStringLiteral(
    "Giọng nam trưởng thành, trung trầm, hơi khàn nhẹ, ấm và chắc. "
    "Lấy cảm hứng từ Peter ‘Bono’ Bonnington khi nói radio với tay đua Mercedes: "
    "bình tĩnh, tiết chế, tự tin và đáng tin cậy. "
    "Nói tiếng Việt tự nhiên, rõ dấu và phụ âm cuối; không giả giọng nước ngoài. "
    "Nhả chữ gọn, tốc độ vừa phải, hơi nhanh ở thông tin ngắn. "
    "Chia câu thành cụm ngắn, nghỉ nhẹ giữa các cụm; nhấn hành động và con số, "
    "hạ giọng dứt khoát cuối câu. "
    "Thông tin thường điềm tĩnh; cảnh báo khẩn trương, chắc giọng nhưng không hét; "
    "động viên ấm áp, chân thành, chỉ tăng năng lượng nhẹ. "
    "Nói trực tiếp với tay đua trong cockpit, không đọc kiểu phát thanh viên, "
    "quảng cáo, kể chuyện hay trợ lý ảo. Không thêm tiếng rè, bíp, nhạc hoặc hiệu ứng radio.");

const QString kRaceEngineerVoiceStyle = QStringLiteral(
    "Nói tiếng Việt tự nhiên, rõ dấu và phụ âm cuối. Radio kỹ sư đua xe: bình tĩnh, "
    "ngắn gọn, tự tin; nghỉ nhẹ giữa các cụm, nhấn hành động và con số. "
    "Cảnh báo khẩn trương nhưng không hét; động viên ấm áp. Không thêm hiệu ứng âm thanh.");

struct WavInfo final {
    quint16 channels{0};
    quint16 bitsPerSample{0};
    quint32 sampleRate{0};
    quint32 dataBytes{0};
};

bool validPcmWav(const QByteArray& bytes, WavInfo& info)
{
    if (bytes.size() < 44 || bytes.mid(0, 4) != "RIFF" || bytes.mid(8, 4) != "WAVE") return false;
    quint16 encoding = 0;
    bool formatFound = false;
    bool dataFound = false;
    qint64 offset = 12;
    while (offset + 8 <= bytes.size()) {
        const auto chunkSize = qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar*>(bytes.constData() + offset + 4));
        if (chunkSize > static_cast<quint64>(bytes.size() - offset - 8)) return false;
        const char* chunk = bytes.constData() + offset;
        if (std::memcmp(chunk, "fmt ", 4) == 0 && chunkSize >= 16) {
            encoding = qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(chunk + 8));
            info.channels = qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(chunk + 10));
            info.sampleRate = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(chunk + 12));
            info.bitsPerSample = qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(chunk + 22));
            formatFound = true;
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            info.dataBytes = chunkSize;
            dataFound = true;
        }
        offset += 8 + chunkSize + (chunkSize & 1U);
    }
    return formatFound && dataFound && encoding == 1 && info.channels == 1
        && info.bitsPerSample == 16 && info.sampleRate >= 8000 && info.sampleRate <= 48000
        && info.dataBytes > 0 && (info.dataBytes & 1U) == 0;
}

QJsonObject audioDelta(const QJsonObject& event)
{
    const QJsonObject delta = event.value(QStringLiteral("delta")).toObject();
    if (delta.value(QStringLiteral("type")).toString() == QStringLiteral("audio")) return delta;
    const QJsonObject content = event.value(QStringLiteral("content")).toObject();
    return content.value(QStringLiteral("type")).toString() == QStringLiteral("audio")
        ? content : QJsonObject{};
}

} // namespace

bool decodeGooglePcmChunk(const QByteArray& encoded, QByteArray& remainder, QByteArray& pcm)
{
    const auto decoded = QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || decoded.decoded.isEmpty()) return false;
    pcm = remainder + decoded.decoded;
    remainder.clear();
    if (pcm.size() & 1) {
        remainder = pcm.right(1);
        pcm.chop(1);
    }
    return true;
}

QString GoogleTtsBackend::bonoVoiceDescription() { return kBonoVoiceDescription; }

GoogleTtsBackend::GoogleTtsBackend(QObject* parent)
    : GoogleTtsBackend(QUrl(QString::fromLatin1(kInteractionsUrl)),
          QUrl(QString::fromLatin1(kVoicesUrl)), parent)
{
}

GoogleTtsBackend::GoogleTtsBackend(QUrl interactionsEndpoint, QUrl voicesEndpoint,
    QObject* parent)
    : ITtsBackend(parent)
    , interactionsEndpoint_(std::move(interactionsEndpoint))
    , voicesEndpoint_(std::move(voicesEndpoint))
    , network_(new QNetworkAccessManager(this))
    , recoveryTimer_(new QTimer(this))
{
    setAudioOutputDevice({});
    initializeSpeech(0);
    recoveryTimer_->setSingleShot(true);
    connect(recoveryTimer_, &QTimer::timeout, this, &GoogleTtsBackend::checkConnection);
}

void GoogleTtsBackend::deleteSpeechTimers()
{
    for (auto* timer : {speech_.timeout_, speech_.retryTimer_, speech_.playbackTimer_}) {
        if (timer) { timer->stop(); timer->deleteLater(); }
    }
}

void GoogleTtsBackend::initializeSpeech(const quint64 token)
{
    speech_.token = token;
    speech_.timeout_ = new QTimer(this);
    speech_.retryTimer_ = new QTimer(this);
    speech_.playbackTimer_ = new QTimer(this);
    speech_.timeout_->setSingleShot(true);
    speech_.timeout_->setInterval(30000);
    connect(speech_.timeout_, &QTimer::timeout, this, [this, token] {
        withSpeech(token, [this] {
            if (speech_.activeRequestId_ != 0 && !retrySynthesis(speech_.activeRequestId_, 0,
                    QNetworkReply::TimeoutError, {}, true)) {
                failRequest(speech_.activeRequestId_, QStringLiteral("Google TTS request timed out."));
                scheduleRecovery();
            }
        });
    });
    speech_.retryTimer_->setSingleShot(true);
    connect(speech_.retryTimer_, &QTimer::timeout, this, [this, token] {
        withSpeech(token, [this] {
            if (speech_.activeRequestId_ != 0 && !speech_.reply_)
                startSynthesisRequest(speech_.activeRequestId_);
        });
    });
    speech_.playbackTimer_->setInterval(10);
    connect(speech_.playbackTimer_, &QTimer::timeout, this, [this, token] {
        withSpeech(token, [this] { playbackTick(); });
    });
}

void GoogleTtsBackend::withSpeech(const quint64 token, const std::function<void()>& action)
{
    if (speech_.token == token) { action(); return; }
    auto it = pausedSpeech_.find(token);
    if (it == pausedSpeech_.end()) return;
    std::swap(speech_, it.value());
    action();
    std::swap(speech_, it.value());
}

quint64 GoogleTtsBackend::pauseSpeech()
{
    if (speech_.activeRequestId_ == 0 || pausedSpeech_.size() >= 5) return 0;
    const auto token = speech_.token;
    speech_.paused = true;
    speech_.wasPaused = true;
    speech_.playbackTimer_->stop();
    if (speech_.audioSink_) speech_.audioSink_->suspend();
    pausedSpeech_.insert(token, speech_);
    speech_ = {};
    initializeSpeech(0);
    return token;
}

bool GoogleTtsBackend::resumeSpeech(const quint64 token)
{
    if (!pausedSpeech_.contains(token)) return false;
    stop();
    deleteSpeechTimers();
    speech_ = pausedSpeech_.take(token);
    speech_.paused = false;
    if (speech_.activeRequestId_ == 0) {
        if (!speech_.failure.isEmpty())
            QTimer::singleShot(0, this, [this, error = speech_.failure] { emit errorOccurred(error); });
        return false;
    }
    if (speech_.audioSink_) {
        speech_.audioSink_->setVolume(volume_);
        speech_.audioSink_->resume();
        speech_.playbackTimer_->start();
    }
    playbackTick();
    return true;
}

void GoogleTtsBackend::discardSpeech(const quint64 token)
{
    if (!pausedSpeech_.contains(token)) return;
    withSpeech(token, [this] { stop(); deleteSpeechTimers(); });
    pausedSpeech_.remove(token);
}

GoogleTtsBackend::~GoogleTtsBackend()
{
    for (const auto token : pausedSpeech_.keys()) discardSpeech(token);
    stop();
}

void GoogleTtsBackend::setApiKey(QString apiKey)
{
    if (apiKey_ != apiKey) {
        for (const auto token : pausedSpeech_.keys()) discardSpeech(token);
        stop();
    }
    apiKey_ = std::move(apiKey);
}

void GoogleTtsBackend::setModel(QString model)
{
    if (model_ != model) {
        for (const auto token : pausedSpeech_.keys()) discardSpeech(token);
        stop();
    }
    model_ = model == QStringLiteral("gemini-3.8-flash-tts")
        ? std::move(model) : QStringLiteral("gemini-3.8-flash-lite-tts");
}

void GoogleTtsBackend::setVoice(QString voice, QString voiceId)
{
    if (voice_ != voice || voiceId_ != voiceId) {
        for (const auto token : pausedSpeech_.keys()) discardSpeech(token);
        stop();
    }
    voice_ = std::move(voice);
    voiceId_ = std::move(voiceId);
}

QStringList GoogleTtsBackend::builtInVoices()
{
    return {QStringLiteral("Kore"), QStringLiteral("Puck"), QStringLiteral("Charon"),
        QStringLiteral("Fenrir"), QStringLiteral("Aoede"), QStringLiteral("Leda"),
        QStringLiteral("Orus"), QStringLiteral("Zephyr")};
}

void GoogleTtsBackend::warmUp()
{
    if (!isAvailable()) {
        const QString error = QStringLiteral("Google AI Studio TTS API key is not configured.");
        emit errorOccurred(error);
        emit warmUpFinished(false, error);
        return;
    }
    emit warmUpFinished(true, {});
}

void GoogleTtsBackend::speak(const QString& text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return;
    stop();
    if (!isAvailable()) {
        emit errorOccurred(QStringLiteral("Google AI Studio TTS API key is not configured."));
        if (!speech_.paused) emit speakingFinished();
        return;
    }

    const quint64 requestId = nextRequestId_++;
    deleteSpeechTimers();
    speech_ = {};
    initializeSpeech(requestId);
    speech_.retryCount_ = 0;
    speech_.requestTimer_.start();
    speech_.activeRequestId_ = requestId;
    speech_.eventBuffer_.clear();
    speech_.pendingPcm_.clear();
    speech_.pcmRemainder_.clear();
    speech_.totalPcmBytes_ = 0;
    speech_.responseFinished_ = false;
    speech_.started_ = false;

    const QString voice = voiceId_.isEmpty() ? voice_ : voiceId_;
    speech_.activeBody_ = QJsonObject{
        {QStringLiteral("model"), model_},
        {QStringLiteral("input"), QJsonArray{QJsonObject{
            {QStringLiteral("type"), QStringLiteral("user_input")},
            {QStringLiteral("content"), QJsonArray{QJsonObject{
                {QStringLiteral("type"), QStringLiteral("text")},
                {QStringLiteral("text"), trimmed},
                {QStringLiteral("annotations"), QJsonArray{QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("speech_metadata")},
                    {QStringLiteral("style"), kRaceEngineerVoiceStyle}}}},
            }}}}}},
        {QStringLiteral("response_format"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("audio")},
            {QStringLiteral("mime_type"), QStringLiteral("audio/l16")},
            {QStringLiteral("sample_rate"), 24000}}},
        {QStringLiteral("generation_config"), QJsonObject{
            {QStringLiteral("speech_config"), QJsonArray{QJsonObject{
                {QStringLiteral("voice"), voice}}}}}},
        {QStringLiteral("stream"), true},
    };
    startSynthesisRequest(requestId);
}

void GoogleTtsBackend::startSynthesisRequest(const quint64 requestId)
{
    if (speech_.activeRequestId_ != requestId || speech_.reply_) return;
    QNetworkRequest request(interactionsEndpoint_);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("x-goog-api-key", apiKey_.toUtf8());
    request.setRawHeader("Accept", "text/event-stream");
    speech_.reply_ = network_->post(request, QJsonDocument(speech_.activeBody_).toJson(QJsonDocument::Compact));
    QNetworkReply* const activeReply = speech_.reply_;
    connect(activeReply, &QIODevice::readyRead, this, [this, activeReply, requestId] {
        withSpeech(requestId, [this, activeReply, requestId] {
            if (speech_.reply_ != activeReply || speech_.activeRequestId_ != requestId) return;
            readStream();
        });
    });
    connect(activeReply, &QNetworkReply::finished, this, [this, activeReply, requestId] {
        withSpeech(requestId, [this, activeReply, requestId] {
            if (speech_.reply_ != activeReply || speech_.activeRequestId_ != requestId) return;
            finishStream();
        });
        activeReply->deleteLater();
    });
    speech_.timeout_->start();
}

void GoogleTtsBackend::readStream()
{
    if (!speech_.reply_ || speech_.activeRequestId_ == 0) return;
    speech_.eventBuffer_.append(speech_.reply_->readAll());
    if (speech_.eventBuffer_.size() > kMaximumEventBufferBytes) {
        failRequest(speech_.activeRequestId_, QStringLiteral("Google TTS stream exceeded the event buffer limit."));
        return;
    }
    consumeEvents();
}

void GoogleTtsBackend::consumeEvents(const bool finalChunk)
{
    speech_.eventBuffer_.replace("\r\n", "\n");
    qsizetype boundary = -1;
    while ((boundary = speech_.eventBuffer_.indexOf("\n\n")) >= 0) {
        const QByteArray eventData = speech_.eventBuffer_.left(boundary);
        speech_.eventBuffer_.remove(0, boundary + 2);
        if (!consumeEvent(eventData)) return;
    }
    if (finalChunk && !speech_.eventBuffer_.trimmed().isEmpty()) {
        const QByteArray eventData = speech_.eventBuffer_;
        speech_.eventBuffer_.clear();
        consumeEvent(eventData);
    }
}

bool GoogleTtsBackend::consumeEvent(const QByteArray& eventData)
{
    QByteArray json;
    for (QByteArray line : eventData.split('\n')) {
        if (line.startsWith("data:")) {
            line.remove(0, 5);
            if (line.startsWith(' ')) line.remove(0, 1);
            if (!json.isEmpty()) json.append('\n');
            json.append(line);
        }
    }
    if (json.isEmpty() || json == "[DONE]") return true;
    const QJsonDocument document = QJsonDocument::fromJson(json);
    if (!document.isObject()) return true;
    const QJsonObject event = document.object();
    const QJsonObject audio = audioDelta(event);
    const auto mime = audio.value(QStringLiteral("mime_type")).toString();
    if (!mime.isEmpty() && mime.section(';', 0, 0).trimmed().toLower() != QStringLiteral("audio/l16")) {
        failRequest(speech_.activeRequestId_, QStringLiteral("Google TTS returned an unsupported audio format; expected raw PCM audio/l16."));
        return false;
    }
    const QByteArray encoded = audio.value(QStringLiteral("data")).toString().toLatin1();
    if (!encoded.isEmpty()) {
        QByteArray pcm;
        if (!decodeGooglePcmChunk(encoded, speech_.pcmRemainder_, pcm)) {
            failRequest(speech_.activeRequestId_, QStringLiteral("Google TTS returned invalid base64 audio."));
            return false;
        }
        if (!pcm.isEmpty() && !appendPcm(pcm)) return false;
    }
    const QString type = event.value(QStringLiteral("event_type")).toString(
        event.value(QStringLiteral("type")).toString());
    if (type == QStringLiteral("interaction.failed") || type == QStringLiteral("error")) {
        const auto error = event.value(QStringLiteral("error")).toObject();
        int code = error.value(QStringLiteral("code")).toInt();
        const auto state = error.value(QStringLiteral("status")).toString();
        if (state == QStringLiteral("UNAVAILABLE")) code = 503;
        if (state == QStringLiteral("RESOURCE_EXHAUSTED")) code = 429;
        if (retrySynthesis(speech_.activeRequestId_, code, QNetworkReply::NoError)) return false;
        failRequest(speech_.activeRequestId_, QStringLiteral("Google TTS request failed."));
        if (code != 429 && transientApiFailure(code, QNetworkReply::NoError)) scheduleRecovery();
        return false;
    }
    return true;
}

bool GoogleTtsBackend::appendPcm(const QByteArray& pcm)
{
    if (speech_.totalPcmBytes_ + static_cast<quint64>(pcm.size()) > kMaximumAudioBytes) {
        failRequest(speech_.activeRequestId_, QStringLiteral("Google TTS audio exceeded the 8 MiB limit."));
        return false;
    }
    if (speech_.pendingPcm_.size() + pcm.size() > (speech_.wasPaused ? kMaximumAudioBytes : kMaximumPendingPcmBytes)) {
        failRequest(speech_.activeRequestId_, QStringLiteral("Google TTS playback buffer exceeded the 2 MiB limit."));
        return false;
    }
    if (!speech_.audioSink_ && !startPcmPlayback()) return false;
    speech_.totalPcmBytes_ += static_cast<quint64>(pcm.size());
    speech_.pendingPcm_.append(pcm);
    drainPcm();
    return speech_.activeRequestId_ != 0;
}

bool GoogleTtsBackend::startPcmPlayback()
{
    QAudioFormat format;
    format.setSampleRate(24000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
    const QAudioDevice device = audioDevice_.isNull() ? QMediaDevices::defaultAudioOutput() : audioDevice_;
    if (device.isNull()) {
        failRequest(speech_.activeRequestId_, QStringLiteral("No audio output device is available for Google TTS."));
        return false;
    }
    // Qt's Windows audio sink resamples input to the WASAPI mix format, as in VieNeu playback.
    // isFormatSupported() describes device capabilities, not the sink's conversion capabilities.
    qCInfo(logTts) << "Google TTS audio output:" << device.description()
                  << "preferred format:" << device.preferredFormat()
                  << "native 24 kHz mono support:" << device.isFormatSupported(format);
    speech_.audioSink_ = new QAudioSink(device, format, this);
    speech_.audioSink_->setVolume(volume_);
    speech_.audioSink_->setBufferSize(48000);
    speech_.audioOutput_ = speech_.audioSink_->start();
    if (!speech_.audioOutput_) {
        speech_.audioSink_->deleteLater();
        speech_.audioSink_ = nullptr;
        failRequest(speech_.activeRequestId_, QStringLiteral("Could not start Google TTS audio output."));
        return false;
    }
    connect(speech_.audioSink_, &QAudioSink::stateChanged, this,
        [this, sink = speech_.audioSink_, requestId = speech_.activeRequestId_](QAudio::State state) {
            withSpeech(requestId, [this, sink, requestId, state] {
                if (speech_.activeRequestId_ != requestId || speech_.audioSink_ != sink) return;
                if (state == QAudio::StoppedState && sink->error() != QAudio::NoError)
                    failRequest(requestId, QStringLiteral("Google TTS audio output stopped with device error %1.")
                        .arg(static_cast<int>(sink->error())));
            });
        }, Qt::QueuedConnection);
    if (speech_.paused) speech_.audioSink_->suspend();
    speech_.started_ = true;
    recoveryAttempts_ = 0;
    qCInfo(logTts) << "Google TTS first audio playback began after" << speech_.requestTimer_.elapsed() << "ms";
    if (!speech_.paused) emit speakingStarted({});
    emit firstAudioReceived(speech_.requestTimer_.elapsed());
    if (!speech_.paused) speech_.playbackTimer_->start();
    return true;
}

void GoogleTtsBackend::drainPcm()
{
    if (speech_.paused || !speech_.audioOutput_ || speech_.pendingPcm_.isEmpty()) return;
    const qint64 written = speech_.audioOutput_->write(speech_.pendingPcm_.constData(), speech_.pendingPcm_.size());
    if (written < 0) {
        failRequest(speech_.activeRequestId_, QStringLiteral("Google TTS audio output failed."));
    } else if (written > 0) {
        speech_.pendingPcm_.remove(0, static_cast<qsizetype>(written));
    }
}

void GoogleTtsBackend::finishStream()
{
    if (!speech_.reply_) return;
    QNetworkReply* const finishedReply = speech_.reply_;
    speech_.reply_ = nullptr;
    const quint64 requestId = speech_.activeRequestId_;
    if (requestId == 0) {
        finishedReply->deleteLater();
        return;
    }
    speech_.eventBuffer_.append(finishedReply->readAll());
    consumeEvents(true);
    const int status = finishedReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool failed = finishedReply->error() != QNetworkReply::NoError || status < 200 || status >= 300;
    const auto networkError = finishedReply->error();
    const auto retryAfter = finishedReply->rawHeader("Retry-After");
    finishedReply->deleteLater();
    if (speech_.activeRequestId_ != requestId || speech_.retryTimer_->isActive()) return;
    speech_.timeout_->stop();
    if (failed) {
        if (retrySynthesis(requestId, status, networkError, retryAfter)) return;
        failRequest(requestId, status == 401 || status == 403
            ? QStringLiteral("Google TTS authentication failed; check the AI Studio API key.")
            : status == 429 ? QStringLiteral("Google TTS quota or rate limit reached.")
            : QStringLiteral("Google TTS network request failed (HTTP %1).").arg(status));
        if (status != 429 && transientApiFailure(status, networkError)) scheduleRecovery();
        return;
    }
    if (speech_.totalPcmBytes_ == 0) {
        if (!speech_.pcmRemainder_.isEmpty()) {
            failRequest(requestId, QStringLiteral("Google TTS PCM stream ended with an incomplete 16-bit sample."));
            return;
        }
        failRequest(requestId, QStringLiteral("Google TTS returned no audio."));
        return;
    }
    if (!speech_.pcmRemainder_.isEmpty()) {
        failRequest(requestId, QStringLiteral("Google TTS PCM stream ended with an incomplete 16-bit sample."));
        return;
    }
    speech_.responseFinished_ = true;
    playbackTick();
}

void GoogleTtsBackend::playbackTick()
{
    if (speech_.paused) return;
    if (speech_.activeRequestId_ == 0) {
        speech_.playbackTimer_->stop();
        return;
    }
    drainPcm();
    if (speech_.activeRequestId_ == 0) return;
    if (speech_.responseFinished_ && speech_.pendingPcm_.isEmpty() && speech_.audioSink_
        && static_cast<quint64>(speech_.audioSink_->processedUSecs())
            >= (speech_.totalPcmBytes_ * 1000000ULL) / 48000ULL) {
        finishRequest(speech_.activeRequestId_);
    }
}

void GoogleTtsBackend::finishRequest(const quint64 requestId)
{
    if (requestId == 0 || speech_.activeRequestId_ != requestId) return;
    qCInfo(logTts) << "Google TTS request and playback completed in" << speech_.requestTimer_.elapsed() << "ms";
    speech_.activeRequestId_ = 0;
    speech_.retryTimer_->stop();
    speech_.timeout_->stop();
    speech_.playbackTimer_->stop();
    resetPlayback();
    if (!speech_.paused) emit speakingFinished();
}

void GoogleTtsBackend::failRequest(const quint64 requestId, const QString& message)
{
    if (requestId == 0 || speech_.activeRequestId_ != requestId) return;
    QNetworkReply* const failedReply = speech_.reply_;
    qCWarning(logTts) << message;
    speech_.failure = message;
    speech_.reply_ = nullptr;
    speech_.activeRequestId_ = 0;
    speech_.retryTimer_->stop();
    if (failedReply) { failedReply->abort(); failedReply->deleteLater(); }
    speech_.timeout_->stop();
    speech_.playbackTimer_->stop();
    resetPlayback();
    if (!speech_.paused) emit errorOccurred(message);
    if (!speech_.paused) emit speakingFinished();
}

void GoogleTtsBackend::resetPlayback()
{
    if (speech_.audioSink_) {
        speech_.audioSink_->stop();
        speech_.audioSink_->deleteLater();
        speech_.audioSink_ = nullptr;
    }
    speech_.audioOutput_ = nullptr;
    speech_.eventBuffer_.clear();
    speech_.pendingPcm_.clear();
    speech_.pcmRemainder_.clear();
    speech_.totalPcmBytes_ = 0;
    speech_.responseFinished_ = false;
    speech_.started_ = false;
    speech_.activeBody_ = {};
}

bool GoogleTtsBackend::retrySynthesis(const quint64 requestId, int status, int networkError,
    const QByteArray& retryAfter, bool timedOut)
{
    if (requestId == 0 || speech_.activeRequestId_ != requestId || speech_.started_ || speech_.totalPcmBytes_ != 0
        || !speech_.pcmRemainder_.isEmpty() || speech_.retryCount_ >= 2
        || !transientApiFailure(status, static_cast<QNetworkReply::NetworkError>(networkError), timedOut)) return false;
    const int delay = apiRetryDelay(retryAfter, speech_.retryCount_);
    if (delay < 0) return false;
    auto* oldReply = speech_.reply_;
    speech_.reply_ = nullptr;
    if (oldReply) { oldReply->abort(); oldReply->deleteLater(); }
    speech_.timeout_->stop();
    speech_.eventBuffer_.clear();
    ++speech_.retryCount_;
    speech_.retryTimer_->start(delay);
    emit retryScheduled(speech_.retryCount_, delay);
    qCInfo(logTts) << "Retrying Google TTS" << speech_.retryCount_ << "/2 in" << delay << "ms";
    return true;
}

void GoogleTtsBackend::stop()
{
    recoveryTimer_->stop();
    if (recoveryReply_) {
        auto* old = recoveryReply_;
        recoveryReply_ = nullptr;
        old->abort(); old->deleteLater();
    }
    if (speech_.activeRequestId_ == 0) return;
    const quint64 requestId = speech_.activeRequestId_;
    speech_.activeRequestId_ = 0;
    speech_.retryTimer_->stop();
    auto* oldReply = speech_.reply_;
    speech_.reply_ = nullptr;
    if (oldReply) { oldReply->abort(); oldReply->deleteLater(); }
    speech_.timeout_->stop();
    speech_.playbackTimer_->stop();
    resetPlayback();
    Q_UNUSED(requestId);
    if (!speech_.paused) emit speakingFinished();
}

void GoogleTtsBackend::scheduleRecovery()
{
    if (!isAvailable()) return;
    const int delay = 15000 << std::min(recoveryAttempts_, 2);
    recoveryAttempts_ = std::min(recoveryAttempts_ + 1, 2);
    recoveryTimer_->start(delay);
}

void GoogleTtsBackend::checkConnection()
{
    if (!isAvailable() || recoveryReply_) return;
    if (speech_.activeRequestId_ != 0 || voiceOperationActive_) { recoveryTimer_->start(15000); return; }
    QUrl endpoint = interactionsEndpoint_;
    const auto path = endpoint.path();
    endpoint.setPath(path.left(path.lastIndexOf('/')) + QStringLiteral("/models/") + model_);
    endpoint.setQuery(QString{});
    QNetworkRequest request(endpoint);
    request.setRawHeader("x-goog-api-key", apiKey_.toUtf8());
    request.setTransferTimeout(5000);
    auto* probe = network_->get(request);
    recoveryReply_ = probe;
    probe->setReadBufferSize(65536);
    connect(probe, &QNetworkReply::finished, this, [this, probe] {
        if (recoveryReply_ != probe) { probe->deleteLater(); return; }
        recoveryReply_ = nullptr;
        const int status = probe->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto error = probe->error();
        const auto name = QJsonDocument::fromJson(probe->readAll()).object().value("name").toString();
        probe->deleteLater();
        if (status == 200 && error == QNetworkReply::NoError
            && (name == model_ || name == QStringLiteral("models/") + model_)) {
            recoveryAttempts_ = 0;
            emit connectionRecovered();
        } else if (status != 429 && transientApiFailure(status, error)) scheduleRecovery();
    });
}

void GoogleTtsBackend::setVolume(const float volume)
{
    volume_ = std::clamp(volume, 0.0F, 1.0F);
    if (speech_.audioSink_) speech_.audioSink_->setVolume(volume_);
}

void GoogleTtsBackend::setSpeed(float speed)
{
    Q_UNUSED(speed);
}

void GoogleTtsBackend::setAudioOutputDevice(const QString& description)
{
    if (description.trimmed().isEmpty()) {
        audioDevice_ = QMediaDevices::defaultAudioOutput();
        return;
    }
    for (const auto& device : QMediaDevices::audioOutputs()) {
        if (device.description().compare(description, Qt::CaseInsensitive) == 0) {
            audioDevice_ = device;
            return;
        }
    }
    audioDevice_ = QMediaDevices::defaultAudioOutput();
}

void GoogleTtsBackend::createReplicatedVoice(const QString& name, const QByteArray& sourceWav,
    const QByteArray& consentWav)
{
    const QString trimmedName = name.trimmed();
    WavInfo sourceInfo;
    WavInfo consentInfo;
    const auto validSize = [](const QByteArray& audio) { return audio.size() <= 5 * 1024 * 1024; };
    if (trimmedName.isEmpty() || trimmedName.size() > 80
        || !validSize(sourceWav) || !validSize(consentWav)
        || !validPcmWav(sourceWav, sourceInfo) || !validPcmWav(consentWav, consentInfo)) {
        emit voiceOperationFinished(false,
            QStringLiteral("Use mono 16-bit PCM WAV files; reference must be 10-30 seconds."));
        return;
    }
    const double bytesPerSecond = static_cast<double>(sourceInfo.sampleRate)
        * sourceInfo.channels * (sourceInfo.bitsPerSample / 8);
    const double sourceDuration = sourceInfo.dataBytes / bytesPerSecond;
    if (sourceDuration < 10.0 || sourceDuration > 30.0) {
        emit voiceOperationFinished(false, QStringLiteral("Reference recording must be 10-30 seconds."));
        return;
    }
    const QJsonObject sourceAudio{
        {QStringLiteral("mime_type"), QStringLiteral("audio/wav")},
        {QStringLiteral("data"), QString::fromLatin1(sourceWav.toBase64())}};
    const QJsonObject consentAudio{
        {QStringLiteral("mime_type"), QStringLiteral("audio/wav")},
        {QStringLiteral("data"), QString::fromLatin1(consentWav.toBase64())}};
    const QJsonObject replicated{
        {QStringLiteral("source_audio"), sourceAudio},
        {QStringLiteral("consent_audio"), consentAudio}};
    const QJsonObject voice{
        {QStringLiteral("model"), model_},
        {QStringLiteral("type"), QStringLiteral("replicated")},
        {QStringLiteral("display_name"), trimmedName},
        {QStringLiteral("replicated"), replicated}};
    postVoice(trimmedName, voice);
}

void GoogleTtsBackend::createPromptedVoice(const QString& name, const QString& description)
{
    const auto prompt = description.trimmed();
    if (prompt.isEmpty() || prompt.size() > 4000) {
        emit voiceOperationFinished(false, QStringLiteral("Voice description must contain 1-4000 characters."));
        return;
    }
    postVoice(name.trimmed(), QJsonObject{
        {QStringLiteral("model"), model_},
        {QStringLiteral("type"), QStringLiteral("prompted")},
        {QStringLiteral("display_name"), name.trimmed()},
        {QStringLiteral("language_code"), QStringLiteral("vi-VN")},
        {QStringLiteral("prompted"), QJsonObject{{QStringLiteral("input"), prompt}}}});
}

void GoogleTtsBackend::postVoice(const QString& trimmedName, const QJsonObject& voice)
{
    if (trimmedName.isEmpty() || trimmedName.size() > 80) {
        emit voiceOperationFinished(false, QStringLiteral("Voice name must contain 1-80 characters."));
        return;
    }
    if (!isAvailable()) {
        emit voiceOperationFinished(false, QStringLiteral("Google AI Studio TTS API key is not configured."));
        return;
    }
    if (voiceOperationActive_) {
        emit voiceOperationFinished(false, QStringLiteral("A Google voice request is already running."));
        return;
    }
    const QJsonObject body{
        {QStringLiteral("store"), true},
        {QStringLiteral("voice"), voice}};
    QNetworkRequest request(voicesEndpoint_);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("x-goog-api-key", apiKey_.toUtf8());
    QNetworkReply* const voiceReply = network_->post(request,
        QJsonDocument(body).toJson(QJsonDocument::Compact));
    voiceReply->setReadBufferSize(kMaximumAudioBytes);
    voiceOperationActive_ = true;
    auto* const voiceTimeout = new QTimer(voiceReply);
    voiceTimeout->setSingleShot(true);
    voiceTimeout->setInterval(30000);
    connect(voiceTimeout, &QTimer::timeout, voiceReply, &QNetworkReply::abort);
    connect(voiceReply, &QNetworkReply::finished, this,
        [this, voiceReply, voiceTimeout, trimmedName] {
            voiceTimeout->stop();
            voiceOperationActive_ = false;
            const int status = voiceReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QJsonObject response = QJsonDocument::fromJson(voiceReply->readAll()).object();
            const QJsonObject voiceObject = response.value(QStringLiteral("voice")).toObject();
            const QJsonObject replicatedObject = voiceObject.value(QStringLiteral("replicated")).toObject();
            const QString id = voiceObject.value(QStringLiteral("id")).toString(
                voiceObject.value(QStringLiteral("voice_id")).toString(
                    replicatedObject.value(QStringLiteral("id")).toString(
                        replicatedObject.value(QStringLiteral("voice_id")).toString(
                            response.value(QStringLiteral("id")).toString(
                                response.value(QStringLiteral("replicated_voice")).toObject()
                                    .value(QStringLiteral("id")).toString())))));
            const bool success = voiceReply->error() == QNetworkReply::NoError
                && status >= 200 && status < 300 && id.startsWith(QStringLiteral("voice_"))
                && id.size() <= 256;
            voiceReply->deleteLater();
            if (!success) {
                emit voiceOperationFinished(false,
                    status == 401 || status == 403
                        ? QStringLiteral("Google rejected the API key or voice consent.")
                        : QStringLiteral("Google could not create the voice (HTTP %1).").arg(status));
                return;
            }
            emit voiceCreated(trimmedName, id);
            emit voiceOperationFinished(true, QStringLiteral("Đã tạo giọng %1.").arg(trimmedName));
        });
    voiceTimeout->start();
}

} // namespace raceengineer
