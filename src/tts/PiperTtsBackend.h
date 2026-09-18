#pragma once

#include "tts/ITtsBackend.h"

#include <QString>
#include <QHash>
#include <QStringList>

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
    [[nodiscard]] QString backendName() const override { return QStringLiteral("Piper · VAIS1000"); }
    [[nodiscard]] static QString normalizeBilingualText(QString text);

public slots:
    void speak(const QString& text) override;
    void stop() override;
    void setVolume(float volume) override;
    void setSpeed(float speed) override;
    void setAudioOutputDevice(const QString& description) override;

signals:
    void statusChanged(const QString& status);

private:
    void loadCachedSpotter();
    bool playCachedSpotter(const QString& text);
    void cleanTemporaryAudio();

    QString executablePath_;
    QString modelPath_;
    QString temporaryAudioPath_;
    QString pendingText_;
    QString cachedSpotterDirectory_;
    QHash<QString, QStringList> cachedSpotterFiles_;
    QHash<QString, int> lastCachedSpotterVariant_;
    QProcess* process_{nullptr};
    QMediaPlayer* player_{nullptr};
    QAudioOutput* output_{nullptr};
    float speed_{1.0F};
    bool stopping_{false};
    bool playbackActive_{false};
};

} // namespace raceengineer
