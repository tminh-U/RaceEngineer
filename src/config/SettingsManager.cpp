#include "config/SettingsManager.h"
#include "events/EventEngine.h"

#include "utils/Logging.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QStringList>

#include <algorithm>

namespace raceengineer {
namespace {
constexpr int currentSettingsVersion = 15;

void normalizeGoogleVoices(TtsSettings& settings)
{
    auto voices = settings.googleCustomVoices;
    if (!settings.googleVoiceId.isEmpty() && !settings.googleVoiceName.isEmpty())
        voices.append(QJsonObject{{"name", settings.googleVoiceName}, {"id", settings.googleVoiceId}});
    QJsonArray valid;
    QStringList names;
    for (const auto& value : voices) {
        const auto voice = value.toObject();
        const auto name = voice.value("name").toString().trimmed();
        const auto id = voice.value("id").toString();
        if (name.isEmpty() || name.size() > 80 || !id.startsWith("voice_") || id.size() > 256
            || names.contains(name) || valid.size() >= 200) continue;
        names.append(name);
        valid.append(QJsonObject{{"name", name}, {"id", id}});
    }
    settings.googleCustomVoices = valid;
}
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

void SettingsManager::setTts(const TtsSettings& settings)
{
    tts_ = settings;
    normalizeGoogleVoices(tts_);
    if (tts_.backend != QStringLiteral("Google Gemini API")) {
        tts_.backend = QStringLiteral("VieNeu-TTS");
    }
    if (tts_.googleModel != QStringLiteral("gemini-3.8-flash-tts")) {
        tts_.googleModel = QStringLiteral("gemini-3.8-flash-lite-tts");
    }
    tts_.volume = std::clamp(tts_.volume, 0.0F, 1.0F);
    tts_.cpuThreads = tts_.cpuThreads == 2 || tts_.cpuThreads == 3 || tts_.cpuThreads == 4
        ? tts_.cpuThreads : 4;
    save();
}

void SettingsManager::setSetupState(const bool completed, const int step)
{
    setupCompleted_ = completed;
    setupStep_ = std::clamp(step, 0, 3);
    save();
}

void SettingsManager::setTtsBenchmark(QJsonObject benchmark)
{
    ttsBenchmark_ = benchmark;
    save();
}

void SettingsManager::setSpotterWarningGapMeters(const double meters)
{
    spotterWarningGapMeters_ = std::clamp(meters, 0.5, 2.5);
    save();
}

void SettingsManager::setAudioInput(const AudioInputSettings& settings)
{
    audioInput_ = settings;
    audioInput_.deviceId = audioInput_.deviceId.trimmed();
    audioInput_.deviceName = audioInput_.deviceName.trimmed();
    if (audioInput_.deviceId.isEmpty() || audioInput_.deviceName.isEmpty()) {
        audioInput_.deviceId.clear();
        audioInput_.deviceName = QStringLiteral("Default (System)");
    }
    save();
}

void SettingsManager::setLocalAi(const LocalAiSettings& settings)
{
    LocalAiSettings normalized = settings;
    normalized.computeMode = normalized.computeMode.trimmed().toLower();
    if (normalized.computeMode != QStringLiteral("auto")
        && normalized.computeMode != QStringLiteral("cpu")
        && normalized.computeMode != QStringLiteral("vulkan")) {
        normalized.computeMode = QStringLiteral("auto");
    }
    normalized.vulkanDevice = normalized.vulkanDevice.trimmed();
    if (normalized.computeMode == QStringLiteral("vulkan")
        && !normalized.vulkanDevice.startsWith(QStringLiteral("vulkan:"))) {
        normalized.computeMode = QStringLiteral("auto");
        normalized.vulkanDevice.clear();
    } else if (normalized.computeMode != QStringLiteral("vulkan")) {
        normalized.vulkanDevice.clear();
    }
    if (localAi_.computeMode == normalized.computeMode
        && localAi_.vulkanDevice == normalized.vulkanDevice) {
        return;
    }
    localAi_ = normalized;
    save();
}

void SettingsManager::setStrategyEnabled(const bool enabled)
{
    if (strategyEnabled_ == enabled) return;
    strategyEnabled_ = enabled;
    save();
}

void SettingsManager::setSpotterEnabled(const bool enabled)
{
    if (spotterEnabled_ == enabled) return;
    spotterEnabled_ = enabled;
    save();
}

void SettingsManager::setFuelAlertsEnabled(const bool enabled)
{
    if (fuelAlertsEnabled_ == enabled) return;
    fuelAlertsEnabled_ = enabled;
    save();
}

void SettingsManager::setTyreAlertsEnabled(const bool enabled)
{
    if (tyreAlertsEnabled_ == enabled) return;
    tyreAlertsEnabled_ = enabled;
    save();
}

void SettingsManager::setLapDeltaEnabled(const bool enabled)
{
    if (lapDeltaEnabled_ == enabled) return;
    lapDeltaEnabled_ = enabled;
    save();
}

void SettingsManager::setFlagAlertsEnabled(const bool enabled)
{
    if (flagAlertsEnabled_ == enabled) return;
    flagAlertsEnabled_ = enabled;
    save();
}

void SettingsManager::setDamageAlertsEnabled(const bool enabled)
{
    if (damageAlertsEnabled_ == enabled) return;
    damageAlertsEnabled_ = enabled;
    save();
}

bool SettingsManager::eventEnabled(const EventType type) const noexcept
{
    switch (type) {
    case EventType::FuelLow:
    case EventType::FuelCritical: return fuelAlertsEnabled_;
    case EventType::TyreOverheating: return tyreAlertsEnabled_;
    case EventType::NewBestLap:
    case EventType::LapDelta: return lapDeltaEnabled_;
    case EventType::YellowFlag:
    case EventType::BlueFlag:
    case EventType::GreenFlag:
    case EventType::RedFlag:
    case EventType::BlackFlag:
    case EventType::WhiteFlag:
    case EventType::ChequeredFlag: return flagAlertsEnabled_;
    case EventType::DamageDetected: return damageAlertsEnabled_;
    case EventType::CarLeft:
    case EventType::CarRight: return spotterEnabled_;
    default: return true;
    }
}

void SettingsManager::setLapSummaryEnabled(const bool enabled)
{
    if (lapSummaryEnabled_ == enabled) return;
    lapSummaryEnabled_ = enabled;
    save();
}

void SettingsManager::setStrategyRecordingEnabled(bool enabled)
{
    if (strategyRecordingEnabled_ == enabled) return;
    strategyRecordingEnabled_ = enabled;
    save();
}

void SettingsManager::setStrategySharingEnabled(bool enabled)
{
    if (strategySharingEnabled_ == enabled) return;
    strategySharingEnabled_ = enabled;
    save();
}

void SettingsManager::setMinimizeToTray(bool enabled)
{
    if (minimizeToTray_ == enabled) return;
    minimizeToTray_ = enabled;
    save();
}

void SettingsManager::setMinimizeOnClose(bool enabled)
{
    if (minimizeOnClose_ == enabled) return;
    minimizeOnClose_ = enabled;
    save();
}

void SettingsManager::setGpuRendererEnabled(bool enabled)
{
    if (gpuRendererEnabled_ == enabled) return;
    gpuRendererEnabled_ = enabled;
    save();
}

void SettingsManager::setStrategyShareEndpoint(const QString& endpoint)
{
    if (strategyShareEndpoint_ == endpoint) return;
    strategyShareEndpoint_ = endpoint;
    save();
}

void SettingsManager::setDriverName(const QString& name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || driverName_ == trimmed) return;
    driverName_ = trimmed;
    save();
}

void SettingsManager::setResponseStyle(const QString& style)
{
    const QString trimmed = style.trimmed();
    QString normalized = QStringLiteral("Tiêu chuẩn");
    if (trimmed.compare(QStringLiteral("Tối giản"), Qt::CaseInsensitive) == 0) {
        normalized = QStringLiteral("Tối giản");
    } else if (trimmed.compare(QStringLiteral("Chi tiết"), Qt::CaseInsensitive) == 0) {
        normalized = QStringLiteral("Chi tiết");
    }
    if (responseStyle_ == normalized) return;
    responseStyle_ = normalized;
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
    const QJsonValue settingsVersionValue = root.value(QStringLiteral("settings_version"));
    const int settingsVersion = settingsVersionValue.toInt(1);
    const bool knownSettings = (settingsVersionValue.isDouble() && settingsVersionValue.toInt(-1) >= 1)
        || root.value(QStringLiteral("llm")).isObject()
        || root.value(QStringLiteral("tts")).isObject();
    setupCompleted_ = knownSettings
        && (!root.contains(QStringLiteral("setup_completed"))
            || root.value(QStringLiteral("setup_completed")).toBool(false));
    setupStep_ = std::clamp(root.value(QStringLiteral("setup_step")).toInt(), 0, 3);
    ttsBenchmark_ = root.value(QStringLiteral("tts_benchmark")).toObject();
    strategyEnabled_ = root.value(QStringLiteral("strategy_enabled")).toBool(false);
    spotterEnabled_ = root.value(QStringLiteral("spotter_enabled")).toBool(true);
    spotterWarningGapMeters_ = std::clamp(
        root.value(QStringLiteral("spotter_warning_gap_meters")).toDouble(spotterWarningGapMeters_),
        0.5, 2.5);
    fuelAlertsEnabled_ = root.value(QStringLiteral("fuel_alerts_enabled")).toBool(true);
    tyreAlertsEnabled_ = root.value(QStringLiteral("tyre_alerts_enabled")).toBool(true);
    lapDeltaEnabled_ = root.value(QStringLiteral("lap_delta_enabled")).toBool(true);
    flagAlertsEnabled_ = root.value(QStringLiteral("flag_alerts_enabled")).toBool(true);
    damageAlertsEnabled_ = root.value(QStringLiteral("damage_alerts_enabled")).toBool(true);
    lapSummaryEnabled_ = root.value(QStringLiteral("lap_summary_enabled")).toBool(false);
    strategyRecordingEnabled_ = root.value(QStringLiteral("strategy_recording_enabled")).toBool(true);
    strategySharingEnabled_ = root.value(QStringLiteral("strategy_sharing_enabled")).toBool(false);
    minimizeToTray_ = root.value(QStringLiteral("minimize_to_tray")).toBool(minimizeToTray_);
    minimizeOnClose_ = root.value(QStringLiteral("minimize_on_close")).toBool(minimizeOnClose_);
    gpuRendererEnabled_ = root.value(QStringLiteral("gpu_renderer_enabled")).toBool(gpuRendererEnabled_);
    strategyShareEndpoint_ = root.value(QStringLiteral("strategy_share_endpoint")).toString();
    const QJsonObject localAi = root.value(QStringLiteral("local_ai")).toObject();
    localAi_.computeMode = localAi.value(QStringLiteral("compute_mode"))
        .toString(localAi_.computeMode).trimmed().toLower();
    localAi_.vulkanDevice = localAi.value(QStringLiteral("vulkan_device")).toString().trimmed();
    if (localAi_.computeMode != QStringLiteral("auto")
        && localAi_.computeMode != QStringLiteral("cpu")
        && localAi_.computeMode != QStringLiteral("vulkan")) {
        localAi_.computeMode = QStringLiteral("auto");
    }
    if (localAi_.computeMode == QStringLiteral("vulkan")
        && !localAi_.vulkanDevice.startsWith(QStringLiteral("vulkan:"))) {
        localAi_.computeMode = QStringLiteral("auto");
        localAi_.vulkanDevice.clear();
    } else if (localAi_.computeMode != QStringLiteral("vulkan")) {
        localAi_.vulkanDevice.clear();
    }
    driverName_ = root.value(QStringLiteral("driver_name")).toString(driverName_);
    const QString style = root.value(QStringLiteral("response_style")).toString(responseStyle_).trimmed();
    if (style.compare(QStringLiteral("Tối giản"), Qt::CaseInsensitive) == 0) {
        responseStyle_ = QStringLiteral("Tối giản");
    } else if (style.compare(QStringLiteral("Chi tiết"), Qt::CaseInsensitive) == 0) {
        responseStyle_ = QStringLiteral("Chi tiết");
    } else {
        responseStyle_ = QStringLiteral("Tiêu chuẩn");
    }
    const auto object = root.value(QStringLiteral("llm")).toObject();
    llm_.provider = object.value(QStringLiteral("provider")).toString(llm_.provider);
    llm_.baseUrl = object.value(QStringLiteral("base_url")).toString(llm_.baseUrl);
    llm_.model = object.value(QStringLiteral("model")).toString(llm_.model);
    llm_.streaming = object.value(QStringLiteral("streaming")).toBool(llm_.streaming);
    llm_.timeoutMilliseconds = object.value(QStringLiteral("timeout_ms")).toInt(llm_.timeoutMilliseconds);
    llm_.maximumTokens = object.value(QStringLiteral("maximum_tokens")).toInt(llm_.maximumTokens);
    llm_.temperature = object.value(QStringLiteral("temperature")).toDouble(llm_.temperature);
    llm_.reasoning = object.value(QStringLiteral("reasoning")).toBool(llm_.reasoning);
    const auto input = root.value(QStringLiteral("push_to_talk")).toObject();
    pushToTalk_.keyboardEnabled = input.value(QStringLiteral("keyboard_enabled"))
                                       .toBool(pushToTalk_.keyboardEnabled);
    pushToTalk_.directInputEnabled = input.value(QStringLiteral("directinput_enabled"))
                                          .toBool(pushToTalk_.directInputEnabled);
    pushToTalk_.deviceGuid = input.value(QStringLiteral("device_guid")).toString();
    pushToTalk_.deviceName = input.value(QStringLiteral("device_name")).toString();
    pushToTalk_.buttonIndex = input.value(QStringLiteral("button_index")).toInt(-1);
    const auto tts = root.value(QStringLiteral("tts")).toObject();
    tts_.backend = tts.value(QStringLiteral("backend")).toString(tts_.backend);
    tts_.voice = tts.value(QStringLiteral("voice")).toString(tts_.voice);
    tts_.googleModel = tts.value(QStringLiteral("google_model")).toString(tts_.googleModel);
    tts_.googleVoice = tts.value(QStringLiteral("google_voice")).toString(tts_.googleVoice);
    tts_.googleVoiceId = tts.value(QStringLiteral("google_voice_id")).toString();
    tts_.googleVoiceName = tts.value(QStringLiteral("google_voice_name")).toString();
    tts_.googleCustomVoices = tts.value(QStringLiteral("google_custom_voices")).toArray();
    normalizeGoogleVoices(tts_);
    tts_.cpuThreads = tts.value(QStringLiteral("cpu_threads")).toInt(4);
    bool shouldSave = false;
    if (tts_.voice.trimmed().isEmpty()) {
        tts_.voice = QStringLiteral("Minh Đức");
    } else if (tts_.voice.compare(QStringLiteral("Kiên Trần"), Qt::CaseInsensitive) == 0
               || tts_.voice.compare(QStringLiteral("kientran"), Qt::CaseInsensitive) == 0) {
        tts_.voice = QStringLiteral("Minh Quân");
        shouldSave = true;
    }
    if (tts_.backend.compare(QStringLiteral("Google Gemini API"), Qt::CaseInsensitive) == 0) {
        tts_.backend = QStringLiteral("Google Gemini API");
    } else {
        tts_.backend = QStringLiteral("VieNeu-TTS");
    }
    if (tts_.googleModel != QStringLiteral("gemini-3.8-flash-tts")) {
        tts_.googleModel = QStringLiteral("gemini-3.8-flash-lite-tts");
    }
    tts_.cpuThreads = tts_.cpuThreads == 2 || tts_.cpuThreads == 3 || tts_.cpuThreads == 4
        ? tts_.cpuThreads : 4;
    tts_.outputDevice = tts.value(QStringLiteral("output_device")).toString();
    tts_.volume = static_cast<float>(tts.value(QStringLiteral("volume")).toDouble(tts_.volume));
    tts_.volume = std::clamp(tts_.volume, 0.0F, 1.0F);
    tts_.audioDucking = tts.value(QStringLiteral("audio_ducking")).toBool(tts_.audioDucking);
    tts_.duckFactor = static_cast<float>(tts.value(QStringLiteral("duck_factor")).toDouble(tts_.duckFactor));
    tts_.duckFactor = std::clamp(tts_.duckFactor, 0.05F, 0.95F);
    const auto audioInput = root.value(QStringLiteral("audio_input")).toObject();
    audioInput_.deviceId = audioInput.value(QStringLiteral("device_id")).toString().trimmed();
    audioInput_.deviceName = audioInput.value(QStringLiteral("device_name"))
                                 .toString(audioInput_.deviceName).trimmed();
    if (audioInput_.deviceId.isEmpty() || audioInput_.deviceName.isEmpty()) {
        audioInput_.deviceId.clear();
        audioInput_.deviceName = QStringLiteral("Default (System)");
    }

    const bool legacyDefaults = llm_.provider.compare(QStringLiteral("Mistral"), Qt::CaseInsensitive) == 0
        && llm_.baseUrl == QStringLiteral("https://api.mistral.ai/v1")
        && llm_.model == QStringLiteral("ministral-3b-latest");
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
    const bool hostedGemma = llm_.baseUrl.contains(QStringLiteral("generativelanguage.googleapis.com"),
                                  Qt::CaseInsensitive)
        && llm_.model.contains(QStringLiteral("gemma"), Qt::CaseInsensitive);
    if (settingsVersion < 6 && hostedGemma && llm_.maximumTokens <= 32) {
        // Gemma spends part of its budget on hidden reasoning/tool orchestration.
        // The old visual-only slider left this at 16, which cannot reach a final answer.
        llm_.maximumTokens = 64;
        shouldSave = true;
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
    llm.insert(QStringLiteral("reasoning"), llm_.reasoning);
    QJsonObject pushToTalk;
    pushToTalk.insert(QStringLiteral("keyboard_enabled"), pushToTalk_.keyboardEnabled);
    pushToTalk.insert(QStringLiteral("directinput_enabled"), pushToTalk_.directInputEnabled);
    pushToTalk.insert(QStringLiteral("device_guid"), pushToTalk_.deviceGuid);
    pushToTalk.insert(QStringLiteral("device_name"), pushToTalk_.deviceName);
    pushToTalk.insert(QStringLiteral("button_index"), pushToTalk_.buttonIndex);
    const QJsonObject tts{{QStringLiteral("backend"), tts_.backend},
        {QStringLiteral("voice"), tts_.voice},
        {QStringLiteral("google_model"), tts_.googleModel},
        {QStringLiteral("google_voice"), tts_.googleVoice},
        {QStringLiteral("google_voice_id"), tts_.googleVoiceId},
        {QStringLiteral("google_voice_name"), tts_.googleVoiceName},
        {QStringLiteral("google_custom_voices"), tts_.googleCustomVoices},
        {QStringLiteral("cpu_threads"), tts_.cpuThreads},
        {QStringLiteral("output_device"), tts_.outputDevice},
        {QStringLiteral("volume"), tts_.volume},
        {QStringLiteral("audio_ducking"), tts_.audioDucking},
        {QStringLiteral("duck_factor"), tts_.duckFactor}};
    const QJsonObject audioInput{{QStringLiteral("device_id"), audioInput_.deviceId},
        {QStringLiteral("device_name"), audioInput_.deviceName}};
    const QJsonObject localAi{{QStringLiteral("compute_mode"), localAi_.computeMode},
        {QStringLiteral("vulkan_device"), localAi_.vulkanDevice}};
    QFile file(filePath_);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(QJsonObject{{QStringLiteral("settings_version"), currentSettingsVersion},
            {QStringLiteral("driver_name"), driverName_},
            {QStringLiteral("response_style"), responseStyle_},
            {QStringLiteral("llm"), llm},
            {QStringLiteral("push_to_talk"), pushToTalk},
            {QStringLiteral("tts"), tts},
        {QStringLiteral("audio_input"), audioInput},
            {QStringLiteral("local_ai"), localAi},
            {QStringLiteral("strategy_enabled"), strategyEnabled_},
            {QStringLiteral("spotter_enabled"), spotterEnabled_},
            {QStringLiteral("spotter_warning_gap_meters"), spotterWarningGapMeters_},
            {QStringLiteral("fuel_alerts_enabled"), fuelAlertsEnabled_},
            {QStringLiteral("tyre_alerts_enabled"), tyreAlertsEnabled_},
            {QStringLiteral("lap_delta_enabled"), lapDeltaEnabled_},
            {QStringLiteral("flag_alerts_enabled"), flagAlertsEnabled_},
            {QStringLiteral("damage_alerts_enabled"), damageAlertsEnabled_},
            {QStringLiteral("lap_summary_enabled"), lapSummaryEnabled_},
        {QStringLiteral("strategy_recording_enabled"), strategyRecordingEnabled_},
        {QStringLiteral("strategy_sharing_enabled"), strategySharingEnabled_},
        {QStringLiteral("minimize_to_tray"), minimizeToTray_},
        {QStringLiteral("minimize_on_close"), minimizeOnClose_},
            {QStringLiteral("gpu_renderer_enabled"), gpuRendererEnabled_},
            {QStringLiteral("strategy_share_endpoint"), strategyShareEndpoint_},
            {QStringLiteral("setup_completed"), setupCompleted_},
            {QStringLiteral("setup_step"), setupStep_},
            {QStringLiteral("tts_benchmark"), ttsBenchmark_}}).toJson(QJsonDocument::Indented));
    }
}

} // namespace raceengineer
