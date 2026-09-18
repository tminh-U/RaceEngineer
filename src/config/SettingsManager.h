#pragma once

#include <QString>

namespace raceengineer {

struct LlmSettings final {
    QString provider{QStringLiteral("OpenAI Compatible")};
    QString baseUrl{QStringLiteral("http://100.114.125.88:8080/v1")};
    QString model{QStringLiteral("race-engineer")};
    bool streaming{true};
    int timeoutMilliseconds{30000};
    int maximumTokens{32};
    double temperature{0.1};
};

struct PushToTalkSettings final {
    bool keyboardEnabled{true};
    bool directInputEnabled{false};
    QString deviceGuid;
    QString deviceName;
    int buttonIndex{-1};
};

struct TtsSettings final {
    // Piper is deliberately the default; Gwen remains the higher-latency option.
    QString backend{QStringLiteral("Piper")};
    QString outputDevice;
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
    [[nodiscard]] QString filePath() const { return filePath_; }
    [[nodiscard]] bool migratedFromLegacyMistral() const noexcept { return migratedFromLegacyMistral_; }

private:
    void load();
    void save() const;

    QString filePath_;
    LlmSettings llm_;
    PushToTalkSettings pushToTalk_;
    TtsSettings tts_;
    bool migratedFromLegacyMistral_{false};
};

} // namespace raceengineer
