// Accelerated AC/ACC-shaped replay through the real native facade and optional QML.
// AI and simulator I/O are stubbed by this target. This is not a game/FPS benchmark.
#include "app/Application.h"
#include "telemetry/TelemetryManager.h"
#include "config/SettingsManager.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaMethod>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <QDir>
#include <algorithm>
#include <ctime>
#include <iostream>
#include <memory>
#include <type_traits>
#include <vector>
#ifdef __linux__
#include <sys/resource.h>
#endif

using namespace raceengineer;
namespace {
template<class T> struct SignalArgument;
template<class C, class A> struct SignalArgument<void(C::*)(const A&)> { using Type = A; };
using Frame = SignalArgument<decltype(&TelemetryManager::stateUpdated)>::Type;
template<class T> T snapshot(const RaceState& state)
{
    if constexpr (std::is_same_v<T, RaceState>) return state;
    else return std::make_shared<const RaceState>(state);
}
const RaceState& stateOf(const RaceState& state) { return state; }
const RaceState& stateOf(const std::shared_ptr<const RaceState>& state) { return *state; }
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
WheelContactPoints wheels(double x, double z)
{
    return {{{x - .9, 0, z + 1.4}, {x + .9, 0, z + 1.4},
             {x - .9, 0, z - 1.4}, {x + .9, 0, z - 1.4}}};
}
RaceState fixture(bool acc, int cars)
{
    RaceState s;
    s.simulator = acc ? Simulator::AssettoCorsaCompetizione : Simulator::AssettoCorsa;
    s.connected = true;
    s.track = "Monza";
    s.carModel = acc ? "ferrari_296_gt3" : "ks_ferrari_488_gt3";
    s.carCategory = "gt"; s.carSubclass = "gt3"; s.driverName = "Synthetic replay driver";
    s.sessionType = SessionType::Race; s.currentLap = 3; s.totalLaps = 40; s.lapsRemaining = 38;
    s.position = 2; s.speedKmh = 220.; s.rpm = 7300; s.gear = 5;
    s.throttle = .8; s.brake = .1; s.currentLapTimeSeconds = 45.;
    s.previousLapTimeSeconds = 90.; s.bestLapTimeSeconds = 89.; s.currentDeltaSeconds = .2;
    s.fuelLiters = 80.; s.fuelCapacityLiters = 100.; s.flag = FlagState::Green; s.pitState = PitState::Track;
    s.tyreTemperaturesCelsius = WheelValues{80, 82, 83, 81};
    s.tyrePressuresPsi = WheelValues{26.8, 26.9, 27., 27.1};
    s.brakeTemperaturesCelsius = WheelValues{430, 420, 380, 390};
    s.gapAheadSeconds = .8; s.gapBehindSeconds = 1.2;
    s.opponentAhead = "Synthetic opponent ahead"; s.opponentBehind = "Synthetic opponent behind";
    for (int i = 0; i < cars; ++i) {
        OpponentState car;
        car.carId = i; car.position = i + 1;
        car.driverName = "Synthetic opponent driver " + std::to_string(i);
        car.teamName = "Synthetic endurance racing team " + std::to_string(i);
        car.speedKmh = 215.; car.splinePosition = .5; car.currentLapTimeSeconds = 46.;
        car.recentLapTimesSeconds = {90., 91., 89.};
        car.worldPosition = WorldPoint{20. + i * 3., 0, 30. + i * 8.};
        car.spotterWheelContactPoints = wheels((*car.worldPosition)[0], (*car.worldPosition)[2]);
        s.opponents.push_back(car);
    }
    if (!acc) {
        s.spotterGeometryFresh = true; s.spotterWorldPosition = WorldPoint{0, 0, 0};
        s.spotterWheelContactPoints = wheels(0, 0); s.spotterOpponents = s.opponents;
    }
    return s;
}
double percentile(std::vector<double> values, double quantile)
{
    std::sort(values.begin(), values.end());
    return values[std::min(values.size() - 1, static_cast<std::size_t>(quantile * (values.size() - 1)))];
}
}

int main(int argc, char** argv)
{
    try {
        QApplication qt(argc, argv);
        QQuickStyle::setStyle("Basic");
        QCommandLineParser cli;
        cli.addHelpOption();
        cli.addOption({"sim", "Synthetic fixture: ac or acc", "sim", "ac"});
        cli.addOption({"samples", "Accelerated telemetry samples", "count", "12000"});
        cli.addOption({"opponents", "Opponent cars per sample", "count", "48"});
        cli.addOption({"label", "Report label (does not change behavior)", "label", "unlabelled"});
        cli.addOption({"output", "JSON output file", "path"});
        cli.addOption({"qml", "Load the actual current Cockpit and publish once per three input samples"});
        cli.process(qt);
        bool ok = false;
        const int count = cli.value("samples").toInt(&ok);
        require(ok && count >= 300 && count <= 120000, "samples must be 300..120000");
        const int cars = cli.value("opponents").toInt(&ok);
        require(ok && cars >= 0 && cars <= 100, "opponents must be 0..100");
        require(cli.value("sim") == "ac" || cli.value("sim") == "acc", "sim must be ac or acc");
        const bool acc = cli.value("sim") == "acc", qml = cli.isSet("qml");
        QTemporaryDir config;
        require(config.isValid(), "temporary configuration directory unavailable");
        qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
        qt.setApplicationName("RuntimeProfile-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
        qt.setOrganizationName("RaceEngineerTests");
#ifdef Q_OS_WIN
        QStandardPaths::setTestModeEnabled(true);
#endif
        SettingsManager settings;
        auto llm = settings.llm(); llm.baseUrl.clear(); settings.setLlm(llm);
        auto tts = settings.tts(); tts.audioDucking = false; settings.setTts(tts);
        settings.setStrategyRecordingEnabled(false); settings.setLowResourceMode(true);
        settings.setSetupState(true, 0);
        Application app(false);
        QEventLoop warm;
        QTimer::singleShot(150, &warm, &QEventLoop::quit); warm.exec();
        std::unique_ptr<QQmlApplicationEngine> engine;
        if (qml) {
            engine = std::make_unique<QQmlApplicationEngine>();
            engine->rootContext()->setContextProperty("backend", &app);
            engine->load(QUrl::fromLocalFile(QStringLiteral(RACEENGINEER_SOURCE_DIR "/qml/Main.qml")));
            require(engine->rootObjects().size() == 1, "current Main QML failed to create its native root");
            auto* window = qobject_cast<QQuickWindow*>(engine->rootObjects().first());
            require(window != nullptr, "Main is not a native Quick window");
            window->resize(1280, 800);
            auto* notice = window->findChild<QObject*>("startupNotice");
            require(notice && notice->setProperty("dismissed", true), "cannot dismiss the expected missing-model notice");
            QCoreApplication::processEvents();
            require(window->findChild<QObject*>("cockpit") != nullptr && !app.setupVisible(),
                "runtime replay must exercise Cockpit, not the setup wizard");
        }
        app.setUiActive(false);
        QMetaObject::invokeMethod(&app, "onConnectionStatusChanged", Qt::DirectConnection,
            Q_ARG(QString, acc ? QStringLiteral("Assetto Corsa Competizione") : QStringLiteral("Assetto Corsa")),
            Q_ARG(bool, true));
        auto sample = fixture(acc, cars);
        const auto epoch = std::chrono::steady_clock::now();
        QMetaMethod slot;
        for (int i = 0; i < app.metaObject()->methodCount(); ++i) {
            const auto m = app.metaObject()->method(i);
            if (m.name() == "onStateUpdated") slot = m;
        }
        require(slot.isValid() && slot.parameterCount() == 1, "native state slot not found");
        const auto argumentName = slot.parameterTypes().first();
        QThread producer;
        auto* feed = new TelemetryManager;
        feed->moveToThread(&producer);
        QObject::connect(&producer, &QThread::finished, feed, &QObject::deleteLater);
        std::vector<double> durations; durations.reserve(count);
        int received = 0, publications = 0;
        QObject::connect(&app, &Application::telemetryChanged, &app, [&] { ++publications; });
        QObject::connect(feed, &TelemetryManager::stateUpdated, &app, [&](const Frame& frame) {
            QElapsedTimer timer; timer.start();
            require(slot.invoke(&app, Qt::DirectConnection, QGenericArgument(argumentName.constData(), &frame)),
                "native state delivery failed");
            if (qml && received % 3 == 0) { app.setUiActive(true); app.setUiActive(false); }
            require(stateOf(frame).connected, "fixture mutated while queued");
            durations.push_back(timer.nsecsElapsed() / 1000.);
            ++received;
        }, Qt::QueuedConnection);
        producer.start();
        const auto cpu = std::clock();
        QElapsedTimer clock; clock.start();
        for (int begin = 0; begin < count; begin += 64) {
            QEventLoop drain;
            const int end = std::min(begin + 64, count);
            QMetaObject::invokeMethod(feed, [&, begin, end] {
                for (int i = begin; i < end; ++i) {
                    sample.capturedAt = epoch + std::chrono::milliseconds(33LL * i);
                    sample.sessionTimeSeconds = 300. + i / 30.;
                    sample.speedKmh = 200. + (i % 100);
                    emit feed->stateUpdated(snapshot<Frame>(sample));
                }
                QMetaObject::invokeMethod(&app, [&] { drain.quit(); }, Qt::QueuedConnection);
            }, Qt::QueuedConnection);
            QTimer::singleShot(30000, &drain, &QEventLoop::quit);
            drain.exec();
            require(received == end, "queued replay did not drain completely");
        }
        const double elapsed = clock.nsecsElapsed() / 1e6;
        const double cpuMs = 1000. * (std::clock() - cpu) / CLOCKS_PER_SEC;
        producer.quit(); producer.wait();
        app.setUiActive(true);
        require(app.telemetry().value("speedKmh").toDouble() == 200. + (count - 1) % 100,
            "final snapshot is stale");
        QJsonObject result{{"schema", 1}, {"kind", "synthetic_native_replay_not_game"},
            {"harness_revision", 2}, {"viewport", qml ? QStringLiteral("1280x800 Cockpit; setup closed; optional AI notice dismissed") : QStringLiteral("none")},
            {"label", cli.value("label")}, {"sim_fixture", cli.value("sim")}, {"qml", qml},
            {"samples", received}, {"opponents", cars}, {"ui_publications", publications},
            {"wall_ms", elapsed}, {"process_cpu_ms", cpuMs},
            {"callback_p50_us", percentile(durations, .5)}, {"callback_p95_us", percentile(durations, .95)},
            {"ai_runtime", "stubbed"}, {"simulator_io", "stubbed"},
            {"shared_snapshot", !std::is_same_v<Frame, RaceState>}};
#ifdef __linux__
        rusage usage{}; getrusage(RUSAGE_SELF, &usage);
        result.insert("peak_rss_bytes", static_cast<double>(usage.ru_maxrss) * 1024.);
#endif
        const auto json = QJsonDocument(result).toJson(QJsonDocument::Indented);
        if (cli.isSet("output")) {
            QFile output(cli.value("output"));
            require(output.open(QIODevice::WriteOnly), "cannot create replay output");
            require(output.write(json) == json.size(), "cannot write complete replay report");
        }
        std::cout << json.constData();
        engine.reset();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
