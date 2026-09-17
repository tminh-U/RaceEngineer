#pragma once

#include <QByteArray>
#include <QObject>

namespace raceengineer {

class AudioCapture;
class VadProcessor;

class VoiceInputController final : public QObject {
    Q_OBJECT

public:
    explicit VoiceInputController(QObject* parent = nullptr);

public slots:
    void start();
    void stop();
    void beginPushToTalk();
    void endPushToTalk();
    void setVoiceActivationEnabled(bool enabled);

signals:
    void levelChanged(float level);
    void statusChanged(const QString& status);
    void microphoneChanged(const QString& description);
    void errorOccurred(const QString& error);
    void utteranceReady(const QByteArray& pcm16k);

private:
    AudioCapture* capture_;
    VadProcessor* vad_;
};

} // namespace raceengineer
