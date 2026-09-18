#include "audio/MessageDispatcher.h"

#include "tts/ITtsBackend.h"

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
        speaking_ = false;
        emit speakingChanged(false, {});
        playNext();
    }, Qt::QueuedConnection));
}

void MessageDispatcher::enqueue(const QString& text, const EventPriority priority)
{
    if (text.trimmed().isEmpty() || !backend_ || !backend_->isAvailable()) return;
    const Message message{text.trimmed(), priority, nextSequence_++};
    if (speaking_ && static_cast<int>(priority) > static_cast<int>(activePriority_)) {
        emit requestStop();
        speaking_ = false;
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
    speaking_ = false;
    emit requestStop();
}

void MessageDispatcher::playNext()
{
    if (speaking_ || queue_.empty()) return;
    const Message message = queue_.front();
    queue_.erase(queue_.begin());
    speaking_ = true;
    activePriority_ = message.priority;
    emit speakingChanged(true, message.text);
    emit requestSpeak(message.text);
}

} // namespace raceengineer
