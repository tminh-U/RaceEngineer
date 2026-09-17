#pragma once

#include "tts/ITtsBackend.h"

#include <QString>

class QAudioOutput;
class QMediaPlayer;
class QProcess;

namespace raceengineer {

class PiperTtsBackend final : public ITtsBackend {
    Q_OBJECT

public:
    explicit PiperTtsBackend(QString executablePath, QString modelPath,
        QObject* parent = nullptr);
    [[nodiscard]] bool isAvailable() const override;
    [[nodiscard]] QString backendName() const override { return QStringLiteral("Piper"); }

public slots:
    void speak(const QString& text) override;
    void stop() override;
    void setVolume(float volume) override;
    void setSpeed(float speed) override;

private:
    void cleanTemporaryAudio();

    QString executablePath_;
    QString modelPath_;
    QString temporaryAudioPath_;
    QString pendingText_;
    QProcess* process_{nullptr};
    QMediaPlayer* player_{nullptr};
    QAudioOutput* output_{nullptr};
    float speed_{1.0F};
    bool stopping_{false};
};

} // namespace raceengineer
