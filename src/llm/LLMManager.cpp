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

bool isOpenAiReasoningModel(const QString& model)
{
    return model.startsWith(QStringLiteral("o1"), Qt::CaseInsensitive)
        || model.startsWith(QStringLiteral("o3"), Qt::CaseInsensitive)
        || model.startsWith(QStringLiteral("o4"), Qt::CaseInsensitive)
        || model.contains(QStringLiteral("deepseek-r1"), Qt::CaseInsensitive)
        || model.contains(QStringLiteral("reasoner"), Qt::CaseInsensitive)
        || model.contains(QStringLiteral("thinking"), Qt::CaseInsensitive);
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
        // Hide private reasoning tokens / internal reasoning channels from flashing on UI.
        if (authoritativeFuelResult_.isEmpty() && !isGemmaModel(settings_.model) && !settings_.reasoning) {
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
    const QString& responseLanguage, const QJsonObject& pitStrategy)
{
    if (!provider_) {
        emit errorOccurred(QStringLiteral("AI provider is unavailable."));
        return;
    }
    provider_->cancelRequest();
    stateSnapshot_ = state;
    historySnapshot_ = history;
    pitStrategySnapshot_ = pitStrategy;
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
            qCWarning(logApp) << "AI exceeded tool call rounds limit (" << toolRounds_
                              << "). Synthesizing fallback radio message.";
            const bool vietnamese = responseLanguage_.contains(QStringLiteral("Vietnamese"), Qt::CaseInsensitive);
            const QString fallback = vietnamese
                ? QStringLiteral("Hiện chưa có dữ liệu telemetry.")
                : QStringLiteral("No telemetry data available.");
            conversation_.addAssistantMessage(fallback);
            emit responseReady(fallback);
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
        bool anyAvailable = false;
        for (const auto& value : normalizedCalls) {
            const auto call = value.toObject();
            const auto function = call.value(QStringLiteral("function")).toObject();
            const QString name = function.value(QStringLiteral("name")).toString();
            QJsonObject arguments;
            const auto argumentsDocument = QJsonDocument::fromJson(
                function.value(QStringLiteral("arguments")).toString().toUtf8());
            if (argumentsDocument.isObject()) arguments = argumentsDocument.object();
            const QJsonObject result = tools_.execute(name, stateSnapshot_, historySnapshot_,
                arguments, pitStrategySnapshot_);
            if (result.value(QStringLiteral("available")).toBool(false)) {
                anyAvailable = true;
            }
            lastTool_ = name;
            if (name == QStringLiteral("get_fuel_status")) authoritativeFuelResult_ = result;
            conversation_.addToolResult(call.value(QStringLiteral("id")).toString(), name, result);
            emit toolCalled(name, result);
            qCInfo(logTool).noquote() << name << QJsonDocument(result).toJson(QJsonDocument::Compact);
        }
        emit statisticsChanged();
        const bool allowAnotherToolRound = (toolRounds_ < 2) && anyAvailable;
        sendCurrentRequest(allowAnotherToolRound);
        return;
    }

    QString text = stripReasoning(messageText(message.value(QStringLiteral("content"))));
    if (text.isEmpty()) {
        if (toolRounds_ > 0) {
            const bool vietnamese = responseLanguage_.contains(QStringLiteral("Vietnamese"), Qt::CaseInsensitive);
            text = vietnamese
                ? QStringLiteral("Hiện chưa có dữ liệu telemetry.")
                : QStringLiteral("No telemetry data available.");
        } else {
            handleFailure(ApiState::ProviderError, 200, QStringLiteral("AI response was empty."), latencyMilliseconds);
            return;
        }
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

QJsonObject LLMManager::buildChatPayload(const LlmSettings& settings,
    const QJsonArray& messages, const bool includeTools, const QJsonArray& tools)
{
    QJsonObject request{{QStringLiteral("messages"), messages},
        {QStringLiteral("temperature"), settings.temperature},
        {QStringLiteral("max_tokens"), settings.maximumTokens}};

    const bool googleApi = isGoogleGeminiApi(settings.baseUrl);
    if (settings.reasoning) {
        if (googleApi && isGemmaModel(settings.model)) {
            // Hosted Gemma uses Gemini ThinkingConfig without thinking_level (which is rejected with 400 on Gemma).
            request.insert(QStringLiteral("extra_body"), QJsonObject{
                {QStringLiteral("google"), QJsonObject{
                    {QStringLiteral("thinking_config"), QJsonObject{
                        {QStringLiteral("include_thoughts"), false}}}}}});
        } else if (isGeminiModel(settings.model)) {
            request.insert(QStringLiteral("reasoning_effort"), QStringLiteral("low"));
        } else if (!googleApi && isLocalThinkingModel(settings.model)) {
            request.insert(QStringLiteral("reasoning_effort"), QStringLiteral("low"));
            request.insert(QStringLiteral("reasoning_budget"), 128);
            request.insert(QStringLiteral("chat_template_kwargs"), QJsonObject{
                {QStringLiteral("enable_thinking"), true}});
        } else if (isOpenAiReasoningModel(settings.model)) {
            request.insert(QStringLiteral("reasoning_effort"), QStringLiteral("low"));
        } else {
            qCInfo(logApp) << "AI reasoning requested, but model/provider does not support reasoning configuration:"
                           << settings.model;
        }
    } else {
        // When OFF: explicitly disable reasoning/thinking if provider API supports that parameter.
        // Do not send unnecessary reasoning configuration to unsupported models.
        if (googleApi && isGemmaModel(settings.model)) {
            request.insert(QStringLiteral("extra_body"), QJsonObject{
                {QStringLiteral("google"), QJsonObject{
                    {QStringLiteral("thinking_config"), QJsonObject{
                        {QStringLiteral("thinking_level"), QStringLiteral("minimal")},
                        {QStringLiteral("include_thoughts"), false}}}}}});
        } else if (isGeminiModel(settings.model)) {
            request.insert(QStringLiteral("reasoning_effort"),
                canDisableGeminiReasoning(settings.model)
                    ? QStringLiteral("none") : QStringLiteral("minimal"));
        } else if (!googleApi && isLocalThinkingModel(settings.model)) {
            request.insert(QStringLiteral("reasoning_effort"), QStringLiteral("none"));
            request.insert(QStringLiteral("reasoning_budget"), 0);
            request.insert(QStringLiteral("chat_template_kwargs"), QJsonObject{
                {QStringLiteral("enable_thinking"), false}});
        }
    }

    if (includeTools && !tools.isEmpty()) {
        request.insert(QStringLiteral("tools"), tools);
        request.insert(QStringLiteral("tool_choice"), QStringLiteral("auto"));
    }

    return request;
}

QJsonObject LLMManager::buildRequest(const bool includeTools) const
{
    const bool sendTools = includeTools && provider_ && provider_->supportsToolCalling();
    const QJsonArray toolsDef = sendTools ? tools_.definitions() : QJsonArray{};
    return buildChatPayload(settings_, conversation_.messages(systemPrompt()), sendTools, toolsDef);
}

void LLMManager::sendCurrentRequest(const bool includeTools)
{
    const QJsonObject request = buildRequest(includeTools);
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
    QString prompt = QStringLiteral(
        "You are a calm, clipped and authoritative real-time race engineer. Use an original "
        "top-tier Formula race-engineer cadence: restrained, dry, precise, reassuring under "
        "pressure, and never chatty. Lead with the action or status and give only one clear "
        "instruction per transmission. Use a brief radio acknowledgement such as 'Copy' or its "
        "natural local-language equivalent only when it adds value. Reserve urgency for immediate "
        "danger, a time-critical strategy call, or a critical mechanical condition. Avoid greetings, "
        "filler, hype, jokes, dramatic wording, catchphrases, and repeated telemetry. Reply with one "
        "short radio sentence, normally 10-25 tokens, but include every fact explicitly requested. Return only the "
        "final radio message; never output thoughts, analysis, reasoning, or control tokens. Never "
        "invent telemetry; use tools for live facts. Tool status and conclusion "
        "fields are authoritative: never recalculate or reinterpret available, enough_fuel, "
        "fuel_status, trend, critical, overheating, major_damage, and damage section severity. Surplus never means deficit. If unavailable, "
        "say so briefly. If a tool returns available: false, do not call other tools to search for "
        "alternative data; immediately answer that the requested data is unavailable. "
        "For every live telemetry or car-condition question, you MUST call the relevant tool before answering; "
        "never answer from memory or from a vague generalization. "
        "For which lap to pit or the AI pit recommendation, call get_pit_strategy; say pit at the end of pit_lap. "
        "For position/leader/ahead/behind questions "
        "call get_position. For lap pace or lap-time questions call get_lap_times, get_recent_laps, or "
        "get_driver_pace. For tyre temperature, pressure, or condition questions call get_tyre_status; when the "
        "driver asks about tyre temperatures (nhiệt độ lốp) or pressures, ALWAYS state the actual numeric values "
        "(e.g. front and rear averages, or specific wheels in degrees Celsius) along with the condition status; NEVER "
        "just say 'quá nhiệt' or 'bình thường' without the temperature numbers. For brake questions call get_brake_status "
        "and report numeric brake temperatures in degrees Celsius. For body, wheel, or suspension damage questions "
        "call get_damage_status. For wheel damage, "
        "use only affected_wheels and wheel_damage_sections when wheel_damage_available=true; FL, FR, RL, RR mean "
        "front-left, front-right, rear-left, rear-right. Do not infer wheel damage from tyre wear, pressure, or "
        "temperature. Wheel suspension values are raw simulator levels: report detected/not detected and the wheel, "
        "but do not label them minor or major. Overall body damage is an aggregate, not a physical location. Include the returned position, "
        "driver names, section, wheel, severity, or time when the user asks for them. Never call minor or moderate damage "
        "major unless major_damage=true. Lap display fields ending in _mmss are authoritative M:SS.mmm values; "
        "say those values instead of converting them to raw seconds. Gap values remain seconds. "
        "Do not imitate distinctive quotes, impersonate, or claim to be any named "
        "real engineer. When replying in Vietnamese, state units and telemetry numbers naturally for clear radio speech synthesis (e.g. độ C/độ xê, lít, vòng, giây, bar, kPa) without markdown formatting. Answer in %1.\n\n"
        "SPEECH-TO-TEXT (STT) INTENT RECOVERY:\n"
        "- Treat incoming user text as speech-to-text output and assume it may contain transcription mistakes.\n"
        "- The user primarily speaks Vietnamese, with occasional motorsport/racing English terms mixed in.\n"
        "- Use the conversation context, current race/telemetry context, and common racing vocabulary to infer "
        "the most likely intended meaning when the transcription is unclear.\n"
        "- Correct obvious phonetic/ASR mistakes silently before interpreting the request. Prefer semantic intent "
        "over literal malformed wording.\n"
        "- Do not mention STT errors unless the meaning is genuinely ambiguous.\n"
        "- Do not over-correct text that already makes sense.\n"
        "- Do not invent a completely new request when the transcript provides insufficient evidence.\n"
        "- If multiple interpretations remain plausible, ask a short clarification question.\n"
        "- Correct common racing terms when context strongly supports them, e.g. pit, gap, delta, DRS, ERS, "
        "brake bias, tyre, understeer, oversteer, front/rear/left/right (trước/sau/trái/phải, lốp, xăng, lap/vòng).\n"
        "- Examples of internal interpretation:\n"
        "  * STT 'gap với xe chước bao nhiêu' -> interpret as 'gap với xe trước bao nhiêu'\n"
        "  * STT 'lốp chước chái thế nào' -> interpret as 'lốp trước trái thế nào'\n"
        "  * STT 'nhiệt độ lốp bao nhiêu' / 'nhiệt độ lốp' -> call get_tyre_status and report numeric temperatures (e.g. lốp trước 95 độ, sau 92 độ)\n"
        "  * STT 'pit láp này không' -> interpret as 'pit lap này không'\n"
        "- Use available telemetry/state as contextual evidence, but do not fabricate telemetry values.\n"
        "- Preserve numbers, positions, lap counts, tyre positions, and other concrete details unless the ASR "
        "error is obvious from context.\n"
        "- The purpose of this correction is to understand the user's intent, not to return a cleaned transcript. "
        "Do this internally and answer the intended request naturally in your clipped race engineer radio reply."
    ).arg(responseLanguage_);

    if (stateSnapshot_.connected) {
        QStringList telemetryParts;
        if (stateSnapshot_.position) {
            telemetryParts << QStringLiteral("P%1").arg(*stateSnapshot_.position);
        }
        if (stateSnapshot_.currentLap) {
            if (stateSnapshot_.totalLaps && *stateSnapshot_.totalLaps > 0) {
                telemetryParts << QStringLiteral("Lap %1/%2").arg(*stateSnapshot_.currentLap).arg(*stateSnapshot_.totalLaps);
            } else {
                telemetryParts << QStringLiteral("Lap %1").arg(*stateSnapshot_.currentLap);
            }
        }
        if (stateSnapshot_.sessionType) {
            switch (*stateSnapshot_.sessionType) {
            case SessionType::Practice: telemetryParts << QStringLiteral("Practice"); break;
            case SessionType::Qualifying: telemetryParts << QStringLiteral("Qualifying"); break;
            case SessionType::Race: telemetryParts << QStringLiteral("Race"); break;
            case SessionType::Hotlap: telemetryParts << QStringLiteral("Hotlap"); break;
            default: break;
            }
        }
        if (stateSnapshot_.track && !stateSnapshot_.track->empty()) {
            telemetryParts << QStringLiteral("Track: %1").arg(QString::fromStdString(*stateSnapshot_.track));
        }
        if (!telemetryParts.isEmpty()) {
            prompt += QStringLiteral("\n\nTelemetry Context: %1.").arg(telemetryParts.join(QStringLiteral(", ")));
        }
    }

    if (!driverName_.trimmed().isEmpty()) {
        prompt += QStringLiteral("\n\nThe driver's name is %1. Address the driver by name when natural and appropriate, but maintain your clipped race-engineer cadence.").arg(driverName_.trimmed());
    }

    QString styleInstruction;
    if (responseStyle_.compare(QStringLiteral("Tối giản"), Qt::CaseInsensitive) == 0) {
        styleInstruction = QStringLiteral(
            "RESPONSE STYLE: MINIMAL (Tối giản).\n"
            "- Be extremely concise, clipped, and raw.\n"
            "- Output only the essential metric, delta, or immediate action in 3 to 8 tokens/words.\n"
            "- Zero filler, no conversational padding, no elaboration unless requested.");
    } else if (responseStyle_.compare(QStringLiteral("Chi tiết"), Qt::CaseInsensitive) == 0) {
        styleInstruction = QStringLiteral(
            "RESPONSE STYLE: DETAILED (Chi tiết).\n"
            "- Provide full telemetry context, trends, and analytical reasoning while maintaining a professional engineer tone.\n"
            "- Target 20 to 40 tokens/words with clear actionable advice.\n"
            "- Explain tyre condition, fuel pace, or strategic trade-offs when relevant.");
    } else {
        styleInstruction = QStringLiteral(
            "RESPONSE STYLE: STANDARD (Tiêu chuẩn).\n"
            "- Use standard top-tier race engineer radio cadence: restrained, dry, precise, and clipped.\n"
            "- Lead with the action or status. Target 10 to 25 tokens/words and include all requested telemetry facts.");
    }
    prompt += QStringLiteral("\n\n%1").arg(styleInstruction);

    return prompt;
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
