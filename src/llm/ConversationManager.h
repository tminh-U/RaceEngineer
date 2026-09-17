#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace raceengineer {

class ConversationManager final {
public:
    explicit ConversationManager(int maximumMessages = 12);

    void addUserMessage(const QString& text);
    void addAssistantMessage(const QString& text);
    void addAssistantToolCallMessage(const QJsonObject& message);
    void addToolResult(const QString& toolCallId, const QString& name, const QJsonObject& result);
    [[nodiscard]] QJsonArray messages(const QString& systemPrompt) const;
    [[nodiscard]] int retainedMessageCount() const noexcept { return turns_.size(); }
    void clear();

private:
    void trim();

    QJsonArray turns_;
    int maximumMessages_;
};

} // namespace raceengineer
