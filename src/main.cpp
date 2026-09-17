#include "app/Application.h"

#include <QCommandLineParser>
#include <QApplication>
#include <QAction>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QSystemTrayIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>

#include <cstdlib>

int main(int argc, char* argv[])
{
    QApplication::setApplicationName(QStringLiteral("Race Engineer"));
    QApplication::setOrganizationName(QStringLiteral("RaceEngineer"));
    QApplication::setApplicationVersion(QStringLiteral("0.9.0"));
    qSetMessagePattern(QStringLiteral("[%{time yyyy-MM-dd hh:mm:ss.zzz}] [%{category}] %{message}"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QApplication qtApplication(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Low-latency AC/ACC race engineer"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption mockOption(QStringLiteral("mock"),
        QStringLiteral("Start with mock telemetry (Debug builds only)."));
    parser.addOption(mockOption);
    parser.process(qtApplication);

    raceengineer::Application application(parser.isSet(mockOption));
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("app"), &application);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
        &qtApplication, [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("RaceEngineer"), QStringLiteral("Main"));
    if (!engine.rootObjects().isEmpty()) {
        if (auto* const window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst())) {
            application.startDirectInput(static_cast<quintptr>(window->winId()));
        }
    }

    QMenu trayMenu;
    QAction showAction(QStringLiteral("Show Race Engineer"), &trayMenu);
    QAction quitAction(QStringLiteral("Exit"), &trayMenu);
    trayMenu.addAction(&showAction);
    trayMenu.addSeparator();
    trayMenu.addAction(&quitAction);
    QPixmap trayPixmap(32, 32);
    trayPixmap.fill(Qt::transparent);
    QPainter painter(&trayPixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(QColor(QStringLiteral("#55e09c")));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(1, 1, 30, 30, 8, 8);
    painter.setPen(QColor(QStringLiteral("#07110d")));
    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(13);
    painter.setFont(font);
    painter.drawText(trayPixmap.rect(), Qt::AlignCenter, QStringLiteral("RE"));
    painter.end();
    QSystemTrayIcon tray{QIcon(trayPixmap)};
    if (QSystemTrayIcon::isSystemTrayAvailable() && !engine.rootObjects().isEmpty()) {
        QObject* const window = engine.rootObjects().constFirst();
        tray.setToolTip(QStringLiteral("AI Race Engineer"));
        tray.setContextMenu(&trayMenu);
        QObject::connect(&showAction, &QAction::triggered, window, [window] {
            window->setProperty("visible", true);
            QMetaObject::invokeMethod(window, "raise");
            QMetaObject::invokeMethod(window, "requestActivate");
        });
        QObject::connect(&quitAction, &QAction::triggered, &qtApplication, &QCoreApplication::quit);
        QObject::connect(&tray, &QSystemTrayIcon::activated, window,
            [window](const QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
                    window->setProperty("visible", true);
                    QMetaObject::invokeMethod(window, "raise");
                    QMetaObject::invokeMethod(window, "requestActivate");
                }
            });
        qtApplication.setQuitOnLastWindowClosed(false);
        tray.show();
    }

    return qtApplication.exec();
}
