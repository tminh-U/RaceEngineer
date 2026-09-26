#pragma once

#include "telemetry/common/RaceState.h"
#include "race/RaceHistory.h"
#include "events/EventEngine.h"
#include "spotter/SpotterEngine.h"
#include "config/SettingsManager.h"
#include "ai/LocalAiRuntime.h"
#include "strategy/StrategyPredictor.h"
#include "strategy/StrategyRecorder.h"

#include <QObject>
#include <QJsonObject>
#include <QByteArray>
#include <QThread>
#include <QVariantMap>
#include <QStringList>

#include <memory>

class QAudioOutput;
class QMediaPlayer;

namespace raceengineer {

class TelemetryManager;
class VoiceInputController;
class WhisperRecognizer;
class LLMManager;
class ITtsBackend;
class VieNeuTtsBackend;
class MessageDispatcher;
class DInputButtonMonitor;
class AudioDucker;

class Application final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString simulatorName READ simulatorName NOTIFY connectionChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectionChanged)
    Q_PROPERTY(QString connectionText READ connectionText NOTIFY connectionChanged)
    Q_PROPERTY(QVariantMap telemetry READ telemetry NOTIFY telemetryChanged)
    Q_PROPERTY(bool mockAvailable READ mockAvailable CONSTANT)
    Q_PROPERTY(bool mockEnabled READ mockEnabled NOTIFY mockEnabledChanged)
    Q_PROPERTY(QString latestEvent READ latestEvent NOTIFY latestEventChanged)
    Q_PROPERTY(QVariantList eventLog READ eventLog NOTIFY latestEventChanged)
    Q_PROPERTY(QString voiceStatus READ voiceStatus NOTIFY voiceStatusChanged)
    Q_PROPERTY(QString microphoneName READ microphoneName NOTIFY microphoneChanged)
    Q_PROPERTY(float microphoneLevel READ microphoneLevel NOTIFY microphoneLevelChanged)
    Q_PROPERTY(QString latestUserText READ latestUserText NOTIFY interactionChanged)
    Q_PROPERTY(QString latestEngineerText READ latestEngineerText NOTIFY interactionChanged)
    Q_PROPERTY(QVariantList conversationLog READ conversationLog NOTIFY conversationLogChanged)
    Q_PROPERTY(QString driverName READ driverName WRITE setDriverName NOTIFY driverNameChanged)
    Q_PROPERTY(QString responseStyle READ responseStyle WRITE setResponseStyle NOTIFY responseStyleChanged)
    Q_PROPERTY(QString apiProvider READ apiProvider NOTIFY apiSettingsChanged)
    Q_PROPERTY(QString apiBaseUrl READ apiBaseUrl NOTIFY apiSettingsChanged)
    Q_PROPERTY(QString apiKey READ apiKey NOTIFY apiSettingsChanged)
    Q_PROPERTY(QString apiModel READ apiModel NOTIFY apiSettingsChanged)
    Q_PROPERTY(bool apiStreaming READ apiStreaming NOTIFY apiSettingsChanged)
    Q_PROPERTY(int apiTimeoutMilliseconds READ apiTimeoutMilliseconds NOTIFY apiSettingsChanged)
    Q_PROPERTY(int apiMaximumTokens READ apiMaximumTokens NOTIFY apiSettingsChanged)
    Q_PROPERTY(double apiTemperature READ apiTemperature NOTIFY apiSettingsChanged)
    Q_PROPERTY(bool apiReasoning READ apiReasoning NOTIFY apiSettingsChanged)
    Q_PROPERTY(bool apiConfigured READ apiConfigured NOTIFY apiSettingsChanged)
    Q_PROPERTY(QString apiState READ apiState NOTIFY apiStateChanged)
    Q_PROPERTY(QString apiDetail READ apiDetail NOTIFY apiStateChanged)
    Q_PROPERTY(QVariantMap apiStatistics READ apiStatistics NOTIFY apiStatisticsChanged)
    Q_PROPERTY(bool ttsAvailable READ ttsAvailable NOTIFY ttsStatusChanged)
    Q_PROPERTY(QString ttsBackend READ ttsBackend NOTIFY ttsStatusChanged)
    Q_PROPERTY(QString ttsStatus READ ttsStatus NOTIFY ttsStatusChanged)
    Q_PROPERTY(QVariantList availableTtsVoices READ availableTtsVoices NOTIFY availableTtsVoicesChanged)
    Q_PROPERTY(QString selectedTtsVoice READ selectedTtsVoice NOTIFY ttsVoiceChanged)
    Q_PROPERTY(QStringList audioOutputDevices READ audioOutputDevices NOTIFY audioOutputChanged)
    Q_PROPERTY(QString selectedAudioOutput READ selectedAudioOutput NOTIFY audioOutputChanged)
    Q_PROPERTY(QVariantList audioInputDevices READ audioInputDevices NOTIFY audioInputChanged)
    Q_PROPERTY(QString selectedAudioInput READ selectedAudioInput NOTIFY audioInputChanged)
    Q_PROPERTY(QString selectedAudioInputId READ selectedAudioInputId NOTIFY audioInputChanged)
    Q_PROPERTY(float ttsVolume READ ttsVolume NOTIFY ttsVolumeChanged)
    Q_PROPERTY(QVariantList toolLog READ toolLog NOTIFY toolLogChanged)
    Q_PROPERTY(bool keyboardPttEnabled READ keyboardPttEnabled NOTIFY pttSettingsChanged)
    Q_PROPERTY(bool directInputPttEnabled READ directInputPttEnabled NOTIFY pttSettingsChanged)
    Q_PROPERTY(QString directInputBinding READ directInputBinding NOTIFY pttSettingsChanged)
    Q_PROPERTY(QString directInputStatus READ directInputStatus NOTIFY directInputStatusChanged)
    Q_PROPERTY(bool audioDuckingEnabled READ audioDuckingEnabled WRITE setAudioDuckingEnabled NOTIFY audioDuckingEnabledChanged)
    Q_PROPERTY(bool isAudioDucked READ isAudioDucked NOTIFY audioDuckingStateChanged)
    Q_PROPERTY(bool startupReady READ startupReady NOTIFY startupChanged)
    Q_PROPERTY(double startupProgress READ startupProgress NOTIFY startupChanged)
    Q_PROPERTY(QString startupError READ startupError NOTIFY startupChanged)
    Q_PROPERTY(QVariantList aiComputeDevices READ aiComputeDevices NOTIFY aiComputeDevicesChanged)
    Q_PROPERTY(QString selectedAiComputeDevice READ selectedAiComputeDevice NOTIFY aiComputeSettingsChanged)
    Q_PROPERTY(QString aiComputeStatus READ aiComputeStatus NOTIFY aiComputeStatusChanged)
    Q_PROPERTY(bool aiComputeRestartRequired READ aiComputeRestartRequired NOTIFY aiComputeSettingsChanged)
    Q_PROPERTY(bool strategyEnabled READ strategyEnabled NOTIFY strategyChanged)
    Q_PROPERTY(bool strategyAvailable READ strategyAvailable NOTIFY strategyChanged)
    Q_PROPERTY(QString strategyStatus READ strategyStatus NOTIFY strategyChanged)
    Q_PROPERTY(int strategyPitLap READ strategyPitLap NOTIFY strategyChanged)
    Q_PROPERTY(QString strategyDetail READ strategyDetail NOTIFY strategyChanged)
    Q_PROPERTY(QObject* strategyData READ strategyData CONSTANT)

public:
    explicit Application(bool startWithMock, QObject* parent = nullptr);
    ~Application() override;

    [[nodiscard]] QString simulatorName() const { return simulatorName_; }
    [[nodiscard]] bool connected() const noexcept { return connected_; }
    [[nodiscard]] QString connectionText() const
    {
        return connected_
            ? QStringLiteral("Đã kết nối %1").arg(simulatorName_)
            : QStringLiteral("Chưa kết nối AC / ACC");
    }
    [[nodiscard]] QVariantMap telemetry() const { return telemetry_; }
    [[nodiscard]] bool mockAvailable() const noexcept;
    [[nodiscard]] bool mockEnabled() const noexcept { return mockEnabled_; }
    [[nodiscard]] QString latestEvent() const { return latestEvent_; }
    [[nodiscard]] QVariantList eventLog() const { return eventLog_; }
    [[nodiscard]] QString voiceStatus() const { return voiceStatus_; }
    [[nodiscard]] QString microphoneName() const { return microphoneName_; }
    [[nodiscard]] float microphoneLevel() const noexcept { return microphoneLevel_; }
    [[nodiscard]] QString latestUserText() const { return latestUserText_; }
    [[nodiscard]] QString latestEngineerText() const { return latestEngineerText_; }
    [[nodiscard]] QVariantList conversationLog() const { return conversationLog_; }
    [[nodiscard]] QString driverName() const { return settingsManager_.driverName(); }
    [[nodiscard]] QString responseStyle() const { return settingsManager_.responseStyle(); }
    [[nodiscard]] QString apiProvider() const { return settingsManager_.llm().provider; }
    [[nodiscard]] QString apiBaseUrl() const { return settingsManager_.llm().baseUrl; }
    [[nodiscard]] QString apiKey() const { return apiKey_; }
    [[nodiscard]] QString apiModel() const { return settingsManager_.llm().model; }
    [[nodiscard]] bool apiStreaming() const noexcept { return settingsManager_.llm().streaming; }
    [[nodiscard]] int apiTimeoutMilliseconds() const noexcept
    {
        return settingsManager_.llm().timeoutMilliseconds;
    }
    [[nodiscard]] int apiMaximumTokens() const noexcept { return settingsManager_.llm().maximumTokens; }
    [[nodiscard]] double apiTemperature() const noexcept { return settingsManager_.llm().temperature; }
    [[nodiscard]] bool apiReasoning() const noexcept { return settingsManager_.llm().reasoning; }
    [[nodiscard]] bool apiConfigured() const noexcept { return apiConfigured_; }
    [[nodiscard]] QString apiState() const { return apiState_; }
    [[nodiscard]] QString apiDetail() const { return apiDetail_; }
    [[nodiscard]] QVariantMap apiStatistics() const { return apiStatistics_; }
    [[nodiscard]] bool ttsAvailable() const noexcept { return ttsAvailable_; }
    [[nodiscard]] QString ttsBackend() const;
    [[nodiscard]] QString ttsStatus() const { return ttsStatus_; }
    [[nodiscard]] QVariantList aiComputeDevices() const;
    [[nodiscard]] QString selectedAiComputeDevice() const;
    [[nodiscard]] QString aiComputeStatus() const;
    [[nodiscard]] bool aiComputeRestartRequired() const;
    [[nodiscard]] bool strategyEnabled() const noexcept { return settingsManager_.strategyEnabled(); }
    [[nodiscard]] bool strategyAvailable() const noexcept
    { return strategyPredictor_ && strategyPredictor_->available(); }
    [[nodiscard]] QString strategyStatus() const { return strategyStatus_; }
    [[nodiscard]] int strategyPitLap() const noexcept { return strategyPitLap_; }
    [[nodiscard]] QString strategyDetail() const { return strategyDetail_; }
    Q_INVOKABLE void setStrategyEnabled(bool enabled);
    Q_INVOKABLE bool configureStrategySharing(const QString& endpoint, const QString& token);
    QObject* strategyData() { return &strategyRecorder_; }
    [[nodiscard]] QVariantList availableTtsVoices() const;
    [[nodiscard]] QString selectedTtsVoice() const { return settingsManager_.tts().voice; }
    [[nodiscard]] QStringList audioOutputDevices() const;
    [[nodiscard]] QString selectedAudioOutput() const;
    [[nodiscard]] QVariantList audioInputDevices() const { return audioInputDevices_; }
    [[nodiscard]] QString selectedAudioInput() const;
    [[nodiscard]] QString selectedAudioInputId() const;
    [[nodiscard]] float ttsVolume() const noexcept { return settingsManager_.tts().volume; }
    [[nodiscard]] QVariantList toolLog() const { return toolLog_; }
    [[nodiscard]] bool keyboardPttEnabled() const noexcept { return settingsManager_.pushToTalk().keyboardEnabled; }
    [[nodiscard]] bool directInputPttEnabled() const noexcept { return settingsManager_.pushToTalk().directInputEnabled; }
    [[nodiscard]] QString directInputBinding() const;
    [[nodiscard]] QString directInputStatus() const { return directInputStatus_; }
    [[nodiscard]] bool audioDuckingEnabled() const noexcept { return settingsManager_.tts().audioDucking; }
    [[nodiscard]] bool isAudioDucked() const noexcept;
    [[nodiscard]] bool startupReady() const noexcept { return startupReady_; }
    [[nodiscard]] double startupProgress() const noexcept
    {
        return (static_cast<int>(sttWarmUpReady_) + static_cast<int>(ttsWarmUpReady_)) / 2.0;
    }
    [[nodiscard]] QString startupError() const { return startupError_; }
    Q_INVOKABLE void setAudioDuckingEnabled(bool enabled);
    Q_INVOKABLE void retryStartup();

    Q_INVOKABLE void setUseMockTelemetry(bool enabled);
    Q_INVOKABLE void beginPushToTalk();
    Q_INVOKABLE void endPushToTalk();
    Q_INVOKABLE void saveAiSettings(const QString& provider, const QString& baseUrl,
        const QString& apiKey, const QString& model, bool streaming,
        int timeoutMilliseconds = 30000, int maximumTokens = 32, double temperature = 0.1,
        bool reasoning = false);
    Q_INVOKABLE void setApiReasoning(bool enabled);
    Q_INVOKABLE void testApiConnection(const QString& baseUrl = QString(),
        const QString& apiKey = QString(), const QString& model = QString());
    Q_INVOKABLE void askText(const QString& text);
    Q_INVOKABLE void resetConversation();
    Q_INVOKABLE void setTtsBackend(const QString& backend);
    Q_INVOKABLE void setAiComputeDevice(const QString& deviceId);
    Q_INVOKABLE void setTtsVoice(const QString& voice);
    Q_INVOKABLE void setAudioOutputDevice(const QString& description);
    Q_INVOKABLE void setAudioInputDevice(const QString& deviceId);
    Q_INVOKABLE void refreshAudioInputDevices();
    Q_INVOKABLE void setTtsVolume(double volume);
    Q_INVOKABLE void setPushToTalkOptions(bool keyboardEnabled, bool directInputEnabled);
    Q_INVOKABLE void beginDirectInputMapping();
    Q_INVOKABLE void setDriverName(const QString& name);
    Q_INVOKABLE void setResponseStyle(const QString& style);
    void startDirectInput(quintptr nativeWindowHandle);

signals:
    void connectionChanged();
    void telemetryChanged();
    void mockEnabledChanged();
    void latestEventChanged();
    void requestMockTelemetry(bool enabled);
    void requestBeginPushToTalk();
    void requestEndPushToTalk();
    void requestStartVoiceInput(const QByteArray& deviceId);
    void requestConfigureAudioInput(const QByteArray& deviceId);
    void voiceStatusChanged();
    void microphoneChanged();
    void microphoneLevelChanged();
    void interactionChanged();
    void conversationLogChanged();
    void driverNameChanged();
    void responseStyleChanged();
    void apiSettingsChanged();
    void apiStateChanged();
    void apiStatisticsChanged();
    void ttsStatusChanged();
    void aiComputeDevicesChanged();
    void aiComputeSettingsChanged();
    void aiComputeStatusChanged();
    void strategyChanged();
    void availableTtsVoicesChanged();
    void ttsVoiceChanged();
    void audioOutputChanged();
    void audioInputChanged();
    void ttsVolumeChanged();
    void toolLogChanged();
    void pttSettingsChanged();
    void directInputStatusChanged();
    void audioDuckingEnabledChanged();
    void audioDuckingStateChanged();
    void startupChanged();
    void requestStartDirectInput(quintptr nativeWindowHandle);
    void requestConfigureDirectInput(bool enabled, const QString& deviceGuid, int buttonIndex);
    void requestDirectInputMapping();
    void requestTranscription(const QByteArray& pcm16k, const QString& language);

private slots:
    void onStateUpdated(const raceengineer::RaceState& state);
    void onConnectionStatusChanged(const QString& simulator, bool connected);
    void onVoiceStatusChanged(const QString& status);
    void onUtteranceReady(const QByteArray& pcm16k);
    void onTranscriptionReady(const QString& text, const QString& detectedLanguage);
    void onSttWarmUpFinished(bool success, const QString& error);
    void onTtsWarmUpFinished(bool success, const QString& error);

private:
    static QVariantMap toVariantMap(const RaceState& state);
    void appendConversationMessage(const QString& role, const QString& text);
    void updateEngineerMessage(const QString& text);
    void finishStartupIfReady();
    void updateStrategy(const RaceState& state);
    [[nodiscard]] QJsonObject pitStrategyToolData() const;
    void setStrategyState(const QString& status, const QString& detail, int pitLap = 0);
    void announceStrategy(const QString& text, EventPriority priority);
    bool eventFilter(QObject* watched, QEvent* event) override;

    SettingsManager settingsManager_;
    LocalAiRuntimeSelection aiRuntimeSelection_;
    QThread telemetryThread_;
    QThread audioThread_;
    QThread sttThread_;
    QThread inputThread_;
    TelemetryManager* telemetryManager_{nullptr};
    VoiceInputController* voiceInput_{nullptr};
    WhisperRecognizer* speechRecognizer_{nullptr};
    VieNeuTtsBackend* vieNeuTtsBackend_{nullptr};
    ITtsBackend* ttsBackend_{nullptr};
    DInputButtonMonitor* directInput_{nullptr};
    QMediaPlayer* pttSoundPlayer_{nullptr};
    QAudioOutput* pttSoundOutput_{nullptr};
    QString simulatorName_{QStringLiteral("Not Connected")};
    bool connected_{false};
    QVariantMap telemetry_;
    bool mockEnabled_{false};
    RaceHistory raceHistory_;
    StrategyRecorder strategyRecorder_;
    std::unique_ptr<StrategyPredictor> strategyPredictor_;
    QString strategyStatus_{QStringLiteral("Đã tắt")};
    QString strategyDetail_{QStringLiteral("Bật để chọn vòng pit khi có dữ liệu chiến thuật đã duyệt.")};
    int strategyPitLap_{0};
    bool strategyLastLegalLap_{false};
    int strategyObservedLap_{0};
    int strategyRequestedLap_{0};
    int strategyAnnouncedPrepareLap_{0};
    int strategyAnnouncedPitLap_{0};
    bool strategySeenStart_{false};
    bool strategyPitted_{false};
    bool strategyWasInPit_{false};
    bool strategyWasInPitBox_{false};
    quint64 strategyRevision_{0};
    QString strategySessionKey_;
    QString strategySessionId_;
    int strategyRecordedLap_{0};
    bool strategyLapExcluded_{true};
    int strategyLastTelemetryLap_{0};
    EventEngine eventEngine_;
    SpotterEngine spotterEngine_;
    QString latestEvent_;
    QVariantList eventLog_;
    QString voiceStatus_{QStringLiteral("Idle")};
    QString microphoneName_{QStringLiteral("Default microphone")};
    QVariantList audioInputDevices_;
    float microphoneLevel_{0.0F};
    bool pushToTalkPressed_{false};
    QString latestUserText_;
    QString latestEngineerText_;
    QVariantList conversationLog_;
    QString apiKey_;
    std::unique_ptr<LLMManager> llmManager_;
    RaceState latestState_;
    QString apiState_{QStringLiteral("Unavailable")};
    QString apiDetail_{QStringLiteral("Configured; connection not tested yet")};
    QVariantMap apiStatistics_;
    bool apiConfigured_{false};
    bool manualConnectionTestActive_{false};
    QString streamedResponse_;
    std::unique_ptr<MessageDispatcher> messageDispatcher_;
    bool ttsAvailable_{false};
    QString ttsStatus_{QStringLiteral("Not installed")};
    QString whisperComputeBackend_{QStringLiteral("Đang khởi tạo")};
    QString vieNeuComputeBackend_{QStringLiteral("Chưa khởi tạo")};
    QString whisperComputeFallback_;
    QString vieNeuComputeFallback_;
    QVariantList toolLog_;
    QString directInputStatus_{QStringLiteral("DirectInput not initialized")};
    void updateAudioDuckingState();
    std::unique_ptr<AudioDucker> audioDucker_;
    bool isSpeaking_{false};
    bool sttWarmUpReady_{false};
    bool ttsWarmUpReady_{false};
    bool startupReady_{false};
    QString startupError_;
};

} // namespace raceengineer
