#include "tts/TtsBenchmark.h"
#include "config/SettingsManager.h"
#include "tts/VieNeuTtsBackend.h"
#include <QCoreApplication>
#include <QAudioSink>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTimer>
#include <QThread>
#include <QSemaphore>
#include <cstdio>
#include <cstdlib>

using namespace raceengineer;
namespace {
void check(bool condition, const char* message)
{
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
QJsonObject row(int threads, double p95 = 800)
{
    return {{"version", 1}, {"type", "result"}, {"threads", threads}, {"samples", 23},
        {"short_samples", 20}, {"p50_ms", 500}, {"p95_ms", p95}, {"cpu_ms", 1100},
        {"peak_working_set", 400000000}, {"peak_private_bytes", 500000000},
        {"init_ms", 1200}, {"runtime", "mock CPU"}};
}
int mockHarness(QCoreApplication& app, const QString& mode, int threads)
{
    if (mode == "hang") { QTimer::singleShot(10000, &app, &QCoreApplication::quit); return app.exec(); }
    QByteArray stream;
    for (int count = 1; count <= 23; ++count)
        stream += QJsonDocument(QJsonObject{{"version", 1}, {"type", "progress"},
            {"threads", threads}, {"completed", count}}).toJson(QJsonDocument::Compact) + '\n';
    auto result = row(threads);
    if (mode == "wrong") result.insert("threads", 6);
    if (mode == "missing") result.remove("cpu_ms");
    if (mode == "null") result.insert("p95_ms", QJsonValue());
    if (mode == "runtime" && threads == 3) result.insert("runtime", "mock GPU");
    if (mode != "incomplete") stream += QJsonDocument(result).toJson(QJsonDocument::Compact) + '\n';
    if (mode == "bad") stream = "bad json\n";
    // Deliberately split in the middle of the first object and result object.
    fwrite(stream.constData(), 7, 1, stdout); fflush(stdout);
    QTimer::singleShot(10, &app, [&] {
        fwrite(stream.constData() + 7, size_t(stream.size() - 7), 1, stdout);
        fflush(stdout);
        app.quit();
    });
    return app.exec();
}
void save(const QString& path, const QJsonObject& object)
{
    QFile file(path);
    check(file.open(QIODevice::WriteOnly), "settings write");
    file.write(QJsonDocument(object).toJson());
}
}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() > 2 && args[1] == "--mock") return mockHarness(app, args[2], args.last().toInt());
    if (args.contains("--playback-resume")) {
        VieNeuTtsBackend backend("missing-test-model", "test", LocalAiRuntimeSelection{}, 2);
        backend.setVolume(0);
        const auto wait = [](int ms) {
            QEventLoop loop;
            QTimer::singleShot(ms, &loop, &QEventLoop::quit);
            loop.exec();
        };
        int finished = 0;
        QObject::connect(&backend, &ITtsBackend::speakingFinished, &app, [&] { ++finished; });
        const auto text = QStringLiteral("Nhiên liệu sắp hết.");
        check(backend.canSpeakCached(text), "local cache available for offline playback");
        backend.speak(text);
        wait(80);
        auto* original = backend.findChild<QAudioSink*>();
        check(original != nullptr, "local original sink exists");
        const auto token = backend.pauseSpeech();
        const auto position = original->processedUSecs();
        wait(100);
        check(original->state() == QAudio::SuspendedState && original->processedUSecs() == position,
            "local sink preserves PCM position while suspended");
        backend.speak(QStringLiteral("Nhiên liệu nguy cấp."));
        wait(3000);
        check(finished == 1, "urgent cached speech completes without finishing suspended speech");
        check(backend.resumeSpeech(token), "local original speech resumes");
        wait(3000);
        check(finished == 2, "original local audio completes after interruption");
        check(!backend.resumeSpeech(token), "consumed token cannot be resumed twice");
        backend.speak(text);
        const auto cancelled = backend.pauseSpeech();
        backend.discardSpeech(cancelled);
        check(!backend.resumeSpeech(cancelled), "cancelled local continuation is discarded");
        return 0;
    }
    if (args.contains("--lifecycle-owned") || args.contains("--lifecycle-external")) {
        const bool external = args.contains("--lifecycle-external");
        if (external) qputenv("OMP_NUM_THREADS", "6");
        else qunsetenv("OMP_NUM_THREADS");
        check(VieNeuTtsBackend::hasExternalThreadOverride() == external, "external OMP detected before writes");
        VieNeuTtsBackend backend("missing-test-model", "test", LocalAiRuntimeSelection{}, 2);
        for (int threads : {2, 3, 4}) {
            check(backend.setCpuThreads(threads), "thread setter while context stopped");
            check(qEnvironmentVariableIntValue("OMP_NUM_THREADS") == (external ? 6 : threads), "owned OMP updates");
            check(VieNeuTtsBackend::hasExternalThreadOverride() == external, "owned OMP isn't external");
        }
        check(!backend.setCpuThreads(6), "six threads rejected");
        int releases = 0;
        QEventLoop loop;
        QObject::connect(&backend, &VieNeuTtsBackend::modelReleased, &loop, [&] {
            ++releases;
            check(backend.setCpuThreads(3), "setter allowed after worker acknowledgement");
            loop.quit();
        });
        backend.releaseModel();
        backend.releaseModel(); // The first queued acknowledgement is stale.
        check(releases == 0 && !backend.setCpuThreads(4), "release is asynchronous, no premature thread change");
        QTimer::singleShot(2000, &loop, &QEventLoop::quit);
        loop.exec();
        check(releases == 1, "only current release generation acknowledged");
        bool failedWarmup = false;
        QObject::connect(&backend, &ITtsBackend::warmUpFinished, &app, [&](bool success, const QString&) {
            failedWarmup = !success;
        });
        backend.warmUp();
        check(failedWarmup, "model failure remains recoverable");
        auto* workerThread = backend.findChild<QThread*>();
        check(workerThread != nullptr, "worker thread owned by backend");
        QSemaphore started;
        if (!external) {
            auto* slowNativeWork = new QObject;
            slowNativeWork->moveToThread(workerThread);
            QObject::connect(workerThread, &QThread::finished, slowNativeWork, &QObject::deleteLater);
            QMetaObject::invokeMethod(slowNativeWork, [&] {
                started.release();
                QThread::msleep(2100); // Exceeds the former unsafe two-second destruction timeout.
            }, Qt::QueuedConnection);
            check(started.tryAcquire(1, 2000), "simulated native work started");
        }
        backend.shutdown();
        check(!workerThread->isRunning(), "shutdown waits for native work before destruction");
        return 0;
    }
    app.setApplicationName("RaceEngineer-TtsBenchmarkTests");
    QStandardPaths::setTestModeEnabled(true);
    const QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).filePath("settings.json");
    QFile::remove(path);
    SettingsManager fresh;
    check(!fresh.setupCompleted() && fresh.setupStep() == 0 && fresh.tts().cpuThreads == 2, "fresh defaults");
    fresh.setSetupState(false, 2);
    SettingsManager resume;
    check(!resume.setupCompleted() && resume.setupStep() == 2, "resume wizard");
    resume.setSetupState(true, 3);
    check(SettingsManager().setupCompleted(), "skip/finish persists");
    resume.setSetupState(false, 0);
    check(!SettingsManager().setupCompleted(), "reopen persists");
    for (int step = 0; step < 4; ++step) {
        resume.setSetupState(false, step);
        check(SettingsManager().setupStep() == step && !SettingsManager().setupCompleted(), "resume each step");
        resume.setSetupState(true, step);
        check(SettingsManager().setupCompleted() && SettingsManager().tts().cpuThreads == 2, "skip each step preserves configuration");
    }
    for (int threads : {2, 3, 4, 6, 0}) {
        save(path, {{"settings_version", 14}, {"tts", QJsonObject{{"cpu_threads", threads}}}});
        SettingsManager upgrade;
        check(upgrade.setupCompleted(), "valid upgrade doesn't open wizard");
        check(upgrade.tts().cpuThreads == (threads >= 2 && threads <= 4 ? threads : 4), "thread migration");
    }
    save(path, {{"settings_version", 14}});
    check(SettingsManager().tts().cpuThreads == 4, "missing old thread value uses previous default");
    save(path, {});
    check(!SettingsManager().setupCompleted(), "empty object not a configured upgrade");
    QFile invalid(path); check(invalid.open(QIODevice::WriteOnly), "invalid settings open");
    invalid.write("{bad"); invalid.close();
    check(!SettingsManager().setupCompleted(), "malformed config opens setup");
    save(path, {{"settings_version", 15}, {"setup_completed", false}, {"setup_step", 99}});
    check(SettingsManager().setupStep() == 3, "step bounded");
    SettingsManager persistence;
    auto voiceSettings = persistence.tts();
    voiceSettings.googleVoiceName = QStringLiteral("Bono · Tiếng Việt");
    voiceSettings.googleVoiceId = QStringLiteral("voice_bono_test");
    persistence.setTts(voiceSettings);
    check(SettingsManager().tts().googleCustomVoices.size() == 1, "selected legacy/custom voice enters persisted library");
    voiceSettings = persistence.tts();
    voiceSettings.googleVoiceName.clear();
    voiceSettings.googleVoiceId.clear();
    voiceSettings.googleVoice = "Orus";
    persistence.setTts(voiceSettings);
    check(SettingsManager().tts().googleCustomVoices.first().toObject().value("name").toString()
        == QStringLiteral("Bono · Tiếng Việt"), "switching to built-in retains named custom voice after restart");
    persistence.setTtsBenchmark({{"complete", true}, {"results", QJsonArray{row(2), row(3), row(4)}}});
    check(SettingsManager().ttsBenchmark().value("results").toArray().size() == 3, "benchmark persists");
    check(chooseTtsThreads({row(2, 1000), row(3, 500), row(4, 300)}) == 2, "least threads at threshold");
    check(chooseTtsThreads({row(2, 1200), row(3, 1000), row(4, 900)}) == 3, "threshold at 3");
    check(chooseTtsThreads({row(2, 2200), row(3, 1600), row(4, 1500)}) == 4, "fastest when target missed");
    check(chooseTtsThreads({row(2, 1575), row(3, 1550), row(4, 1500)}) == 2, "5 percent tie");
    check(chooseTtsThreads({row(2), row(3)}) == 0, "partial never applies");
    check(chooseTtsThreads({row(6), row(3), row(4)}) == 0, "wrong level never applies");
    for (const auto& mode : {"ok", "bad", "missing", "null", "wrong", "incomplete", "runtime", "hang", "cancel"}) {
        TtsBenchmark benchmark;
        QEventLoop loop;
        int chosen = -1, finishes = 0;
        QObject::connect(&benchmark, &TtsBenchmark::finished, &loop, [&](int selected) {
            chosen = selected; ++finishes; loop.quit();
        });
        benchmark.prepare();
        check(benchmark.running() && benchmark.results().isEmpty(), "release reservation");
        const QString actualMode = QString::fromLatin1(mode) == "cancel" ? "hang" : QString::fromLatin1(mode);
        benchmark.start(app.applicationFilePath(), {"--mock", actualMode}, actualMode == "hang" ? 250 : 3000);
        if (QString::fromLatin1(mode) == "cancel") QTimer::singleShot(50, &benchmark, [&] { benchmark.cancel(); });
        QTimer::singleShot(12000, &loop, &QEventLoop::quit);
        loop.exec();
        check(chosen == (QString::fromLatin1(mode) == "ok" ? 2 : 0), mode);
        check(!benchmark.running(), "completion releases coordinator");
        QEventLoop drain; QTimer::singleShot(50, &drain, &QEventLoop::quit); drain.exec();
        check(finishes == 1, "stale completion ignored");
    }
    TtsBenchmark beforeRelease;
    int cancelled = -1;
    QObject::connect(&beforeRelease, &TtsBenchmark::finished, &app, [&](int selected) { cancelled = selected; });
    beforeRelease.prepare(); beforeRelease.cancel();
    check(cancelled == 0 && !beforeRelease.running(), "cancel while waiting for release");
    QFile::remove(path);
    puts("TtsBenchmarkTests passed");
    return 0;
}
