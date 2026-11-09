#include "app/Application.h"
#include "audio/VoiceInputController.h"
#include "audio/AudioCapture.h"
#include "config/SettingsManager.h"
#include "stt/WhisperRecognizer.h"
#include "tts/VieNeuTtsBackend.h"
#include "telemetry/TelemetryManager.h"
#include "fakes/VoiceRuntimeStubs.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTimer>
#include <QDir>
#include <QStandardPaths>
#include <QUuid>
#include <cstdio>
#include <cstdlib>

using namespace raceengineer;
namespace {
void check(bool condition, const char* message)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
void wait(int milliseconds)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}
template <typename Predicate> void until(Predicate ready)
{
    QElapsedTimer clock;
    clock.start();
    while (!ready() && clock.elapsed() < 3000) wait(5);
    check(ready(), "asynchronous lifecycle operation completed");
}
void touch(const QString& path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    check(file.open(QIODevice::WriteOnly), "create synthetic model marker");
}

void testLazyStt()
{
    QTemporaryDir directory;
    const auto path = directory.filePath("synthetic-model.bin");
    touch(path);
    const int before = voice_test::sttLoads;
    const int freedBefore = voice_test::sttFrees;
    qputenv("RACEENGINEER_STT_THREADS", "8");
    WhisperRecognizer recognizer(path, {}, true);
    bool warm = false;
    int transcripts = 0;
    QObject::connect(&recognizer, &WhisperRecognizer::warmUpFinished, &recognizer,
        [&](bool success, const QString&) { warm = success; });
    QObject::connect(&recognizer, &WhisperRecognizer::transcriptionReady, &recognizer,
        [&](const QString&, const QString&) { ++transcripts; });
    recognizer.warmUp();
    check(warm && voice_test::sttLoads == before, "low-resource STT startup does not load a model");
    recognizer.transcribe(QByteArray(30 * 16000 * 2 + 2, '\0'));
    check(voice_test::sttLoads == before, "oversized audio is rejected before model allocation");
    recognizer.findChild<QTimer*>()->setInterval(10);
    recognizer.transcribe(QByteArray(3200, '\0'));
    check(transcripts == 1 && voice_test::sttLoads == before + 1, "PTT loads STT on demand");
    check(voice_test::sttThreads == 2, "low-resource STT caps an eight-thread override at two");
    until([&] { return voice_test::sttFrees == freedBefore + 1; });
    recognizer.transcribe(QByteArray(3200, '\0'));
    check(transcripts == 2 && voice_test::sttLoads == before + 2, "STT remains usable after idle release");
    qunsetenv("RACEENGINEER_STT_THREADS");
}

void testLazyTts()
{
    QTemporaryDir directory;
    for (const auto* file : {"backbone.gguf", "config.json", "tokenizer.json", "vieneu_v3_heads.npz",
             "acoustic/vieneu_acoustic_weights.npz", "codec/moss_audio_tokenizer_decode_full.onnx"})
        touch(directory.filePath(QString::fromLatin1(file)));
    const int before = voice_test::ttsLoads;
    const int freedBefore = voice_test::ttsFrees;
    VieNeuTtsBackend backend(directory.path(), "test", {}, 4);
    backend.setLowResourceMode(true);
    bool warm = false;
    QObject::connect(&backend, &ITtsBackend::warmUpFinished, &backend,
        [&](bool success, const QString&) { warm = success; });
    backend.warmUp();
    check(warm && voice_test::ttsLoads == before, "low-resource TTS startup retains only cached audio");
    backend.findChild<QTimer*>()->setInterval(10);
    backend.speak("On-demand lifecycle test");
    until([&] { return voice_test::ttsFrees == freedBefore + 1; });
    check(voice_test::ttsLoads == before + 1 && voice_test::ttsThreads == 2,
        "local TTS loads on demand with two threads then frees its idle context");
    const int syntheses = voice_test::ttsSyntheses;
    backend.speak("Reload lifecycle test");
    until([&] { return voice_test::ttsFrees == freedBefore + 2; });
    check(voice_test::ttsLoads == before + 2 && voice_test::ttsSyntheses == syntheses + 1,
        "local TTS can reload and skips throwaway warm-up inference in low-resource mode");
}

void testBoundedPtt()
{
    VoiceInputController input;
    auto* capture = input.findChild<AudioCapture*>();
    int utterances = 0;
    QByteArray received;
    QObject::connect(&input, &VoiceInputController::utteranceReady, &input,
        [&](const QByteArray& audio) { ++utterances; received = audio; });
    emit capture->pcm16kReady(QByteArray(16000, 'p')); // More than the 80 ms pre-roll.
    input.beginPushToTalk();
    emit capture->pcm16kReady(QByteArray(30 * 16000 * 2 + 16000, 's'));
    input.endPushToTalk();
    check(utterances == 1 && received.size() == 30 * 16000 * 2,
        "a held PTT is bounded at 30 seconds and releasing it cannot submit twice");
    check(received.startsWith(QByteArray(2560, 'p')) && received[2560] == 's',
        "bounded capture retains exactly the existing 80 ms pre-roll");
}

void testTtsInitializationFailure()
{
    QTemporaryDir directory;
    for (const auto* file : {"backbone.gguf", "config.json", "tokenizer.json", "vieneu_v3_heads.npz",
             "acoustic/vieneu_acoustic_weights.npz", "codec/moss_audio_tokenizer_decode_full.onnx"})
        touch(directory.filePath(QString::fromLatin1(file)));
    VieNeuTtsBackend backend(directory.path(), "test", {}, 2);
    backend.setLowResourceMode(true);
    int finished = 0;
    QObject::connect(&backend, &ITtsBackend::speakingFinished, &backend, [&] { ++finished; });
    voice_test::failTtsInit = true;
    backend.speak("First-use initialization failure");
    until([&] { return finished == 1; });
    check(finished == 1, "failed lazy initialization finishes the radio request instead of blocking its queue");
    voice_test::failTtsInit = false;
    backend.speak("Retry after initialization failure");
    until([&] { return finished == 2; });
    check(finished == 2, "a failed deferred utterance is not replayed during a subsequent request");
}

void testPresentationCadence(bool lightweight)
{
    SettingsManager settings;
    auto llm = settings.llm();
    llm.baseUrl.clear(); // Do not make network requests during this integration test.
    settings.setLlm(llm);
    auto tts = settings.tts();
    tts.audioDucking = false; // The PTT test must not change a running game's volume.
    settings.setTts(tts);
    settings.setLowResourceMode(lightweight);
    check(QFileInfo::exists(settings.filePath()), "test settings are writable in the isolated config directory");
    Application application(false);
    check(application.lowResourceMode() == lightweight, "resource policy survives settings serialization");
    check(!application.aiComputeRestartRequired(), "startup AI policy does not report an unapplied device change");
    application.setAiComputeDevice(QStringLiteral("cpu"));
    check(application.aiComputeRestartRequired(), "a new saved AI device still requires restart");
    application.setAiComputeDevice(QStringLiteral("auto"));
    check(!application.aiComputeRestartRequired(), "restoring the startup device clears the restart requirement");
    wait(100); // Drain the initial disconnected/warm-up notifications.
    // Immutable samples survive a queued cross-thread-style delivery without cloning
    // the opponent tree and without observing later mutations of the producer's state.
    TelemetryManager feed;
    RaceState source;
    source.connected = true; source.speedKmh = 123.;
    OpponentState car; car.driverName = "Original driver name stored in published snapshot";
    source.opponents.push_back(car);
    auto first = std::make_shared<const RaceState>(source);
    const auto* identity = first.get();
    std::weak_ptr<const RaceState> lifetime = first;
    bool delivered = false;
    const auto queued = QObject::connect(&feed, &TelemetryManager::stateUpdated, &application,
        [&](const RaceStateSnapshot& sample) {
            check(sample.get() == identity, "queued delivery shares the immutable sample instead of deep copying it");
            check(sample->opponents[0].driverName == "Original driver name stored in published snapshot",
                "a producer update cannot mutate a previously published opponent tree");
            check(QMetaObject::invokeMethod(&application, "onStateUpdated", Qt::DirectConnection,
                Q_ARG(raceengineer::RaceStateSnapshot, sample)), "deliver retained immutable sample to the native facade");
            delivered = true;
        }, Qt::QueuedConnection);
    emit feed.stateUpdated(first);
    source.opponents[0].driverName = "Producer already changed";
    first.reset();
    check(!lifetime.expired(), "queued event owns the pending sample");
    until([&] { return delivered; });
    QObject::disconnect(queued);
    check(!lifetime.expired(), "facade retains its current immutable snapshot");
    int updates = 0;
    QObject::connect(&application, &Application::telemetryChanged, &application, [&] { ++updates; });
    RaceState state;
    state.connected = true;
    for (int i = 0; i < 31; ++i) {
        state.speedKmh = 100.0 + i;
        state.capturedAt = std::chrono::steady_clock::now();
        check(QMetaObject::invokeMethod(&application, "onStateUpdated", Qt::DirectConnection,
            Q_ARG(raceengineer::RaceStateSnapshot, std::make_shared<const RaceState>(state))), "deliver native telemetry sample");
        wait(35);
    }
    check(lifetime.expired(), "superseded snapshots are released after queued consumers finish");
    std::fprintf(stderr, "Cadence observed: low_resource=%d, updates=%d\n", lightweight, updates);
    check(lightweight ? updates >= 8 && updates <= 12 : updates >= 28,
        "only presentation is capped; normal mode can publish each 30 Hz input sample");
    const int visibleUpdates = updates;
    application.setUiActive(false);
    for (int i = 0; i < 100; ++i) {
        state.speedKmh = 200.0 + i;
        QMetaObject::invokeMethod(&application, "onStateUpdated", Qt::DirectConnection,
            Q_ARG(raceengineer::RaceStateSnapshot, std::make_shared<const RaceState>(state)));
    }
    check(updates == visibleUpdates, "hidden UI produces zero telemetry property notifications");
    application.setUiActive(true);
    check(updates == visibleUpdates + 1 && application.telemetry().value("speedKmh").toDouble() == 299.0,
        "restoring UI immediately publishes the latest input, not a stale snapshot");
    state.connected = false;
    QMetaObject::invokeMethod(&application, "onStateUpdated", Qt::DirectConnection,
        Q_ARG(raceengineer::RaceStateSnapshot, std::make_shared<const RaceState>(state)));
    check(!application.telemetry().value("connected").toBool(), "disconnect bypasses the presentation rate cap");
    int pttEnds = 0;
    QObject::connect(&application, &Application::requestEndPushToTalk, &application, [&] { ++pttEnds; });
    application.beginPushToTalk();
    check(QMetaObject::invokeMethod(&application, "onUtteranceReady", Qt::DirectConnection,
        Q_ARG(QByteArray, QByteArray(3200, '\0'))), "deliver bounded PTT audio to the facade");
    application.endPushToTalk();
    check(pttEnds == 1, "facade PTT release/ducking path ends exactly once after automatic capture completion");
    std::printf("Native telemetry: low_resource=%d, samples=31, visible_notifications=%d, hidden_samples=100, hidden_notifications=0\n",
        lightweight, visibleUpdates);
}

void testSessionHistoryContinuity()
{
    SettingsManager settings;
    auto llm = settings.llm(); llm.baseUrl.clear(); settings.setLlm(llm);
    auto tts = settings.tts(); tts.audioDucking = false; settings.setTts(tts);
    settings.setStrategyRecordingEnabled(false);
    Application application(false);
    wait(100);
    application.setUiActive(false);
    RaceState sample;
    sample.connected = true; sample.currentLap = 1; sample.fuelLiters = 60.;
    sample.previousLapTimeSeconds = 90.; sample.pitState = PitState::Track;
    const auto deliver = [&] {
        sample.capturedAt = std::chrono::steady_clock::now();
        check(QMetaObject::invokeMethod(&application, "onStateUpdated", Qt::DirectConnection,
            Q_ARG(raceengineer::RaceStateSnapshot, std::make_shared<const RaceState>(sample))), "deliver session fixture");
    };
    const auto readings = [&] {
        application.setUiActive(true);
        const auto result = application.telemetry();
        application.setUiActive(false);
        return result;
    };
    deliver();
    sample.currentLap = 2; sample.fuelLiters = 57.; deliver();
    check(readings().value("fuelPerLapLiters").toDouble() == 3., "lap fuel history is available across stable samples");
    sample.track = std::string{}; sample.carModel = std::string{};
    sample.totalLaps = 0; sample.sessionType = SessionType::Unknown;
    for (int i = 0; i < 100; ++i) { sample.speedKmh = 100. + i; deliver(); }
    check(readings().value("fuelPerLapLiters").toDouble() == 3.,
        "empty/missing metadata and changing live values do not reset the existing session history");
    sample.track = "Monza"; deliver();
    check(!readings().contains("fuelPerLapLiters"), "a real metadata/session change still resets history");
    sample.currentLap = 3; sample.fuelLiters = 54.; deliver();
    check(readings().value("fuelPerLapLiters").toDouble() == 3., "history records new laps after a metadata change");
    sample.currentLap = 1; deliver();
    check(!readings().contains("fuelPerLapLiters"), "lap rollback still starts a new session");
    RaceStateSnapshot empty;
    check(QMetaObject::invokeMethod(&application, "onStateUpdated", Qt::DirectConnection,
        Q_ARG(raceengineer::RaceStateSnapshot, empty)), "deliver null defensive snapshot");
    check(readings().value("connected").toBool(), "null transport samples do not erase valid race data");
}
} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QTemporaryDir config;
    QCoreApplication::setApplicationName("ResourceUsage-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    // Keep tests isolated on Windows too; SettingsManager follows AppConfigLocation.
    QCoreApplication::setOrganizationName("RaceEngineerTests");
#ifdef Q_OS_WIN
    QStandardPaths::setTestModeEnabled(true);
#endif
    const auto settingsDirectory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    qunsetenv("OMP_NUM_THREADS");
    testLazyStt();
    testLazyTts();
    testBoundedPtt();
    testTtsInitializationFailure();
    testPresentationCadence(true);
    testPresentationCadence(false);
    testSessionHistoryContinuity();
    QDir(settingsDirectory).removeRecursively();
    std::puts("Resource lifecycle, PTT and native presentation checks passed (AI runtimes stubbed).");
}
