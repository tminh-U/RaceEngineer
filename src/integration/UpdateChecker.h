#pragma once

#include <QNetworkAccessManager>
#include <QJsonObject>
#include <functional>

namespace raceengineer {

class UpdateChecker final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool checking READ checking NOTIFY changed)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY changed)
public:
    explicit UpdateChecker(QObject* parent = nullptr);
    QString currentVersion() const;
    QString latestVersion() const { return latestVersion_; }
    QString status() const { return status_; }
    bool checking() const { return checking_; }
    bool updateAvailable() const { return updateAvailable_; }
    Q_INVOKABLE void check();
    Q_INVOKABLE void openRelease();
signals:
    void changed();
private:
    void fetch(const QString& endpoint, std::function<void(const QJsonObject&)> accept);
    void finish(const QString& status, bool available = false);
    QNetworkAccessManager network_;
    QString sourceCommit_;
    QString latestVersion_;
    QString releaseTag_;
    QString status_{QStringLiteral("Chưa kiểm tra cập nhật.")};
    bool checking_{false};
    bool updateAvailable_{false};
};

} // namespace raceengineer
