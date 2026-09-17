#pragma once

#include "vad/TenVadProcessor.h"

#include <QByteArray>
#include <QObject>

namespace raceengineer {

class VadProcessor final : public QObject {
    Q_OBJECT

public:
    explicit VadProcessor(QObject* parent = nullptr);
    [[nodiscard]] bool isAvailable() const noexcept { return tenVad_.isAvailable(); }
    [[nodiscard]] QString backendDescription() const;

public slots:
    void processPcm16k(const QByteArray& pcm);
    void setVoiceActivationEnabled(bool enabled) noexcept { voiceActivationEnabled_ = enabled; }
    void beginPushToTalk();
    void endPushToTalk();
    void reset();

signals:
    void speechStarted();
    void speechEnded();
    void utteranceRejected(const QString& reason);
    void probabilityChanged(float probability);
    void utteranceReady(const QByteArray& pcm16k);

private:
    void appendPreRoll(const QByteArray& frame);
    void finishUtterance();

    TenVadProcessor tenVad_;
    QByteArray pending_;
    QByteArray preRoll_;
    QByteArray utterance_;
    int speechFrames_{0};
    int silenceFrames_{0};
    bool voiceActivationEnabled_{true};
    bool pushToTalk_{false};
    bool speechActive_{false};
};

} // namespace raceengineer
