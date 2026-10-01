#pragma once

#include "ai/LocalAiRuntime.h"
#include "tts/ITtsBackend.h"

#include <QAudioDevice>
#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

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
        int cpuThreads = 2,
        QObject* parent = nullptr);
    ~VieNeuTtsBackend() override;

    [[nodiscard]] bool isAvailable() const override;
    [[nodiscard]] bool canSpeakCached(const QString& text) const override;
    [[nodiscard]] static bool hasExternalThreadOverride();
    [[nodiscard]] QString backendName() const override
    {
        return QStringLiteral("VieNeu-TTS v3 Turbo · %1 (Native)").arg(voice_);
    }
    [[nodiscard]] QString voice() const { return voice_; }
    void setVoice(const QString& voice);
    quint64 pauseSpeech() override;
    bool resumeSpeech(quint64 token) override;
    void discardSpeech(quint64 token) override;

public slots:
    void warmUp() override;
    void speak(const QString& text) override;
    void requestBenchmarkSynthesis(const QString& text);
    void stop() override;
    void releaseModel();
    void shutdown();
    bool setCpuThreads(int cpuThreads);
    void setVolume(float volume) override;
    void setSpeed(float speed) override;
    void setAudioOutputDevice(const QString& description) override;

signals:
    void statusChanged(const QString& status);
    void runtimeBackendChanged(const QString& backend, const QString& fallbackReason);
    void modelReleased();
    void initializationFinished(bool success, const QString& backend, const QString& error);
    void synthesisMeasured(double elapsedMs, bool success, const QString& error);
    void requestSynthesis(quint64 generation, quint64 requestId, const QString& text);
    void requestBenchmarkSynthesisInternal(quint64 generation, quint64 requestId, const QString& text);

private slots:
    void onAudioReady(quint64 generation, quint64 requestId, const QString& text,
        const QByteArray& pcmData, int sampleRate, double elapsedMs);
    void onSynthesisFailed(quint64 generation, quint64 requestId, const QString& error);
    void onBenchmarkSynthesisMeasured(quint64 generation, quint64 requestId,
        double elapsedMs, bool success, const QString& error);
    void onWorkerInitialized(quint64 generation, bool success, const QString& error,
        const QString& backend, const QString& fallbackReason);
    void onWorkerContextReleased(quint64 generation);

private:
    struct CachedSpotterAudio final {
        QByteArray pcm;
        int sampleRate{0};
    };
    struct PausedSpeech {
        QAudioSink* sink{};
        QBuffer* buffer{};
        quint64 requestId{};
        QString text;
        QByteArray pcm;
        int sampleRate{};
        bool synthesizing{};
        QString deferred;
        QString failure;
    };
    QHash<quint64, PausedSpeech> pausedSpeech_;

    void loadCachedSpotter();
    bool playCachedSpotter(const QString& text);
    void startPlayback(const QString& text, const QByteArray& pcmData, int sampleRate);
    void stopPlayback();

    QString cachedSpotterDirectory_;
    QHash<QString, QVector<CachedSpotterAudio>> cachedSpotterAudio_;
    QHash<QString, int> lastCachedSpotterVariant_;

    QString modelDir_;
    LocalAiRuntimeSelection runtimeSelection_;
    int cpuThreads_{4};
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
    quint64 activeBenchmarkRequestId_{0};
    quint64 workerGeneration_{1};
    QElapsedTimer requestTimer_;
    QElapsedTimer benchmarkTimer_;
    bool ready_{false};
    bool initializing_{false};
    bool releasePending_{false};
    bool contextReleased_{true};
    bool warmUpRequestedAfterRelease_{false};
    QString deferredText_;
    bool synthesizing_{false};
    bool playbackActive_{false};
    bool shuttingDown_{false};
    QString currentPlayingText_;
};

} // namespace raceengineer
