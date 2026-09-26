#include "audio/MessageDispatcher.h"

#include "tts/ITtsBackend.h"

#include <QTimer>
#include <algorithm>

namespace raceengineer {

MessageDispatcher::MessageDispatcher(ITtsBackend* const backend, QObject* const parent)
    : QObject(parent)
{
    setBackend(backend);
}

void MessageDispatcher::setBackend(ITtsBackend* const backend)
{
    if (backend_ == backend) return;
    clear();
    for (const auto& connection : backendConnections_) QObject::disconnect(connection);
    backendConnections_.clear();
    backend_ = backend;
    if (!backend_) return;
    backendConnections_.push_back(connect(this, &MessageDispatcher::requestSpeak,
        backend_, &ITtsBackend::speak, Qt::QueuedConnection));
    backendConnections_.push_back(connect(this, &MessageDispatcher::requestStop,
        backend_, &ITtsBackend::stop, Qt::QueuedConnection));
    backendConnections_.push_back(connect(backend_, &ITtsBackend::speakingFinished, this, [this] {
        if (currentSpokenSequence_ == 0) {
            return; // Ignore stale finished signal from cancelled/preempted utterance
        }
        currentSpokenSequence_ = 0;
        speaking_ = false;
        currentText_.clear();
        emit speakingChanged(false, {});

        // 250ms cadence pause between messages to let audio hardware drain and prevent voice collision
        QTimer::singleShot(250, this, [this] {
            playNext();
        });
    }, Qt::QueuedConnection));
}

void MessageDispatcher::enqueue(const QString& text, const EventPriority priority)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || !backend_ || !backend_->isAvailable()) return;

    const auto now = std::chrono::steady_clock::now();

    // Prevent immediate audio repetition of identical text
    if (trimmed == lastSpokenText_ && lastSpokenTime_.time_since_epoch().count() != 0
        && (now - lastSpokenTime_ < std::chrono::milliseconds{3500})) {
        return;
    }

    // Spotter-specific queue protections:
    if (priority == EventPriority::Spotter) {
        // 1. If currently speaking a spotter call, do NOT queue another spotter alert.
        // Spotter callouts are instantaneous real-time alerts. Queuing them causes
        // back-to-back chatter ("Có xe bên trái. Có xe bên phải.") which is obsolete
        // and clashes with the active radio message.
        if (speaking_ && activePriority_ == EventPriority::Spotter) {
            return;
        }

        // 2. Drop duplicates or obsolete spotter alerts waiting in queue
        queue_.erase(std::remove_if(queue_.begin(), queue_.end(), [](const Message& m) {
            return m.priority == EventPriority::Spotter;
        }), queue_.end());
    }

    const Message message{trimmed, priority, nextSequence_++};

    // Preemption: if a higher-priority message arrives while speaking
    if (speaking_ && static_cast<int>(priority) > static_cast<int>(activePriority_)) {
        const Message interrupted{currentText_, activePriority_, nextSequence_++};

        // Clear lower-priority messages in queue so they don't play after this urgent alert
        queue_.erase(std::remove_if(queue_.begin(), queue_.end(), [priority](const Message& m) {
            return static_cast<int>(m.priority) < static_cast<int>(priority);
        }), queue_.end());

        // Put the high-priority message at front
        queue_.insert(queue_.begin(), message);
        // TTS backends can stop, but they cannot resume from the middle of an
        // utterance. Replay the interrupted lower-priority transmission after
        // the spotter/critical message instead of dropping the conversation.
        if (activePriority_ != EventPriority::Spotter && !interrupted.text.trimmed().isEmpty()) {
            queue_.insert(queue_.begin() + 1, interrupted);
        }
        currentSpokenSequence_ = 0; // Invalidate any in-flight speakingFinished from aborted utterance
        speaking_ = false;
        currentText_.clear();
        emit speakingChanged(false, {});
        emit requestStop();

        // Brief delay (80ms) to allow audio sink to stop before starting urgent message
        QTimer::singleShot(80, this, [this] {
            playNext();
        });
        return;
    }

    queue_.push_back(message);
    std::stable_sort(queue_.begin(), queue_.end(), [](const Message& left, const Message& right) {
        if (left.priority != right.priority) return left.priority > right.priority;
        return left.sequence < right.sequence;
    });
    playNext();
}

void MessageDispatcher::clear()
{
    queue_.clear();
    const bool wasSpeaking = speaking_;
    currentSpokenSequence_ = 0;
    speaking_ = false;
    currentText_.clear();
    emit requestStop();
    if (wasSpeaking) emit speakingChanged(false, {});
}

void MessageDispatcher::cancelByPrefix(const QString& prefix)
{
    queue_.erase(std::remove_if(queue_.begin(), queue_.end(), [&prefix](const Message& message) {
        return message.text.startsWith(prefix);
    }), queue_.end());
    if (speaking_ && currentText_.startsWith(prefix)) {
        currentSpokenSequence_ = 0;
        speaking_ = false;
        currentText_.clear();
        emit requestStop();
        emit speakingChanged(false, {});
        QTimer::singleShot(250, this, [this] { playNext(); });
    }
}

void MessageDispatcher::playNext()
{
    if (speaking_ || queue_.empty()) return;
    const Message message = queue_.front();
    queue_.erase(queue_.begin());
    speaking_ = true;
    currentSpokenSequence_ = nextSequence_++;
    activePriority_ = message.priority;
    currentText_ = message.text;
    lastSpokenText_ = message.text;
    lastSpokenTime_ = std::chrono::steady_clock::now();
    emit speakingChanged(true, message.text);
    emit requestSpeak(message.text);
}

} // namespace raceengineer
