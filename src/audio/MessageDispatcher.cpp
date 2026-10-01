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
        backend_, &ITtsBackend::speak, Qt::AutoConnection));
    backendConnections_.push_back(connect(this, &MessageDispatcher::requestStop,
        backend_, &ITtsBackend::stop, Qt::AutoConnection));
    backendConnections_.push_back(connect(backend_, &ITtsBackend::speakingFinished, this, [this] {
        if (activeLocalBackend_) return;
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
    }, Qt::AutoConnection));
}

void MessageDispatcher::setLocalBackend(ITtsBackend* const backend)
{
    if (localBackend_ == backend) return;
    clear();
    for (const auto& connection : localBackendConnections_) QObject::disconnect(connection);
    localBackendConnections_.clear();
    localBackend_ = backend;
    if (!localBackend_) return;
    localBackendConnections_.push_back(connect(this, &MessageDispatcher::requestLocalSpeak,
        localBackend_, &ITtsBackend::speak, Qt::AutoConnection));
    localBackendConnections_.push_back(connect(this, &MessageDispatcher::requestLocalStop,
        localBackend_, &ITtsBackend::stop, Qt::AutoConnection));
    localBackendConnections_.push_back(connect(localBackend_, &ITtsBackend::speakingFinished,
        this, [this] {
            if (!activeLocalBackend_ || currentSpokenSequence_ == 0) return;
            currentSpokenSequence_ = 0;
            speaking_ = false;
            currentText_.clear();
            emit speakingChanged(false, {});
            QTimer::singleShot(250, this, [this] { playNext(); });
        }, Qt::AutoConnection));
}

void MessageDispatcher::stopActiveBackend()
{
    if (activeLocalBackend_) emit requestLocalStop();
    else emit requestStop();
}

void MessageDispatcher::enqueue(const QString& text, const EventPriority priority,
    const MessageSource source, const std::optional<EventType> eventType)
{
    const QString trimmed = text.trimmed();
    if (conversationSuppressed_ && priority == EventPriority::Conversation
        && source != MessageSource::LocalSystem) return;
    const bool local = source == MessageSource::ProximitySpotter
        || source == MessageSource::FuelAlerts || source == MessageSource::TyreAlerts
        || source == MessageSource::EngineAlerts || source == MessageSource::FlagAlerts
        || source == MessageSource::DamageAlerts
        || source == MessageSource::LocalSystem;
    ITtsBackend* const target = local ? localBackend_ : backend_;
    if (trimmed.isEmpty() || !target) return;
    if (local && !target->canSpeakCached(trimmed)) return;
    if (!local && !target->isAvailable()) return;

    const auto now = std::chrono::steady_clock::now();

    // Prevent immediate audio repetition of identical text
    if (source != MessageSource::ProximitySpotter && source != MessageSource::LocalSystem
        && trimmed == lastSpokenText_
        && lastSpokenTime_.time_since_epoch().count() != 0
        && (now - lastSpokenTime_ < std::chrono::milliseconds{3500})) {
        return;
    }

    if (source == MessageSource::LocalSystem
        && ((speaking_ && activeSource_ == source && currentText_ == trimmed)
            || std::any_of(queue_.begin(), queue_.end(), [&trimmed](const Message& message) {
                return message.source == MessageSource::LocalSystem && message.text == trimmed;
            }))) {
        return;
    }

    // Spotter-specific queue protections:
    if (source == MessageSource::ProximitySpotter) {
        queue_.erase(std::remove_if(queue_.begin(), queue_.end(), [this, eventType](const Message& queued) {
            const bool remove = queued.source == MessageSource::ProximitySpotter && queued.eventType == eventType;
            if (remove) discardMessage(queued);
            return remove;
        }), queue_.end());
        if (speaking_ && activeSource_ == MessageSource::ProximitySpotter
            && activeEventType_ == eventType) return;
    }

    if (source == MessageSource::LapSummary) {
        queue_.erase(std::remove_if(queue_.begin(), queue_.end(), [this](const Message& message) {
            const bool remove = message.source == MessageSource::LapSummary;
            if (remove) discardMessage(message);
            return remove;
        }), queue_.end());
    }

    const Message message{trimmed, priority, source, eventType, nextSequence_++, local};

    if (speaking_ && priority > activePriority_) {
        Message interrupted{currentText_, activePriority_, activeSource_, activeEventType_,
            activeMessageSequence_, activeLocalBackend_};
        auto* activeBackend = activeLocalBackend_ ? localBackend_ : backend_;
        interrupted.resumeToken = activeBackend->pauseSpeech();
        if (interrupted.resumeToken != 0) {
            const bool obsolete = (interrupted.source == MessageSource::ProximitySpotter
                    && !isCurrentSpotterMessage(interrupted))
                || (interrupted.source == MessageSource::LapSummary
                    && std::any_of(queue_.begin(), queue_.end(), [](const Message& queued) {
                        return queued.source == MessageSource::LapSummary;
                    }));
            if (obsolete) discardMessage(interrupted);
            else queue_.push_back(interrupted);
            currentSpokenSequence_ = 0;
            speaking_ = false;
            currentText_.clear();
            activeSource_ = MessageSource::General;
            activeEventType_.reset();
        }
        // If a backend cannot suspend, finish its current utterance before draining priorities.
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
    for (const auto& message : queue_) discardMessage(message);
    queue_.clear();
    const bool wasSpeaking = speaking_;
    currentSpokenSequence_ = 0;
    speaking_ = false;
    activeSource_ = MessageSource::General;
    activeEventType_.reset();
    currentText_.clear();
    stopActiveBackend();
    if (wasSpeaking) emit speakingChanged(false, {});
}

void MessageDispatcher::cancelBySource(const MessageSource source)
{
    queue_.erase(std::remove_if(queue_.begin(), queue_.end(), [this, source](const Message& message) {
        const bool remove = message.source == source;
        if (remove) discardMessage(message);
        return remove;
    }), queue_.end());
    if (!speaking_ || activeSource_ != source) return;
    currentSpokenSequence_ = 0;
    speaking_ = false;
    currentText_.clear();
    activeSource_ = MessageSource::General;
    activeEventType_.reset();
    stopActiveBackend();
    emit speakingChanged(false, {});
    QTimer::singleShot(250, this, [this] { playNext(); });
}

bool MessageDispatcher::isCurrentSpotterMessage(const Message& message) const noexcept
{
    if (message.eventType == EventType::CarLeft) return spotterLeftCurrent_;
    if (message.eventType == EventType::CarRight) return spotterRightCurrent_;
    return false;
}

void MessageDispatcher::setProximitySpotterState(const bool left, const bool right)
{
    spotterLeftCurrent_ = left;
    spotterRightCurrent_ = right;
    queue_.erase(std::remove_if(queue_.begin(), queue_.end(), [this](const Message& message) {
        const bool remove = message.source == MessageSource::ProximitySpotter && !isCurrentSpotterMessage(message);
        if (remove) discardMessage(message);
        return remove;
    }), queue_.end());

    if (speaking_ && activeSource_ == MessageSource::ProximitySpotter) {
        const Message active{currentText_, activePriority_, activeSource_, activeEventType_,
            0, activeLocalBackend_};
        if (!isCurrentSpotterMessage(active)) {
            currentSpokenSequence_ = 0;
            speaking_ = false;
            currentText_.clear();
            activeSource_ = MessageSource::General;
            activeEventType_.reset();
            stopActiveBackend();
            emit speakingChanged(false, {});
            QTimer::singleShot(250, this, [this] { playNext(); });
        }
    }
}

void MessageDispatcher::cancelByPrefix(const QString& prefix)
{
    queue_.erase(std::remove_if(queue_.begin(), queue_.end(), [this, &prefix](const Message& message) {
        const bool remove = message.text.startsWith(prefix);
        if (remove) discardMessage(message);
        return remove;
    }), queue_.end());
    if (speaking_ && currentText_.startsWith(prefix)) {
        currentSpokenSequence_ = 0;
        speaking_ = false;
        currentText_.clear();
        activeSource_ = MessageSource::General;
    activeEventType_.reset();
        stopActiveBackend();
        emit speakingChanged(false, {});
        QTimer::singleShot(250, this, [this] { playNext(); });
    }
}

void MessageDispatcher::setConversationSuppressed(const bool suppressed)
{
    conversationSuppressed_ = suppressed;
    if (suppressed) {
        queue_.erase(std::remove_if(queue_.begin(), queue_.end(), [this](const Message& message) {
            const bool remove = message.priority == EventPriority::Conversation && !message.localBackend;
            if (remove) discardMessage(message);
            return remove;
        }), queue_.end());
    }
}

void MessageDispatcher::discardMessage(const Message& message)
{
    auto* target = message.localBackend ? localBackend_ : backend_;
    if (target && message.resumeToken != 0) target->discardSpeech(message.resumeToken);
}

void MessageDispatcher::playNext()
{
    if (speaking_ || queue_.empty()) return;
    const Message message = queue_.front();
    queue_.erase(queue_.begin());
    speaking_ = true;
    currentSpokenSequence_ = nextSequence_++;
    activeMessageSequence_ = message.sequence;
    activePriority_ = message.priority;
    activeLocalBackend_ = message.localBackend;
    activeSource_ = message.source;
    activeEventType_ = message.eventType;
    currentText_ = message.text;
    lastSpokenText_ = message.text;
    lastSpokenTime_ = std::chrono::steady_clock::now();
    emit speakingChanged(true, message.text);
    if (message.resumeToken != 0) {
        auto* target = message.localBackend ? localBackend_ : backend_;
        if (!target || !target->resumeSpeech(message.resumeToken)) {
            currentSpokenSequence_ = 0;
            speaking_ = false;
            currentText_.clear();
            emit speakingChanged(false, {});
            QTimer::singleShot(0, this, [this] { playNext(); });
        }
    } else if (activeLocalBackend_) emit requestLocalSpeak(message.text);
    else emit requestSpeak(message.text);
}

} // namespace raceengineer
