#include "tts/GoogleTtsBackend.h"
#include "llm/providers/OpenAICompatibleProvider.h"
#include "utils/ApiRetry.h"

#include <QCoreApplication>
#include <QAudioFormat>
#include <QAudioSink>
#include <QMediaDevices>
#include <QDataStream>
#include <QEventLoop>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <functional>
#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>

using raceengineer::GoogleTtsBackend;

namespace {

int failures = 0;

void expect(const bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

class HttpMock final : public QObject {
public:
    HttpMock(const int status, QByteArray body, const bool splitResponse = false)
        : status_(status), body_(std::move(body)), splitResponse_(splitResponse)
    {
        if (!server_.listen(QHostAddress::LocalHost, 0)) {
            throw std::runtime_error("Could not start local HTTP mock.");
        }
        connect(&server_, &QTcpServer::newConnection, this, [this] { acceptRequest(); });
    }

    [[nodiscard]] QUrl endpoint(const QString& path) const
    {
        QUrl url;
        url.setScheme(QStringLiteral("http"));
        url.setHost(QStringLiteral("127.0.0.1"));
        url.setPort(server_.serverPort());
        url.setPath(path);
        return url;
    }

    [[nodiscard]] const QByteArray& request() const noexcept { return request_; }
    [[nodiscard]] int requestCount() const { return requestCount_; }
    void setStatuses(QList<int> statuses) { statuses_ = std::move(statuses); }

private:
    void acceptRequest()
    {
        QTcpSocket* const socket = server_.nextPendingConnection();
        auto bytes = std::make_shared<QByteArray>();
        auto responded = std::make_shared<bool>(false);
        const auto readRequest = [this, socket, bytes, responded] {
            if (!socket || *responded) return;
            bytes->append(socket->readAll());
            const qsizetype headerEnd = bytes->indexOf("\r\n\r\n");
            if (headerEnd < 0) return;
            const QByteArray headers = bytes->left(headerEnd).toLower();
            const auto match = QRegularExpression(QStringLiteral("content-length:\\s*(\\d+)"))
                .match(QString::fromLatin1(headers));
            if (!match.hasMatch() && !bytes->startsWith("GET ")) return;
            const qsizetype bodyLength = match.hasMatch() ? match.captured(1).toLongLong() : 0;
            if (bytes->size() < headerEnd + 4 + bodyLength) return;

            request_ = *bytes;
            *responded = true;
            const int status = statuses_.isEmpty() ? status_ : statuses_.value(std::min(requestCount_, static_cast<int>(statuses_.size()) - 1));
            ++requestCount_;
            const QByteArray reason = status == 200 ? "OK" : status == 401 ? "Unauthorized"
                : status == 403 ? "Forbidden" : status == 429 ? "Too Many Requests" : "Error";
            const QByteArray header = "HTTP/1.1 " + QByteArray::number(status) + ' ' + reason
                + "\r\nContent-Type: application/json\r\nContent-Length: "
                + QByteArray::number(body_.size()) + "\r\nConnection: close\r\n\r\n";
            socket->write(header);
            if (!splitResponse_) {
                socket->write(body_);
                socket->disconnectFromHost();
                return;
            }

            const QByteArray first = body_.left(body_.size() / 2);
            const QByteArray second = body_.mid(body_.size() / 2);
            QTimer::singleShot(5, socket, [socket, first] { socket->write(first); });
            QTimer::singleShot(15, socket, [socket, second] {
                socket->write(second);
                socket->disconnectFromHost();
            });
        };
        connect(socket, &QIODevice::readyRead, socket, readRequest);
        readRequest();
    }

    QTcpServer server_;
    int status_;
    QByteArray body_;
    bool splitResponse_{false};
    int requestCount_{0};
    QList<int> statuses_;
    QByteArray request_;
};

QByteArray pcmWav(const int seconds)
{
    constexpr quint32 sampleRate = 8000;
    constexpr quint16 channels = 1;
    constexpr quint16 bits = 16;
    const QByteArray pcm(static_cast<qsizetype>(sampleRate * seconds * channels * (bits / 8)), '\0');
    QByteArray wav;
    QDataStream out(&wav, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("RIFF", 4);
    out << static_cast<quint32>(36 + pcm.size());
    out.writeRawData("WAVEfmt ", 8);
    out << quint32(16) << quint16(1) << channels << sampleRate
        << quint32(sampleRate * channels * (bits / 8))
        << quint16(channels * (bits / 8)) << bits;
    out.writeRawData("data", 4);
    out << static_cast<quint32>(pcm.size());
    out.writeRawData(pcm.constData(), pcm.size());
    return wav;
}

QString awaitError(GoogleTtsBackend& backend, const std::function<void()>& request)
{
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.start(3000);
    QString error;
    const auto errorConnection = QObject::connect(&backend, &GoogleTtsBackend::errorOccurred,
        &loop, [&loop, &error](const QString& message) { error = message; loop.quit(); });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    request();
    loop.exec();
    QObject::disconnect(errorConnection);
    return error;
}

void testHttpFailures()
{
    for (const auto [status, expected] : {
             std::pair{401, QStringLiteral("authentication failed")},
             std::pair{429, QStringLiteral("quota or rate limit")}}) {
        HttpMock mock(status, QByteArrayLiteral("{}"));
        GoogleTtsBackend backend(mock.endpoint(QStringLiteral("/v1beta/interactions")),
            mock.endpoint(QStringLiteral("/v1beta/voices")));
        backend.setApiKey(QStringLiteral("offline-test-key"));
        const QString error = awaitError(backend, [&] { backend.speak(QStringLiteral("Test")); });
        if (!error.contains(expected, Qt::CaseInsensitive)) {
            std::cerr << "HTTP " << status << " returned unexpected TTS error: "
                      << error.toStdString() << '\n';
            expect(false, "HTTP status maps to useful TTS error");
        }
        expect(mock.request().startsWith("POST /v1beta/interactions "), "uses Interactions endpoint");
        expect(mock.request().contains("x-goog-api-key: offline-test-key"), "sends key in header");
        const qsizetype requestLineEnd = mock.request().indexOf("\r\n");
        expect(requestLineEnd > 0 && !mock.request().left(requestLineEnd).contains('?'),
            "does not put API key in URL");
    }
}

void testSplitStreamError()
{
    HttpMock mock(200, QByteArrayLiteral("data: {\"type\":\"error\",\"message\":\"offline mock\"}\n\n"), true);
    GoogleTtsBackend backend(mock.endpoint(QStringLiteral("/v1beta/interactions")),
        mock.endpoint(QStringLiteral("/v1beta/voices")));
    backend.setApiKey(QStringLiteral("offline-test-key"));
    const QString error = awaitError(backend, [&] { backend.speak(QStringLiteral("Test")); });
    expect(error.contains(QStringLiteral("request failed"), Qt::CaseInsensitive),
        "parses an SSE error split across response writes");
}

void testRaceEngineerVoiceStyle()
{
    const QString transcript = QStringLiteral("Vào pit vòng này. Xe phía sau cách một phẩy tám giây.");
    for (const auto& model : {QStringLiteral("gemini-3.8-flash-tts"),
             QStringLiteral("gemini-3.8-flash-lite-tts")}) {
        for (const auto& voiceId : {QString{}, QStringLiteral("voice_mock")}) {
            HttpMock mock(401, QByteArrayLiteral("{}"));
            GoogleTtsBackend backend(mock.endpoint(QStringLiteral("/v1beta/interactions")),
                mock.endpoint(QStringLiteral("/v1beta/voices")));
            backend.setApiKey(QStringLiteral("offline-test-key"));
            backend.setModel(model);
            backend.setVoice(QStringLiteral("Orus"), voiceId);
            awaitError(backend, [&] { backend.speak(transcript); });
            const auto body = QJsonDocument::fromJson(mock.request().mid(
                mock.request().indexOf("\r\n\r\n") + 4)).object();
            const auto text = body.value("input").toArray().first().toObject()
                .value("content").toArray().first().toObject();
            expect(text.value("text").toString() == transcript,
                "voice instructions do not change the spoken transcript");
            const auto annotation = text.value("annotations").toArray().first().toObject();
            const auto style = annotation.value("style").toString();
            expect(annotation.value("type") == QStringLiteral("speech_metadata")
                && style.contains(QStringLiteral("tiếng Việt tự nhiên"))
                && style.contains(QStringLiteral("không hét")),
                "all Google requests carry Vietnamese radio delivery metadata");
            const auto voice = body.value("generation_config").toObject()
                .value("speech_config").toArray().first().toObject().value("voice").toString();
            expect(voice == (voiceId.isEmpty() ? QStringLiteral("Orus") : voiceId)
                && body.value("model") == model && body.value("stream").toBool(),
                "style preserves selected model, built-in/custom voice and streaming");
        }
    }
}

void testVoiceReplication()
{
    HttpMock mock(200, QByteArrayLiteral("{\"id\":\"voice_mock\"}"));
    GoogleTtsBackend backend(mock.endpoint(QStringLiteral("/v1beta/interactions")),
        mock.endpoint(QStringLiteral("/v1beta/voices")));
    backend.setApiKey(QStringLiteral("offline-test-key"));

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.start(3000);
    bool success = false;
    QString createdId;
    QObject::connect(&backend, &GoogleTtsBackend::voiceCreated, &loop,
        [&createdId](const QString&, const QString& id) { createdId = id; });
    QObject::connect(&backend, &GoogleTtsBackend::voiceOperationFinished, &loop,
        [&loop, &success](const bool ok, const QString&) { success = ok; loop.quit(); });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    const QByteArray sample = pcmWav(10);
    backend.createReplicatedVoice(QStringLiteral("Offline voice"), sample, sample);
    loop.exec();
    expect(success && createdId == QStringLiteral("voice_mock"), "accepts successful mock voice creation");
    expect(mock.request().startsWith("POST /v1beta/voices "), "uses Voices endpoint");
    expect(mock.request().contains("\"store\":true"), "stores cloned voice through Voices API");
    expect(mock.request().contains("\"source_audio\"")
        && mock.request().contains("\"consent_audio\""), "sends both WAV recordings");
}

void testVoiceReplicationFailure()
{
    HttpMock mock(403, QByteArrayLiteral("{}"));
    GoogleTtsBackend backend(mock.endpoint(QStringLiteral("/v1beta/interactions")),
        mock.endpoint(QStringLiteral("/v1beta/voices")));
    backend.setApiKey(QStringLiteral("offline-test-key"));
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.start(3000);
    bool success = true;
    QObject::connect(&backend, &GoogleTtsBackend::voiceOperationFinished, &loop,
        [&loop, &success](const bool ok, const QString&) { success = ok; loop.quit(); });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    const QByteArray sample = pcmWav(10);
    backend.createReplicatedVoice(QStringLiteral("Offline voice"), sample, sample);
    loop.exec();
    expect(!success, "reports mock voice API authentication failure");
}

void testPromptedVoice()
{
    for (const auto& model : {QStringLiteral("gemini-3.8-flash-tts"),
             QStringLiteral("gemini-3.8-flash-lite-tts")}) {
        HttpMock mock(200, QByteArrayLiteral("{\"id\":\"voice_bono_mock\"}"));
        GoogleTtsBackend backend(mock.endpoint("/v1beta/interactions"), mock.endpoint("/v1beta/voices"));
        backend.setApiKey("offline-test-key");
        backend.setModel(model);
        QEventLoop loop;
        bool created = false;
        QObject::connect(&backend, &GoogleTtsBackend::voiceCreated, &loop,
            [&](const QString& name, const QString& id) {
                created = name == QStringLiteral("Bono · Tiếng Việt") && id == "voice_bono_mock";
            });
        QObject::connect(&backend, &GoogleTtsBackend::voiceOperationFinished, &loop,
            [&](bool, const QString&) { loop.quit(); });
        QTimer::singleShot(3000, &loop, &QEventLoop::quit);
        backend.createPromptedVoice(QStringLiteral("Bono · Tiếng Việt"), backend.bonoVoiceDescription());
        loop.exec();
        const auto body = QJsonDocument::fromJson(mock.request().mid(
            mock.request().indexOf("\r\n\r\n") + 4)).object();
        const auto voice = body.value("voice").toObject();
        expect(created && body.value("store").toBool() && voice.value("type") == "prompted"
            && voice.value("language_code") == "vi-VN" && voice.value("model") == model
            && voice.value("prompted").toObject().value("input").toString().contains("Bono"),
            "creates persistent named Vietnamese Bono persona via real Voices payload");
    }
    for (const auto& response : {QByteArrayLiteral("{\"id\":\"not_a_voice\"}"), QByteArrayLiteral("{}")}) {
        HttpMock mock(200, response);
        GoogleTtsBackend backend(mock.endpoint("/interactions"), mock.endpoint("/voices"));
        backend.setApiKey("offline-test-key");
        QEventLoop loop;
        bool success = true;
        QObject::connect(&backend, &GoogleTtsBackend::voiceOperationFinished, &loop,
            [&](bool ok, const QString&) { success = ok; loop.quit(); });
        QTimer::singleShot(3000, &loop, &QEventLoop::quit);
        backend.createPromptedVoice("Bono", backend.bonoVoiceDescription());
        loop.exec();
        expect(!success, "invalid voice ID never reports creation success");
    }
}

void testPcmChunkBoundaries()
{
    QByteArray remainder, pcm, joined;
    const QByteArray input = QByteArray::fromHex("010203040506");
    for (const auto& bytes : {input.left(1), input.mid(1, 2), input.mid(3)}) {
        expect(raceengineer::decodeGooglePcmChunk(bytes.toBase64(), remainder, pcm),
            "odd-sized PCM chunks are valid before stream completion");
        expect((pcm.size() & 1) == 0 && remainder.size() <= 1, "only complete 16-bit samples reach playback");
        joined += pcm;
    }
    expect(joined == input && remainder.isEmpty(), "sample bytes crossing chunk boundaries are preserved");
    expect(!raceengineer::decodeGooglePcmChunk("!!bad-base64!!", remainder, pcm),
        "malformed base64 is rejected strictly");
    HttpMock mock(200, QByteArrayLiteral("data: {\"delta\":{\"type\":\"audio\",\"data\":\"AQ==\"}}\n\n"), true);
    GoogleTtsBackend backend(mock.endpoint("/interactions"), mock.endpoint("/voices"));
    backend.setApiKey("offline-test-key");
    const auto error = awaitError(backend, [&] { backend.speak("Test"); });
    expect(error.contains("incomplete 16-bit sample"), "truncated stream fails at completion, not first odd chunk");
    for (const auto& event : {
             QByteArrayLiteral("data: {\"delta\":{\"type\":\"audio\",\"data\":\"!!bad!!\"}}\n\n"),
             QByteArrayLiteral("data: {\"delta\":{\"type\":\"audio\",\"mime_type\":\"audio/wav\",\"data\":\"AQI=\"}}\n\n")}) {
        HttpMock invalid(200, event, true);
        GoogleTtsBackend invalidBackend(invalid.endpoint("/interactions"), invalid.endpoint("/voices"));
        invalidBackend.setApiKey("offline-test-key");
        const auto failure = awaitError(invalidBackend, [&] { invalidBackend.speak("Test"); });
        expect(failure.contains("invalid base64") || failure.contains("unsupported audio format"),
            "invalid audio reports the specific format/decoding error without requiring speakers");
    }
}

// Explicit manual check: local HTTP mock + silent audio, never a paid API call.
void testDevicePlayback(const QString& description)
{
    auto device = QMediaDevices::defaultAudioOutput();
    if (!description.isEmpty()) {
        const auto devices = QMediaDevices::audioOutputs();
        const auto selected = std::find_if(devices.cbegin(), devices.cend(), [&](const QAudioDevice& output) {
            return output.description() == description;
        });
        expect(selected != devices.cend(), "configured audio device is present");
        if (selected == devices.cend()) return;
        device = *selected;
    }
    QAudioFormat source;
    source.setSampleRate(24000);
    source.setChannelCount(1);
    source.setSampleFormat(QAudioFormat::Int16);
    std::cout << "Device: " << device.description().toStdString()
              << "; preferred rate=" << device.preferredFormat().sampleRate()
              << "; channels=" << device.preferredFormat().channelCount()
              << "; sample format=" << static_cast<int>(device.preferredFormat().sampleFormat())
              << "; native 24k mono support=" << device.isFormatSupported(source) << '\n';
    expect(!device.isNull(), "manual check has a real audio output device");
    if (device.isNull()) return;
    const auto encoded = QByteArray(48000, '\0').toBase64();
    const auto body = QByteArrayLiteral("data: {\"delta\":{\"type\":\"audio\",\"mime_type\":\"audio/l16\",\"data\":\"")
        + encoded + QByteArrayLiteral("\"}}\n\n");
    HttpMock mock(200, body, true);
    GoogleTtsBackend backend(mock.endpoint("/interactions"), mock.endpoint("/voices"));
    backend.setApiKey("offline-test-key");
    backend.setVolume(0);
    backend.setAudioOutputDevice(description);
    QEventLoop loop;
    bool started = false, finished = false;
    QString error;
    QObject::connect(&backend, &GoogleTtsBackend::speakingStarted, &loop,
        [&](const QString&) { started = true; });
    QObject::connect(&backend, &GoogleTtsBackend::errorOccurred, &loop,
        [&](const QString& message) { error = message; });
    QObject::connect(&backend, &GoogleTtsBackend::speakingFinished, &loop,
        [&] { finished = true; loop.quit(); });
    QTimer::singleShot(6000, &loop, &QEventLoop::quit);
    backend.speak("Offline silent playback check");
    loop.exec();
    if (!error.isEmpty()) std::cerr << error.toStdString() << '\n';
    expect(started && finished && error.isEmpty(), "24kHz mono PCM plays through device conversion and completes");
    if (!failures) std::cout << "Device playback: passed\n";
}

void waitMs(int milliseconds)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

void testInterruptedPlayback(const QString& description)
{
    const auto encoded = QByteArray(96000, '\0').toBase64();
    const auto body = QByteArrayLiteral("data: {\"delta\":{\"type\":\"audio\",\"mime_type\":\"audio/l16\",\"data\":\"")
        + encoded + QByteArrayLiteral("\"}}\n\n");
    HttpMock mock(200, body, true);
    GoogleTtsBackend backend(mock.endpoint("/interactions"), mock.endpoint("/voices"));
    backend.setApiKey("offline-test-key");
    backend.setVolume(0);
    backend.setAudioOutputDevice(description);
    int finished = 0;
    QString error;
    QObject::connect(&backend, &GoogleTtsBackend::speakingFinished, &backend, [&] { ++finished; });
    QObject::connect(&backend, &GoogleTtsBackend::errorOccurred, &backend,
        [&](const QString& message) { error = message; });
    backend.speak("Conversation already speaking");
    waitMs(250);
    auto* original = backend.findChild<QAudioSink*>();
    expect(original && original->processedUSecs() > 0, "conversation has played some PCM");
    if (!original) return;
    const auto token = backend.pauseSpeech();
    const auto position = original->processedUSecs();
    waitMs(150);
    expect(token != 0 && original->state() == QAudio::SuspendedState,
        "original sink suspends for interruption");
    expect(original->processedUSecs() == position, "paused PCM position stays unchanged");
    backend.speak("Higher-priority speech on the same Google backend");
    waitMs(2600);
    expect(finished == 1, "only urgent speech finishes while conversation is paused");
    expect(backend.resumeSpeech(token), "original conversation resumes");
    waitMs(2800);
    expect(finished == 2 && mock.requestCount() == 2,
        "resume completes original audio without another synthesis request");
    expect(error.isEmpty(), "interrupt/resume has no audio or stream error");
    backend.speak("Paused before streaming audio arrives");
    const auto beforeAudio = backend.pauseSpeech();
    waitMs(250);
    expect(finished == 2, "paused stream receiving audio does not finish active radio state");
    expect(backend.resumeSpeech(beforeAudio), "paused incoming stream is retained");
    waitMs(2600);
    expect(finished == 3 && mock.requestCount() == 3, "suspended incoming stream resumes without replay");
    backend.speak("Cancelled continuation");
    const auto cancelled = backend.pauseSpeech();
    backend.discardSpeech(cancelled);
    expect(!backend.resumeSpeech(cancelled), "discarded speech token cannot resume");
    waitMs(50);
    expect(error.isEmpty(), "cancelled paused callbacks are ignored");
}

void testApiRetries()
{
    {
        HttpMock mock(401, "{}");
        GoogleTtsBackend backend(mock.endpoint("/interactions"), mock.endpoint("/voices"));
        backend.setApiKey("offline-test-key");
        int completions = 0;
        QString error;
        QObject::connect(&backend, &GoogleTtsBackend::speakingFinished, &backend, [&] { ++completions; });
        QObject::connect(&backend, &GoogleTtsBackend::errorOccurred, &backend,
            [&](const QString& message) { error = message; });
        backend.speak("Suspended request that fails");
        const auto token = backend.pauseSpeech();
        waitMs(100);
        expect(completions == 0 && error.isEmpty(), "paused failure cannot finish the active radio message");
        expect(!backend.resumeSpeech(token), "failed suspended stream cannot resume");
        waitMs(20);
        expect(error.contains("authentication") && completions == 0, "paused error is reported when its continuation is reached");
        backend.speak("Cancelled paused request");
        const auto cancelled = backend.pauseSpeech();
        backend.discardSpeech(cancelled);
        waitMs(50);
        expect(!backend.resumeSpeech(cancelled) && completions == 0, "cancelled paused request ignores stale completions");
    }
    using namespace raceengineer;
    expect(!transientApiFailure(401, QNetworkReply::AuthenticationRequiredError), "bad credentials never retry");
    expect(transientApiFailure(0, QNetworkReply::ConnectionRefusedError), "server starting/offline is retryable");
    expect(apiRetryDelay("2", 0) == 2000 && apiRetryDelay("60", 0) == -1, "respects Retry-After without holding radio too long");
    {
        HttpMock mock(503, "{}");
        mock.setStatuses({503, 429, 401});
        GoogleTtsBackend backend(mock.endpoint("/interactions"), mock.endpoint("/voices"));
        backend.setApiKey("offline-test-key");
        int retries = 0;
        QObject::connect(&backend, &GoogleTtsBackend::retryScheduled, &backend, [&](int, int) { ++retries; });
        const auto error = awaitError(backend, [&] { backend.speak("Retry test"); });
        expect(error.contains("authentication") && retries == 2 && mock.requestCount() == 3,
            "TTS transient failures retry twice and stop on authentication errors");
    }
    {
        HttpMock mock(503, "{}");
        GoogleTtsBackend backend(mock.endpoint("/interactions"), mock.endpoint("/voices"));
        backend.setApiKey("offline-test-key");
        QObject::connect(&backend, &GoogleTtsBackend::retryScheduled, &backend, [&](int, int) { backend.stop(); });
        backend.speak("Cancel retry");
        waitMs(700);
        expect(mock.requestCount() == 1, "TTS cancellation stops scheduled retries");
    }
    {
        HttpMock mock(503, QByteArrayLiteral("data: {\"delta\":{\"type\":\"audio\",\"data\":\"AQ==\"}}\n\n"));
        GoogleTtsBackend backend(mock.endpoint("/interactions"), mock.endpoint("/voices"));
        backend.setApiKey("offline-test-key");
        awaitError(backend, [&] { backend.speak("Partial audio"); });
        expect(mock.requestCount() == 1, "TTS never retries once any audio sample data arrives");
    }
    for (const bool connectionTest : {false, true}) {
        const QByteArray body = connectionTest ? QByteArrayLiteral("{\"data\":[{\"id\":\"offline-model\"}]}")
            : QByteArrayLiteral("{\"choices\":[{\"message\":{\"content\":\"OK\"}}]}");
        HttpMock mock(200, body);
        mock.setStatuses({503, 200});
        ProviderConfiguration config;
        config.baseUrl = mock.endpoint("/v1"); config.model = "offline-model"; config.streaming = false;
        OpenAICompatibleProvider provider(config);
        QEventLoop loop;
        bool success = false;
        QObject::connect(&provider, &ILLMProvider::responseReceived, &loop,
            [&](const QJsonObject&, qint64, qint64) { success = true; loop.quit(); });
        QObject::connect(&provider, &ILLMProvider::connectionTested, &loop,
            [&](bool ok, const QString&) { success = ok; loop.quit(); });
        QTimer::singleShot(3000, &loop, &QEventLoop::quit);
        if (connectionTest) provider.testConnection(); else provider.sendChatRequest({});
        loop.exec();
        if (!success || mock.requestCount() != 2)
            std::cerr << "LLM retry check: models=" << connectionTest << " success=" << success
                      << " requests=" << mock.requestCount() << '\n';
        expect(success && mock.requestCount() == 2, "LLM chat/models checks recover from a transient server failure");
    }
    for (const bool connectionTest : {false, true}) {
        HttpMock mock(503, "{}");
        ProviderConfiguration config;
        config.baseUrl = mock.endpoint("/v1"); config.model = "offline-model"; config.streaming = false;
        OpenAICompatibleProvider provider(config);
        if (connectionTest) provider.testConnection(); else provider.sendChatRequest({});
        waitMs(80);
        provider.cancelRequest();
        waitMs(600);
        if (mock.requestCount() != 1) std::cerr << "LLM cancel check: models=" << connectionTest << " requests=" << mock.requestCount() << '\n';
        expect(mock.requestCount() == 1, "LLM cancellation invalidates queued chat and connection retries");
    }
    {
        HttpMock mock(200, QByteArrayLiteral("{\"data\":[{\"id\":\"offline-model\"}]}"));
        mock.setStatuses({503, 503, 503, 200});
        ProviderConfiguration config;
        config.baseUrl = mock.endpoint("/v1"); config.model = "offline-model";
        OpenAICompatibleProvider provider(config);
        QEventLoop loop;
        bool recovered = false;
        QObject::connect(&provider, &ILLMProvider::connectionTested, &loop,
            [&](bool ok, const QString&) { if (ok) { recovered = true; loop.quit(); } });
        QTimer::singleShot(20000, &loop, &QEventLoop::quit);
        provider.testConnection();
        loop.exec();
        expect(recovered && mock.requestCount() == 4, "LLM automatically reconnects later after immediate retries are exhausted");
    }
    {
        HttpMock mock(200, QByteArrayLiteral("{\"name\":\"models/gemini-3.8-flash-lite-tts\"}"));
        mock.setStatuses({503, 503, 503, 200});
        GoogleTtsBackend backend(mock.endpoint("/interactions"), mock.endpoint("/voices"));
        backend.setApiKey("offline-test-key");
        QEventLoop loop;
        bool recovered = false;
        QObject::connect(&backend, &GoogleTtsBackend::connectionRecovered, &loop,
            [&] { recovered = true; loop.quit(); });
        QTimer::singleShot(20000, &loop, &QEventLoop::quit);
        backend.speak("Failed utterance must not be replayed");
        loop.exec();
        expect(recovered && mock.requestCount() == 4 && mock.request().startsWith("GET /models/"),
            "TTS idle recovery uses model metadata, never audio generation or old speech replay");
    }
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    if (app.arguments().contains(QStringLiteral("--playback-device"))) {
        const auto description = app.arguments().value(app.arguments().indexOf(QStringLiteral("--playback-device")) + 1);
        testDevicePlayback(description);
        testInterruptedPlayback(description);
        return failures == 0 ? 0 : 1;
    }
    testHttpFailures();
    testSplitStreamError();
    testRaceEngineerVoiceStyle();
    testVoiceReplication();
    testVoiceReplicationFailure();
    testPromptedVoice();
    testPcmChunkBoundaries();
    testApiRetries();
    return failures == 0 ? 0 : 1;
}
