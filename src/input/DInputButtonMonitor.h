#pragma once

#include <QObject>
#include <QString>

#include <memory>

namespace raceengineer {

class DInputButtonMonitor final : public QObject {
    Q_OBJECT

public:
    struct Impl;

    explicit DInputButtonMonitor(QObject* parent = nullptr);
    ~DInputButtonMonitor() override;

public slots:
    void start(quintptr nativeWindowHandle);
    void stop();
    void configure(bool enabled, const QString& deviceGuid, int buttonIndex);
    void beginMapping();
    void cancelMapping();

signals:
    void buttonPressedChanged(bool pressed);
    void mappingCaptured(const QString& deviceName, const QString& deviceGuid, int buttonIndex);
    void statusChanged(const QString& status);

private:
    void scanDevices();
    void pollDevices();

    std::unique_ptr<Impl> impl_;
};

} // namespace raceengineer
