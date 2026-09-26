#include "strategy/StrategyRecorder.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QUuid>
#include <iostream>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    const QString testName = QStringLiteral("RaceEngineerRecorderTest-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QCoreApplication::setApplicationName(testName);
    using namespace raceengineer;
    QString directory;
    RaceState state;
    state.connected = true;
    state.simulator = Simulator::AssettoCorsa;
    state.currentLap = 3;
    state.carModel = "test_gt3";
    state.carCategory = "gt";
    state.track = "test_track";
    state.pitState = PitState::Track;
    LapRecord lap;
    lap.lapNumber = 2;
    lap.lapTimeSeconds = 90.0;
    {
        StrategyRecorder recorder;
        directory = recorder.storagePath();
        recorder.setEnabled(false);
        recorder.record("session", state, lap);
        recorder.setEnabled(true);
        recorder.record("session", state, lap, true);
        recorder.recordPitEvent("session", state, true);
        recorder.setSimulatorConnected(true);
        recorder.processData();
        if (recorder.busy()) return 1;
    } // Drain pending writes before inspecting disk.
    QFile log(QDir(directory).filePath("pit_strategy_laps.jsonl"));
    if (!log.open(QIODevice::ReadOnly)) return 2;
    const auto first = QJsonDocument::fromJson(log.readLine()).object();
    const auto second = QJsonDocument::fromJson(log.readLine()).object();
    if (!first.value("lap_excluded").toBool() || first.value("car_category").toString() != "gt"
        || second.value("record_type").toString() != "pit_enter" || !log.atEnd()) return 3;
    log.close();
    // An archive rotation must preserve the old file and the historical .old file.
    if (!log.open(QIODevice::ReadWrite) || !log.resize(20 * 1024 * 1024)) return 4;
    log.close();
    QFile legacy(log.fileName() + ".old");
    if (!legacy.open(QIODevice::WriteOnly) || legacy.write("legacy") != 6) return 5;
    legacy.close();
    {
        StrategyRecorder recorder;
        recorder.recordPitEvent("session", state, false);
    }
    const QDir archives(QDir(directory).filePath("recordings"));
    const auto files = archives.entryList({"pit_strategy_laps-*.jsonl"}, QDir::Files);
    if (files.size() != 1 || QFileInfo(archives.filePath(files.first())).size() != 20 * 1024 * 1024)
        return 6;
    if (!legacy.open(QIODevice::ReadOnly) || legacy.readAll() != "legacy") return 7;
    legacy.close();
    if (!log.open(QIODevice::ReadOnly)
        || QJsonDocument::fromJson(log.readLine()).object().value("record_type").toString() != "pit_exit") return 8;
    log.close();
    // Only remove the unique test application's resolved directory.
    const QString resolved = QFileInfo(directory).canonicalFilePath();
    if (resolved.isEmpty() || QFileInfo(resolved).fileName() != testName) return 9;
    if (!QDir(resolved).removeRecursively()) return 10;
    std::cout << "Recorder persistence, disabled recording and archive retention passed.\n";
    return 0;
}
