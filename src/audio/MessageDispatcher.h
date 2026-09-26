#pragma once

#include "events/EventEngine.h"

#include <QObject>
#include <QMetaObject>
#include <QString>
#include <chrono>
#include <vector>

namespace raceengineer {

class ITtsBackend;

class MessageDispatcher final : public QObject {
    Q_OBJECT

public:
    explicit MessageDispatcher(ITtsBackend* backend, QObject* parent = nullptr);
    void setBackend(ITtsBackend* backend);
    void enqueue(const QString& text, EventPriority priority);
    void clear();
    void cancelByPrefix(const QString& prefix);

signals:
    void requestSpeak(const QString& text);
    void requestStop();
    void speakingChanged(bool speaking, const QString& text);

private:
    struct Message { QString text; EventPriority priority; quint64 sequence; };
    void playNext();

    ITtsBackend* backend_{nullptr};
    std::vector<QMetaObject::Connection> backendConnections_;
    std::vector<Message> queue_;
    quint64 nextSequence_{0};
    quint64 currentSpokenSequence_{0};
    bool speaking_{false};
    EventPriority activePriority_{EventPriority::Conversation};
    QString currentText_{};
    QString lastSpokenText_{};
    std::chrono::steady_clock::time_point lastSpokenTime_{};
};

} // namespace raceengineer
