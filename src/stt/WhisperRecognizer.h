#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <atomic>

struct whisper_context;

namespace raceengineer {

class WhisperRecognizer final : public QObject {
    Q_OBJECT

public:
    explicit WhisperRecognizer(QString modelPath, QObject* parent = nullptr);
    ~WhisperRecognizer() override;

public slots:
    void warmUp();
    void transcribe(const QByteArray& pcm16k, const QString& language = QStringLiteral("auto"));
    void setModelPath(const QString& modelPath);
    void cancel() noexcept;

signals:
    void recognitionStarted();
    void transcriptionReady(const QString& text, const QString& detectedLanguage);
    void recognitionError(const QString& error);
    void recognitionFinished();

private:
    bool ensureModelLoaded();
    static QString normalizeRacingTerms(QString text);

    QString modelPath_;
    whisper_context* context_{nullptr};
    std::atomic_bool cancelRequested_{false};
};

} // namespace raceengineer
