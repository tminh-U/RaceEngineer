#pragma once

#include <QAudioDevice>
#include <QAudioFormat>
#include <QByteArray>
#include <QObject>

#include <memory>

class QAudioSource;
class QIODevice;

namespace raceengineer {

class AudioCapture final : public QObject {
    Q_OBJECT

public:
    explicit AudioCapture(QObject* parent = nullptr);
    ~AudioCapture() override;

    [[nodiscard]] static QStringList availableInputDevices();

public slots:
    void start(const QByteArray& deviceId = {});
    void stop();

signals:
    void pcm16kReady(const QByteArray& pcm);
    void levelChanged(float level);
    void captureError(const QString& message);
    void deviceChanged(const QString& description);

private slots:
    void readAudio();

private:
    static QByteArray convertToMono16k(const QByteArray& input, const QAudioFormat& format);

    std::unique_ptr<QAudioSource> source_;
    QIODevice* io_{nullptr};
    QAudioFormat activeFormat_;
};

} // namespace raceengineer
