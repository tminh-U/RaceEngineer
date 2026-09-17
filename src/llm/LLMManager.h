#pragma once

#include "config/SettingsManager.h"
#include "llm/ConversationManager.h"
#include "llm/ILLMProvider.h"
#include "llm/tools/ToolRegistry.h"
#include "race/RaceHistory.h"

#include <QJsonObject>
#include <QObject>
#include <QVariantMap>

#include <memory>

namespace raceengineer {

class OpenAICompatibleProvider;

class LLMManager final : public QObject {
    Q_OBJECT

public:
    explicit LLMManager(const LlmSettings& settings, const QString& apiKey,
        QObject* parent = nullptr);

    void configure(const LlmSettings& settings, const QString& apiKey);
    void ask(const QString& text, const RaceState& state, const RaceHistory& history,
        const QString& responseLanguage = QStringLiteral("Vietnamese"));
    void testConnection();
    void resetConversation();
    [[nodiscard]] QVariantMap statistics() const;
    [[nodiscard]] QString providerName() const;
    [[nodiscard]] QString modelName() const { return settings_.model; }
    [[nodiscard]] static QString stripReasoning(QString response);
    [[nodiscard]] static QString applyAuthoritativePostValidation(const QString& response,
        const QJsonObject& fuelResult, const QString& responseLanguage);

signals:
    void stateChanged(const QString& state, const QString& detail);
    void responseChunk(const QString& text);
    void responseReady(const QString& text);
    void errorOccurred(const QString& message);
    void statisticsChanged();
    void toolCalled(const QString& name, const QJsonObject& result);
    void connectionTested(bool success, const QString& detail);

private slots:
    void handleResponse(const QJsonObject& response, qint64 latencyMilliseconds,
        qint64 firstTokenMilliseconds);
    void handleFailure(raceengineer::ApiState state, int httpStatus, const QString& message,
        qint64 latencyMilliseconds);

private:
    void sendCurrentRequest(bool includeTools);
    static QString stateName(ApiState state);
    static QString messageText(const QJsonValue& content);
    QString systemPrompt() const;

    LlmSettings settings_;
    std::unique_ptr<ILLMProvider> provider_;
    ConversationManager conversation_{8};
    ToolRegistry tools_;
    RaceState stateSnapshot_;
    RaceHistory historySnapshot_;
    QString responseLanguage_{QStringLiteral("Vietnamese")};
    int toolRounds_{0};
    int requests_{0};
    int failures_{0};
    int rateLimits_{0};
    qint64 inputTokens_{0};
    qint64 outputTokens_{0};
    qint64 totalLatency_{0};
    qint64 lastLatency_{0};
    qint64 firstTokenLatency_{-1};
    int lastHttpStatus_{0};
    QString lastTool_;
    QJsonObject authoritativeFuelResult_;
};

} // namespace raceengineer
