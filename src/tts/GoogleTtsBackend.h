#pragma once

#include "tts/ITtsBackend.h"

#include <QAudioDevice>
#include <QByteArray>
#include <QElapsedTimer>
#include <QStringList>
#include <QUrl>
#include <QJsonObject>
#include <QHash>
#include <functional>

class QAudioSink;
class QIODevice;
class QNetworkAccessManager;
class QNetworkReply;
class QTimer;
class QJsonObject;

namespace raceengineer {

// PCM samples may cross streaming chunk boundaries; retain at most one byte.
bool decodeGooglePcmChunk(const QByteArray& encoded, QByteArray& remainder, QByteArray& pcm);

class GoogleTtsBackend final : public ITtsBackend {
    Q_OBJECT

public:
    explicit GoogleTtsBackend(QObject* parent = nullptr);
    GoogleTtsBackend(QUrl interactionsEndpoint, QUrl voicesEndpoint, QObject* parent = nullptr);
    ~GoogleTtsBackend() override;

    [[nodiscard]] bool isAvailable() const override { return !apiKey_.trimmed().isEmpty(); }
    [[nodiscard]] QString backendName() const override { return QStringLiteral("Google Gemini TTS API"); }
    quint64 pauseSpeech() override;
    bool resumeSpeech(quint64 token) override;
    void discardSpeech(quint64 token) override;
    void setApiKey(QString apiKey);
    void setModel(QString model);
    void setVoice(QString voice, QString voiceId = {});
    [[nodiscard]] static QStringList builtInVoices();
    [[nodiscard]] static QString bonoVoiceDescription();

public slots:
    void warmUp() override;
    void speak(const QString& text) override;
    void stop() override;
    void setVolume(float volume) override;
    void setSpeed(float speed) override;
    void setAudioOutputDevice(const QString& description) override;
    void createReplicatedVoice(const QString& name, const QByteArray& sourceWav,
        const QByteArray& consentWav);
    void createPromptedVoice(const QString& name, const QString& description);

signals:
    void voiceCreated(const QString& name, const QString& voiceId);
    void voiceOperationFinished(bool success, const QString& message);
    void retryScheduled(int attempt, int delayMs);
    void firstAudioReceived(qint64 milliseconds);
    void connectionRecovered();

private slots:
    void readStream();
    void finishStream();
    void playbackTick();

private:
    void consumeEvents(bool finalChunk = false);
    bool consumeEvent(const QByteArray& eventData);
    bool appendPcm(const QByteArray& pcm);
    bool startPcmPlayback();
    void startSynthesisRequest(quint64 requestId);
    bool retrySynthesis(quint64 requestId, int status, int networkError,
        const QByteArray& retryAfter = {}, bool timedOut = false);
    void scheduleRecovery();
    void checkConnection();
    void postVoice(const QString& name, const QJsonObject& voice);
    void drainPcm();
    void finishRequest(quint64 requestId);
    void failRequest(quint64 requestId, const QString& message);
    void resetPlayback();

    struct SpeechState {
        QNetworkReply* reply_{nullptr};
        QTimer* timeout_{nullptr};
        QTimer* retryTimer_{nullptr};
        QTimer* playbackTimer_{nullptr};
        QAudioSink* audioSink_{nullptr};
        QIODevice* audioOutput_{nullptr};
        QByteArray eventBuffer_;
        QByteArray pendingPcm_;
        QByteArray pcmRemainder_;
        QJsonObject activeBody_;
        int retryCount_{0};
        QElapsedTimer requestTimer_;
        quint64 activeRequestId_{0};
        quint64 totalPcmBytes_{0};
        bool responseFinished_{false};
        bool started_{false};
        quint64 token{};
        bool paused{};
        bool wasPaused{};
        QString failure;
    } speech_;
    QHash<quint64, SpeechState> pausedSpeech_;
    void initializeSpeech(quint64 token);
    void withSpeech(quint64 token, const std::function<void()>& action);
    void deleteSpeechTimers();

    QString apiKey_;
    QString model_{QStringLiteral("gemini-3.8-flash-lite-tts")};
    QString voice_{QStringLiteral("Kore")};
    QString voiceId_;
    float volume_{0.85F};
    QAudioDevice audioDevice_;
    QUrl interactionsEndpoint_;
    QUrl voicesEndpoint_;
    QNetworkAccessManager* network_{nullptr};
    QNetworkReply* recoveryReply_{nullptr};
    QTimer* recoveryTimer_{nullptr};
    int recoveryAttempts_{0};
    quint64 nextRequestId_{1};
    bool voiceOperationActive_{false};
};

} // namespace raceengineer
