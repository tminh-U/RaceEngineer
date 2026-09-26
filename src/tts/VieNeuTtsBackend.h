#pragma once

#include "ai/LocalAiRuntime.h"
#include "tts/ITtsBackend.h"

#include <QAudioDevice>
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

class QAudioSink;
class QBuffer;
class QThread;

namespace raceengineer {

class VieNeuWorker;

class VieNeuTtsBackend final : public ITtsBackend {
    Q_OBJECT

public:
    explicit VieNeuTtsBackend(QString modelDir,
        QString voice,
        LocalAiRuntimeSelection runtimeSelection,
        QObject* parent = nullptr);
    ~VieNeuTtsBackend() override;

    [[nodiscard]] bool isAvailable() const override;
    [[nodiscard]] QString backendName() const override
    {
        return QStringLiteral("VieNeu-TTS v3 Turbo · %1 (Native)").arg(voice_);
    }
    [[nodiscard]] QString voice() const { return voice_; }
    void setVoice(const QString& voice);

public slots:
    void warmUp() override;
    void speak(const QString& text) override;
    void stop() override;
    void shutdown();
    void setVolume(float volume) override;
    void setSpeed(float speed) override;
    void setAudioOutputDevice(const QString& description) override;

signals:
    void statusChanged(const QString& status);
    void runtimeBackendChanged(const QString& backend, const QString& fallbackReason);
    void requestSynthesis(quint64 requestId, const QString& text);

private slots:
    void onAudioReady(quint64 requestId, const QString& text, const QByteArray& pcmData, int sampleRate, double elapsedMs);
    void onSynthesisFailed(quint64 requestId, const QString& error);
    void onWorkerInitialized(bool success, const QString& error,
        const QString& backend, const QString& fallbackReason);

private:
    void loadCachedSpotter();
    bool playCachedSpotter(const QString& text);
    void startPlayback(const QString& text, const QByteArray& pcmData, int sampleRate);
    void stopPlayback();

    QString cachedSpotterDirectory_;
    QHash<QString, QStringList> cachedSpotterFiles_;
    QHash<QString, int> lastCachedSpotterVariant_;

    QString modelDir_;
    LocalAiRuntimeSelection runtimeSelection_;
    QString voice_{QStringLiteral("Minh Đức")};
    float volume_{0.85F};
    float speed_{1.0F};
    QAudioDevice audioDevice_;

    QThread* workerThread_{nullptr};
    VieNeuWorker* worker_{nullptr};

    QAudioSink* audioSink_{nullptr};
    QBuffer* audioBuffer_{nullptr};

    quint64 nextRequestId_{1};
    quint64 activeRequestId_{0};
    bool ready_{false};
    bool initializing_{false};
    QString deferredText_;
    bool synthesizing_{false};
    bool playbackActive_{false};
    bool shuttingDown_{false};
    QString currentPlayingText_;
};

} // namespace raceengineer
