#pragma once

#include <QString>

namespace raceengineer {

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
    QString outputDevice;
    float volume{0.85F};
    bool audioDucking{true};
    float duckFactor{0.25F};
};

struct AudioInputSettings final {
    // Empty means the Windows system default capture endpoint.
    QString deviceId;
    QString deviceName{QStringLiteral("Default (System)")};
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
    [[nodiscard]] QString driverName() const noexcept { return driverName_; }
    void setDriverName(const QString& name);
    [[nodiscard]] QString responseStyle() const noexcept { return responseStyle_; }
    void setResponseStyle(const QString& style);
    [[nodiscard]] QString filePath() const { return filePath_; }
    [[nodiscard]] bool migratedFromLegacyMistral() const noexcept { return migratedFromLegacyMistral_; }

private:
    void load();
    void save() const;

    QString filePath_;
    LlmSettings llm_;
    PushToTalkSettings pushToTalk_;
    TtsSettings tts_;
    AudioInputSettings audioInput_;
    QString driverName_{QStringLiteral("Minh Vũ")};
    QString responseStyle_{QStringLiteral("Tiêu chuẩn")};
    bool migratedFromLegacyMistral_{false};
};

} // namespace raceengineer
