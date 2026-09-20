#pragma once

#include <QAudioDevice>
#include <QAudioFormat>
#include <QByteArray>
#include <QList>
#include <QObject>

#include <memory>
#include <vector>

class QAudioSource;
class QIODevice;

namespace raceengineer {

class AudioCapture final : public QObject {
    Q_OBJECT

public:
    struct InputDeviceInfo final {
        QByteArray id;
        QString description;
    };

    explicit AudioCapture(QObject* parent = nullptr);
    ~AudioCapture() override;

    [[nodiscard]] static QList<InputDeviceInfo> inputDevices();

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
    QByteArray convertToMono16k(const QByteArray& input, const QAudioFormat& format);

    std::unique_ptr<QAudioSource> source_;
    QIODevice* io_{nullptr};
    QAudioFormat activeFormat_;
    std::vector<float> monoBuffer_;
    QByteArray pcmBuffer_;
    qint64 conversionElapsedUs_{0};
    qint64 conversionCalls_{0};
    qint64 conversionInputBytes_{0};
    qint64 conversionOutputBytes_{0};
};

} // namespace raceengineer
