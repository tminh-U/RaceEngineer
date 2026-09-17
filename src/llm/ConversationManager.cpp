#include "llm/ConversationManager.h"

#include <QJsonDocument>

#include <algorithm>

namespace raceengineer {

ConversationManager::ConversationManager(const int maximumMessages)
    : maximumMessages_(std::max(4, maximumMessages))
{
}

void ConversationManager::addUserMessage(const QString& text)
{
    turns_.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
        {QStringLiteral("content"), text}});
    trim();
}

void ConversationManager::addAssistantMessage(const QString& text)
{
    turns_.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
        {QStringLiteral("content"), text}});
    trim();
}

void ConversationManager::addAssistantToolCallMessage(const QJsonObject& message)
{
    turns_.append(message);
    trim();
}

void ConversationManager::addToolResult(const QString& toolCallId, const QString& name,
    const QJsonObject& result)
{
    turns_.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("tool")},
        {QStringLiteral("tool_call_id"), toolCallId}, {QStringLiteral("name"), name},
        {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact))}});
    trim();
}

QJsonArray ConversationManager::messages(const QString& systemPrompt) const
{
    QJsonArray result{QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
        {QStringLiteral("content"), systemPrompt}}};
    for (const auto& turn : turns_) {
        result.append(turn);
    }
    return result;
}

void ConversationManager::clear()
{
    turns_ = {};
}

void ConversationManager::trim()
{
    while (turns_.size() > maximumMessages_) {
        turns_.removeFirst();
        // A tool result must not remain without its assistant tool-call message.
        while (!turns_.isEmpty() && turns_.first().toObject().value(QStringLiteral("role"))
                                          .toString() == QStringLiteral("tool")) {
            turns_.removeFirst();
        }
    }
}

} // namespace raceengineer
