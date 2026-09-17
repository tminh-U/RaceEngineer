#include "config/SettingsManager.h"

#include "utils/Logging.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include <algorithm>

namespace raceengineer {
namespace {
constexpr int currentSettingsVersion = 2;
}

SettingsManager::SettingsManager()
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(directory);
    filePath_ = QDir(directory).filePath(QStringLiteral("settings.json"));
    load();
}

void SettingsManager::setLlm(const LlmSettings& settings)
{
    llm_ = settings;
    llm_.timeoutMilliseconds = std::clamp(llm_.timeoutMilliseconds, 1000, 60000);
    llm_.maximumTokens = std::clamp(llm_.maximumTokens, 16, 512);
    llm_.temperature = std::clamp(llm_.temperature, 0.0, 1.0);
    save();
}

void SettingsManager::setPushToTalk(const PushToTalkSettings& settings)
{
    pushToTalk_ = settings;
    if (pushToTalk_.buttonIndex < -1 || pushToTalk_.buttonIndex > 127) {
        pushToTalk_.buttonIndex = -1;
    }
    save();
}

void SettingsManager::load()
{
    QFile file(filePath_);
    if (!file.open(QIODevice::ReadOnly)) {
        save();
        return;
    }
    QJsonParseError error{};
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        qCWarning(logApp) << "Invalid settings JSON; using defaults:" << error.errorString();
        return;
    }
    const QJsonObject root = document.object();
    const int settingsVersion = root.value(QStringLiteral("settings_version")).toInt(1);
    const auto object = root.value(QStringLiteral("llm")).toObject();
    llm_.provider = object.value(QStringLiteral("provider")).toString(llm_.provider);
    llm_.baseUrl = object.value(QStringLiteral("base_url")).toString(llm_.baseUrl);
    llm_.model = object.value(QStringLiteral("model")).toString(llm_.model);
    llm_.streaming = object.value(QStringLiteral("streaming")).toBool(llm_.streaming);
    llm_.timeoutMilliseconds = object.value(QStringLiteral("timeout_ms")).toInt(llm_.timeoutMilliseconds);
    llm_.maximumTokens = object.value(QStringLiteral("maximum_tokens")).toInt(llm_.maximumTokens);
    llm_.temperature = object.value(QStringLiteral("temperature")).toDouble(llm_.temperature);
    const auto input = root.value(QStringLiteral("push_to_talk")).toObject();
    pushToTalk_.keyboardEnabled = input.value(QStringLiteral("keyboard_enabled"))
                                       .toBool(pushToTalk_.keyboardEnabled);
    pushToTalk_.directInputEnabled = input.value(QStringLiteral("directinput_enabled"))
                                          .toBool(pushToTalk_.directInputEnabled);
    pushToTalk_.deviceGuid = input.value(QStringLiteral("device_guid")).toString();
    pushToTalk_.deviceName = input.value(QStringLiteral("device_name")).toString();
    pushToTalk_.buttonIndex = input.value(QStringLiteral("button_index")).toInt(-1);

    const bool legacyDefaults = llm_.provider.compare(QStringLiteral("Mistral"), Qt::CaseInsensitive) == 0
        && llm_.baseUrl == QStringLiteral("https://api.mistral.ai/v1")
        && llm_.model == QStringLiteral("ministral-3b-latest");
    bool shouldSave = false;
    if (legacyDefaults) {
        llm_ = LlmSettings{};
        migratedFromLegacyMistral_ = true;
        shouldSave = true;
        qCInfo(logApp) << "Migrated legacy Mistral defaults to OpenAI-compatible local server defaults";
    }
    if (settingsVersion < 2 && llm_.timeoutMilliseconds == 10000) {
        llm_.timeoutMilliseconds = 30000;
        shouldSave = true;
        qCInfo(logApp) << "Migrated LLM timeout from 10 to 30 seconds";
    }
    if (settingsVersion < currentSettingsVersion) shouldSave = true;
    if (shouldSave) save();
}

void SettingsManager::save() const
{
    QJsonObject llm;
    llm.insert(QStringLiteral("provider"), llm_.provider);
    llm.insert(QStringLiteral("base_url"), llm_.baseUrl);
    llm.insert(QStringLiteral("model"), llm_.model);
    llm.insert(QStringLiteral("streaming"), llm_.streaming);
    llm.insert(QStringLiteral("timeout_ms"), llm_.timeoutMilliseconds);
    llm.insert(QStringLiteral("maximum_tokens"), llm_.maximumTokens);
    llm.insert(QStringLiteral("temperature"), llm_.temperature);
    QJsonObject pushToTalk;
    pushToTalk.insert(QStringLiteral("keyboard_enabled"), pushToTalk_.keyboardEnabled);
    pushToTalk.insert(QStringLiteral("directinput_enabled"), pushToTalk_.directInputEnabled);
    pushToTalk.insert(QStringLiteral("device_guid"), pushToTalk_.deviceGuid);
    pushToTalk.insert(QStringLiteral("device_name"), pushToTalk_.deviceName);
    pushToTalk.insert(QStringLiteral("button_index"), pushToTalk_.buttonIndex);
    QFile file(filePath_);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(QJsonObject{{QStringLiteral("settings_version"), currentSettingsVersion},
            {QStringLiteral("llm"), llm},
            {QStringLiteral("push_to_talk"), pushToTalk}}).toJson(QJsonDocument::Indented));
    }
}

} // namespace raceengineer
