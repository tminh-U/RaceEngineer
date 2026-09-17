#include "vad/VadProcessor.h"

#include <QString>

#include <algorithm>
#include <cstring>
#include <span>

namespace raceengineer {
namespace {
constexpr int sampleRate = 16000;
constexpr int bytesPerSample = 2;
constexpr int preRollMilliseconds = 320;
constexpr int minimumSpeechMilliseconds = 120;
constexpr int endSilenceMilliseconds = 650;
}

VadProcessor::VadProcessor(QObject* const parent)
    : QObject(parent)
    , tenVad_(256, 0.5F)
{
}

QString VadProcessor::backendDescription() const
{
    if (!tenVad_.isAvailable()) {
        return QStringLiteral("TEN VAD unavailable");
    }
    return QStringLiteral("TEN VAD %1").arg(QString::fromStdString(tenVad_.version()));
}

void VadProcessor::processPcm16k(const QByteArray& pcm)
{
    pending_.append(pcm);
    const int frameBytes = static_cast<int>(tenVad_.frameSamples() * bytesPerSample);
    const int frameMilliseconds = static_cast<int>(tenVad_.frameSamples() * 1000 / sampleRate);

    while (pending_.size() >= frameBytes) {
        const QByteArray frame = pending_.left(frameBytes);
        pending_.remove(0, frameBytes);

        if (pushToTalk_) {
            utterance_.append(frame);
            continue;
        }

        appendPreRoll(frame);
        if (!voiceActivationEnabled_ || !tenVad_.isAvailable()) {
            continue;
        }

        const auto* samples = reinterpret_cast<const std::int16_t*>(frame.constData());
        const auto decision = tenVad_.process({samples, tenVad_.frameSamples()});
        emit probabilityChanged(decision.probability);

        if (!speechActive_) {
            speechFrames_ = decision.speech ? speechFrames_ + 1 : 0;
            if (speechFrames_ * frameMilliseconds >= minimumSpeechMilliseconds) {
                speechActive_ = true;
                silenceFrames_ = 0;
                utterance_ = preRoll_;
                emit speechStarted();
            }
            continue;
        }

        utterance_.append(frame);
        silenceFrames_ = decision.speech ? 0 : silenceFrames_ + 1;
        if (silenceFrames_ * frameMilliseconds >= endSilenceMilliseconds) {
            finishUtterance();
        }
    }
}

void VadProcessor::beginPushToTalk()
{
    if (pushToTalk_) {
        return;
    }
    pushToTalk_ = true;
    speechActive_ = true;
    utterance_ = preRoll_;
    emit speechStarted();
}

void VadProcessor::endPushToTalk()
{
    if (!pushToTalk_) {
        return;
    }
    pushToTalk_ = false;
    finishUtterance();
}

void VadProcessor::reset()
{
    pending_.clear();
    preRoll_.clear();
    utterance_.clear();
    speechFrames_ = 0;
    silenceFrames_ = 0;
    pushToTalk_ = false;
    speechActive_ = false;
}

void VadProcessor::appendPreRoll(const QByteArray& frame)
{
    preRoll_.append(frame);
    constexpr int maximumBytes = sampleRate * bytesPerSample * preRollMilliseconds / 1000;
    if (preRoll_.size() > maximumBytes) {
        preRoll_.remove(0, preRoll_.size() - maximumBytes);
    }
}

void VadProcessor::finishUtterance()
{
    if (!speechActive_) {
        return;
    }
    speechActive_ = false;
    speechFrames_ = 0;
    silenceFrames_ = 0;
    emit speechEnded();
    if (utterance_.size() >= sampleRate * bytesPerSample / 5) {
        emit utteranceReady(utterance_);
    } else {
        emit utteranceRejected(QStringLiteral(
            "Không thu được đủ âm thanh. Hãy giữ nút PTT trong lúc nói và kiểm tra microphone."));
    }
    utterance_.clear();
}

} // namespace raceengineer
