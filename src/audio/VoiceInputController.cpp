#include "audio/VoiceInputController.h"

#include "audio/AudioCapture.h"
#include "vad/VadProcessor.h"

namespace raceengineer {

VoiceInputController::VoiceInputController(QObject* const parent)
    : QObject(parent)
    , capture_(new AudioCapture(this))
    , vad_(new VadProcessor(this))
{
    connect(capture_, &AudioCapture::pcm16kReady, vad_, &VadProcessor::processPcm16k);
    connect(capture_, &AudioCapture::levelChanged, this, &VoiceInputController::levelChanged);
    connect(capture_, &AudioCapture::deviceChanged, this, &VoiceInputController::microphoneChanged);
    connect(capture_, &AudioCapture::captureError, this, &VoiceInputController::errorOccurred);
    connect(vad_, &VadProcessor::speechStarted, this, [this] { emit statusChanged(QStringLiteral("Listening")); });
    connect(vad_, &VadProcessor::speechEnded, this, [this] { emit statusChanged(QStringLiteral("Recognizing")); });
    connect(vad_, &VadProcessor::utteranceRejected, this, [this](const QString& reason) {
        emit errorOccurred(reason);
        emit statusChanged(QStringLiteral("Idle"));
    });
    connect(vad_, &VadProcessor::utteranceReady, this, &VoiceInputController::utteranceReady);
}

void VoiceInputController::start()
{
    emit statusChanged(QStringLiteral("Idle"));
    capture_->start();
}

void VoiceInputController::stop()
{
    capture_->stop();
    vad_->reset();
}

void VoiceInputController::beginPushToTalk()
{
    vad_->beginPushToTalk();
}

void VoiceInputController::endPushToTalk()
{
    vad_->endPushToTalk();
}

void VoiceInputController::setVoiceActivationEnabled(const bool enabled)
{
    vad_->setVoiceActivationEnabled(enabled);
}

} // namespace raceengineer
