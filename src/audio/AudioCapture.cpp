#include "audio/AudioCapture.h"

#include "utils/Logging.h"

#include <QAudioSource>
#include <QIODevice>
#include <QMediaDevices>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace raceengineer {

AudioCapture::AudioCapture(QObject* const parent)
    : QObject(parent)
{
}

AudioCapture::~AudioCapture()
{
    stop();
}

QStringList AudioCapture::availableInputDevices()
{
    QStringList result;
    for (const auto& device : QMediaDevices::audioInputs()) {
        result.push_back(device.description());
    }
    return result;
}

void AudioCapture::start(const QByteArray& deviceId)
{
    stop();
    QAudioDevice selected = QMediaDevices::defaultAudioInput();
    for (const auto& device : QMediaDevices::audioInputs()) {
        if (!deviceId.isEmpty() && device.id() == deviceId) {
            selected = device;
            break;
        }
    }
    if (selected.isNull()) {
        emit captureError(QStringLiteral("Không tìm thấy microphone."));
        return;
    }

    QAudioFormat desired;
    desired.setSampleRate(16000);
    desired.setChannelCount(1);
    desired.setSampleFormat(QAudioFormat::Int16);
    activeFormat_ = selected.isFormatSupported(desired) ? desired : selected.preferredFormat();

    source_ = std::make_unique<QAudioSource>(selected, activeFormat_);
    source_->setBufferSize(activeFormat_.bytesForDuration(200000));
    connect(source_.get(), &QAudioSource::stateChanged, this, [this](const QAudio::State state) {
        if (state == QAudio::StoppedState && source_ && source_->error() != QAudio::NoError) {
            emit levelChanged(0.0F);
            emit captureError(QStringLiteral(
                "Microphone đã dừng do lỗi thiết bị hoặc quyền truy cập (mã %1).")
                    .arg(static_cast<int>(source_->error())));
        }
    });
    io_ = source_->start();
    if (io_ == nullptr) {
        emit captureError(QStringLiteral("Không thể bắt đầu thu âm từ microphone."));
        source_.reset();
        return;
    }
    connect(io_, &QIODevice::readyRead, this, &AudioCapture::readAudio);
    emit deviceChanged(selected.description());
    qCInfo(logAudio) << "Microphone started:" << selected.description()
                     << activeFormat_.sampleRate() << "Hz" << activeFormat_.channelCount() << "channels";
}

void AudioCapture::stop()
{
    if (source_) {
        source_->stop();
    }
    io_ = nullptr;
    source_.reset();
}

void AudioCapture::readAudio()
{
    if (io_ == nullptr) {
        return;
    }
    const QByteArray input = io_->readAll();
    const QByteArray pcm = convertToMono16k(input, activeFormat_);
    if (pcm.isEmpty()) {
        return;
    }

    const auto* samples = reinterpret_cast<const std::int16_t*>(pcm.constData());
    const auto count = static_cast<std::size_t>(pcm.size() / 2);
    double sum = 0.0;
    for (std::size_t index = 0; index < count; ++index) {
        const double normalized = samples[index] / 32768.0;
        sum += normalized * normalized;
    }
    emit levelChanged(static_cast<float>(std::min(1.0, std::sqrt(sum / std::max<std::size_t>(1, count)) * 4.0)));
    emit pcm16kReady(pcm);
}

QByteArray AudioCapture::convertToMono16k(const QByteArray& input, const QAudioFormat& format)
{
    if (input.isEmpty() || format.channelCount() <= 0 || format.sampleRate() <= 0) {
        return {};
    }
    const int bytesPerSample = format.bytesPerSample();
    const int frameBytes = bytesPerSample * format.channelCount();
    if (bytesPerSample <= 0 || frameBytes <= 0) {
        return {};
    }
    const int frameCount = input.size() / frameBytes;
    if (frameCount <= 0) {
        return {};
    }

    std::vector<float> mono(static_cast<std::size_t>(frameCount));
    const char* data = input.constData();
    for (int frame = 0; frame < frameCount; ++frame) {
        double value = 0.0;
        for (int channel = 0; channel < format.channelCount(); ++channel) {
            const char* sample = data + frame * frameBytes + channel * bytesPerSample;
            switch (format.sampleFormat()) {
            case QAudioFormat::UInt8:
                value += (static_cast<unsigned char>(*sample) - 128.0) / 128.0;
                break;
            case QAudioFormat::Int16: {
                std::int16_t sampleValue{};
                std::memcpy(&sampleValue, sample, sizeof(sampleValue));
                value += sampleValue / 32768.0;
                break;
            }
            case QAudioFormat::Int32: {
                std::int32_t sampleValue{};
                std::memcpy(&sampleValue, sample, sizeof(sampleValue));
                value += sampleValue / 2147483648.0;
                break;
            }
            case QAudioFormat::Float: {
                float sampleValue{};
                std::memcpy(&sampleValue, sample, sizeof(sampleValue));
                value += sampleValue;
                break;
            }
            case QAudioFormat::Unknown: return {};
            }
        }
        mono[static_cast<std::size_t>(frame)] = static_cast<float>(value / format.channelCount());
    }

    const int outputFrames = static_cast<int>(static_cast<long long>(frameCount) * 16000 / format.sampleRate());
    QByteArray result(outputFrames * 2, Qt::Uninitialized);
    auto* output = reinterpret_cast<std::int16_t*>(result.data());
    for (int index = 0; index < outputFrames; ++index) {
        double value = 0.0;
        if (format.sampleRate() > 16000) {
            const double begin = static_cast<double>(index) * format.sampleRate() / 16000.0;
            const double end = static_cast<double>(index + 1) * format.sampleRate() / 16000.0;
            const int first = std::max(0, static_cast<int>(std::floor(begin)));
            const int last = std::min(frameCount, static_cast<int>(std::ceil(end)));
            double weightSum = 0.0;
            for (int source = first; source < last; ++source) {
                const double weight = std::max(0.0,
                    std::min(end, static_cast<double>(source + 1))
                        - std::max(begin, static_cast<double>(source)));
                value += mono[static_cast<std::size_t>(source)] * weight;
                weightSum += weight;
            }
            if (weightSum > 0.0) value /= weightSum;
        } else {
            const double position = static_cast<double>(index) * format.sampleRate() / 16000.0;
            const int left = std::min(static_cast<int>(position), frameCount - 1);
            const int right = std::min(left + 1, frameCount - 1);
            const double fraction = position - left;
            value = mono[static_cast<std::size_t>(left)] * (1.0 - fraction)
                + mono[static_cast<std::size_t>(right)] * fraction;
        }
        output[index] = static_cast<std::int16_t>(std::clamp(value, -1.0, 1.0) * 32767.0);
    }
    return result;
}

} // namespace raceengineer
