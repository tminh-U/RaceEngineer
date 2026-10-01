#include "tts/TtsBenchmark.h"
#include "tts/VieNeuTtsBackend.h"
#include "ai/LocalAiRuntime.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QTimer>
#include <windows.h>
#include <psapi.h>
#include <io.h>
#include <cstdio>
#include <algorithm>
#include <cmath>

using namespace raceengineer;
namespace {
HANDLE protocolOutput;
void output(QJsonObject event, int threads)
{
    event.insert("version", ttsBenchmarkVersion);
    event.insert("threads", threads);
    const auto line = QJsonDocument(event).toJson(QJsonDocument::Compact) + '\n';
    DWORD written = 0;
    if (!WriteFile(protocolOutput, line.constData(), DWORD(line.size()), &written, nullptr)
        || written != DWORD(line.size())) QCoreApplication::exit(2);
}
double cpuMilliseconds()
{
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return -1;
    const auto ticks = [](FILETIME time) { return (quint64(time.dwHighDateTime) << 32) | time.dwLowDateTime; };
    return (ticks(kernel) + ticks(user)) / 10000.0;
}
}
int main(int argc, char** argv)
{
    // Reserve stdout for JSONL even if the native runtime prints diagnostics.
    if (!DuplicateHandle(GetCurrentProcess(), GetStdHandle(STD_OUTPUT_HANDLE),
        GetCurrentProcess(), &protocolOutput, 0, FALSE, DUPLICATE_SAME_ACCESS)) return 2;
    fflush(stdout);
    _dup2(_fileno(stderr), _fileno(stdout));
    SetStdHandle(STD_OUTPUT_HANDLE, GetStdHandle(STD_ERROR_HANDLE));
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({QCommandLineOption("threads", "CPU threads (2/3/4)", "count"),
        QCommandLineOption("model-dir", "VieNeu model directory", "path"),
        QCommandLineOption("voice", "Voice preset", "name", QStringLiteral("Minh Đức")),
        QCommandLineOption("device", "AI compute device", "id", "auto")});
    parser.process(app);
    const int threads = parser.value("threads").toInt();
    if ((threads != 2 && threads != 3 && threads != 4) || parser.value("model-dir").isEmpty()
        || VieNeuTtsBackend::hasExternalThreadOverride()) return 2;
    const auto selection = LocalAiRuntime::resolveAndApply(parser.value("device"));
    VieNeuTtsBackend backend(parser.value("model-dir"), parser.value("voice"), selection, threads);
    const QStringList shortTexts{QStringLiteral("Bạn đang giữ tốc độ tốt."),
        QStringLiteral("Giữ đều ga ở đoạn này."), QStringLiteral("Vòng vừa rồi rất ổn định."),
        QStringLiteral("Hãy phanh sớm hơn một chút."), QStringLiteral("Tiếp tục giữ nhịp đua này.")};
    QStringList texts;
    for (int repeat = 0; repeat < 4; ++repeat) texts.append(shortTexts);
    texts << QStringLiteral("Bạn đang giữ nhịp tốt. Hãy tiếp tục tập trung vào điểm phanh ở góc tiếp theo.")
        << QStringLiteral("Khoảng cách phía trước đang ổn định. Chúng ta có thể giữ chiến thuật hiện tại thêm vài vòng.")
        << QStringLiteral("Đoạn đường tiếp theo cần sự chính xác. Giữ ga đều và tránh đánh lái quá nhanh khi thoát góc.");
    QVector<double> latencies;
    double totalCpu = 0, cpuStart = 0, initMs = 0;
    quint64 peakWorkingSet = 0, peakPrivate = 0;
    bool measurementFailed = false;
    QString runtime, fallback;
    QElapsedTimer initialization;
    initialization.start();
    QTimer memory;
    memory.setInterval(100);
    const auto sampleMemory = [&] {
        PROCESS_MEMORY_COUNTERS_EX counters{};
        counters.cb = sizeof(counters);
        if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) {
            measurementFailed = true;
            return;
        }
        peakWorkingSet = std::max(peakWorkingSet, quint64(counters.WorkingSetSize));
        peakPrivate = std::max(peakPrivate, quint64(counters.PrivateUsage));
    };
    QObject::connect(&memory, &QTimer::timeout, &app, sampleMemory);
    memory.start();
    sampleMemory();
    QObject::connect(&backend, &VieNeuTtsBackend::runtimeBackendChanged, &app,
        [&](const QString& name, const QString& reason) { runtime = name; fallback = reason; });
    QObject::connect(&backend, &VieNeuTtsBackend::initializationFinished, &app,
        [&](bool success, const QString&, const QString&) {
            initMs = initialization.nsecsElapsed() / 1e6;
            if (!success) app.exit(1);
        });
    const auto next = [&] {
        cpuStart = cpuMilliseconds();
        if (cpuStart < 0) { app.exit(1); return; }
        backend.requestBenchmarkSynthesis(texts.at(latencies.size()));
    };
    QObject::connect(&backend, &ITtsBackend::warmUpFinished, &app, [&](bool success, const QString&) {
        if (!success) { app.exit(1); return; }
        next(); // Backend's normal warm-up sentence is discarded, never timed as a sample.
    });
    QObject::connect(&backend, &VieNeuTtsBackend::synthesisMeasured, &app,
        [&](double ms, bool success, const QString& error) {
            const double cpuEnd = cpuMilliseconds();
            if (!success || measurementFailed || cpuEnd < cpuStart || !std::isfinite(ms) || ms < 0) {
                output({{"type", "error"}, {"message", error}}, threads);
                app.exit(1); return;
            }
            latencies.append(ms);
            totalCpu += cpuEnd - cpuStart;
            sampleMemory();
            output({{"type", "progress"}, {"completed", int(latencies.size())}}, threads);
            if (latencies.size() < texts.size()) { QTimer::singleShot(0, &app, next); return; }
            auto sorted = latencies.mid(0, 20);
            std::sort(sorted.begin(), sorted.end());
            output({{"type", "result"}, {"samples", 23}, {"short_samples", 20},
                {"p50_ms", sorted.at(9)}, {"p95_ms", sorted.at(int(std::ceil(0.95 * sorted.size())) - 1)},
                {"cpu_ms", totalCpu / latencies.size()}, {"peak_working_set", double(peakWorkingSet)},
                {"peak_private_bytes", double(peakPrivate)}, {"init_ms", initMs},
                {"runtime", runtime}, {"fallback", fallback}}, threads);
            app.quit();
        });
    QTimer::singleShot(0, &backend, &VieNeuTtsBackend::warmUp);
    QTimer::singleShot(120000, &app, [&] { app.exit(1); });
    const int result = app.exec();
    backend.shutdown();
    CloseHandle(protocolOutput);
    return result;
}
