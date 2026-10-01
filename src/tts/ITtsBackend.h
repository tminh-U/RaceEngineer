#pragma once

#include <QObject>
#include <QString>

namespace raceengineer {

class ITtsBackend : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~ITtsBackend() override = default;

    [[nodiscard]] virtual bool isAvailable() const = 0;
    [[nodiscard]] virtual QString backendName() const = 0;
    [[nodiscard]] virtual bool canSpeakCached(const QString&) const { return false; }
    // Backends and dispatcher own GUI-thread playback; tokens retain unfinished audio.
    virtual quint64 pauseSpeech() { return 0; }
    virtual bool resumeSpeech(quint64) { return false; }
    virtual void discardSpeech(quint64) {}

public slots:
    virtual void warmUp() = 0;
    virtual void speak(const QString& text) = 0;
    virtual void stop() = 0;
    virtual void setVolume(float volume) = 0;
    virtual void setSpeed(float speed) = 0;
    virtual void setAudioOutputDevice(const QString& description) = 0;

signals:
    void warmUpFinished(bool success, const QString& error);
    void speakingStarted(const QString& text);
    void speakingFinished();
    void errorOccurred(const QString& message);
};

} // namespace raceengineer
