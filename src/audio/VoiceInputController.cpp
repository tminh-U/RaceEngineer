#include "audio/VoiceInputController.h"

#include "audio/AudioCapture.h"

#include <utility>

namespace raceengineer {
namespace {

constexpr qsizetype kPreRollBytes = 16'000 * 2 * 80 / 1000;

} // namespace

VoiceInputController::VoiceInputController(QObject* const parent)
    : QObject(parent)
    , capture_(new AudioCapture(this))
{
    connect(capture_, &AudioCapture::pcm16kReady, this, &VoiceInputController::onPcm16k);
    connect(capture_, &AudioCapture::levelChanged, this, &VoiceInputController::levelChanged);
    connect(capture_, &AudioCapture::deviceChanged, this, &VoiceInputController::microphoneChanged);
    connect(capture_, &AudioCapture::captureError, this, &VoiceInputController::errorOccurred);
}

void VoiceInputController::start(const QByteArray& deviceId)
{
    inputDeviceId_ = deviceId;
    started_ = true;
    emit statusChanged(QStringLiteral("Idle"));
    capture_->start(inputDeviceId_);
}

void VoiceInputController::setInputDevice(const QByteArray& deviceId)
{
    inputDeviceId_ = deviceId;
    if (!started_) {
        return;
    }
    pushToTalk_ = false;
    preRoll_.clear();
    recording_.clear();
    capture_->stop();
    capture_->start(inputDeviceId_);
    emit statusChanged(QStringLiteral("Idle"));
}

void VoiceInputController::stop()
{
    started_ = false;
    capture_->stop();
    preRoll_.clear();
    recording_.clear();
    pushToTalk_ = false;
}

void VoiceInputController::beginPushToTalk()
{
    if (pushToTalk_) {
        return;
    }
    pushToTalk_ = true;
    recording_ = preRoll_;
    emit statusChanged(QStringLiteral("Listening"));
}

void VoiceInputController::endPushToTalk()
{
    if (!pushToTalk_) {
        return;
    }
    pushToTalk_ = false;
    const QByteArray utterance = std::move(recording_);
    recording_.clear();
    if (utterance.size() <= kPreRollBytes) {
        emit statusChanged(QStringLiteral("Idle"));
        return;
    }
    emit statusChanged(QStringLiteral("Recognizing"));
    emit utteranceReady(utterance);
}

void VoiceInputController::onPcm16k(const QByteArray& pcm)
{
    if (pushToTalk_) {
        recording_.append(pcm);
        return;
    }
    preRoll_.append(pcm);
    if (preRoll_.size() > kPreRollBytes) {
        preRoll_.remove(0, preRoll_.size() - kPreRollBytes);
    }
}

} // namespace raceengineer
