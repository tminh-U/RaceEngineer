#pragma once

#include "telemetry/common/RaceState.h"
#include "race/RaceHistory.h"
#include "events/EventEngine.h"
#include "spotter/SpotterEngine.h"
#include "config/SettingsManager.h"

#include <QObject>
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
class GwenTtsBackend;
class PiperTtsBackend;
class MessageDispatcher;
class DInputButtonMonitor;

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
    Q_PROPERTY(QString apiProvider READ apiProvider NOTIFY apiSettingsChanged)
    Q_PROPERTY(QString apiBaseUrl READ apiBaseUrl NOTIFY apiSettingsChanged)
    Q_PROPERTY(QString apiKey READ apiKey NOTIFY apiSettingsChanged)
    Q_PROPERTY(QString apiModel READ apiModel NOTIFY apiSettingsChanged)
    Q_PROPERTY(bool apiStreaming READ apiStreaming NOTIFY apiSettingsChanged)
    Q_PROPERTY(int apiTimeoutMilliseconds READ apiTimeoutMilliseconds NOTIFY apiSettingsChanged)
    Q_PROPERTY(int apiMaximumTokens READ apiMaximumTokens NOTIFY apiSettingsChanged)
    Q_PROPERTY(double apiTemperature READ apiTemperature NOTIFY apiSettingsChanged)
    Q_PROPERTY(bool apiConfigured READ apiConfigured NOTIFY apiSettingsChanged)
    Q_PROPERTY(QString apiState READ apiState NOTIFY apiStateChanged)
    Q_PROPERTY(QString apiDetail READ apiDetail NOTIFY apiStateChanged)
    Q_PROPERTY(QVariantMap apiStatistics READ apiStatistics NOTIFY apiStatisticsChanged)
    Q_PROPERTY(bool ttsAvailable READ ttsAvailable NOTIFY ttsStatusChanged)
    Q_PROPERTY(QString ttsBackend READ ttsBackend NOTIFY ttsStatusChanged)
    Q_PROPERTY(QString ttsStatus READ ttsStatus NOTIFY ttsStatusChanged)
    Q_PROPERTY(QStringList audioOutputDevices READ audioOutputDevices NOTIFY audioOutputChanged)
    Q_PROPERTY(QString selectedAudioOutput READ selectedAudioOutput NOTIFY audioOutputChanged)
    Q_PROPERTY(QVariantList toolLog READ toolLog NOTIFY toolLogChanged)
    Q_PROPERTY(bool keyboardPttEnabled READ keyboardPttEnabled NOTIFY pttSettingsChanged)
    Q_PROPERTY(bool directInputPttEnabled READ directInputPttEnabled NOTIFY pttSettingsChanged)
    Q_PROPERTY(QString directInputBinding READ directInputBinding NOTIFY pttSettingsChanged)
    Q_PROPERTY(QString directInputStatus READ directInputStatus NOTIFY directInputStatusChanged)

public:
    explicit Application(bool startWithMock, QObject* parent = nullptr);
    ~Application() override;

    [[nodiscard]] QString simulatorName() const { return simulatorName_; }
    [[nodiscard]] bool connected() const noexcept { return connected_; }
    [[nodiscard]] QString connectionText() const
    {
        return connected_ ? QStringLiteral("Connected") : QStringLiteral("Waiting for AC or ACC");
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
    [[nodiscard]] bool apiConfigured() const noexcept { return apiConfigured_; }
    [[nodiscard]] QString apiState() const { return apiState_; }
    [[nodiscard]] QString apiDetail() const { return apiDetail_; }
    [[nodiscard]] QVariantMap apiStatistics() const { return apiStatistics_; }
    [[nodiscard]] bool ttsAvailable() const noexcept { return ttsAvailable_; }
    [[nodiscard]] QString ttsBackend() const;
    [[nodiscard]] QString ttsStatus() const { return ttsStatus_; }
    [[nodiscard]] QStringList audioOutputDevices() const;
    [[nodiscard]] QString selectedAudioOutput() const;
    [[nodiscard]] QVariantList toolLog() const { return toolLog_; }
    [[nodiscard]] bool keyboardPttEnabled() const noexcept { return settingsManager_.pushToTalk().keyboardEnabled; }
    [[nodiscard]] bool directInputPttEnabled() const noexcept { return settingsManager_.pushToTalk().directInputEnabled; }
    [[nodiscard]] QString directInputBinding() const;
    [[nodiscard]] QString directInputStatus() const { return directInputStatus_; }

    Q_INVOKABLE void setUseMockTelemetry(bool enabled);
    Q_INVOKABLE void beginPushToTalk();
    Q_INVOKABLE void endPushToTalk();
    Q_INVOKABLE void saveAiSettings(const QString& provider, const QString& baseUrl,
        const QString& apiKey, const QString& model, bool streaming,
        int timeoutMilliseconds = 30000, int maximumTokens = 32, double temperature = 0.1);
    Q_INVOKABLE void testApiConnection();
    Q_INVOKABLE void askText(const QString& text);
    Q_INVOKABLE void resetConversation();
    Q_INVOKABLE void setTtsBackend(const QString& backend);
    Q_INVOKABLE void setAudioOutputDevice(const QString& description);
    Q_INVOKABLE void setPushToTalkOptions(bool keyboardEnabled, bool directInputEnabled);
    Q_INVOKABLE void beginDirectInputMapping();
    void startDirectInput(quintptr nativeWindowHandle);

signals:
    void connectionChanged();
    void telemetryChanged();
    void mockEnabledChanged();
    void latestEventChanged();
    void requestMockTelemetry(bool enabled);
    void requestBeginPushToTalk();
    void requestEndPushToTalk();
    void voiceStatusChanged();
    void microphoneChanged();
    void microphoneLevelChanged();
    void interactionChanged();
    void apiSettingsChanged();
    void apiStateChanged();
    void apiStatisticsChanged();
    void ttsStatusChanged();
    void audioOutputChanged();
    void toolLogChanged();
    void pttSettingsChanged();
    void directInputStatusChanged();
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

private:
    static QVariantMap toVariantMap(const RaceState& state);
    bool eventFilter(QObject* watched, QEvent* event) override;

    QThread telemetryThread_;
    QThread audioThread_;
    QThread sttThread_;
    QThread inputThread_;
    TelemetryManager* telemetryManager_{nullptr};
    VoiceInputController* voiceInput_{nullptr};
    WhisperRecognizer* speechRecognizer_{nullptr};
    PiperTtsBackend* piperTtsBackend_{nullptr};
    GwenTtsBackend* gwenTtsBackend_{nullptr};
    ITtsBackend* ttsBackend_{nullptr};
    DInputButtonMonitor* directInput_{nullptr};
    QMediaPlayer* pttSoundPlayer_{nullptr};
    QAudioOutput* pttSoundOutput_{nullptr};
    QString simulatorName_{QStringLiteral("Not Connected")};
    bool connected_{false};
    QVariantMap telemetry_;
    bool mockEnabled_{false};
    RaceHistory raceHistory_;
    EventEngine eventEngine_;
    SpotterEngine spotterEngine_;
    QString latestEvent_;
    QVariantList eventLog_;
    QString voiceStatus_{QStringLiteral("Idle")};
    QString microphoneName_{QStringLiteral("Default microphone")};
    float microphoneLevel_{0.0F};
    bool pushToTalkPressed_{false};
    QString latestUserText_;
    QString latestEngineerText_;
    SettingsManager settingsManager_;
    QString apiKey_;
    std::unique_ptr<LLMManager> llmManager_;
    RaceState latestState_;
    QString apiState_{QStringLiteral("Unavailable")};
    QString apiDetail_{QStringLiteral("Configured; connection not tested yet")};
    QVariantMap apiStatistics_;
    bool apiConfigured_{false};
    QString streamedResponse_;
    std::unique_ptr<MessageDispatcher> messageDispatcher_;
    bool ttsAvailable_{false};
    QString ttsStatus_{QStringLiteral("Not installed")};
    QVariantList toolLog_;
    QString directInputStatus_{QStringLiteral("DirectInput not initialized")};
};

} // namespace raceengineer
