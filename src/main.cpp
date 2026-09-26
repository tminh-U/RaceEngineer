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
#include <iostream>

#ifdef _WIN32
#include <windows.h>

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
    QGuiApplication::setApplicationVersion(QStringLiteral("1.0.3"));
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
    parser.addOptions({mockOption, mapOption, setKeyOption, testLlmOption});
    parser.process(qtApplication);

    if (parser.isSet(setKeyOption)) {
        if (raceengineer::CredentialStore::writeApiKey(parser.value(setKeyOption).trimmed())) {
            std::cout << "[Config] Đã lưu API key.\n";
            return 0;
        }
        std::cerr << "[Lỗi] Không thể lưu API key.\n";
        return 1;
    }

    raceengineer::Application application(parser.isSet(mockOption));
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

    if (!application.gpuRendererEnabled()) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    }

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, [](const QList<QQmlError>& warnings) {
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
