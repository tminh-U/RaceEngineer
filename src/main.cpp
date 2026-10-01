#include "app/Application.h"
#include "config/CredentialStore.h"
#include "utils/Logging.h"

#include <QCommandLineParser>
#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSystemTrayIcon>
#include <QTextStream>
#include <QTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QDir>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>

BOOL WINAPI consoleHandler(DWORD signal)
{
    if (signal != CTRL_C_EVENT && signal != CTRL_BREAK_EVENT && signal != CTRL_CLOSE_EVENT) return FALSE;
    QGuiApplication::quit();
    return TRUE;
}
#endif

int main(int argc, char* argv[])
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    SetConsoleCtrlHandler(consoleHandler, TRUE);
#endif
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QGuiApplication::setApplicationName(QStringLiteral("RaceEngineer"));
    QGuiApplication::setOrganizationName(QStringLiteral("RaceEngineer"));
    QGuiApplication::setApplicationVersion(QStringLiteral(RACEENGINEER_VERSION));
    qSetMessagePattern(QStringLiteral("[%{time hh:mm:ss.zzz}] [%{category}] %{message}"));
    QApplication qtApplication(argc, argv);
    QIcon appIcon(QStringLiteral(":/qt/qml/RaceEngineer/assets/final_icon.ico"));
    if (appIcon.isNull()) {
        appIcon = QIcon(QStringLiteral(":/qt/qml/RaceEngineer/assets/final_icon_64.png"));
    }
    if (appIcon.isNull()) {
        appIcon = QIcon(QStringLiteral(":/RaceEngineer/assets/final_icon.ico"));
    }
    if (appIcon.isNull()) {
        appIcon = QIcon(QStringLiteral(":/RaceEngineer/assets/final_icon_64.png"));
    }
    if (appIcon.isNull()) {
        appIcon = QIcon(QStringLiteral("assets/final_icon.ico"));
    }
    if (appIcon.isNull()) {
        appIcon = QIcon(QStringLiteral("assets/final_icon_64.png"));
    }
    if (!appIcon.isNull()) {
        qtApplication.setWindowIcon(appIcon);
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("RaceEngineer - native AC/ACC companion"));
    const QStringList commandLineArguments = QCoreApplication::arguments();
    if (commandLineArguments.contains(QStringLiteral("--version"))
        || commandLineArguments.contains(QStringLiteral("-v"))) {
        QTextStream(stdout) << QGuiApplication::applicationName() << ' '
                            << QGuiApplication::applicationVersion() << '\n';
        return 0;
    }
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption mockOption(QStringLiteral("mock"), QStringLiteral("Bật mock telemetry để kiểm thử."));
    const QCommandLineOption mapOption(QStringLiteral("map-button"), QStringLiteral("Gán nút PTT DirectInput."));
    const QCommandLineOption setKeyOption(QStringLiteral("set-key"), QStringLiteral("Lưu API key vào Windows Credential Manager."), QStringLiteral("api_key"));
    const QCommandLineOption testLlmOption(QStringLiteral("test-llm"), QStringLiteral("Kiểm tra kết nối LLM rồi thoát."));
    const QCommandLineOption benchmarkOption(QStringLiteral("benchmark-tts"),
        QStringLiteral("Measure local TTS at 2/3/4 threads, apply the result, and exit."));
    const QCommandLineOption verifySetupOption(QStringLiteral("verify-setup-ui"),
        QStringLiteral("Render setup steps at minimum window size with isolated test settings."));
    parser.addOptions({mockOption, mapOption, setKeyOption, testLlmOption, benchmarkOption, verifySetupOption});
    parser.process(qtApplication);
    if (parser.isSet(verifySetupOption)) {
        QStandardPaths::setTestModeEnabled(true);
        qtApplication.setApplicationName(QStringLiteral("RaceEngineer-SetupUiTests"));
    }

    if (parser.isSet(setKeyOption)) {
        if (raceengineer::CredentialStore::writeApiKey(parser.value(setKeyOption).trimmed())) {
            std::cout << "[Config] Đã lưu API key.\n";
            return 0;
        }
        std::cerr << "[Lỗi] Không thể lưu API key.\n";
        return 1;
    }

    raceengineer::Application application(parser.isSet(mockOption));
    if (parser.isSet(benchmarkOption)) {
        QTimer polling;
        polling.setInterval(100);
        bool started = false;
        bool complete = false;
        QObject::connect(&polling, &QTimer::timeout, &qtApplication, [&] {
            if (!started) {
                if (!application.startupReady() || application.voiceStatus() != QStringLiteral("Idle")) return;
                if (!application.ttsBenchmarkAllowed()) {
                    QTextStream(stderr) << application.ttsBenchmarkUnavailableReason() << '\n';
                    qtApplication.exit(1); return;
                }
                started = true;
                application.startTtsBenchmark();
            } else if (!application.ttsBenchmarkRunning() && !complete) {
                complete = true;
                QTextStream(stdout) << QJsonDocument(QJsonObject{
                    {"results", QJsonArray::fromVariantList(application.ttsBenchmarkResults())},
                    {"selected_threads", application.ttsCpuThreads()},
                    {"status", application.ttsBenchmarkStatus()}}).toJson(QJsonDocument::Compact) << '\n';
            } else if (complete && !application.ttsBenchmarkRestoring()) {
                QTextStream(stdout) << QJsonDocument(QJsonObject{{"backend_restored", application.ttsAvailable()}})
                    .toJson(QJsonDocument::Compact) << '\n';
                qtApplication.exit(application.ttsAvailable()
                    && application.ttsBenchmarkStatus().startsWith(QStringLiteral("Đã áp dụng")) ? 0 : 1);
            }
        });
        polling.start();
        QTimer::singleShot(400000, &qtApplication, [&] { application.cancelTtsBenchmark(); qtApplication.exit(1); });
        return qtApplication.exec();
    }
    if (parser.isSet(testLlmOption)) {
        QObject::connect(&application, &raceengineer::Application::apiStateChanged, [&] {
            std::cout << application.apiState().toStdString() << " - " << application.apiDetail().toStdString() << '\n';
            qtApplication.quit();
        });
        application.testApiConnection();
        return qtApplication.exec();
    }
    if (parser.isSet(mapOption)) {
        application.startDirectInput(0);
        QObject::connect(&application, &raceengineer::Application::pttSettingsChanged, [&] {
            std::cout << application.directInputBinding().toStdString() << '\n';
            qtApplication.quit();
        });
        application.beginDirectInputMapping();
        return qtApplication.exec();
    }

    if (!application.gpuRendererEnabled() || parser.isSet(verifySetupOption)) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    }

    QQmlApplicationEngine engine;
    bool qmlWarnings = false;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, [&qmlWarnings](const QList<QQmlError>& warnings) {
        qmlWarnings = true;
        for (const auto& w : warnings) {
            std::cerr << "[QML Warning] " << w.toString().toStdString() << "\n";
        }
    });
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &application);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &qtApplication,
        [&application](QObject* object, const QUrl&) {
            if (const auto window = qobject_cast<QQuickWindow*>(object)) {
                application.startDirectInput(window->winId());
            }
        }, Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("RaceEngineer"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        std::cerr << "[QML Error] rootObjects is empty! Failed to load RaceEngineer/Main\n";
        return 1;
    }

    auto* const window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
#ifdef _WIN32
    if (window && QGuiApplication::platformName() == QStringLiteral("windows")) {
        const auto handle = reinterpret_cast<HWND>(window->winId());
        const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
        const HRESULT result = DwmSetWindowAttribute(handle, DWMWA_WINDOW_CORNER_PREFERENCE,
                                                     &corners, sizeof(corners));
        if (SUCCEEDED(result)) qInfo("Window rounded corners enabled.");
    }
#endif
    if (parser.isSet(verifySetupOption)) {
        if (!window) return 1;
        window->resize(880, 576);
        application.setTtsBackend(QStringLiteral("VieNeu-TTS"));
        application.openSetup();
        QDir().mkpath(QStringLiteral("build/setup-ui-check"));
        QTimer capture;
        capture.setInterval(600);
        int step = 0;
        bool failed = qmlWarnings;
        QObject::connect(&engine, &QQmlApplicationEngine::warnings, &qtApplication,
            [&failed](const QList<QQmlError>&) { failed = true; });
        QObject::connect(&capture, &QTimer::timeout, &qtApplication, [&] {
            const auto image = window->grabWindow();
            if (image.isNull() || !image.save(QStringLiteral("build/setup-ui-check/step-%1.png").arg(step))) failed = true;
            if (++step == 4) {
                application.setTtsBackend(QStringLiteral("Google Gemini API"));
                application.setSetupStep(1);
            } else if (step == 5) {
                application.setSetupStep(3);
            } else if (step == 6) {
                application.finishSetup();
                application.openSetup();
                if (!application.setupVisible() || application.setupStep() != 0) failed = true;
                if (!QMetaObject::invokeMethod(window, "openGoogleVoiceDialog")) failed = true;
            } else if (step == 7) {
                auto* dialog = window->property("googleVoiceDialog").value<QObject*>();
                if (!dialog || !dialog->property("visible").toBool()) failed = true;
                else {
                    const auto left = dialog->property("x").toDouble();
                    const auto top = dialog->property("y").toDouble();
                    const auto width = dialog->property("width").toDouble();
                    const auto height = dialog->property("height").toDouble();
                    if (qAbs(left + width / 2 - window->width() / 2.0) > 1
                        || qAbs(top + height / 2 - window->height() / 2.0) > 1
                        || left < 0 || top < 0 || height > window->height()) failed = true;
                }
                QTextStream(stdout) << "Setup UI 880x576: " << (failed ? "FAILED" : "passed") << '\n';
                qtApplication.exit(failed ? 1 : 0);
            } else application.setSetupStep(step);
        });
        capture.start();
        return qtApplication.exec();
    }
    QMenu trayMenu;
    QSystemTrayIcon trayIcon(appIcon);
    const auto showWindow = [window] {
        if (!window) return;
        window->showNormal();
        window->raise();
        window->requestActivate();
    };
    QObject::connect(trayMenu.addAction(QStringLiteral("Mở RaceEngineer")), &QAction::triggered,
                     &qtApplication, showWindow);
    QObject::connect(trayMenu.addAction(QStringLiteral("Thoát")), &QAction::triggered,
                     &qtApplication, &QCoreApplication::quit);
    trayIcon.setContextMenu(&trayMenu);
    QObject::connect(&trayIcon, &QSystemTrayIcon::activated, &qtApplication,
                     [showWindow](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) showWindow();
    });
    const auto updateTrayIcon = [&] {
        trayIcon.setVisible(application.trayAvailable()
            && (application.minimizeToTray() || application.minimizeOnClose()));
    };
    QObject::connect(&application, &raceengineer::Application::generalSettingsChanged,
                     &qtApplication, updateTrayIcon);
    updateTrayIcon();
    return qtApplication.exec();
}
