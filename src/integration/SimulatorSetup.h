#pragma once

#include <QObject>
#include <QVariantMap>

namespace raceengineer {

class SimulatorSetup final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap status READ status NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
public:
    explicit SimulatorSetup(QObject* parent = nullptr);
    QVariantMap status() const { return status_; }
    QString message() const { return message_; }
    bool busy() const { return busy_; }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void chooseAcFolder();
    Q_INVOKABLE bool installAc();
    Q_INVOKABLE bool configureAcc();
    bool configureDetectedGames();
    bool installAcFiles(const QString& path);
signals:
    void changed();
private:
    bool mayConfigure();
    QString companionPath() const;
    QString acPath_;
    QString accPath_;
    QString message_;
    QVariantMap status_;
    bool busy_{false};
};

} // namespace raceengineer
