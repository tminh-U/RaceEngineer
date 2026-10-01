#pragma once

#include <QJsonObject>
#include <QJsonArray>
#include <QString>

namespace raceengineer {

enum class EventType;

struct LlmSettings final {
    QString provider{QStringLiteral("OpenAI Compatible")};
    QString baseUrl;
    QString model{QStringLiteral("race-engineer")};
    bool streaming{true};
    int timeoutMilliseconds{30000};
    int maximumTokens{64};
    double temperature{0.1};
    bool reasoning{false};
};

struct PushToTalkSettings final {
    bool keyboardEnabled{true};
    bool directInputEnabled{false};
    QString deviceGuid;
    QString deviceName;
    int buttonIndex{-1};
};

struct TtsSettings final {
    // VieNeu-TTS is the only supported local backend.
    QString backend{QStringLiteral("VieNeu-TTS")};
    QString voice{QStringLiteral("Minh Đức")};
    QString googleModel{QStringLiteral("gemini-3.8-flash-lite-tts")};
    QString googleVoice{QStringLiteral("Kore")};
    QString googleVoiceId;
    QString googleVoiceName;
    QJsonArray googleCustomVoices;
    QString outputDevice;
    float volume{0.85F};
    bool audioDucking{true};
    float duckFactor{0.25F};
    int cpuThreads{2};
};

struct AudioInputSettings final {
    // Empty means the Windows system default capture endpoint.
    QString deviceId;
    QString deviceName{QStringLiteral("Default (System)")};
};

struct LocalAiSettings final {
    QString computeMode{QStringLiteral("auto")};
    QString vulkanDevice;

    [[nodiscard]] QString deviceId() const
    {
        return computeMode == QStringLiteral("vulkan") ? vulkanDevice : computeMode;
    }
};

class SettingsManager final {
public:
    SettingsManager();

    [[nodiscard]] const LlmSettings& llm() const noexcept { return llm_; }
    void setLlm(const LlmSettings& settings);
    [[nodiscard]] const PushToTalkSettings& pushToTalk() const noexcept { return pushToTalk_; }
    void setPushToTalk(const PushToTalkSettings& settings);
    [[nodiscard]] const TtsSettings& tts() const noexcept { return tts_; }
    void setTts(const TtsSettings& settings);
    [[nodiscard]] const AudioInputSettings& audioInput() const noexcept { return audioInput_; }
    void setAudioInput(const AudioInputSettings& settings);
    [[nodiscard]] const LocalAiSettings& localAi() const noexcept { return localAi_; }
    void setLocalAi(const LocalAiSettings& settings);
    [[nodiscard]] bool strategyEnabled() const noexcept { return strategyEnabled_; }
    void setStrategyEnabled(bool enabled);
    [[nodiscard]] bool spotterEnabled() const noexcept { return spotterEnabled_; }
    void setSpotterEnabled(bool enabled);
    [[nodiscard]] double spotterWarningGapMeters() const noexcept { return spotterWarningGapMeters_; }
    void setSpotterWarningGapMeters(double meters);
    [[nodiscard]] bool fuelAlertsEnabled() const noexcept { return fuelAlertsEnabled_; }
    void setFuelAlertsEnabled(bool enabled);
    [[nodiscard]] bool tyreAlertsEnabled() const noexcept { return tyreAlertsEnabled_; }
    void setTyreAlertsEnabled(bool enabled);
    [[nodiscard]] bool lapDeltaEnabled() const noexcept { return lapDeltaEnabled_; }
    void setLapDeltaEnabled(bool enabled);
    [[nodiscard]] bool flagAlertsEnabled() const noexcept { return flagAlertsEnabled_; }
    void setFlagAlertsEnabled(bool enabled);
    [[nodiscard]] bool damageAlertsEnabled() const noexcept { return damageAlertsEnabled_; }
    void setDamageAlertsEnabled(bool enabled);
    [[nodiscard]] bool eventEnabled(EventType type) const noexcept;
    [[nodiscard]] bool lapSummaryEnabled() const noexcept { return lapSummaryEnabled_; }
    void setLapSummaryEnabled(bool enabled);
    bool strategyRecordingEnabled() const { return strategyRecordingEnabled_; }
    void setStrategyRecordingEnabled(bool enabled);
    bool strategySharingEnabled() const { return strategySharingEnabled_; }
    void setStrategySharingEnabled(bool enabled);
    [[nodiscard]] bool minimizeToTray() const noexcept { return minimizeToTray_; }
    void setMinimizeToTray(bool enabled);
    [[nodiscard]] bool minimizeOnClose() const noexcept { return minimizeOnClose_; }
    void setMinimizeOnClose(bool enabled);
    [[nodiscard]] bool gpuRendererEnabled() const noexcept { return gpuRendererEnabled_; }
    void setGpuRendererEnabled(bool enabled);
    QString strategyShareEndpoint() const { return strategyShareEndpoint_; }
    void setStrategyShareEndpoint(const QString& endpoint);
    [[nodiscard]] QString driverName() const noexcept { return driverName_; }
    void setDriverName(const QString& name);
    [[nodiscard]] QString responseStyle() const noexcept { return responseStyle_; }
    void setResponseStyle(const QString& style);
    [[nodiscard]] QString filePath() const { return filePath_; }
    [[nodiscard]] bool migratedFromLegacyMistral() const noexcept { return migratedFromLegacyMistral_; }
    [[nodiscard]] bool setupCompleted() const noexcept { return setupCompleted_; }
    [[nodiscard]] int setupStep() const noexcept { return setupStep_; }
    void setSetupState(bool completed, int step);
    [[nodiscard]] QJsonObject ttsBenchmark() const { return ttsBenchmark_; }
    void setTtsBenchmark(QJsonObject benchmark);

private:
    void load();
    void save() const;

    QString filePath_;
    LlmSettings llm_;
    PushToTalkSettings pushToTalk_;
    TtsSettings tts_;
    AudioInputSettings audioInput_;
    LocalAiSettings localAi_;
    bool strategyEnabled_{false};
    bool spotterEnabled_{true};
    double spotterWarningGapMeters_{1.5};
    bool fuelAlertsEnabled_{true};
    bool tyreAlertsEnabled_{true};
    bool lapDeltaEnabled_{true};
    bool flagAlertsEnabled_{true};
    bool damageAlertsEnabled_{true};
    bool lapSummaryEnabled_{false};
    bool strategyRecordingEnabled_{true};
    bool strategySharingEnabled_{false};
    bool minimizeToTray_{false};
    bool minimizeOnClose_{false};
    bool gpuRendererEnabled_{true};
    QString strategyShareEndpoint_;
    QString driverName_{QStringLiteral("Minh Vũ")};
    QString responseStyle_{QStringLiteral("Tiêu chuẩn")};
    bool migratedFromLegacyMistral_{false};
    bool setupCompleted_{false};
    int setupStep_{0};
    QJsonObject ttsBenchmark_;
};

} // namespace raceengineer
