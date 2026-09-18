#pragma once

#include "tts/ITtsBackend.h"

#include <QPointer>
#include <QHash>
#include <QString>
#include <QStringList>

class QAudioOutput;
class QMediaPlayer;
class QNetworkAccessManager;
class QNetworkReply;
class QProcess;
class QTimer;

namespace raceengineer {

class GwenTtsBackend final : public ITtsBackend {
    Q_OBJECT

public:
    explicit GwenTtsBackend(QString executablePath, QString modelPath,
        QString codecPath, QString voiceDirectory, QObject* parent = nullptr);
    ~GwenTtsBackend() override;

    [[nodiscard]] bool isAvailable() const override;
    [[nodiscard]] QString backendName() const override
    {
        return QStringLiteral("Gwen-TTS · Khánh Toàn");
    }

public slots:
    void warmUp();
    void speak(const QString& text) override;
    void stop() override;
    void shutdown();
    void setVolume(float volume) override;
    void setSpeed(float speed) override;
    void setAudioOutputDevice(const QString& description) override;

signals:
    void statusChanged(const QString& status);

private:
    [[nodiscard]] bool dynamicTtsAvailable() const;
    bool playCachedSpotter(const QString& text);
    void loadCachedSpotter();
    void pollHealth();
    void requestSpeech();
    void cancelActiveRequest();
    void finishWithError(const QString& error);
    void cleanTemporaryAudio();
    void setServerReady(bool ready);

    QString executablePath_;
    QString modelPath_;
    QString codecPath_;
    QString voiceDirectory_;
    QString voicePath_;
    QString voiceTranscript_;
    QString temporaryAudioPath_;
    QString pendingText_;
    QString cachedSpotterDirectory_;
    QHash<QString, QStringList> cachedSpotterFiles_;
    QHash<QString, int> lastCachedSpotterVariant_;
    QProcess* server_{nullptr};
    QNetworkAccessManager* network_{nullptr};
    QPointer<QNetworkReply> healthReply_;
    QPointer<QNetworkReply> speechReply_;
    QTimer* healthTimer_{nullptr};
    QMediaPlayer* player_{nullptr};
    QAudioOutput* output_{nullptr};
    float speed_{1.0F};
    int healthAttempts_{0};
    bool serverReady_{false};
    bool playbackActive_{false};
    bool shuttingDown_{false};
};

} // namespace raceengineer
