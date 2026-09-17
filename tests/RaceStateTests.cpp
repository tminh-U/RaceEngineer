#include "telemetry/common/RaceState.h"
#include "telemetry/mock/MockTelemetryProvider.h"
#include "race/RaceHistory.h"
#include "events/EventEngine.h"
#include "vad/TenVadProcessor.h"
#include "llm/ConversationManager.h"
#include "llm/LLMManager.h"
#include "llm/providers/OpenAICompatibleProvider.h"
#include "llm/tools/ToolRegistry.h"
#include "spotter/SpotterEngine.h"
#include "structed_file_AC.h"
#include "structed_file_ACC.h"

#include <cmath>
#include <string_view>

#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>

int main()
{
    using namespace raceengineer;
    static_assert(sizeof(SPageFilePhysics) == 256);
    static_assert(sizeof(acc::SPageFilePhysics) == 716);
    static_assert(sizeof(SPageFileGraphic) == 284);
    static_assert(sizeof(acc::SPageFileGraphic) == 1588);
    int failures = 0;
    const auto expect = [&failures](const bool condition) {
        if (!condition) {
            ++failures;
        }
    };

    expect(normalizeAcGear(0) == -1);
    expect(normalizeAcGear(1) == 0);
    expect(normalizeAcGear(2) == 1);
    expect(clampUnit(-0.2) == 0.0);
    expect(clampUnit(0.4) == 0.4);
    expect(clampUnit(1.2) == 1.0);
    expect(std::string_view(simulatorName(Simulator::AssettoCorsa)) == "Assetto Corsa");

    RaceState state;
    expect(!state.fuelLiters.has_value());
    expect(!state.flag.has_value());

    MockTelemetryProvider mock;
    const bool started = mock.start();
    expect(started);
    expect(mock.isConnected());
    const bool updated = mock.update();
    expect(updated);
    const auto mockState = mock.getCurrentState();
    expect(mockState.connected);
    expect(mockState.simulator == Simulator::Mock);
    expect(mockState.speedKmh.has_value());
    expect(mockState.fuelLiters.has_value());
    expect(mockState.tyreTemperaturesCelsius.has_value());
    mock.stop();
    expect(!mock.isConnected());

    RaceHistory history;
    RaceState lapState;
    lapState.connected = true;
    lapState.capturedAt = std::chrono::steady_clock::now();
    lapState.currentLap = 1;
    lapState.fuelLiters = 20.0;
    history.update(lapState);
    lapState.capturedAt += std::chrono::seconds(90);
    lapState.currentLap = 2;
    lapState.previousLapTimeSeconds = 90.0;
    lapState.fuelLiters = 17.5;
    history.update(lapState);
    lapState.capturedAt += std::chrono::seconds(89);
    lapState.currentLap = 3;
    lapState.previousLapTimeSeconds = 89.0;
    lapState.fuelLiters = 15.0;
    history.update(lapState);
    expect(history.laps().size() == 2);
    expect(history.averageFuelConsumption().has_value());
    expect(std::abs(*history.averageFuelConsumption() - 2.5) < 0.0001);
    expect(std::abs(*history.averageLapTime() - 89.5) < 0.0001);
    expect(std::abs(*history.bestLap() - 89.0) < 0.0001);

    EventEngine events;
    RaceState eventState;
    eventState.connected = true;
    eventState.fuelCapacityLiters = 100.0;
    eventState.fuelLiters = 50.0;
    eventState.waterTemperatureCelsius = 90.0;
    eventState.pitLimiter = false;
    eventState.flag = FlagState::Green;
    const auto baseTime = std::chrono::steady_clock::now();
    expect(events.process(eventState, baseTime).empty());
    eventState.fuelLiters = 14.0;
    auto emitted = events.process(eventState, baseTime + std::chrono::seconds(1));
    expect(emitted.size() == 1 && emitted.front().type == EventType::FuelLow);
    expect(events.process(eventState, baseTime + std::chrono::seconds(2)).empty());
    eventState.fuelLiters = 6.0;
    emitted = events.process(eventState, baseTime + std::chrono::seconds(3));
    expect(emitted.size() == 1 && emitted.front().type == EventType::FuelCritical);
    eventState.pitLimiter = true;
    emitted = events.process(eventState, baseTime + std::chrono::seconds(4));
    expect(emitted.size() == 1 && emitted.front().type == EventType::PitLimiterOn);

    TenVadProcessor vad;
    expect(vad.isAvailable());
    expect(!vad.version().empty());
    std::array<std::int16_t, 256> silence{};
    const auto vadResult = vad.process(silence);
    expect(!vadResult.speech);

    ConversationManager conversation(6);
    for (int index = 0; index < 10; ++index) {
        conversation.addUserMessage(QStringLiteral("question %1").arg(index));
        conversation.addAssistantMessage(QStringLiteral("answer %1").arg(index));
    }
    expect(conversation.retainedMessageCount() <= 6);
    expect(conversation.messages(QStringLiteral("system")).first().toObject()
               .value(QStringLiteral("role")).toString() == QStringLiteral("system"));

    ToolRegistry tools;
    const auto fuel = tools.execute(QStringLiteral("get_fuel_status"), lapState, history);
    expect(fuel.value(QStringLiteral("available")).toBool());
    expect(std::abs(fuel.value(QStringLiteral("average_liters_per_lap")).toDouble() - 2.5) < 0.0001);
    expect(!tools.execute(QStringLiteral("unknown_tool"), lapState, history)
                .value(QStringLiteral("available")).toBool());
    expect(tools.definitions().size() >= 19);

    OpponentState opponent;
    opponent.carId = 44;
    opponent.driverName = "Lewis Hamilton";
    opponent.teamName = "Mercedes-AMG";
    opponent.position = 2;
    opponent.classPosition = 2;
    opponent.completedLaps = 12;
    opponent.previousLapTimeSeconds = 89.2;
    opponent.bestLapTimeSeconds = 88.8;
    opponent.recentLapTimesSeconds = {89.6, 89.2};
    lapState.position = 3;
    lapState.bestLapTimeSeconds = 89.1;
    lapState.opponents = {opponent};
    const auto leaderboard = tools.execute(QStringLiteral("get_leaderboard"), lapState, history);
    expect(leaderboard.value(QStringLiteral("available")).toBool());
    expect(leaderboard.value(QStringLiteral("opponent_count")).toInt() == 1);
    const auto driverPace = tools.execute(QStringLiteral("get_driver_pace"), lapState, history,
        QJsonObject{{QStringLiteral("driver"), QStringLiteral("Hamilton")}});
    expect(driverPace.value(QStringLiteral("available")).toBool());
    expect(driverPace.value(QStringLiteral("position")).toInt() == 2);
    expect(std::abs(driverPace.value(QStringLiteral("recent_average_seconds")).toDouble() - 89.4) < 0.001);
    expect(std::abs(driverPace.value(QStringLiteral("best_lap_delta_to_player_seconds")).toDouble() + 0.3) < 0.001);
    const auto position = tools.execute(QStringLiteral("get_position"), lapState, history);
    expect(position.value(QStringLiteral("opponent_ahead")).toString() == QStringLiteral("Lewis Hamilton"));

    expect(OpenAICompatibleProvider::chatEndpoint(QUrl(QStringLiteral("https://example.test/v1/")))
               == QUrl(QStringLiteral("https://example.test/v1/chat/completions")));
    expect(OpenAICompatibleProvider::parseErrorMessage(
               QByteArrayLiteral(R"({"error":{"message":"rate limited"}})"))
               == QStringLiteral("rate limited"));
    expect(OpenAICompatibleProvider::parseErrorMessage(QByteArrayLiteral("not json"))
               == QStringLiteral("Provider returned an invalid response."));
    const QByteArray geminiModels = QByteArrayLiteral(
        R"({"data":[{"id":"models/gemini-3.5-flash-lite"}]})");
    expect(OpenAICompatibleProvider::modelListContains(
        geminiModels, QStringLiteral("gemini-3.5-flash-lite")));
    expect(OpenAICompatibleProvider::modelListContains(
        geminiModels, QStringLiteral("models/gemini-3.5-flash-lite")));
    expect(!OpenAICompatibleProvider::modelListContains(
        geminiModels, QStringLiteral("gemini-3.8-flash")));
    ProviderConfiguration providerConfig;
    providerConfig.model = QStringLiteral("test-model");
    providerConfig.streaming = true;
    const auto serialized = OpenAICompatibleProvider::serializeRequest(
        QJsonObject{{QStringLiteral("messages"), QJsonArray{}}}, providerConfig);
    expect(serialized.value(QStringLiteral("model")).toString() == QStringLiteral("test-model"));
    expect(serialized.value(QStringLiteral("stream")).toBool());
    expect(OpenAICompatibleProvider::classifyError(429, false) == ApiState::RateLimited);
    expect(OpenAICompatibleProvider::classifyError(401, false) == ApiState::AuthenticationError);
    expect(OpenAICompatibleProvider::classifyError(0, true) == ApiState::NetworkError);
    // An offline LLM is isolated from telemetry and deterministic event processing.
    MockTelemetryProvider offlineProof;
    expect(offlineProof.start());
    expect(offlineProof.update());
    EventEngine offlineEvents;
    const auto offlineEventResult = offlineEvents.process(offlineProof.getCurrentState());
    expect(offlineEventResult.empty());
    expect(offlineProof.isConnected());
    offlineProof.stop();
    expect(OpenAICompatibleProvider::authorizationHeader(QString{}).isEmpty());
    expect(OpenAICompatibleProvider::authorizationHeader(QStringLiteral("secret"))
               == QByteArrayLiteral("Bearer secret"));
    expect(OpenAICompatibleProvider::modelsEndpoint(
               QUrl(QStringLiteral("http://100.114.125.88:8080/v1/")))
               == QUrl(QStringLiteral("http://100.114.125.88:8080/v1/models")));
    const QByteArray modelList = QByteArrayLiteral(
        R"({"object":"list","data":[{"id":"race-engineer","object":"model"}]})");
    expect(OpenAICompatibleProvider::modelListContains(modelList, QStringLiteral("race-engineer")));
    expect(!OpenAICompatibleProvider::modelListContains(modelList, QStringLiteral("missing")));

    const QByteArray llamaToolResponse = QByteArrayLiteral(R"({
        "choices":[{"message":{"role":"assistant","content":null,"tool_calls":[{
            "id":"call_42","type":"function","function":{"name":"get_fuel_status","arguments":"{}"}
        }]},"finish_reason":"tool_calls"}]
    })");
    QString llamaParseError;
    const auto parsedLlama = OpenAICompatibleProvider::parseResponseBody(
        llamaToolResponse, &llamaParseError);
    expect(llamaParseError.isEmpty());
    const auto parsedCall = parsedLlama.value(QStringLiteral("choices")).toArray().first()
                                .toObject().value(QStringLiteral("message")).toObject()
                                .value(QStringLiteral("tool_calls")).toArray().first().toObject();
    expect(parsedCall.value(QStringLiteral("id")).toString() == QStringLiteral("call_42"));
    expect(parsedCall.value(QStringLiteral("function")).toObject()
               .value(QStringLiteral("name")).toString() == QStringLiteral("get_fuel_status"));

    RaceHistory surplusHistory;
    RaceState surplusState;
    surplusState.connected = true;
    surplusState.currentLap = 1;
    surplusState.fuelLiters = 16.6;
    surplusState.capturedAt = std::chrono::steady_clock::now();
    surplusHistory.update(surplusState);
    surplusState.currentLap = 2;
    surplusState.previousLapTimeSeconds = 90.0;
    surplusState.fuelLiters = 14.5;
    surplusState.capturedAt += std::chrono::seconds(90);
    surplusHistory.update(surplusState);
    surplusState.currentLap = 3;
    surplusState.fuelLiters = 12.4;
    surplusState.capturedAt += std::chrono::seconds(90);
    surplusHistory.update(surplusState);
    surplusState.lapsRemaining = 5;
    const auto surplus = tools.execute(QStringLiteral("get_fuel_status"), surplusState, surplusHistory);
    expect(surplus.value(QStringLiteral("enough_fuel")).toBool());
    expect(surplus.value(QStringLiteral("fuel_status")).toString() == QStringLiteral("surplus"));
    expect(std::abs(surplus.value(QStringLiteral("spare_laps")).toDouble() - 0.9047619) < 0.001);
    expect(!surplus.contains(QStringLiteral("missing_laps")));

    RaceHistory deficitHistory;
    RaceState deficitState;
    deficitState.connected = true;
    deficitState.currentLap = 1;
    deficitState.fuelLiters = 12.0;
    deficitState.capturedAt = std::chrono::steady_clock::now();
    deficitHistory.update(deficitState);
    deficitState.currentLap = 2;
    deficitState.previousLapTimeSeconds = 90.0;
    deficitState.fuelLiters = 10.0;
    deficitState.capturedAt += std::chrono::seconds(90);
    deficitHistory.update(deficitState);
    deficitState.currentLap = 3;
    deficitState.fuelLiters = 8.0;
    deficitState.capturedAt += std::chrono::seconds(90);
    deficitHistory.update(deficitState);
    deficitState.lapsRemaining = 5;
    const auto deficit = tools.execute(QStringLiteral("get_fuel_status"), deficitState, deficitHistory);
    expect(!deficit.value(QStringLiteral("enough_fuel")).toBool(true));
    expect(deficit.value(QStringLiteral("fuel_status")).toString() == QStringLiteral("deficit"));
    expect(std::abs(deficit.value(QStringLiteral("missing_laps")).toDouble() - 1.0) < 0.001);
    expect(!deficit.contains(QStringLiteral("spare_laps")));

    expect(LLMManager::applyAuthoritativePostValidation(QStringLiteral("Fuel deficit."),
               surplus, QStringLiteral("English")) == QStringLiteral("Fuel good. 0.9 laps spare."));
    expect(LLMManager::applyAuthoritativePostValidation(QStringLiteral("Đủ nhiên liệu."),
               deficit, QStringLiteral("Vietnamese")) == QStringLiteral("Thiếu nhiên liệu khoảng 1.0 vòng."));

    expect(LLMManager::stripReasoning(QStringLiteral(
               "<|channel>thought\nI should inspect telemetry.<channel|>"
               "<|channel>final\nLốp trước hơi nóng."))
        == QStringLiteral("Lốp trước hơi nóng."));
    expect(LLMManager::stripReasoning(QStringLiteral(
               "<think>Long hidden reasoning.</think> Giữ tốc độ hiện tại."))
        == QStringLiteral("Giữ tốc độ hiện tại."));
    expect(LLMManager::stripReasoning(QStringLiteral(
               "Thought: inspect the telemetry\nFinal answer: Box this lap."))
        == QStringLiteral("Box this lap."));
    expect(LLMManager::stripReasoning(QStringLiteral(
               "<think>This reasoning was truncated by max tokens"))
        .isEmpty());
    expect(LLMManager::stripReasoning(QStringLiteral(
               "<analysis>hidden</analysis>Áp suất lốp ổn định."))
        == QStringLiteral("Áp suất lốp ổn định."));

    ConversationManager toolConversation(8);
    toolConversation.addUserMessage(QStringLiteral("Do I have enough fuel?"));
    const QJsonObject assistantToolMessage{{QStringLiteral("role"), QStringLiteral("assistant")},
        {QStringLiteral("content"), QJsonValue::Null},
        {QStringLiteral("tool_calls"), parsedLlama.value(QStringLiteral("choices")).toArray().first()
            .toObject().value(QStringLiteral("message")).toObject().value(QStringLiteral("tool_calls"))}};
    toolConversation.addAssistantToolCallMessage(assistantToolMessage);
    toolConversation.addToolResult(QStringLiteral("call_42"), QStringLiteral("get_fuel_status"), surplus);
    const auto secondTurn = toolConversation.messages(QStringLiteral("system"));
    expect(secondTurn.size() == 4);
    expect(secondTurn.at(2).toObject().value(QStringLiteral("role")).toString() == QStringLiteral("assistant"));
    expect(secondTurn.at(3).toObject().value(QStringLiteral("role")).toString() == QStringLiteral("tool"));
    expect(secondTurn.at(3).toObject().value(QStringLiteral("tool_call_id")).toString()
               == QStringLiteral("call_42"));
    const auto serializedFuel = QJsonDocument::fromJson(secondTurn.at(3).toObject()
        .value(QStringLiteral("content")).toString().toUtf8()).object();
    expect(serializedFuel.value(QStringLiteral("enough_fuel")).toBool());
    expect(serializedFuel.value(QStringLiteral("fuel_status")).toString() == QStringLiteral("surplus"));
    expect(std::abs(serializedFuel.value(QStringLiteral("spare_laps")).toDouble() - 0.9047619) < 0.001);

    LlmSettings localDefaults;
    expect(localDefaults.provider == QStringLiteral("OpenAI Compatible"));
    expect(localDefaults.baseUrl == QStringLiteral("http://100.114.125.88:8080/v1"));
    expect(localDefaults.model == QStringLiteral("race-engineer"));
    expect(localDefaults.maximumTokens == 32);
    expect(localDefaults.timeoutMilliseconds == 30000);

    SpotterEngine spotter;
    expect(spotter.process(lapState).empty());
    expect(!SpotterEngine::unavailableReason().empty());

    return failures == 0 ? 0 : 1;
}
