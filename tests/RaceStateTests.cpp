#include "telemetry/common/RaceState.h"
#include "telemetry/mock/MockTelemetryProvider.h"
#include "race/RaceHistory.h"
#include "events/EventEngine.h"
#include "llm/ConversationManager.h"
#include "llm/LLMManager.h"
#include "llm/providers/OpenAICompatibleProvider.h"
#include "llm/tools/ToolRegistry.h"
#include "spotter/SpotterEngine.h"
#include "telemetry/ac/AcExtensionClient.h"
#include "tts/RacingTextNormalizer.h"
#include "audio/AudioDucker.h"
#include "telemetry/common/WindowsSharedMemory.h"
#include "structed_file_AC.h"
#include "structed_file_ACC.h"

#include <whisper.h>

#include <cmath>
#include <string_view>

#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFileInfo>

int main()
{
    using namespace raceengineer;
    static_assert(sizeof(SPageFilePhysics) == 256);
    static_assert(sizeof(acc::SPageFilePhysics) == 716);
    static_assert(sizeof(SPageFileGraphic) == 284);
    static_assert(sizeof(acc::SPageFileGraphic) == 1588);
    int failures = 0;
#define expect(cond) do { if (!(cond)) { ++failures; fprintf(stderr, "ASSERT FAILED at line %d: %s\n", __LINE__, #cond); } } while(0)

    const QByteArray phoWhisperModel = qgetenv("RACEENGINEER_PHOWHISPER_MODEL");
    if (!phoWhisperModel.isEmpty()) {
        expect(QFileInfo::exists(QString::fromUtf8(phoWhisperModel)));
        auto params = whisper_context_default_params();
        whisper_context* const context = whisper_init_from_file_with_params(phoWhisperModel.constData(), params);
        expect(context != nullptr);
        if (context != nullptr) {
            whisper_free(context);
        }
    }

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

    eventState.flag = FlagState::Yellow;
    emitted = events.process(eventState, baseTime + std::chrono::seconds(5));
    expect(emitted.size() == 1 && emitted.front().type == EventType::YellowFlag
           && emitted.front().priority == EventPriority::Critical && emitted.front().message == "Cờ vàng.");

    eventState.flag = FlagState::Green;
    emitted = events.process(eventState, baseTime + std::chrono::seconds(11));
    expect(emitted.size() == 1 && emitted.front().type == EventType::GreenFlag
           && emitted.front().priority == EventPriority::Important && emitted.front().message == "Cờ xanh lá.");

    eventState.flag = FlagState::Blue;
    emitted = events.process(eventState, baseTime + std::chrono::seconds(17));
    expect(emitted.size() == 1 && emitted.front().type == EventType::BlueFlag
           && emitted.front().priority == EventPriority::Important && emitted.front().message == "Cờ xanh dương.");

    eventState.flag = FlagState::White;
    emitted = events.process(eventState, baseTime + std::chrono::seconds(28));
    expect(emitted.size() == 1 && emitted.front().type == EventType::WhiteFlag
           && emitted.front().priority == EventPriority::Important && emitted.front().message == "Cờ trắng.");

    eventState.flag = FlagState::Red;
    emitted = events.process(eventState, baseTime + std::chrono::seconds(39));
    expect(emitted.size() == 1 && emitted.front().type == EventType::RedFlag
           && emitted.front().priority == EventPriority::Critical && emitted.front().message == "Cờ đỏ.");

    eventState.flag = FlagState::Black;
    emitted = events.process(eventState, baseTime + std::chrono::seconds(50));
    expect(emitted.size() == 1 && emitted.front().type == EventType::BlackFlag
           && emitted.front().priority == EventPriority::Critical && emitted.front().message == "Cờ đen.");

    eventState.flag = FlagState::Chequered;
    emitted = events.process(eventState, baseTime + std::chrono::seconds(61));
    expect(emitted.size() == 1 && emitted.front().type == EventType::ChequeredFlag
           && emitted.front().priority == EventPriority::Important && emitted.front().message == "Cờ ca rô.");

    EventEngine connEvents;
    RaceState connState;
    connState.connected = false;
    expect(connEvents.process(connState, baseTime).empty());

    connState.connected = true;
    auto connEmitted = connEvents.process(connState, baseTime + std::chrono::seconds(1));
    expect(connEmitted.size() == 1 && connEmitted.front().type == EventType::SessionStarted
           && connEmitted.front().priority == EventPriority::Engineer
           && connEmitted.front().message == "Radio check, Minh.");

    connState.connected = false;
    auto disconnEmitted = connEvents.process(connState, baseTime + std::chrono::seconds(2));
    expect(disconnEmitted.empty());

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
    lapState.driverName = "Player Driver";
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
    expect(driverPace.value(QStringLiteral("last_lap_mmss")).toString() == QStringLiteral("1:29.200"));
    expect(driverPace.value(QStringLiteral("best_lap_mmss")).toString() == QStringLiteral("1:28.800"));
    expect(std::abs(driverPace.value(QStringLiteral("best_lap_delta_to_player_seconds")).toDouble() + 0.3) < 0.001);
    const auto position = tools.execute(QStringLiteral("get_position"), lapState, history);
    expect(position.value(QStringLiteral("opponent_ahead")).toString() == QStringLiteral("Lewis Hamilton"));
    expect(position.value(QStringLiteral("position_label")).toString() == QStringLiteral("P3"));
    expect(position.value(QStringLiteral("ahead")).toObject().value(QStringLiteral("position")).toInt() == 2);

    RaceState damageState;
    damageState.connected = true;
    damageState.damage = std::array<double, 5>{0.12, 0.0, 0.30, 0.0, 0.0};
    damageState.suspensionDamage = WheelValues{0.04, 0.0, 0.12, 0.0};
    const auto damage = tools.execute(QStringLiteral("get_damage_status"), damageState, history);
    expect(damage.value(QStringLiteral("status")).toString() == QStringLiteral("moderate"));
    expect(!damage.value(QStringLiteral("major_damage")).toBool());
    expect(damage.value(QStringLiteral("damage_sections")).toArray().at(0).toObject()
               .value(QStringLiteral("section")).toString() == QStringLiteral("front"));
    expect(damage.value(QStringLiteral("damage_sections")).toArray().at(0).toObject()
               .value(QStringLiteral("severity")).toString() == QStringLiteral("minor"));
    expect(damage.value(QStringLiteral("affected_wheels")).toArray().size() == 2);
    expect(damage.value(QStringLiteral("affected_wheels")).toArray().at(0).toString() == QStringLiteral("FL"));
    expect(damage.value(QStringLiteral("affected_wheels")).toArray().at(1).toString() == QStringLiteral("RL"));
    expect(damage.value(QStringLiteral("wheel_damage_sections")).toArray().at(0).toObject()
               .value(QStringLiteral("damaged")).toBool());
    expect(!damage.value(QStringLiteral("wheel_damage_sections")).toArray().at(1).toObject()
               .value(QStringLiteral("damaged")).toBool());
    RaceState bodyOnlyDamage = damageState;
    bodyOnlyDamage.suspensionDamage.reset();
    const auto unavailableWheelDamage = tools.execute(QStringLiteral("get_damage_status"),
        bodyOnlyDamage, history);
    expect(!unavailableWheelDamage.value(QStringLiteral("wheel_damage_available")).toBool());
    expect(unavailableWheelDamage.value(QStringLiteral("wheel_damage_status")).toString()
               == QStringLiteral("unavailable"));

    RaceState tyreState;
    tyreState.connected = true;
    tyreState.tyreTemperaturesCelsius = WheelValues{104.2, 103.8, 95.1, 94.9};
    tyreState.brakeTemperaturesCelsius = WheelValues{520.0, 515.0, 430.0, 425.0};
    const auto tyreResult = tools.execute(QStringLiteral("get_tyre_status"), tyreState, history);
    expect(tyreResult.value(QStringLiteral("available")).toBool());
    expect(tyreResult.value(QStringLiteral("front_avg_c")).toDouble() == 104.0);
    expect(tyreResult.value(QStringLiteral("rear_avg_c")).toDouble() == 95.0);
    expect(tyreResult.value(QStringLiteral("fl_c")).toDouble() == 104.0);
    expect(tyreResult.value(QStringLiteral("front_status")).toString() == QStringLiteral("hot"));

    const auto brakeResult = tools.execute(QStringLiteral("get_brake_status"), tyreState, history);
    expect(brakeResult.value(QStringLiteral("available")).toBool());
    expect(brakeResult.value(QStringLiteral("front_avg_c")).toDouble() == 518.0);
    expect(brakeResult.value(QStringLiteral("rear_avg_c")).toDouble() == 428.0);

    SpotterEngine spotter;
    RaceState spotterState;
    spotterState.connected = true;
    spotterState.worldPosition = std::array<double, 3>{0.0, 0.0, 0.0};
    spotterState.heading = 0.0;

    expect(spotter.process(spotterState).empty());

    OpponentState oppLeft;
    oppLeft.carId = 1;
    oppLeft.worldPosition = std::array<double, 3>{-2.5, 0.0, 1.0};
    spotterState.opponents = {oppLeft};

    auto spotterEvents = spotter.process(spotterState);
    expect(spotterEvents.empty());
    spotterEvents = spotter.process(spotterState);
    expect(spotterEvents.size() == 1);
    expect(spotterEvents.front().type == EventType::CarLeft);
    expect(spotterEvents.front().priority == EventPriority::Spotter);
    expect(spotter.hasLeft());
    expect(!spotter.hasRight());

    spotterEvents = spotter.process(spotterState);
    expect(spotterEvents.empty());

    OpponentState oppRight;
    oppRight.carId = 2;
    oppRight.worldPosition = std::array<double, 3>{2.5, 0.0, 0.0};
    spotterState.opponents = {oppLeft, oppRight};

    spotterEvents = spotter.process(spotterState);
    expect(spotterEvents.empty());
    spotterEvents = spotter.process(spotterState);
    expect(spotterEvents.size() == 1);
    expect(spotterEvents.front().type == EventType::ThreeWide);
    expect(spotter.isThreeWide());

    spotterState.opponents.clear();
    expect(spotter.process(spotterState).empty());
    expect(spotter.process(spotterState).empty());
    spotterEvents = spotter.process(spotterState);
    expect(spotterEvents.empty());
    expect(!spotter.hasLeft());
    expect(!spotter.hasRight());

    AcExtensionClient acExt;
    expect(acExt.start(19996));
    expect(!acExt.hasData());
    expect(acExt.opponents().empty());
    expect(!acExt.gapAhead().has_value());
    expect(!acExt.gapBehind().has_value());
    expect(!acExt.opponentAhead().has_value());
    expect(!acExt.opponentBehind().has_value());
    expect(!acExt.playerSectors().has_value());
    expect(!acExt.brakeTemperatures().has_value());

    // Test writing shared memory and having AcExtensionClient read it
    {
        WindowsSharedMemory testWriter;
        if (testWriter.createOrOpen(L"Local\\race_engineer_ac_ext", 10428)) {
            uint8_t* raw = static_cast<uint8_t*>(const_cast<void*>(testWriter.data()));
            std::memset(raw, 0, 10428);
            std::memcpy(raw, "RAEX", 4);
            uint32_t ver = 1;
            uint32_t seq = 2; // even = stable
            int64_t ts = 1234567;
            int32_t numCars = 1;
            float gapA = 1.2f;
            float gapB = 2.4f;
            float sec[3] = {28.5f, 35.2f, 29.1f};
            float bt[4] = {450.0f, 452.0f, 410.0f, 408.0f};
            std::memcpy(raw + 4, &ver, 4);
            std::memcpy(raw + 8, &seq, 4);
            std::memcpy(raw + 12, &ts, 8);
            std::memcpy(raw + 20, &numCars, 4);
            std::memcpy(raw + 24, &gapA, 4);
            std::memcpy(raw + 28, &gapB, 4);
            std::memcpy(raw + 32, sec, 12);
            std::memcpy(raw + 44, bt, 16);
            std::memcpy(reinterpret_cast<char*>(raw + 60), "Verstappen", 10);
            std::memcpy(reinterpret_cast<char*>(raw + 124), "Leclerc", 7);

            // Car 0: id=1, pos=2, speed=215.0, lastLap=92.5, bestLap=91.8, coords={-2.5, 0.0, 1.0}, driver="Verstappen", car="RedBull"
            uint8_t* carRaw = raw + 188;
            int32_t cId = 1;
            int32_t cPos = 2;
            float cSpd = 215.0f;
            float cLast = 92.5f;
            float cBest = 91.8f;
            float cPos3[3] = {-2.5f, 0.0f, 1.0f};
            std::memcpy(carRaw, &cId, 4);
            std::memcpy(carRaw + 4, &cPos, 4);
            std::memcpy(carRaw + 8, &cSpd, 4);
            std::memcpy(carRaw + 12, &cLast, 4);
            std::memcpy(carRaw + 16, &cBest, 4);
            std::memcpy(carRaw + 20, cPos3, 12);
            std::memcpy(reinterpret_cast<char*>(carRaw + 32), "Verstappen", 10);
            std::memcpy(reinterpret_cast<char*>(carRaw + 96), "RedBull", 7);

            acExt.update();
            expect(acExt.hasData());
            expect(acExt.opponents().size() == 1);
            expect(acExt.opponents()[0].carId == 1);
            expect(acExt.opponents()[0].driverName == "Verstappen");
            expect(acExt.opponents()[0].teamName == "RedBull");
            expect(acExt.opponents()[0].position == 2);
            expect(acExt.opponents()[0].speedKmh.value_or(0.0) == 215.0);
            expect(acExt.opponents()[0].worldPosition.has_value());
            expect(acExt.opponents()[0].worldPosition.value()[0] == -2.5);
            expect(std::abs(acExt.gapAhead().value_or(0.0) - 1.2) < 0.01);
            expect(std::abs(acExt.gapBehind().value_or(0.0) - 2.4) < 0.01);
            expect(acExt.opponentAhead().value_or("") == "Verstappen");
            expect(acExt.opponentBehind().value_or("") == "Leclerc");
            expect(acExt.playerSectors().has_value());
            expect(acExt.brakeTemperatures().has_value());

            // Test Spotter with opponents received from AcExtensionClient!
            spotter.reset();
            spotterState.opponents = acExt.opponents();
            auto extSpotterEvents = spotter.process(spotterState);
            expect(extSpotterEvents.empty());
            extSpotterEvents = spotter.process(spotterState);
            expect(extSpotterEvents.size() == 1);
            expect(extSpotterEvents.front().type == EventType::CarLeft);
            expect(spotter.hasLeft());
        }
    }
    acExt.stop();

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
    expect(localDefaults.maximumTokens == 64);
    expect(localDefaults.timeoutMilliseconds == 30000);
    expect(!localDefaults.reasoning);

    // Verify reasoning JSON round-trip serialization
    QJsonObject savedJson;
    savedJson.insert(QStringLiteral("reasoning"), true);
    expect(savedJson.value(QStringLiteral("reasoning")).toBool(false) == true);
    savedJson.insert(QStringLiteral("reasoning"), false);
    expect(savedJson.value(QStringLiteral("reasoning")).toBool(true) == false);

    LLMManager llmTestManager(localDefaults, QString{});
    const QString prompt = llmTestManager.systemPrompt();
    expect(prompt.contains(QStringLiteral("SPEECH-TO-TEXT")));
    expect(prompt.contains(QStringLiteral("gap với xe chước bao nhiêu")));
    expect(prompt.contains(QStringLiteral("lốp chước chái thế nào")));
    expect(prompt.contains(QStringLiteral("pit láp này không")));
    expect(prompt.contains(QStringLiteral("If a tool returns available: false, do not call other tools")));
    expect(prompt.contains(QStringLiteral("MUST call the relevant tool")));
    expect(prompt.contains(QStringLiteral("major_damage=true")));
    expect(prompt.contains(QStringLiteral("affected_wheels")));
    expect(prompt.contains(QStringLiteral("raw simulator levels")));
    expect(prompt.contains(QStringLiteral("_mmss")));
    expect(prompt.contains(QStringLiteral("Minh Vũ")));
    llmTestManager.setDriverName(QStringLiteral("Tuấn Minh"));
    expect(llmTestManager.systemPrompt().contains(QStringLiteral("Tuấn Minh")));
    llmTestManager.setDriverName(QStringLiteral("Minh Vũ"));

    SettingsManager settingsManagerTest;
    expect(settingsManagerTest.driverName() == QStringLiteral("Minh Vũ"));
    settingsManagerTest.setDriverName(QStringLiteral("Tuấn Minh"));
    expect(settingsManagerTest.driverName() == QStringLiteral("Tuấn Minh"));
    settingsManagerTest.setDriverName(QStringLiteral("Minh Vũ"));

    expect(settingsManagerTest.responseStyle() == QStringLiteral("Tiêu chuẩn"));
    settingsManagerTest.setResponseStyle(QStringLiteral("Tối giản"));
    expect(settingsManagerTest.responseStyle() == QStringLiteral("Tối giản"));
    settingsManagerTest.setResponseStyle(QStringLiteral("Chi tiết"));
    expect(settingsManagerTest.responseStyle() == QStringLiteral("Chi tiết"));
    settingsManagerTest.setResponseStyle(QStringLiteral("Tiêu chuẩn"));
    expect(settingsManagerTest.responseStyle() == QStringLiteral("Tiêu chuẩn"));

    expect(llmTestManager.systemPrompt().contains(QStringLiteral("RESPONSE STYLE: STANDARD (Tiêu chuẩn)")));
    llmTestManager.setResponseStyle(QStringLiteral("Tối giản"));
    expect(llmTestManager.systemPrompt().contains(QStringLiteral("RESPONSE STYLE: MINIMAL (Tối giản)")));
    llmTestManager.setResponseStyle(QStringLiteral("Chi tiết"));
    expect(llmTestManager.systemPrompt().contains(QStringLiteral("RESPONSE STYLE: DETAILED (Chi tiết)")));
    llmTestManager.setResponseStyle(QStringLiteral("Tiêu chuẩn"));

    // Local model (race-engineer) reasoning OFF vs ON
    LlmSettings offSettings = localDefaults;
    offSettings.reasoning = false;
    const auto offReq = LLMManager::buildChatPayload(offSettings, QJsonArray{}, false, QJsonArray{});
    expect(offReq.value(QStringLiteral("reasoning_effort")).toString() == QStringLiteral("none"));
    expect(offReq.value(QStringLiteral("reasoning_budget")).toInt() == 0);
    expect(!offReq.value(QStringLiteral("chat_template_kwargs")).toObject()
                .value(QStringLiteral("enable_thinking")).toBool());

    LlmSettings onSettings = localDefaults;
    onSettings.reasoning = true;
    const auto onReq = LLMManager::buildChatPayload(onSettings, QJsonArray{}, false, QJsonArray{});
    expect(onReq.value(QStringLiteral("reasoning_effort")).toString() == QStringLiteral("low"));
    expect(onReq.value(QStringLiteral("reasoning_budget")).toInt() == 128);
    expect(onReq.value(QStringLiteral("chat_template_kwargs")).toObject()
               .value(QStringLiteral("enable_thinking")).toBool());

    // Google Gemini reasoning OFF vs ON
    LlmSettings geminiOff = localDefaults;
    geminiOff.baseUrl = QStringLiteral("https://generativelanguage.googleapis.com/v1beta/openai/");
    geminiOff.model = QStringLiteral("gemini-2.5-flash");
    geminiOff.reasoning = false;
    const auto geminiOffReq = LLMManager::buildChatPayload(geminiOff, QJsonArray{}, false, QJsonArray{});
    expect(geminiOffReq.value(QStringLiteral("reasoning_effort")).toString() == QStringLiteral("none"));

    LlmSettings geminiOn = geminiOff;
    geminiOn.reasoning = true;
    const auto geminiOnReq = LLMManager::buildChatPayload(geminiOn, QJsonArray{}, false, QJsonArray{});
    expect(geminiOnReq.value(QStringLiteral("reasoning_effort")).toString() == QStringLiteral("low"));

    // Google Gemma thinking_config OFF vs ON
    LlmSettings gemmaOff = geminiOff;
    gemmaOff.model = QStringLiteral("gemma-4-31b-it");
    gemmaOff.reasoning = false;
    const auto gemmaOffReq = LLMManager::buildChatPayload(gemmaOff, QJsonArray{}, false, QJsonArray{});
    expect(gemmaOffReq.value(QStringLiteral("extra_body")).toObject()
               .value(QStringLiteral("google")).toObject()
               .value(QStringLiteral("thinking_config")).toObject()
               .value(QStringLiteral("thinking_level")).toString() == QStringLiteral("minimal"));

    LlmSettings gemmaOn = gemmaOff;
    gemmaOn.reasoning = true;
    const auto gemmaOnReq = LLMManager::buildChatPayload(gemmaOn, QJsonArray{}, false, QJsonArray{});
    expect(!gemmaOnReq.value(QStringLiteral("extra_body")).toObject()
               .value(QStringLiteral("google")).toObject()
               .value(QStringLiteral("thinking_config")).toObject()
               .value(QStringLiteral("include_thoughts")).toBool());

    // Unsupported model (Ministral): keeps normal inference working without invalid reasoning fields
    LlmSettings mistralOff = localDefaults;
    mistralOff.model = QStringLiteral("ministral-3b-latest");
    mistralOff.reasoning = false;
    const auto mistralOffReq = LLMManager::buildChatPayload(mistralOff, QJsonArray{}, false, QJsonArray{});
    expect(!mistralOffReq.contains(QStringLiteral("reasoning_effort")));
    expect(!mistralOffReq.contains(QStringLiteral("extra_body")));

    LlmSettings mistralOn = mistralOff;
    mistralOn.reasoning = true;
    const auto mistralOnReq = LLMManager::buildChatPayload(mistralOn, QJsonArray{}, false, QJsonArray{});
    expect(!mistralOnReq.contains(QStringLiteral("reasoning_effort")));
    expect(!mistralOnReq.contains(QStringLiteral("extra_body")));

    SpotterEngine spotterFallback;
    expect(spotterFallback.process(lapState).empty());
    expect(!SpotterEngine::unavailableReason().empty());

    SettingsManager settingsMgr;
    expect(settingsMgr.tts().voice == QStringLiteral("Minh Đức"));
    TtsSettings ttsTest;
    ttsTest.backend = QStringLiteral("VieNeu-TTS");
    settingsMgr.setTts(ttsTest);
    expect(settingsMgr.tts().backend == QStringLiteral("VieNeu-TTS"));
    expect(settingsMgr.tts().voice == QStringLiteral("Minh Đức"));
    ttsTest.voice = QStringLiteral("Mai Anh");
    settingsMgr.setTts(ttsTest);
    expect(settingsMgr.tts().voice == QStringLiteral("Mai Anh"));
    ttsTest.voice = QStringLiteral("Thái Sơn");
    settingsMgr.setTts(ttsTest);
    expect(settingsMgr.tts().voice == QStringLiteral("Thái Sơn"));
    ttsTest.voice = QStringLiteral("Minh Quân");
    settingsMgr.setTts(ttsTest);
    expect(settingsMgr.tts().voice == QStringLiteral("Minh Quân"));
    ttsTest.voice = QStringLiteral("Minh Đức");
    ttsTest.backend = QStringLiteral("Gwen-TTS");
    settingsMgr.setTts(ttsTest);
    expect(settingsMgr.tts().backend == QStringLiteral("Piper"));
    expect(settingsMgr.tts().voice == QStringLiteral("Minh Đức"));

    // RacingTextNormalizer tests
    expect(RacingTextNormalizer::normalize(QStringLiteral("Box, box, box!")) ==
           QStringLiteral("vào pít, vào pít, vào pít!"));
    expect(RacingTextNormalizer::normalize(QStringLiteral("Lap 5, vào box thay lốp.")) ==
           QStringLiteral("vòng năm, vào pít thay lốp."));
    expect(RacingTextNormalizer::normalize(QStringLiteral("P3 rồi, gap là +1.5s.")) ==
           QStringLiteral("vị trí ba rồi, khoảng cách là nhanh hơn một phẩy năm giây."));
    expect(RacingTextNormalizer::normalize(QStringLiteral("Lap time 1:42.350")) ==
           QStringLiteral("thời gian vòng một phút bốn mươi hai phẩy ba trăm năm mươi giây"));
    expect(RacingTextNormalizer::normalize(QStringLiteral("Tốc độ 250 km/h, áp suất 2.1 bar, nhiệt độ 95°C.")) ==
           QStringLiteral("Tốc độ hai trăm năm mươi ki lô mét trên giờ, áp suất hai phẩy một ba, nhiệt độ chín mươi lăm độ xê."));
    expect(RacingTextNormalizer::normalize(QStringLiteral("**Cảnh báo:** Bị understeer ở turn 4!")) ==
           QStringLiteral("Cảnh báo: Bị thiếu lái ở khúc cua bốn!"));
    // Number-to-Vietnamese-words edge cases
    expect(RacingTextNormalizer::normalize(QStringLiteral("75 độ xê")) ==
           QStringLiteral("bảy mươi lăm độ xê"));
    expect(RacingTextNormalizer::normalize(QStringLiteral("Còn 11 lít")) ==
           QStringLiteral("Còn mười một lít"));
    expect(RacingTextNormalizer::normalize(QStringLiteral("Vòng 101")) ==
           QStringLiteral("Vòng một trăm lẻ một"));
    // Decimal number handling
    expect(RacingTextNormalizer::normalize(QStringLiteral("Áp suất 2.15 bar")) ==
           QStringLiteral("Áp suất hai phẩy mười lăm ba"));
    // Additional units
    expect(RacingTextNormalizer::normalize(QStringLiteral("Áp lốp 210 kPa")) ==
           QStringLiteral("Áp lốp hai trăm mười ki lô pát can"));
    expect(RacingTextNormalizer::normalize(QStringLiteral("Động cơ 7500 rpm")) ==
           QStringLiteral("Động cơ bảy nghìn năm trăm vòng trên phút"));
    expect(RacingTextNormalizer::normalize(QStringLiteral("Mô-men 350 Nm")) ==
           QStringLiteral("Mô men ba trăm năm mươi niu tơn mét"));
    expect(RacingTextNormalizer::normalize(QStringLiteral("Nặng 1250 kg")) ==
           QStringLiteral("Nặng một nghìn hai trăm năm mươi ki lô gam"));
    expect(RacingTextNormalizer::normalize(QStringLiteral("Gầm 45 mm")) ==
           QStringLiteral("Gầm bốn mươi lăm mi li mét"));
    expect(RacingTextNormalizer::normalize(QStringLiteral("Nhiệt 180°F")) ==
           QStringLiteral("Nhiệt một trăm tám mươi độ ép"));
    // Decimals with leading zeros after decimal point
    expect(RacingTextNormalizer::normalize(QStringLiteral("Gap 0.05s")) ==
           QStringLiteral("khoảng cách không phẩy không năm giây"));
    expect(RacingTextNormalizer::normalize(QStringLiteral("Áp suất 2.05 bar")) ==
           QStringLiteral("Áp suất hai phẩy không năm ba"));
    expect(RacingTextNormalizer::normalize(QStringLiteral("Chênh lệch 0.005s")) ==
           QStringLiteral("Chênh lệch không phẩy không không năm giây"));
    expect(RacingTextNormalizer::normalize(QStringLiteral("Áp suất 2,5 bar")) ==
           QStringLiteral("Áp suất hai phẩy năm ba"));
    // Negative numbers
    expect(RacingTextNormalizer::normalize(QStringLiteral("Nhiệt độ ngoài trời -5°C")) ==
           QStringLiteral("Nhiệt độ ngoài trời âm năm độ xê"));
    expect(RacingTextNormalizer::normalize(QStringLiteral("Áp suất -2.1 bar")) ==
           QStringLiteral("Áp suất âm hai phẩy một ba"));
    // Large numbers and RPM
    expect(RacingTextNormalizer::normalize(QStringLiteral("Vòng tua 12000 rpm")) ==
           QStringLiteral("Vòng tua mười hai nghìn vòng trên phút"));
    // Additional racing units
    expect(RacingTextNormalizer::normalize(QStringLiteral("Tiêu thụ 2.8 L/lap")) ==
           QStringLiteral("Tiêu thụ hai phẩy tám lít trên vòng"));
    expect(RacingTextNormalizer::normalize(QStringLiteral("Công suất 500 hp")) ==
           QStringLiteral("Công suất năm trăm mã lực"));
    expect(RacingTextNormalizer::normalize(QStringLiteral("Lực phanh 1.8G")) ==
           QStringLiteral("Lực phanh một phẩy tám Gờ"));
    expect(RacingTextNormalizer::normalize(QStringLiteral("Khoảng cách 15 km")) ==
           QStringLiteral("Khoảng cách mười lăm ki lô mét"));
    expect(RacingTextNormalizer::normalize(QStringLiteral("Cách đích 200m")) ==
           QStringLiteral("Cách đích hai trăm mét"));
    expect(RacingTextNormalizer::normalize(QStringLiteral("Độ trễ 150 ms")) ==
           QStringLiteral("Độ trễ một trăm năm mươi mi li giây"));
    expect(RacingTextNormalizer::normalize(QStringLiteral("Điện áp 12.5V")) ==
           QStringLiteral("Điện áp mười hai phẩy năm Vôn"));
    expect(RacingTextNormalizer::normalize(QStringLiteral("Góc lái 45°")) ==
           QStringLiteral("Góc lái bốn mươi lăm độ"));

    // AudioDucker tests
    AudioDucker ducker;
    expect(ducker.isEnabled());
    expect(std::abs(ducker.duckFactor() - 0.25f) < 0.001f);
    expect(!ducker.isDucked());
    ducker.setDucked(true);
    expect(ducker.isDucked());
    ducker.setDucked(false);
    expect(!ducker.isDucked());
    ducker.setDuckFactor(1.5f);
    expect(std::abs(ducker.duckFactor() - 0.95f) < 0.001f);
    ducker.setDuckFactor(-0.5f);
    expect(std::abs(ducker.duckFactor() - 0.05f) < 0.001f);
    ducker.setDuckFactor(0.25f);
    ducker.setEnabled(false);
    expect(!ducker.isEnabled());
    ducker.setDucked(true);
    expect(!ducker.isDucked());
    ducker.setEnabled(true);
    expect(ducker.isEnabled());

    // SettingsManager Audio Ducking defaults & roundtrip
    expect(settingsMgr.tts().audioDucking == true);
    expect(std::abs(settingsMgr.tts().duckFactor - 0.25f) < 0.001f);
    TtsSettings duckSettings = settingsMgr.tts();
    duckSettings.audioDucking = false;
    duckSettings.duckFactor = 0.5f;
    settingsMgr.setTts(duckSettings);
    expect(!settingsMgr.tts().audioDucking);
    expect(std::abs(settingsMgr.tts().duckFactor - 0.5f) < 0.001f);

    return failures == 0 ? 0 : 1;
}
