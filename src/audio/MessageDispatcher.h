#pragma once

#include "events/EventEngine.h"

#include <QObject>
#include <QMetaObject>
#include <QString>
#include <chrono>
#include <optional>
#include <vector>

namespace raceengineer {

class ITtsBackend;

enum class MessageSource { General, ProximitySpotter, LapSummary, FuelAlerts, TyreAlerts,
    LapDelta, EngineAlerts, FlagAlerts, DamageAlerts, LocalSystem };

class MessageDispatcher final : public QObject {
    Q_OBJECT

public:
    explicit MessageDispatcher(ITtsBackend* backend, QObject* parent = nullptr);
    void setBackend(ITtsBackend* backend);
    void setLocalBackend(ITtsBackend* backend);
    void enqueue(const QString& text, EventPriority priority,
        MessageSource source = MessageSource::General,
        std::optional<EventType> eventType = std::nullopt);
    void setProximitySpotterState(bool left, bool right);
    void clear();
    void setConversationSuppressed(bool suppressed);
    void cancelByPrefix(const QString& prefix);
    void cancelBySource(MessageSource source);

signals:
    void requestSpeak(const QString& text);
    void requestStop();
    void requestLocalSpeak(const QString& text);
    void requestLocalStop();
    void speakingChanged(bool speaking, const QString& text);

private:
    struct Message {
        QString text;
        EventPriority priority;
        MessageSource source;
        std::optional<EventType> eventType;
        quint64 sequence;
        bool localBackend{false};
        quint64 resumeToken{0};
    };
    [[nodiscard]] bool isCurrentSpotterMessage(const Message& message) const noexcept;
    void stopActiveBackend();
    void playNext();
    void discardMessage(const Message& message);

    ITtsBackend* backend_{nullptr};
    ITtsBackend* localBackend_{nullptr};
    std::vector<QMetaObject::Connection> backendConnections_;
    std::vector<QMetaObject::Connection> localBackendConnections_;
    std::vector<Message> queue_;
    quint64 nextSequence_{0};
    quint64 currentSpokenSequence_{0};
    quint64 activeMessageSequence_{0};
    bool speaking_{false};
    bool conversationSuppressed_{false};
    bool activeLocalBackend_{false};
    EventPriority activePriority_{EventPriority::Conversation};
    MessageSource activeSource_{MessageSource::General};
    std::optional<EventType> activeEventType_;
    bool spotterLeftCurrent_{false};
    bool spotterRightCurrent_{false};
    QString currentText_{};
    QString lastSpokenText_{};
    std::chrono::steady_clock::time_point lastSpokenTime_{};
};

} // namespace raceengineer
