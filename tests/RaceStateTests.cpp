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
#include "audio/MessageDispatcher.h"
#include "tts/ITtsBackend.h"
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
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>

class RadioTestBackend final : public raceengineer::ITtsBackend
{
public:
    bool isAvailable() const override { return true; }
    bool canSpeakCached(const QString&) const override { return true; }
    QString backendName() const override { return QStringLiteral("test"); }
    void warmUp() override {}
    void speak(const QString& text) override { current = text; emit speakingStarted(text); }
    void stop() override {
        const bool wasActive = !current.isEmpty();
        current.clear();
        if (wasActive) emit speakingFinished();
    }
    quint64 pauseSpeech() override {
        const auto token = ++nextToken;
        paused.insert(token, current);
        current.clear();
        return token;
    }
    bool resumeSpeech(quint64 token) override {
        if (!paused.contains(token)) return false;
        ++resumes;
        current = paused.take(token);
        emit speakingStarted(current);
        return true;
    }
    void discardSpeech(quint64 token) override { discarded += paused.remove(token); }
    QHash<quint64, QString> paused;
    quint64 nextToken{};
    QString current;
    int resumes{};
    int discarded{};
    void setVolume(float) override {}
    void setSpeed(float) override {}
    void setAudioOutputDevice(const QString&) override {}
    void finish() { current.clear(); emit speakingFinished(); }
};

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
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
    expect(emitted.empty());
    eventState.pitLimiter = false;
    expect(events.process(eventState, baseTime + std::chrono::milliseconds(4500)).empty());

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

    EventEngine damageEvents;
    RaceState damageEventState;
    damageEventState.connected = true;
    damageEventState.damage = std::array<double, 5>{0.0, 0.0, 0.0, 0.0, 0.0};
    damageEventState.suspensionDamage = WheelValues{0.0, 0.0, 0.0, 0.0};
    expect(damageEvents.process(damageEventState, baseTime).empty());
    damageEventState.damage = std::array<double, 5>{0.12, 0.0, 0.0, 0.0, 0.0};
    damageEventState.suspensionDamage = WheelValues{0.04, 0.0, 0.0, 0.0};
    emitted = damageEvents.process(damageEventState, baseTime + std::chrono::seconds(1));
    expect(emitted.size() == 1 && emitted.front().type == EventType::DamageDetected);
    expect(emitted.front().priority == EventPriority::Spotter);
    expect(emitted.front().message
        == "Phát hiện hư hại nhẹ ở phía trước, hư hại ở bánh trước trái.");
    expect(damageEvents.process(damageEventState, baseTime + std::chrono::seconds(2)).empty());
    damageEventState.damage = std::array<double, 5>{0.30, 0.0, 0.0, 0.0, 0.0};
    emitted = damageEvents.process(damageEventState, baseTime + std::chrono::seconds(3));
    expect(emitted.size() == 1 && emitted.front().type == EventType::DamageDetected);
    expect(emitted.front().message == "Phát hiện hư hại trung bình ở phía trước.");
    damageEventState.connected = false;
    expect(damageEvents.process(damageEventState, baseTime + std::chrono::seconds(4)).empty());

    EventEngine aggregateDamageEvents;
    RaceState aggregateDamageState;
    aggregateDamageState.connected = true;
    aggregateDamageState.damage = std::array<double, 5>{0.0, 0.0, 0.0, 0.0, 0.0};
    expect(aggregateDamageEvents.process(aggregateDamageState, baseTime).empty());
    aggregateDamageState.damage = std::array<double, 5>{0.0, 0.0, 0.0, 0.0, 0.12};
    emitted = aggregateDamageEvents.process(aggregateDamageState, baseTime + std::chrono::seconds(1));
    expect(emitted.size() == 1 && emitted.front().type == EventType::DamageDetected);
    expect(emitted.front().message == "Phát hiện hư hại mới trên xe.");

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
    {
        ToolRegistry featureTools;
        const QStringList ids{QStringLiteral("fuel_alerts"), QStringLiteral("tyre_alerts"),
            QStringLiteral("lap_delta"), QStringLiteral("flag_alerts"), QStringLiteral("damage_alerts")};
        QJsonArray allowed;
        for (const auto& entry : featureTools.definitions()) {
            const auto function = entry.toObject().value(QStringLiteral("function")).toObject();
            if (function.value(QStringLiteral("name")) == QStringLiteral("set_feature_enabled"))
                allowed = function.value(QStringLiteral("parameters")).toObject()
                    .value(QStringLiteral("properties")).toObject().value(QStringLiteral("feature")).toObject()
                    .value(QStringLiteral("enum")).toArray();
        }
        int changes = 0;
        featureTools.setFeatureControlHandlers({}, [&](const QString& id, bool enabled, quint64 revision) {
            ++changes;
            expect(ids.contains(id));
            expect(revision == 42);
            return QJsonObject{{QStringLiteral("success"), true}, {QStringLiteral("enabled"), enabled}};
        });
        for (const auto& id : ids) {
            expect(allowed.contains(id));
            for (const bool enabled : {false, true}) {
                const auto result = featureTools.execute(QStringLiteral("set_feature_enabled"), RaceState{}, history,
                    {{QStringLiteral("feature"), id}, {QStringLiteral("enabled"), enabled}}, {}, 42);
                expect(result.value(QStringLiteral("success")).toBool());
                expect(result.value(QStringLiteral("enabled")).toBool() == enabled);
            }
        }
        const auto invalid = featureTools.execute(QStringLiteral("set_feature_enabled"), RaceState{}, history,
            {{QStringLiteral("feature"), ids.front()}, {QStringLiteral("enabled"), QStringLiteral("false")}}, {}, 42);
        expect(!invalid.value(QStringLiteral("success")).toBool());
        expect(changes == 10);
    }

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
    spotterState.spotterGeometryFresh = true;
    spotterState.worldPosition = std::array<double, 3>{0.0, 0.0, 0.0};
    spotterState.spotterWorldPosition = spotterState.worldPosition;
    const WheelContactPoints playerWheels{{
        {-0.75, 0.0, 1.2}, {0.75, 0.0, 1.2},
        {-0.75, 0.0, -1.2}, {0.75, 0.0, -1.2}
    }};
    spotterState.spotterWheelContactPoints = playerWheels;
    const auto wheelsAt = [&](const double x) {
        auto points = playerWheels;
        for (auto& point : points) point[0] += x;
        return points;
    };
    const auto spotterBase = std::chrono::steady_clock::now();

    expect(spotter.process(spotterState, spotterBase).empty());

    OpponentState oppLeft;
    oppLeft.carId = 1;
    oppLeft.worldPosition = std::array<double, 3>{-3.0, 0.0, 0.0};
    oppLeft.spotterWheelContactPoints = wheelsAt(-3.0);
    spotterState.spotterOpponents = {oppLeft};

    auto spotterEvents = spotter.process(spotterState, spotterBase);
    expect(spotterEvents.empty());
    spotterEvents = spotter.process(spotterState, spotterBase + std::chrono::milliseconds(99));
    expect(spotterEvents.empty());
    spotterEvents = spotter.process(spotterState, spotterBase + std::chrono::milliseconds(100));
    expect(spotterEvents.size() == 1);
    expect(spotterEvents.front().type == EventType::CarLeft);
    expect(spotterEvents.front().priority == EventPriority::Spotter);
    expect(spotter.hasLeft());
    expect(!spotter.hasRight());

    OpponentState farRight;
    farRight.carId = 3;
    farRight.worldPosition = std::array<double, 3>{3.55, 0.0, 0.0};
    farRight.spotterWheelContactPoints = wheelsAt(3.55);
    spotterState.spotterOpponents = {oppLeft, farRight};
    expect(spotter.process(spotterState, spotterBase + std::chrono::milliseconds(150)).empty());
    expect(spotter.process(spotterState, spotterBase + std::chrono::milliseconds(250)).empty());
    expect(!spotter.hasRight());

    spotterEvents = spotter.process(spotterState, spotterBase + std::chrono::milliseconds(300));
    expect(spotterEvents.empty());

    OpponentState oppRight;
    oppRight.carId = 2;
    oppRight.worldPosition = std::array<double, 3>{1.8, 0.0, 0.0};
    oppRight.spotterWheelContactPoints = wheelsAt(1.8);
    spotterState.spotterOpponents = {oppLeft, oppRight};

    spotterEvents = spotter.process(spotterState, spotterBase + std::chrono::milliseconds(300));
    expect(spotterEvents.empty());
    spotterEvents = spotter.process(spotterState, spotterBase + std::chrono::milliseconds(500));
    expect(spotterEvents.size() == 1);
    expect(spotterEvents.front().type == EventType::CarRight);
    expect(spotter.hasLeft());
    expect(spotter.hasRight());
    spotterState.spotterOpponents.clear();
    expect(spotter.process(spotterState, spotterBase + std::chrono::milliseconds(600)).empty());
    expect(spotter.process(spotterState, spotterBase + std::chrono::milliseconds(2099)).empty());
    spotterEvents = spotter.process(spotterState, spotterBase + std::chrono::milliseconds(2200));
    expect(spotterEvents.empty());
    expect(!spotter.hasLeft());
    expect(!spotter.hasRight());

    // Each new approach can produce one callout after the side clears.
    spotterState.spotterOpponents = {oppLeft};
    expect(spotter.process(spotterState, spotterBase + std::chrono::milliseconds(2300)).empty());
    spotterEvents = spotter.process(spotterState, spotterBase + std::chrono::milliseconds(2500));
    expect(spotter.hasLeft());
    expect(spotterEvents.size() == 1 && spotterEvents.front().type == EventType::CarLeft);

    const auto geometryTestBase = spotterBase + std::chrono::milliseconds(3000);
    RaceState geometryState = spotterState;
    geometryState.speedKmh.reset();

    // The warning window covers approach before the unexpanded footprints overlap.
    OpponentState approaching = oppLeft;
    approaching.worldPosition = std::array<double, 3>{-1.8, 0.0, 3.9};
    approaching.spotterWheelContactPoints = playerWheels;
    for (auto& point : *approaching.spotterWheelContactPoints) {
        point[0] -= 1.8;
        point[2] += 3.9;
    }
    geometryState.spotterOpponents = {approaching};
    SpotterEngine approachSpotter;
    expect(approachSpotter.process(geometryState, geometryTestBase).empty());
    expect(approachSpotter.process(geometryState,
        geometryTestBase + std::chrono::milliseconds(99)).empty());
    spotterEvents = approachSpotter.process(geometryState,
        geometryTestBase + std::chrono::milliseconds(100));
    expect(spotterEvents.size() == 1 && spotterEvents.front().type == EventType::CarLeft);

    // Side-specific hysteresis keeps a car through the exit margin, then clears after 250 ms.
    SpotterEngine hysteresisSpotter;
    geometryState.spotterOpponents = {oppLeft};
    expect(hysteresisSpotter.process(geometryState, geometryTestBase).empty());
    spotterEvents = hysteresisSpotter.process(geometryState,
        geometryTestBase + std::chrono::milliseconds(100));
    expect(spotterEvents.size() == 1 && spotterEvents.front().type == EventType::CarLeft);
    OpponentState insideExitMargin = oppLeft;
    insideExitMargin.worldPosition = std::array<double, 3>{-3.55, 0.0, 0.0};
    insideExitMargin.spotterWheelContactPoints = wheelsAt(-3.55);
    geometryState.spotterOpponents = {insideExitMargin};
    expect(hysteresisSpotter.process(geometryState,
        geometryTestBase + std::chrono::milliseconds(200)).empty());
    expect(hysteresisSpotter.hasLeft());
    OpponentState outsideExitMargin = insideExitMargin;
    outsideExitMargin.worldPosition = std::array<double, 3>{-3.7, 0.0, 0.0};
    outsideExitMargin.spotterWheelContactPoints = wheelsAt(-3.7);
    geometryState.spotterOpponents = {outsideExitMargin};
    expect(hysteresisSpotter.process(geometryState,
        geometryTestBase + std::chrono::milliseconds(400)).empty());
    expect(hysteresisSpotter.process(geometryState,
        geometryTestBase + std::chrono::milliseconds(649)).empty());
    expect(hysteresisSpotter.hasLeft());
    expect(hysteresisSpotter.process(geometryState,
        geometryTestBase + std::chrono::milliseconds(650)).empty());
    expect(!hysteresisSpotter.hasLeft());

    // A rotated opponent remains detectable on the correct side.
    OpponentState rotatedRight = oppRight;
    rotatedRight.worldPosition = std::array<double, 3>{1.8, 0.0, 0.0};
    rotatedRight.spotterWheelContactPoints = playerWheels;
    for (auto& point : *rotatedRight.spotterWheelContactPoints) {
        const double x = point[0];
        point[0] = 1.8 + point[2];
        point[2] = -x;
    }
    geometryState.spotterOpponents = {rotatedRight};
    SpotterEngine rotatedSpotter;
    expect(rotatedSpotter.process(geometryState, geometryTestBase).empty());
    spotterEvents = rotatedSpotter.process(geometryState,
        geometryTestBase + std::chrono::milliseconds(100));
    expect(spotterEvents.size() == 1 && spotterEvents.front().type == EventType::CarRight);

    // A wider valid opponent is counted, while malformed and stale geometry is reported.
    OpponentState wideOpponent = oppLeft;
    wideOpponent.spotterWheelContactPoints = playerWheels;
    for (std::size_t i = 0; i < wideOpponent.spotterWheelContactPoints->size(); ++i) {
        (*wideOpponent.spotterWheelContactPoints)[i][0] = i % 2 == 0 ? -0.95 : 0.95;
    }
    geometryState.spotterOpponents = {wideOpponent};
    SpotterEngine geometryStatusSpotter;
    expect(geometryStatusSpotter.process(geometryState, geometryTestBase).empty());
    expect(geometryStatusSpotter.usableOpponentCount() == 1);
    expect(!geometryStatusSpotter.hasInvalidGeometry());

    OpponentState overpass = oppLeft;
    overpass.worldPosition = std::array<double, 3>{-3.0, 2.0, 0.0};
    overpass.spotterWheelContactPoints = wheelsAt(-3.0);
    for (auto& point : *overpass.spotterWheelContactPoints) point[1] += 2.0;
    geometryState.spotterOpponents = {overpass};
    SpotterEngine bridgeSpotter;
    expect(bridgeSpotter.process(geometryState, geometryTestBase).empty());
    expect(bridgeSpotter.usableOpponentCount() == 1);

    OpponentState invalidOpponent = oppLeft;
    invalidOpponent.spotterWheelContactPoints = WheelContactPoints{};
    geometryState.spotterOpponents = {invalidOpponent};
    expect(geometryStatusSpotter.process(geometryState,
        geometryTestBase + std::chrono::milliseconds(1)).empty());
    expect(geometryStatusSpotter.usableOpponentCount() == 0);
    expect(geometryStatusSpotter.hasInvalidGeometry());
    geometryState.spotterOpponents = {oppLeft};
    geometryState.pitState = PitState::PitLane;
    SpotterEngine pitSpotter;
    expect(pitSpotter.process(geometryState, geometryTestBase).empty());
    expect(!pitSpotter.hasLeft() && pitSpotter.usableOpponentCount() == 0);
    geometryState.pitState = PitState::Track;
    geometryState.spotterGeometryFresh = false;
    geometryState.spotterGeometryStale = true;
    SpotterEngine staleSpotter;
    expect(staleSpotter.process(geometryState, geometryTestBase).empty());
    expect(!staleSpotter.hasLeft() && staleSpotter.usableOpponentCount() == 0);

    // Spotter geometry remains active at low speed.
    spotter.reset();
    spotterState.speedKmh = 10.0;
    expect(spotter.process(spotterState, spotterBase + std::chrono::milliseconds(2600)).empty());
    spotterEvents = spotter.process(spotterState, spotterBase + std::chrono::milliseconds(2800));
    expect(spotterEvents.size() == 1 && spotterEvents.front().type == EventType::CarLeft);
    expect(spotter.hasLeft());
    spotterState.speedKmh.reset();

    AcExtensionClient acExt;
    expect(acExt.start(19996));
    expect(!acExt.hasData());
    expect(acExt.opponents().empty());
    expect(!acExt.playerPosition().has_value());
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
            int32_t numCars = 2;
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

            // Car 0 is the player record; the native client consumes its live position.
            uint8_t* carRaw = raw + 188;
            int32_t playerId = 0;
            int32_t playerPos = 5;
            std::memcpy(carRaw, &playerId, 4);
            std::memcpy(carRaw + 4, &playerPos, 4);
            carRaw += 160;

            // Car 1: id=1, pos=2, speed=215.0, lastLap=92.5, bestLap=91.8, coords={-2.5, 0.0, 1.0}, driver="Verstappen", car="RedBull"
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
            expect(acExt.playerPosition().value_or(0) == 5);
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
            auto extSpotterEvents = spotter.process(spotterState, spotterBase);
            expect(extSpotterEvents.empty());
            extSpotterEvents = spotter.process(spotterState,
                spotterBase + std::chrono::milliseconds(200));
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
    expect(LlmSettings{}.baseUrl.isEmpty());
    expect(OpenAICompatibleProvider::modelsEndpoint(
               QUrl(QStringLiteral("https://example.test/v1/")))
               == QUrl(QStringLiteral("https://example.test/v1/models")));
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
    expect(localDefaults.baseUrl.isEmpty());
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
    // Each UI announcement switch survives reload and filters only its own events.
    {
        const auto base = std::chrono::steady_clock::now();
        const auto checkSwitch = [&](auto getter, auto setter, const std::vector<EventType>& types) {
            const bool saved = (settingsManagerTest.*getter)();
            for (const bool enabled : {false, true}) {
                (settingsManagerTest.*setter)(enabled);
                SettingsManager reloaded;
                expect((reloaded.*getter)() == enabled);
                for (const auto type : types) expect(reloaded.eventEnabled(type) == enabled);
                expect(reloaded.eventEnabled(EventType::EngineCritical));
            }
            (settingsManagerTest.*setter)(saved);
        };
        checkSwitch(&SettingsManager::fuelAlertsEnabled, &SettingsManager::setFuelAlertsEnabled,
            {EventType::FuelLow, EventType::FuelCritical});
        checkSwitch(&SettingsManager::tyreAlertsEnabled, &SettingsManager::setTyreAlertsEnabled,
            {EventType::TyreOverheating});
        checkSwitch(&SettingsManager::lapDeltaEnabled, &SettingsManager::setLapDeltaEnabled,
            {EventType::NewBestLap, EventType::LapDelta});
        checkSwitch(&SettingsManager::flagAlertsEnabled, &SettingsManager::setFlagAlertsEnabled,
            {EventType::YellowFlag, EventType::BlueFlag, EventType::GreenFlag, EventType::RedFlag,
                EventType::BlackFlag, EventType::WhiteFlag, EventType::ChequeredFlag});
        checkSwitch(&SettingsManager::damageAlertsEnabled, &SettingsManager::setDamageAlertsEnabled,
            {EventType::DamageDetected});
        checkSwitch(&SettingsManager::spotterEnabled, &SettingsManager::setSpotterEnabled,
            {EventType::CarLeft, EventType::CarRight});

        EventEngine tyreEvents;
        RaceState tyreState;
        tyreState.connected = true;
        expect(tyreEvents.process(tyreState, base).empty());
        tyreState.tyreTemperaturesCelsius = WheelValues{90.0, 110.0, 95.0, 94.0};
        const auto hotTyres = tyreEvents.process(tyreState, base + std::chrono::seconds(1));
        expect(hotTyres.size() == 1 && hotTyres.front().type == EventType::TyreOverheating);
        expect(tyreEvents.process(tyreState, base + std::chrono::seconds(2)).empty());
        tyreEvents.reset();
        tyreState.tyreTemperaturesCelsius.reset();
        expect(tyreEvents.process(tyreState, base + std::chrono::seconds(35)).empty());

        EventEngine deltaEvents;
        RaceState deltaState;
        deltaState.connected = true;
        deltaState.currentLap = 3;
        deltaState.bestLapTimeSeconds = 90.0;
        expect(deltaEvents.process(deltaState, base).empty());
        deltaState.currentLap = 4;
        deltaState.previousLapTimeSeconds = 92.5;
        const auto lapDelta = deltaEvents.process(deltaState, base + std::chrono::seconds(90));
        expect(lapDelta.size() == 1 && lapDelta.front().type == EventType::LapDelta);
        expect(!lapDelta.empty() && lapDelta.front().message.find("2.5") != std::string::npos);
        expect(deltaEvents.process(deltaState, base + std::chrono::seconds(91)).empty());
        deltaState.currentLap = 6;
        expect(deltaEvents.process(deltaState, base + std::chrono::seconds(180)).empty());
    }
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
    for (const int threads : {2, 3, 4, 6, 5}) {
        ttsTest.cpuThreads = threads;
        settingsMgr.setTts(ttsTest);
        const int expectedThreads = threads == 5 || threads == 6 ? 4 : threads;
        expect(settingsMgr.tts().cpuThreads == expectedThreads);
        SettingsManager reloadedThreads;
        expect(reloadedThreads.tts().cpuThreads == expectedThreads);
    }
    ttsTest.cpuThreads = 4;
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
    ttsTest.backend = QStringLiteral("Legacy-TTS");
    settingsMgr.setTts(ttsTest);
    expect(settingsMgr.tts().backend == QStringLiteral("VieNeu-TTS"));
    expect(settingsMgr.tts().voice == QStringLiteral("Minh Đức"));

    // RacingTextNormalizer tests
    expect(RacingTextNormalizer::normalize(QStringLiteral("Box, box, box!")) ==
           QStringLiteral("vào pít, vào pít, vào pít!"));
    expect(RacingTextNormalizer::normalize(QStringLiteral("Lap 5, vào box thay lốp.")) ==
           QStringLiteral("vòng năm, vào pít thay lốp."));
    expect(RacingTextNormalizer::normalize(QStringLiteral("P3 rồi, gap là +1.5s.")) ==
           QStringLiteral("pê ba rồi, khoảng cách là nhanh hơn một phẩy năm giây."));
    expect(RacingTextNormalizer::normalize(QStringLiteral("Xe đang ở p9.")) ==
           QStringLiteral("Xe đang ở pê chín."));
    expect(RacingTextNormalizer::normalize(QStringLiteral("Vị trí P9")) ==
           QStringLiteral("Vị trí pê chín"));
    expect(RacingTextNormalizer::normalize(QStringLiteral("Hạng p12")) ==
           QStringLiteral("Hạng pê mười hai"));
    expect(RacingTextNormalizer::normalize(QStringLiteral("Lap time 1:42.350")) ==
           QStringLiteral("thời gian vòng một phút bốn mươi hai phẩy ba trăm năm mươi giây"));
    expect(RacingTextNormalizer::normalize(QStringLiteral("Tốc độ 250 km/h, áp suất 2.1 bar, nhiệt độ 95°C.")) ==
           QStringLiteral("Tốc độ hai trăm năm mươi ki lô mét trên giờ, áp suất hai phẩy một ba, nhiệt độ chín mươi lăm độ xê."));
    expect(RacingTextNormalizer::normalize(QStringLiteral("**Cảnh báo:** Bị understeer ở turn 4!")) ==
           QStringLiteral("Cảnh báo: Bị ân đờ stia ở khúc cua bốn!"));
    expect(RacingTextNormalizer::normalize(
               QStringLiteral("tyre, brake, fuel, engine, DRS, ERS, understeer, oversteer")) ==
           QStringLiteral("thai, brây k, phiu ồ, en jin, đi a rờ ét, i a rờ ét, ân đờ stia, ô vờ stia"));
    expect(RacingTextNormalizer::normalize(QStringLiteral("Radio check, Minh.")) ==
           QStringLiteral("ra đi ô check, Minh."));
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

    {
        RadioTestBackend radioBackend;
        RadioTestBackend localRadioBackend;
        MessageDispatcher radio(&radioBackend);
        radio.setLocalBackend(&localRadioBackend);
        QStringList spoken;
        ITtsBackend* activeRadioBackend = &radioBackend;
        QObject::connect(&radioBackend, &ITtsBackend::speakingStarted, &app,
            [&spoken, &activeRadioBackend, &radioBackend](const QString& text) {
                activeRadioBackend = &radioBackend;
                spoken.append(text);
            });
        QObject::connect(&localRadioBackend, &ITtsBackend::speakingStarted, &app,
            [&spoken, &activeRadioBackend, &localRadioBackend](const QString& text) {
                activeRadioBackend = &localRadioBackend;
                spoken.append(text);
            });
        const auto waitForRadio = [](int milliseconds) {
            QEventLoop loop;
            QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
            loop.exec();
        };
        const auto finishRadio = [&] {
            static_cast<RadioTestBackend*>(activeRadioBackend)->finish();
            waitForRadio(300);
        };
        const QString firstSummary = QStringLiteral("Lap 1 summary");
        const QString latestSummary = QStringLiteral("Lap 2 summary");
        const QString bestLapAlert = QStringLiteral("New best lap");
        const QString safetyAlert = QStringLiteral("Yellow flag");

        // A best-lap event on the same telemetry tick must not lose the summary.
        radio.enqueue(firstSummary, EventPriority::Conversation, MessageSource::LapSummary);
        radio.enqueue(bestLapAlert, EventPriority::Engineer, MessageSource::LapDelta);
        waitForRadio(120);
        expect(spoken == QStringList({firstSummary, bestLapAlert}));
        finishRadio();
        expect(spoken == QStringList({firstSummary, bestLapAlert, firstSummary}));
        finishRadio();

        expect(radioBackend.resumes == 1);
        // Nested interruptions: critical -> still-valid Spotter -> remaining conversation.
        radio.setProximitySpotterState(true, false);
        const auto beforeNested = spoken.size();
        radio.enqueue(QStringLiteral("Conversation remainder"), EventPriority::Conversation);
        radio.enqueue(QStringLiteral("Left still occupied"), EventPriority::Spotter,
            MessageSource::ProximitySpotter, EventType::CarLeft);
        radio.enqueue(QStringLiteral("Critical engine"), EventPriority::Critical, MessageSource::EngineAlerts);
        expect(spoken.last() == QStringLiteral("Critical engine"));
        finishRadio();
        expect(spoken.last() == QStringLiteral("Left still occupied"));
        expect(localRadioBackend.resumes == 1);
        finishRadio();
        expect(spoken.last() == QStringLiteral("Conversation remainder"));
        expect(spoken.size() == beforeNested + 5);
        finishRadio();
        expect(radioBackend.paused.isEmpty() && localRadioBackend.paused.isEmpty());

        // An obsolete Spotter continuation must be discarded, not resumed.
        radio.enqueue(QStringLiteral("Left again"), EventPriority::Spotter,
            MessageSource::ProximitySpotter, EventType::CarLeft);
        radio.enqueue(QStringLiteral("Critical fuel"), EventPriority::Critical, MessageSource::FuelAlerts);
        radio.setProximitySpotterState(false, false);
        const auto afterObsolete = spoken.size();
        finishRadio();
        expect(spoken.size() == afterObsolete);
        expect(localRadioBackend.paused.isEmpty() && localRadioBackend.discarded == 1);

        // Keep ordinary waiting speech, and resume the interrupted FIFO head first.
        const auto beforeFifo = spoken.size();
        radio.enqueue(QStringLiteral("FIFO head"), EventPriority::Conversation);
        radio.enqueue(QStringLiteral("FIFO waiting"), EventPriority::Conversation);
        radio.enqueue(QStringLiteral("Urgent FIFO alert"), EventPriority::Critical, MessageSource::FlagAlerts);
        finishRadio();
        expect(spoken.last() == QStringLiteral("FIFO head"));
        radio.enqueue(QStringLiteral("Second urgent FIFO alert"), EventPriority::Critical, MessageSource::EngineAlerts);
        finishRadio();
        expect(spoken.last() == QStringLiteral("FIFO head"));
        finishRadio();
        expect(spoken.last() == QStringLiteral("FIFO waiting"));
        finishRadio();
        expect(spoken.size() == beforeFifo + 6);

        radio.enqueue(QStringLiteral("Cancelled remainder"), EventPriority::Conversation);
        radio.enqueue(QStringLiteral("Cancel guard alert"), EventPriority::Critical, MessageSource::FlagAlerts);
        radio.cancelByPrefix(QStringLiteral("Cancelled"));
        const auto afterRemainderCancel = spoken.size();
        finishRadio();
        expect(spoken.size() == afterRemainderCancel && radioBackend.paused.isEmpty());

        // Preserve a waiting summary through safety preemption, keeping only the latest lap.
        radio.enqueue(QStringLiteral("Car left"), EventPriority::Spotter, MessageSource::ProximitySpotter);
        radio.enqueue(firstSummary, EventPriority::Conversation, MessageSource::LapSummary);
        radio.enqueue(latestSummary, EventPriority::Conversation, MessageSource::LapSummary);
        radio.enqueue(safetyAlert, EventPriority::Critical, MessageSource::FlagAlerts);
        waitForRadio(120);
        expect(spoken.last() == safetyAlert);
        finishRadio();
        expect(spoken.last() == latestSummary);
        finishRadio();

        // Do not replay an interrupted older lap when a newer summary is waiting.
        const auto beforeLatestLap = spoken.size();
        radio.enqueue(QStringLiteral("Lap 3 summary"), EventPriority::Conversation, MessageSource::LapSummary);
        radio.enqueue(QStringLiteral("Lap 4 summary"), EventPriority::Conversation, MessageSource::LapSummary);
        radio.enqueue(safetyAlert, EventPriority::Critical, MessageSource::FlagAlerts);
        waitForRadio(120);
        finishRadio();
        expect(spoken.last() == QStringLiteral("Lap 4 summary"));
        finishRadio();
        expect(spoken.size() == beforeLatestLap + 3);
        radio.enqueue(QStringLiteral("Lap 5 summary"), EventPriority::Conversation, MessageSource::LapSummary);
        radio.cancelBySource(MessageSource::LapSummary);
        const auto spokenAfterCancel = spoken;
        waitForRadio(300);
        expect(spoken == spokenAfterCancel);
        radio.enqueue(QStringLiteral("Car right"), EventPriority::Spotter, MessageSource::ProximitySpotter);
        radio.enqueue(QStringLiteral("Lap 6 summary"), EventPriority::Conversation, MessageSource::LapSummary);
        radio.cancelBySource(MessageSource::LapSummary);
        finishRadio();
        expect(spoken.last() == QStringLiteral("Car right"));
        const int spokenBeforeLocalSystem = spoken.size();
        const QString apiUnavailable = QStringLiteral("Google TTS unavailable");
        radio.enqueue(apiUnavailable, EventPriority::Conversation, MessageSource::LocalSystem);
        waitForRadio(120);
        expect(spoken.size() == spokenBeforeLocalSystem + 1);
        expect(spoken.last() == apiUnavailable);
        expect(activeRadioBackend == &localRadioBackend);
        radio.enqueue(apiUnavailable, EventPriority::Conversation, MessageSource::LocalSystem);
        expect(spoken.size() == spokenBeforeLocalSystem + 1);
        finishRadio();
        radio.setConversationSuppressed(true);
        const auto beforeBenchmark = spoken.size();
        radio.enqueue(QStringLiteral("Benchmark conversation"), EventPriority::Conversation);
        waitForRadio(120);
        expect(spoken.size() == beforeBenchmark);
        radio.setConversationSuppressed(false);
        radio.enqueue(QStringLiteral("Before benchmark"), EventPriority::Conversation);
        radio.enqueue(QStringLiteral("Pending benchmark conversation"), EventPriority::Conversation);
        static_cast<RadioTestBackend*>(activeRadioBackend)->finish();
        waitForRadio(50); // Finished, but the existing 250 ms cadence timer has not drained the queue.
        radio.setConversationSuppressed(true);
        const auto beforeQueuedBenchmark = spoken.size();
        waitForRadio(300);
        expect(spoken.size() == beforeQueuedBenchmark);
        radio.enqueue(QStringLiteral("Cached safety during benchmark"), EventPriority::Conversation, MessageSource::LocalSystem);
        waitForRadio(120);
        expect(spoken.last() == QStringLiteral("Cached safety during benchmark"));
        finishRadio();
        radio.setConversationSuppressed(false);
        radio.enqueue(QStringLiteral("Clear interrupted conversation"), EventPriority::Conversation);
        radio.enqueue(QStringLiteral("Clear urgent alert"), EventPriority::Critical, MessageSource::FlagAlerts);
        radio.clear();
        expect(radioBackend.paused.isEmpty() && localRadioBackend.paused.isEmpty());
        bool speakingAfterClear = false;
        QObject::connect(&radio, &MessageDispatcher::speakingChanged, &app,
            [&](bool speaking, const QString&) { speakingAfterClear = speaking; });
        radio.enqueue(QStringLiteral("New conversation after clear"), EventPriority::Conversation);
        waitForRadio(300);
        expect(speakingAfterClear && radioBackend.current == QStringLiteral("New conversation after clear"));
        radio.clear();
    }

    return failures == 0 ? 0 : 1;
}
