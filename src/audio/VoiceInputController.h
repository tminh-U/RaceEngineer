#pragma once

#include <QByteArray>
#include <QObject>

namespace raceengineer {

class AudioCapture;

class VoiceInputController final : public QObject {
    Q_OBJECT

public:
    explicit VoiceInputController(QObject* parent = nullptr);

public slots:
    void start(const QByteArray& deviceId = {});
    void setInputDevice(const QByteArray& deviceId);
    void stop();
    void beginPushToTalk();
    void endPushToTalk();

signals:
    void levelChanged(float level);
    void statusChanged(const QString& status);
    void microphoneChanged(const QString& description);
    void errorOccurred(const QString& error);
    void utteranceReady(const QByteArray& pcm16k);

private:
    void onPcm16k(const QByteArray& pcm);

    AudioCapture* capture_;
    QByteArray preRoll_;
    QByteArray recording_;
    QByteArray inputDeviceId_;
    bool started_{false};
    bool pushToTalk_{false};
};

} // namespace raceengineer
