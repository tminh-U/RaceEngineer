#pragma once

#include "race/RaceHistory.h"

#include <QThreadPool>
#include <QFile>
#include <QString>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QProcess>
#include <atomic>

namespace raceengineer {

class StrategyRecorder final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool simulatorConnected READ simulatorConnected NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString storagePath READ storagePath CONSTANT)
    Q_PROPERTY(qulonglong savedRecords READ savedRecords NOTIFY changed)
    Q_PROPERTY(bool sharingEnabled READ sharingEnabled WRITE setSharingEnabled NOTIFY changed)
    Q_PROPERTY(bool realisticSetupConfirmed READ realisticSetupConfirmed WRITE setRealisticSetupConfirmed NOTIFY changed)
    Q_PROPERTY(bool raceActive READ raceActive NOTIFY changed)
    Q_PROPERTY(QString sharingEndpoint READ sharingEndpoint NOTIFY changed)
    Q_PROPERTY(bool sharingConfigured READ sharingConfigured NOTIFY changed)
    Q_PROPERTY(QString sharingStatus READ sharingStatus NOTIFY changed)
public:
    StrategyRecorder();
    ~StrategyRecorder();
    void record(const QString& sessionId, const RaceState& state, const LapRecord& lap, bool lapExcluded = false);
    void recordPitEvent(const QString& sessionId, const RaceState& state, bool entering);
    void recordPitBoxEvent(const QString& sessionId, const RaceState& state, bool entering);
    bool enabled() const { return enabled_; }
    bool busy() const { return busy_; }
    bool simulatorConnected() const { return simulatorConnected_; }
    QString status() const { return status_; }
    QString storagePath() const;
    qulonglong savedRecords() const { return savedRecords_; }
    bool sharingEnabled() const { return sharingEnabled_; }
    bool realisticSetupConfirmed() const { return realisticSetupConfirmed_; }
    bool raceActive() const { return raceActive_; }
    QString sharingEndpoint() const { return sharingEndpoint_; }
    bool sharingConfigured() const { return !sharingEndpoint_.isEmpty() && !shareToken_.isEmpty(); }
    QString sharingStatus() const { return sharingStatus_; }
    void setEnabled(bool enabled);
    void setSharingEnabled(bool enabled);
    void setRealisticSetupConfirmed(bool confirmed);
    void setSharingConfig(const QString& endpoint, const QString& token);
    void finishSession(const QString& sessionId);
    void setSimulatorConnected(bool connected);
    void setRaceActive(bool racing);
    Q_INVOKABLE void processData();
    Q_INVOKABLE void trainModel();
    Q_INVOKABLE void trainTyreModels();
    Q_INVOKABLE void cancelJob();
    Q_INVOKABLE void openStorage();
    Q_INVOKABLE void retrySharing();

signals:
    void changed();
    void enabledChanged(bool enabled);
    void sharingEnabledChanged(bool enabled);

private:
    void startJob(const QString& action);
    void launchJob(const QString& action);
    void readJobOutput();
    void reportWrite(const QString& error);
    void recordPitEvent(const QString& sessionId, const RaceState& state, const QString& recordType);
    void enqueue(const QJsonObject& row);
    void sendNextShare();
    void clearPendingShares();
    QThreadPool pool_;
    std::atomic<int> pending_{0};
    QProcess process_;
    QNetworkAccessManager network_;
    QNetworkReply* uploadReply_{nullptr};
    QFile* uploadFile_{nullptr};
    QByteArray output_;
    QString errorOutput_;
    QString status_{QStringLiteral("Tự lưu vòng đua và sự kiện pit khi AC / ACC đang đua.")};
    QString sharingStatus_{QStringLiteral("Chia sẻ dữ liệu đang tắt.")};
    QString sharingEndpoint_;
    QString shareToken_;
    bool enabled_{true};
    bool busy_{false};
    bool cancelled_{false};
    bool simulatorConnected_{false};
    bool raceActive_{false};
    bool realisticSetupConfirmed_{false};
    bool sessionRealisticSetupConfirmed_{false};
    bool sharingEnabled_{false};
    qulonglong savedRecords_{0};
    quint64 jobRevision_{0};
    std::atomic<quint64> sharingRevision_{0};
};

} // namespace raceengineer
