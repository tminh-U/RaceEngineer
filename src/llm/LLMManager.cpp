#include "llm/LLMManager.h"

#include "llm/providers/OpenAICompatibleProvider.h"
#include "utils/Logging.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>

namespace raceengineer {
namespace {

bool isGemmaModel(const QString& model)
{
    return model.contains(QStringLiteral("gemma"), Qt::CaseInsensitive);
}

bool isGeminiModel(QString model)
{
    model = model.trimmed();
    if (model.startsWith(QStringLiteral("models/"), Qt::CaseInsensitive)) model.remove(0, 7);
    return model.startsWith(QStringLiteral("gemini-"), Qt::CaseInsensitive);
}

bool isGemini25Model(QString model)
{
    model = model.trimmed();
    if (model.startsWith(QStringLiteral("models/"), Qt::CaseInsensitive)) model.remove(0, 7);
    return model.startsWith(QStringLiteral("gemini-2.5-"), Qt::CaseInsensitive);
}

bool canDisableGeminiReasoning(const QString& model)
{
    return isGemini25Model(model)
        && !model.contains(QStringLiteral("pro"), Qt::CaseInsensitive);
}

bool isLocalThinkingModel(const QString& model)
{
    return isGemmaModel(model)
        || model.contains(QStringLiteral("qwen"), Qt::CaseInsensitive)
        || model.compare(QStringLiteral("race-engineer"), Qt::CaseInsensitive) == 0;
}

bool isGoogleGeminiApi(const QString& baseUrl)
{
    const QString host = QUrl(baseUrl).host().toLower();
    return host == QStringLiteral("generativelanguage.googleapis.com")
        || host.endsWith(QStringLiteral(".generativelanguage.googleapis.com"));
}

} // namespace

LLMManager::LLMManager(const LlmSettings& settings, const QString& apiKey, QObject* const parent)
    : QObject(parent)
{
    configure(settings, apiKey);
}

void LLMManager::configure(const LlmSettings& settings, const QString& apiKey)
{
    settings_ = settings;
    ProviderConfiguration configuration;
    configuration.name = settings.provider;
    configuration.baseUrl = QUrl(settings.baseUrl);
    configuration.apiKey = apiKey;
    configuration.model = settings.model;
    configuration.streaming = settings.streaming;
    configuration.timeoutMilliseconds = settings.timeoutMilliseconds;

    provider_ = std::make_unique<OpenAICompatibleProvider>(configuration);
    connect(provider_.get(), &ILLMProvider::responseReceived, this, &LLMManager::handleResponse);
    connect(provider_.get(), &ILLMProvider::requestFailed, this, &LLMManager::handleFailure);
    connect(provider_.get(), &ILLMProvider::responseChunk, this, [this](const QString& chunk) {
        // Gemma can put its reasoning channel inside message.content. Buffer it so the
        // UI never flashes (and accessibility never reads) private reasoning tokens.
        if (authoritativeFuelResult_.isEmpty() && !isGemmaModel(settings_.model)) {
            emit responseChunk(chunk);
        }
    });
    connect(provider_.get(), &ILLMProvider::connectionTested,
        this, &LLMManager::connectionTested);
    connect(provider_.get(), &ILLMProvider::stateChanged, this,
        [this](const ApiState state, const QString& detail) { emit stateChanged(stateName(state), detail); });
    const bool configured = configuration.baseUrl.isValid() && !configuration.model.isEmpty();
    emit stateChanged(QStringLiteral("Unavailable"), configured
            ? QStringLiteral("Configured; connection not tested yet")
            : QStringLiteral("Base URL and model are required"));
}

void LLMManager::ask(const QString& text, const RaceState& state, const RaceHistory& history,
    const QString& responseLanguage)
{
    if (!provider_) {
        emit errorOccurred(QStringLiteral("AI provider is unavailable."));
        return;
    }
    provider_->cancelRequest();
    stateSnapshot_ = state;
    historySnapshot_ = history;
    responseLanguage_ = responseLanguage;
    toolRounds_ = 0;
    authoritativeFuelResult_ = {};
    conversation_.addUserMessage(text);
    sendCurrentRequest(true);
}

void LLMManager::testConnection()
{
    if (!provider_) return;
    ++requests_;
    emit statisticsChanged();
    provider_->testConnection();
}

void LLMManager::resetConversation()
{
    if (provider_) provider_->cancelRequest();
    conversation_.clear();
}

QVariantMap LLMManager::statistics() const
{
    return {{QStringLiteral("requests"), requests_}, {QStringLiteral("failed"), failures_},
        {QStringLiteral("rateLimits"), rateLimits_}, {QStringLiteral("inputTokens"), inputTokens_},
        {QStringLiteral("outputTokens"), outputTokens_}, {QStringLiteral("lastLatencyMs"), lastLatency_},
        {QStringLiteral("averageLatencyMs"), requests_ > 0 ? totalLatency_ / requests_ : 0},
        {QStringLiteral("firstTokenLatencyMs"), firstTokenLatency_},
        {QStringLiteral("lastHttpStatus"), lastHttpStatus_},
        {QStringLiteral("lastTool"), lastTool_}, {QStringLiteral("streaming"), settings_.streaming}};
}

QString LLMManager::providerName() const
{
    return provider_ ? provider_->providerName() : QStringLiteral("Unavailable");
}

void LLMManager::handleResponse(const QJsonObject& response, const qint64 latencyMilliseconds,
    const qint64 firstTokenMilliseconds)
{
    lastHttpStatus_ = 200;
    lastLatency_ = latencyMilliseconds;
    totalLatency_ += latencyMilliseconds;
    firstTokenLatency_ = firstTokenMilliseconds;
    const auto usage = response.value(QStringLiteral("usage")).toObject();
    inputTokens_ += usage.value(QStringLiteral("prompt_tokens")).toInteger();
    outputTokens_ += usage.value(QStringLiteral("completion_tokens")).toInteger();
    emit statisticsChanged();

    const auto choices = response.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) {
        handleFailure(ApiState::ProviderError, 200, QStringLiteral("AI response contained no choices."), latencyMilliseconds);
        return;
    }
    const auto message = choices.first().toObject().value(QStringLiteral("message")).toObject();
    const auto calls = message.value(QStringLiteral("tool_calls")).toArray();
    if (!calls.isEmpty()) {
        if (++toolRounds_ > 2) {
            handleFailure(ApiState::ProviderError, 200, QStringLiteral("AI requested too many tool rounds."), latencyMilliseconds);
            return;
        }
        QJsonArray normalizedCalls;
        int callIndex = 0;
        for (const auto& value : calls) {
            QJsonObject call = value.toObject();
            QJsonObject function = call.value(QStringLiteral("function")).toObject();
            const QString name = function.value(QStringLiteral("name")).toString();
            if (name.isEmpty()) {
                handleFailure(ApiState::ProviderError, 200,
                    QStringLiteral("LLM returned an invalid tool call."), latencyMilliseconds);
                return;
            }
            const QJsonValue argumentsValue = function.value(QStringLiteral("arguments"));
            QString arguments;
            if (argumentsValue.isString()) {
                arguments = argumentsValue.toString();
            } else if (argumentsValue.isObject()) {
                arguments = QString::fromUtf8(QJsonDocument(argumentsValue.toObject())
                    .toJson(QJsonDocument::Compact));
            } else if (!argumentsValue.isUndefined() && !argumentsValue.isNull()) {
                handleFailure(ApiState::ProviderError, 200,
                    QStringLiteral("LLM returned invalid tool arguments."), latencyMilliseconds);
                return;
            }
            if (arguments.isEmpty()) arguments = QStringLiteral("{}");
            if (!arguments.isEmpty()) {
                QJsonParseError parseError{};
                const auto parsedArguments = QJsonDocument::fromJson(arguments.toUtf8(), &parseError);
                if (parseError.error != QJsonParseError::NoError || !parsedArguments.isObject()) {
                    handleFailure(ApiState::ProviderError, 200,
                        QStringLiteral("LLM returned invalid tool arguments."), latencyMilliseconds);
                    return;
                }
            }
            QString callId = call.value(QStringLiteral("id")).toString();
            if (callId.isEmpty()) {
                callId = QStringLiteral("tool_call_%1_%2").arg(toolRounds_).arg(callIndex);
            }
            function.insert(QStringLiteral("arguments"), arguments);
            call.insert(QStringLiteral("id"), callId);
            call.insert(QStringLiteral("type"), QStringLiteral("function"));
            call.insert(QStringLiteral("function"), function);
            normalizedCalls.append(call);
            ++callIndex;
        }

        QJsonObject normalizedMessage = message;
        normalizedMessage.insert(QStringLiteral("role"), QStringLiteral("assistant"));
        normalizedMessage.insert(QStringLiteral("tool_calls"), normalizedCalls);
        conversation_.addAssistantToolCallMessage(normalizedMessage);
        for (const auto& value : normalizedCalls) {
            const auto call = value.toObject();
            const auto function = call.value(QStringLiteral("function")).toObject();
            const QString name = function.value(QStringLiteral("name")).toString();
            QJsonObject arguments;
            const auto argumentsDocument = QJsonDocument::fromJson(
                function.value(QStringLiteral("arguments")).toString().toUtf8());
            if (argumentsDocument.isObject()) arguments = argumentsDocument.object();
            const QJsonObject result = tools_.execute(name, stateSnapshot_, historySnapshot_, arguments);
            lastTool_ = name;
            if (name == QStringLiteral("get_fuel_status")) authoritativeFuelResult_ = result;
            conversation_.addToolResult(call.value(QStringLiteral("id")).toString(), name, result);
            emit toolCalled(name, result);
            qCInfo(logTool).noquote() << name << QJsonDocument(result).toJson(QJsonDocument::Compact);
        }
        emit statisticsChanged();
        sendCurrentRequest(true);
        return;
    }

    QString text = stripReasoning(messageText(message.value(QStringLiteral("content"))));
    if (text.isEmpty()) {
        handleFailure(ApiState::ProviderError, 200, QStringLiteral("AI response was empty."), latencyMilliseconds);
        return;
    }
    text = applyAuthoritativePostValidation(text, authoritativeFuelResult_, responseLanguage_);
    conversation_.addAssistantMessage(text);
    emit responseReady(text);
}

void LLMManager::handleFailure(const ApiState state, const int httpStatus, const QString& message,
    const qint64 latencyMilliseconds)
{
    ++failures_;
    if (state == ApiState::RateLimited) ++rateLimits_;
    lastHttpStatus_ = httpStatus;
    lastLatency_ = latencyMilliseconds;
    emit statisticsChanged();
    emit errorOccurred(state == ApiState::RateLimited
            ? QStringLiteral("AI API rate limit reached.") : message);
}

void LLMManager::sendCurrentRequest(const bool includeTools)
{
    QJsonObject request{{QStringLiteral("messages"), conversation_.messages(systemPrompt())},
        {QStringLiteral("temperature"), settings_.temperature},
        {QStringLiteral("max_tokens"), settings_.maximumTokens}};
    const bool googleApi = isGoogleGeminiApi(settings_.baseUrl);
    if (googleApi && isGemmaModel(settings_.model)) {
        // Hosted Gemma 4 uses Gemini ThinkingConfig. For Gemma specifically,
        // "minimal" is the documented OFF value. Do not send llama.cpp fields to
        // Google's compatibility endpoint.
        request.insert(QStringLiteral("extra_body"), QJsonObject{
            {QStringLiteral("google"), QJsonObject{
                {QStringLiteral("thinking_config"), QJsonObject{
                    {QStringLiteral("thinking_level"), QStringLiteral("minimal")},
                    {QStringLiteral("include_thoughts"), false}}}}}});
    // Google's OpenAI-compatible endpoint only permits fully disabling thinking on
    // non-Pro Gemini 2.5 models. Gemini 3 uses "minimal" as its lowest setting.
    } else if (isGeminiModel(settings_.model)) {
        request.insert(QStringLiteral("reasoning_effort"),
            canDisableGeminiReasoning(settings_.model)
                ? QStringLiteral("none") : QStringLiteral("minimal"));
    } else if (!googleApi && isLocalThinkingModel(settings_.model)) {
        // llama.cpp accepts reasoning_effort=none in current builds, while older
        // builds use the template kwarg. Supplying both keeps the phone server and
        // named Gemma/Qwen endpoints in no-thinking mode.
        request.insert(QStringLiteral("reasoning_effort"), QStringLiteral("none"));
        request.insert(QStringLiteral("reasoning_budget"), 0);
        request.insert(QStringLiteral("chat_template_kwargs"), QJsonObject{
            {QStringLiteral("enable_thinking"), false}});
    }
    if (includeTools && provider_->supportsToolCalling()) {
        request.insert(QStringLiteral("tools"), tools_.definitions());
        request.insert(QStringLiteral("tool_choice"), QStringLiteral("auto"));
    }
    ++requests_;
    emit statisticsChanged();
    provider_->sendChatRequest(request);
}

QString LLMManager::stateName(const ApiState state)
{
    switch (state) {
    case ApiState::Connected: return QStringLiteral("Connected");
    case ApiState::Requesting: return QStringLiteral("Requesting");
    case ApiState::RateLimited: return QStringLiteral("RateLimited");
    case ApiState::AuthenticationError: return QStringLiteral("AuthenticationError");
    case ApiState::NetworkError: return QStringLiteral("LLM Server Offline");
    case ApiState::ProviderError: return QStringLiteral("ProviderError");
    case ApiState::Unavailable: break;
    }
    return QStringLiteral("Unavailable");
}

QString LLMManager::messageText(const QJsonValue& content)
{
    if (content.isString()) return content.toString();
    QString result;
    for (const auto& block : content.toArray()) {
        const auto object = block.toObject();
        if (object.value(QStringLiteral("type")).toString() == QStringLiteral("text")) {
            result += object.value(QStringLiteral("text")).toString();
        }
    }
    return result;
}

QString LLMManager::stripReasoning(QString response)
{
    const auto options = QRegularExpression::CaseInsensitiveOption
        | QRegularExpression::DotMatchesEverythingOption;
    static const QRegularExpression xmlThinking(
        QStringLiteral(R"(<think\b[^>]*>.*?</think\s*>)"), options);
    static const QRegularExpression xmlAnalysis(
        QStringLiteral(R"(<(?:analysis|reasoning)\b[^>]*>.*?</(?:analysis|reasoning)\s*>)"), options);
    static const QRegularExpression gemmaThinking(
        QStringLiteral(R"(<\|channel\|?>(?:thought|analysis)\b.*?<\|?channel\|>)"), options);
    static const QRegularExpression finalMarker(
        QStringLiteral(R"(<\|channel\|?>(?:final|answer)\s*)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression openingReasoning(
        QStringLiteral(R"(<(?:think|analysis|reasoning)\b[^>]*>|<\|channel\|?>(?:thought|analysis)\b)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression plainReasoningHeading(
        QStringLiteral(R"(^\s*(?:thought|thinking process|analysis|reasoning)\s*:\s*)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression finalHeading(
        QStringLiteral(R"((?:^|[\r\n]+)\s*(?:final(?: answer| response)?|answer|response|kết luận)\s*:\s*)"),
        QRegularExpression::CaseInsensitiveOption);

    response.remove(xmlThinking);
    response.remove(xmlAnalysis);
    response.remove(gemmaThinking);
    response.remove(finalMarker);
    response.replace(QStringLiteral("<channel|>"), QString{});
    response.replace(QStringLiteral("<|channel|>"), QString{});

    // A short max-token limit can truncate reasoning before its closing token.
    // Never pass such a fragment to TTS. Recover only when an explicit final
    // heading is present; otherwise treating it as empty is safer than speaking it.
    const bool beginsWithPlainReasoning = plainReasoningHeading.match(response).hasMatch();
    if (openingReasoning.match(response).hasMatch() || beginsWithPlainReasoning) {
        QRegularExpressionMatch lastFinal;
        auto matches = finalHeading.globalMatch(response);
        while (matches.hasNext()) lastFinal = matches.next();
        if (!lastFinal.hasMatch()) return {};
        response = response.mid(lastFinal.capturedEnd());
    }
    return response.trimmed();
}

QString LLMManager::systemPrompt() const
{
    return QStringLiteral("You are a calm, clipped and authoritative real-time race engineer. Use an original "
                          "top-tier Formula race-engineer cadence: restrained, dry, precise, reassuring under "
                          "pressure, and never chatty. Lead with the action or status and give only one clear "
                          "instruction per transmission. Use a brief radio acknowledgement such as 'Copy' or its "
                          "natural local-language equivalent only when it adds value. Reserve urgency for immediate "
                          "danger, a time-critical strategy call, or a critical mechanical condition. Avoid greetings, "
                          "filler, hype, jokes, dramatic wording, catchphrases, and repeated telemetry. Reply with one "
                          "very short radio sentence, preferably 5-15 tokens. Return only the "
                          "final radio message; never output thoughts, analysis, reasoning, or control tokens. Never "
                          "invent telemetry; use tools for live facts. Tool status and conclusion "
                          "fields are authoritative: never recalculate or reinterpret available, enough_fuel, "
                          "fuel_status, trend, critical, or overheating. Surplus never means deficit. If unavailable, "
                          "say so briefly. Do not imitate distinctive quotes, impersonate, or claim to be any named "
                          "real engineer. Answer in %1.").arg(responseLanguage_);
}

QString LLMManager::applyAuthoritativePostValidation(const QString& response,
    const QJsonObject& fuelResult, const QString& responseLanguage)
{
    if (!fuelResult.value(QStringLiteral("available")).toBool()
        || !fuelResult.contains(QStringLiteral("enough_fuel"))) return response;
    const bool enough = fuelResult.value(QStringLiteral("enough_fuel")).toBool();
    const QString lower = response.toLower();
    const QStringList contradictions = enough
        ? QStringList{QStringLiteral("not enough"), QStringLiteral("short"), QStringLiteral("deficit"),
              QStringLiteral("need more"), QStringLiteral("không đủ"), QStringLiteral("thiếu"),
              QStringLiteral("cần thêm")}
        : QStringList{QStringLiteral("spare"), QStringLiteral("surplus"), QStringLiteral("fuel good"),
              QStringLiteral("enough fuel"), QStringLiteral("đủ nhiên liệu"), QStringLiteral("dư")};
    const bool contradictory = std::any_of(contradictions.cbegin(), contradictions.cend(),
        [&lower](const QString& marker) { return lower.contains(marker); });
    if (!contradictory) return response;

    const bool vietnamese = responseLanguage.contains(QStringLiteral("Vietnamese"), Qt::CaseInsensitive);
    if (enough) {
        const double spare = fuelResult.value(QStringLiteral("spare_laps")).toDouble();
        return vietnamese
            ? QStringLiteral("Đủ nhiên liệu. Dư khoảng %1 vòng.").arg(spare, 0, 'f', 1)
            : QStringLiteral("Fuel good. %1 laps spare.").arg(spare, 0, 'f', 1);
    }
    const double missing = fuelResult.value(QStringLiteral("missing_laps")).toDouble();
    return vietnamese
        ? QStringLiteral("Thiếu nhiên liệu khoảng %1 vòng.").arg(missing, 0, 'f', 1)
        : QStringLiteral("Fuel short by about %1 laps.").arg(missing, 0, 'f', 1);
}

} // namespace raceengineer
