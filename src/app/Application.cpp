#include "app/Application.h"

#include "telemetry/TelemetryManager.h"
#include "audio/AudioCapture.h"
#include "audio/VoiceInputController.h"
#include "stt/WhisperRecognizer.h"
#include "config/CredentialStore.h"
#include "llm/LLMManager.h"
#include "audio/MessageDispatcher.h"
#include "audio/AudioDucker.h"
#include "tts/VieNeuTtsBackend.h"
#include "input/DInputButtonMonitor.h"
#include "utils/Logging.h"

#include <QCoreApplication>
#include <QDir>
#include <QDateTime>
#include <QEvent>
#include <QFile>
#include <QKeyEvent>
#include <QJsonDocument>
#include <QMetaObject>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QAudioDevice>
#include <QMediaDevices>
#include <QTimer>
#include <QFileInfo>
#include <QUrl>
#include <QVariantList>
#include <QUuid>

#include <algorithm>

namespace raceengineer {
namespace {

template <typename T>
void addOptional(QVariantMap& map, const char* key, const std::optional<T>& value)
{
    if (value.has_value()) {
        map.insert(QString::fromLatin1(key), QVariant::fromValue(*value));
    }
}

QVariantList wheelsToList(const WheelValues& wheels)
{
    return {wheels[0], wheels[1], wheels[2], wheels[3]};
}

QString utf8(const std::string& value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

QString stableAudioInputId(const QByteArray& id)
{
    return QString::fromLatin1(id.toHex());
}

QByteArray audioInputIdBytes(const QString& id)
{
    return QByteArray::fromHex(id.trimmed().toLatin1());
}

QString resolvePackagedOrDevelopmentPath(const QString& relativePath)
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString packaged = QDir(appDir).filePath(relativePath);
    if (QFileInfo::exists(packaged)) return packaged;
    return QDir(appDir).absoluteFilePath(QStringLiteral("../") + relativePath);
}

QString resolveVieNeuModelPath()
{
    return resolvePackagedOrDevelopmentPath(QStringLiteral("models/vieneu-v3"));
}

QString resolvePhoWhisperModelPath()
{
    return resolvePackagedOrDevelopmentPath(QStringLiteral("models/ggml-phowhisper-small-q5_1.bin"));
}

} // namespace

Application::Application(const bool startWithMock, QObject* const parent)
    : QObject(parent)
    , aiRuntimeSelection_(LocalAiRuntime::resolveAndApply(
          settingsManager_.localAi().deviceId()))
    , telemetryManager_(new TelemetryManager)
    , voiceInput_(new VoiceInputController)
    , speechRecognizer_(new WhisperRecognizer(
          resolvePhoWhisperModelPath(), aiRuntimeSelection_))
    , vieNeuTtsBackend_(new VieNeuTtsBackend(
          resolveVieNeuModelPath(), settingsManager_.tts().voice, aiRuntimeSelection_, this))
    , ttsBackend_(vieNeuTtsBackend_)
    , directInput_(new DInputButtonMonitor)
    , pttSoundPlayer_(new QMediaPlayer(this))
    , pttSoundOutput_(new QAudioOutput(this))
    , mockEnabled_(startWithMock && mockAvailable())
    , apiKey_(settingsManager_.migratedFromLegacyMistral() ? QString{} : CredentialStore::readApiKey())
    , llmManager_(std::make_unique<LLMManager>(settingsManager_.llm(), apiKey_))
    , messageDispatcher_(std::make_unique<MessageDispatcher>(vieNeuTtsBackend_))
    , audioDucker_(std::make_unique<AudioDucker>(this))
{
    qRegisterMetaType<RaceState>();
    strategyRecorder_.setEnabled(settingsManager_.strategyRecordingEnabled());
    connect(&strategyRecorder_, &StrategyRecorder::enabledChanged, this,
            [this](bool enabled) { settingsManager_.setStrategyRecordingEnabled(enabled); });
    QString shareEndpoint = settingsManager_.strategyShareEndpoint();
    QString shareToken = CredentialStore::readRaceDataShareToken();
    QFile shareEnv(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral(".env")));
    if (shareEnv.open(QIODevice::ReadOnly) && shareEnv.size() <= 4096) {
        for (QByteArray line : shareEnv.readAll().split('\n')) {
            line = line.trimmed();
            if (line.startsWith("\xEF\xBB\xBF")) line.remove(0, 3);
            if (line.isEmpty() || line.startsWith('#')) continue;
            const qsizetype separator = line.indexOf('=');
            if (separator < 0) continue;
            const QByteArray key = line.left(separator).trimmed();
            QByteArray value = line.mid(separator + 1).trimmed();
            if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"')
                || (value.front() == '\'' && value.back() == '\''))) value = value.mid(1, value.size() - 2);
            if (key == "RACEENGINEER_SHARE_ENDPOINT") shareEndpoint = QString::fromUtf8(value);
            else if (key == "RACEENGINEER_SHARE_TOKEN") shareToken = QString::fromUtf8(value);
        }
    }
    if (qEnvironmentVariableIsSet("RACEENGINEER_SHARE_ENDPOINT"))
        shareEndpoint = qEnvironmentVariable("RACEENGINEER_SHARE_ENDPOINT");
    if (qEnvironmentVariableIsSet("RACEENGINEER_SHARE_TOKEN"))
        shareToken = qEnvironmentVariable("RACEENGINEER_SHARE_TOKEN");
    shareEndpoint = shareEndpoint.trimmed();
    shareToken = shareToken.trimmed();
    const QUrl shareUrl(shareEndpoint, QUrl::StrictMode);
    const bool validShareEndpoint = shareUrl.isValid() && shareUrl.scheme() == QStringLiteral("https")
        && !shareUrl.host().isEmpty() && shareUrl.userInfo().isEmpty()
        && shareUrl.query().isEmpty() && shareUrl.fragment().isEmpty();
    strategyRecorder_.setSharingConfig(validShareEndpoint && shareToken.size() >= 32 ? shareEndpoint : QString{},
                                       shareToken);
    strategyRecorder_.setSharingEnabled(settingsManager_.strategySharingEnabled());
    connect(&strategyRecorder_, &StrategyRecorder::sharingEnabledChanged, this,
            [this](bool enabled) { settingsManager_.setStrategySharingEnabled(enabled); });
    strategyPredictor_ = std::make_unique<StrategyPredictor>(
        resolvePackagedOrDevelopmentPath(QStringLiteral("models/pit_strategy")), this);
    if (settingsManager_.strategyEnabled()) {
        strategyStatus_ = QStringLiteral("Đang chờ dữ liệu");
        strategyDetail_ = QStringLiteral("Đang kiểm tra chiến thuật, race profile và telemetry.");
    }
    audioDucker_->setEnabled(settingsManager_.tts().audioDucking);
    audioDucker_->setDuckFactor(settingsManager_.tts().duckFactor);
    connect(audioDucker_.get(), &AudioDucker::duckedChanged, this, &Application::audioDuckingStateChanged);
    llmManager_->setDriverName(settingsManager_.driverName());
    llmManager_->setResponseStyle(settingsManager_.responseStyle());
    refreshAudioInputDevices();
    microphoneName_ = selectedAudioInput();
    pttSoundOutput_->setVolume(0.7F);
    for (const QAudioDevice& device : QMediaDevices::audioOutputs()) {
        if (device.description() == settingsManager_.tts().outputDevice) {
            pttSoundOutput_->setDevice(device);
            break;
        }
    }
    pttSoundPlayer_->setAudioOutput(pttSoundOutput_);
    const QString pttSoundPath = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("sound.mp3"));
    if (QFileInfo::exists(pttSoundPath)) {
        pttSoundPlayer_->setSource(QUrl::fromLocalFile(pttSoundPath));
    } else {
        qCWarning(logAudio).noquote() << "PTT sound not found:" << pttSoundPath;
    }
    telemetryManager_->moveToThread(&telemetryThread_);
    voiceInput_->moveToThread(&audioThread_);
    speechRecognizer_->moveToThread(&sttThread_);
    directInput_->moveToThread(&inputThread_);

    connect(&telemetryThread_, &QThread::started, telemetryManager_, &TelemetryManager::start);
    connect(&telemetryThread_, &QThread::finished, telemetryManager_, &QObject::deleteLater);
    connect(telemetryManager_, &TelemetryManager::stateUpdated,
        this, &Application::onStateUpdated, Qt::QueuedConnection);
    connect(telemetryManager_, &TelemetryManager::connectionStatusChanged,
        this, &Application::onConnectionStatusChanged, Qt::QueuedConnection);
    connect(this, &Application::requestMockTelemetry,
        telemetryManager_, &TelemetryManager::setUseMockTelemetry, Qt::QueuedConnection);

    connect(&audioThread_, &QThread::started, this, [this] {
        emit requestStartVoiceInput(audioInputIdBytes(settingsManager_.audioInput().deviceId));
    });
    connect(&audioThread_, &QThread::finished, voiceInput_, &QObject::deleteLater);
    connect(this, &Application::requestStartVoiceInput,
        voiceInput_, &VoiceInputController::start, Qt::QueuedConnection);
    connect(this, &Application::requestConfigureAudioInput,
        voiceInput_, &VoiceInputController::setInputDevice, Qt::QueuedConnection);
    connect(this, &Application::requestBeginPushToTalk,
        voiceInput_, &VoiceInputController::beginPushToTalk, Qt::QueuedConnection);
    connect(this, &Application::requestEndPushToTalk,
        voiceInput_, &VoiceInputController::endPushToTalk, Qt::QueuedConnection);
    connect(voiceInput_, &VoiceInputController::statusChanged,
        this, &Application::onVoiceStatusChanged, Qt::QueuedConnection);
    connect(voiceInput_, &VoiceInputController::levelChanged, this, [this](const float level) {
        microphoneLevel_ = level;
        emit microphoneLevelChanged();
    }, Qt::QueuedConnection);
    connect(voiceInput_, &VoiceInputController::microphoneChanged, this, [this](const QString& name) {
        microphoneName_ = name;
        emit microphoneChanged();
    }, Qt::QueuedConnection);
    connect(voiceInput_, &VoiceInputController::errorOccurred, this, [this](const QString& error) {
        qCWarning(logAudio).noquote() << error;
        microphoneName_ = error;
        updateEngineerMessage(error);
        onVoiceStatusChanged(QStringLiteral("Idle"));
        emit microphoneChanged();
        emit interactionChanged();
    }, Qt::QueuedConnection);
    connect(voiceInput_, &VoiceInputController::utteranceReady,
        this, &Application::onUtteranceReady, Qt::QueuedConnection);

    connect(&sttThread_, &QThread::started, speechRecognizer_, &WhisperRecognizer::warmUp);
    connect(&sttThread_, &QThread::finished, speechRecognizer_, &QObject::deleteLater);
    connect(speechRecognizer_, &WhisperRecognizer::warmUpFinished,
        this, &Application::onSttWarmUpFinished, Qt::QueuedConnection);
    connect(speechRecognizer_, &WhisperRecognizer::runtimeBackendChanged, this,
        [this](const QString& backend, const QString& fallbackReason) {
            whisperComputeBackend_ = backend;
            whisperComputeFallback_ = fallbackReason;
            emit aiComputeStatusChanged();
        });
    connect(this, &Application::requestTranscription,
        speechRecognizer_, &WhisperRecognizer::transcribe, Qt::QueuedConnection);
    connect(speechRecognizer_, &WhisperRecognizer::recognitionStarted, this, [this] {
        onVoiceStatusChanged(QStringLiteral("Recognizing"));
    }, Qt::QueuedConnection);
    connect(speechRecognizer_, &WhisperRecognizer::transcriptionReady,
        this, &Application::onTranscriptionReady, Qt::QueuedConnection);
    connect(speechRecognizer_, &WhisperRecognizer::recognitionError, this, [this](const QString& error) {
        updateEngineerMessage(error);
        onVoiceStatusChanged(QStringLiteral("Idle"));
        emit interactionChanged();
    }, Qt::QueuedConnection);
    connect(speechRecognizer_, &WhisperRecognizer::recognitionFinished, this, [this] {
        if (voiceStatus_ == QStringLiteral("Recognizing")) {
            updateEngineerMessage(QStringLiteral("Whisper không trả về kết quả nhận dạng."));
            onVoiceStatusChanged(QStringLiteral("Idle"));
            emit interactionChanged();
        }
    }, Qt::QueuedConnection);

    if (settingsManager_.migratedFromLegacyMistral()) {
        CredentialStore::clearApiKey();
        apiKey_.clear();
    }
    apiConfigured_ = !settingsManager_.llm().baseUrl.trimmed().isEmpty()
        && !settingsManager_.llm().model.trimmed().isEmpty();
    apiDetail_ = apiConfigured_ ? QStringLiteral("Configured; connection not tested yet")
                                : QStringLiteral("Base URL and model are required");
    apiStatistics_ = llmManager_->statistics();
    connect(llmManager_.get(), &LLMManager::stateChanged, this,
        [this](const QString& state, const QString& detail) {
            apiState_ = state;
            apiDetail_ = detail;
            emit apiStateChanged();
        });
    connect(llmManager_.get(), &LLMManager::responseChunk, this, [this](const QString& chunk) {
        streamedResponse_ += chunk;
        updateEngineerMessage(streamedResponse_);
        emit interactionChanged();
    });
    connect(llmManager_.get(), &LLMManager::responseReady, this, [this](const QString& text) {
        updateEngineerMessage(text);
        streamedResponse_.clear();
        onVoiceStatusChanged(QStringLiteral("Idle"));
        emit interactionChanged();
        messageDispatcher_->enqueue(text, EventPriority::Conversation);
    });
    connect(llmManager_.get(), &LLMManager::errorOccurred, this, [this](const QString& message) {
        updateEngineerMessage(message);
        streamedResponse_.clear();
        onVoiceStatusChanged(QStringLiteral("Idle"));
        emit interactionChanged();
    });
    connect(llmManager_.get(), &LLMManager::statisticsChanged, this, [this] {
        apiStatistics_ = llmManager_->statistics();
        emit apiStatisticsChanged();
    });
    connect(llmManager_.get(), &LLMManager::toolCalled, this,
        [this](const QString& name, const QJsonObject& result) {
            toolLog_.prepend(QVariantMap{{QStringLiteral("name"), name},
                {QStringLiteral("result"), QString::fromUtf8(
                    QJsonDocument(result).toJson(QJsonDocument::Compact))},
                {QStringLiteral("timestamp"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs)}});
            while (toolLog_.size() > 50) toolLog_.removeLast();
            emit toolLogChanged();
        });
    connect(llmManager_.get(), &LLMManager::connectionTested, this,
        [this](const bool, const QString& detail) {
            if (manualConnectionTestActive_) {
                updateEngineerMessage(detail);
                manualConnectionTestActive_ = false;
            }
            streamedResponse_.clear();
            onVoiceStatusChanged(QStringLiteral("Idle"));
            emit interactionChanged();
        });

    ttsBackend_ = vieNeuTtsBackend_;
    messageDispatcher_->setBackend(ttsBackend_);
    connect(ttsBackend_, &ITtsBackend::warmUpFinished,
        this, &Application::onTtsWarmUpFinished, Qt::QueuedConnection);
    connect(vieNeuTtsBackend_, &VieNeuTtsBackend::runtimeBackendChanged, this,
        [this](const QString& backend, const QString& fallbackReason) {
            vieNeuComputeBackend_ = backend;
            vieNeuComputeFallback_ = fallbackReason;
            emit aiComputeStatusChanged();
        }, Qt::QueuedConnection);
    vieNeuTtsBackend_->setVoice(settingsManager_.tts().voice);
    vieNeuTtsBackend_->setAudioOutputDevice(settingsManager_.tts().outputDevice);
    vieNeuTtsBackend_->setVolume(settingsManager_.tts().volume);
    ttsAvailable_ = ttsBackend_->isAvailable();
    ttsStatus_ = ttsAvailable_
        ? QStringLiteral("%1 sẵn sàng").arg(ttsBackend_->backendName())
        : QStringLiteral("Thiếu runtime/model %1").arg(ttsBackend_->backendName());
    connect(messageDispatcher_.get(), &MessageDispatcher::speakingChanged, this,
        [this](const bool speaking, const QString&) {
            isSpeaking_ = speaking;
            updateAudioDuckingState();
            ttsStatus_ = speaking
                ? QStringLiteral("Đang chuẩn bị · %1…").arg(ttsBackend_->backendName())
                : (ttsAvailable_
                    ? QStringLiteral("%1 sẵn sàng").arg(ttsBackend_->backendName())
                    : QStringLiteral("Thiếu runtime/model %1").arg(ttsBackend_->backendName()));
            if (speaking) onVoiceStatusChanged(QStringLiteral("Speaking"));
            else if (voiceStatus_ == QStringLiteral("Speaking")) onVoiceStatusChanged(QStringLiteral("Idle"));
            emit ttsStatusChanged();
    });
    connect(vieNeuTtsBackend_, &VieNeuTtsBackend::statusChanged,
        this, [this](const QString& status) {
            if (ttsBackend_ != vieNeuTtsBackend_) return;
            ttsStatus_ = status;
            emit ttsStatusChanged();
        }, Qt::QueuedConnection);
    const auto connectBackendError = [this](ITtsBackend* backend) {
        connect(backend, &ITtsBackend::errorOccurred, this, [this, backend](const QString& error) {
            qCWarning(logTts).noquote() << error;
            if (ttsBackend_ != backend) return;
            ttsStatus_ = error;
            emit ttsStatusChanged();
        }, Qt::QueuedConnection);
    };
    connectBackendError(vieNeuTtsBackend_);
    ttsBackend_->warmUp();

    connect(&inputThread_, &QThread::finished, directInput_, &QObject::deleteLater);
    connect(this, &Application::requestStartDirectInput,
        directInput_, &DInputButtonMonitor::start, Qt::QueuedConnection);
    connect(this, &Application::requestConfigureDirectInput,
        directInput_, &DInputButtonMonitor::configure, Qt::QueuedConnection);
    connect(this, &Application::requestDirectInputMapping,
        directInput_, &DInputButtonMonitor::beginMapping, Qt::QueuedConnection);
    connect(directInput_, &DInputButtonMonitor::buttonPressedChanged, this,
        [this](const bool pressed) { pressed ? beginPushToTalk() : endPushToTalk(); },
        Qt::QueuedConnection);
    connect(directInput_, &DInputButtonMonitor::statusChanged, this,
        [this](const QString& status) {
            directInputStatus_ = status;
            emit directInputStatusChanged();
        }, Qt::QueuedConnection);
    connect(directInput_, &DInputButtonMonitor::mappingCaptured, this,
        [this](const QString& deviceName, const QString& deviceGuid, const int buttonIndex) {
            auto settings = settingsManager_.pushToTalk();
            settings.directInputEnabled = true;
            settings.deviceName = deviceName;
            settings.deviceGuid = deviceGuid;
            settings.buttonIndex = buttonIndex;
            settingsManager_.setPushToTalk(settings);
            emit requestConfigureDirectInput(true, deviceGuid, buttonIndex);
            emit pttSettingsChanged();
        }, Qt::QueuedConnection);

    telemetryThread_.setObjectName(QStringLiteral("TelemetryWorker"));
    telemetryThread_.start(QThread::LowPriority);
    audioThread_.setObjectName(QStringLiteral("AudioCaptureWorker"));
    audioThread_.start(QThread::LowPriority);
    sttThread_.setObjectName(QStringLiteral("SpeechRecognitionWorker"));
    sttThread_.start(QThread::NormalPriority);
    inputThread_.setObjectName(QStringLiteral("DirectInputWorker"));
    inputThread_.start(QThread::LowPriority);
    QCoreApplication::instance()->installEventFilter(this);
    if (mockEnabled_) {
        emit requestMockTelemetry(true);
    }
    if (apiConfigured_) {
        QTimer::singleShot(500, this, [this] {
            if (llmManager_ && apiConfigured_) {
                llmManager_->testConnection();
            }
        });
    }
}

Application::~Application()
{
    if (latestState_.sessionType == SessionType::Race && strategyRecordedLap_ > 0)
        strategyRecorder_.finishSession(strategySessionId_);
    QCoreApplication::instance()->removeEventFilter(this);
    if (audioDucker_) {
        audioDucker_->setDucked(false);
    }
    messageDispatcher_->clear();
    if (inputThread_.isRunning()) {
        QMetaObject::invokeMethod(directInput_, &DInputButtonMonitor::stop,
            Qt::BlockingQueuedConnection);
        inputThread_.quit();
        inputThread_.wait(3000);
    }
    vieNeuTtsBackend_->shutdown();
    if (sttThread_.isRunning()) {
        speechRecognizer_->cancel();
        sttThread_.quit();
        sttThread_.wait(5000);
    }
    if (audioThread_.isRunning()) {
        QMetaObject::invokeMethod(voiceInput_, &VoiceInputController::stop,
            Qt::BlockingQueuedConnection);
        audioThread_.quit();
        audioThread_.wait(3000);
    }
    if (telemetryThread_.isRunning()) {
        QMetaObject::invokeMethod(telemetryManager_, &TelemetryManager::stop,
            Qt::BlockingQueuedConnection);
        telemetryThread_.quit();
        telemetryThread_.wait(3000);
    }
}

void Application::beginPushToTalk()
{
    if (!startupReady_) return;
    if (!pushToTalkPressed_) {
        pushToTalkPressed_ = true;
        updateAudioDuckingState();
        messageDispatcher_->clear();
        if (pttSoundPlayer_->source().isValid()) {
            pttSoundPlayer_->setPosition(0);
            pttSoundPlayer_->play();
        }
        emit requestBeginPushToTalk();
    }
}

void Application::endPushToTalk()
{
    if (pushToTalkPressed_) {
        pushToTalkPressed_ = false;
        updateAudioDuckingState();
        emit requestEndPushToTalk();
    }
}

QString Application::directInputBinding() const
{
    const auto& settings = settingsManager_.pushToTalk();
    if (settings.deviceGuid.isEmpty() || settings.buttonIndex < 0) {
        return QStringLiteral("Not mapped");
    }
    return QStringLiteral("%1 — Button %2").arg(settings.deviceName).arg(settings.buttonIndex + 1);
}

QString Application::ttsBackend() const
{
    return settingsManager_.tts().backend;
}

QVariantList Application::aiComputeDevices() const
{
    QVariantList devices = LocalAiRuntime::availableDevices();
    const QString selected = selectedAiComputeDevice();
    bool selectedAvailable = false;
    for (QVariant& value : devices) {
        QVariantMap option = value.toMap();
        const bool isSelected = option.value(QStringLiteral("id")).toString() == selected;
        option.insert(QStringLiteral("selected"), isSelected);
        selectedAvailable = selectedAvailable || isSelected;
        value = option;
    }
    if (!selectedAvailable && selected.startsWith(QStringLiteral("vulkan:"))) {
        devices.append(QVariantMap{
            {QStringLiteral("id"), selected},
            {QStringLiteral("label"), QStringLiteral("Vulkan — thiết bị không khả dụng")},
            {QStringLiteral("backend"), QStringLiteral("vulkan")},
            {QStringLiteral("available"), false},
            {QStringLiteral("selected"), true}});
    }
    return devices;
}

QString Application::selectedAiComputeDevice() const
{
    return settingsManager_.localAi().deviceId();
}

QString Application::aiComputeStatus() const
{
    QStringList status{
        QStringLiteral("PhoWhisper: %1").arg(whisperComputeBackend_),
        QStringLiteral("VieNeu-TTS: %1").arg(vieNeuComputeBackend_)};
    if (!aiRuntimeSelection_.fallbackReason.isEmpty()) {
        status.append(aiRuntimeSelection_.fallbackReason);
    }
    if (!whisperComputeFallback_.isEmpty()) {
        status.append(whisperComputeFallback_);
    }
    if (!vieNeuComputeFallback_.isEmpty()) {
        status.append(vieNeuComputeFallback_);
    }
    if (aiComputeRestartRequired()) {
        status.append(QStringLiteral("Thay đổi sẽ áp dụng sau khi khởi động lại"));
    }
    return status.join(QStringLiteral(" · "));
}

bool Application::aiComputeRestartRequired() const
{
    return selectedAiComputeDevice() != aiRuntimeSelection_.requestedDeviceId;
}

void Application::setAiComputeDevice(const QString& deviceId)
{
    if (deviceId == selectedAiComputeDevice()) {
        return;
    }
    bool available = false;
    for (const QVariant& value : LocalAiRuntime::availableDevices()) {
        const QVariantMap option = value.toMap();
        if (option.value(QStringLiteral("id")).toString() == deviceId
            && option.value(QStringLiteral("available")).toBool()) {
            available = true;
            break;
        }
    }
    if (!available) {
        return;
    }

    LocalAiSettings settings;
    if (deviceId == QStringLiteral("cpu")) {
        settings.computeMode = QStringLiteral("cpu");
    } else if (deviceId.startsWith(QStringLiteral("vulkan:"))) {
        settings.computeMode = QStringLiteral("vulkan");
        settings.vulkanDevice = deviceId;
    }
    settingsManager_.setLocalAi(settings);
    emit aiComputeSettingsChanged();
    emit aiComputeDevicesChanged();
    emit aiComputeStatusChanged();
}

void Application::setTtsBackend(const QString& backend)
{
    Q_UNUSED(backend);
    auto settings = settingsManager_.tts();
    settings.backend = QStringLiteral("VieNeu-TTS");
    settingsManager_.setTts(settings);
}

QVariantList Application::availableTtsVoices() const
{
    QVariantList list;
    QJsonObject presetsObj;
    const QString voicesJsonPath = QDir(resolveVieNeuModelPath()).filePath(QStringLiteral("voices_v3_turbo.json"));
    QFile file(voicesJsonPath);
        if (file.open(QIODevice::ReadOnly)) {
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            presetsObj = doc.object().value(QStringLiteral("presets")).toObject();
        }

    static const struct {
            const char* id;
            const char* fallbackDesc;
        } kKnownPresets[] = {
            {"Minh Đức", "Nam · Bắc · Tin tức"},
            {"Mai Anh", "Nữ · Bắc · Tin tức"},
            {"Thái Sơn", "Nam · Nam · Kể chuyện"},
            {"Thanh Bình", "Nam · Bắc · Kể chuyện"},
            {"Trúc Ly", "Nữ · Bắc · Tự nhiên"},
            {"Đoan Trang", "Nữ · Bắc · Tự nhiên"},
            {"Ngọc Linh", "Nữ · Bắc · Kể chuyện"},
            {"Phạm Tuyên", "Nam · Bắc · Tự nhiên"},
            {"Xuân Vĩnh", "Nam · Nam · Tự nhiên"},
            {"Thục Đoan", "Nữ · Nam · Kể chuyện"}
        };
        const auto isRemovedVoice = [](const QString& id) {
            return id.compare(QStringLiteral("Kiên Trần"), Qt::CaseInsensitive) == 0
                || id.compare(QStringLiteral("kientran"), Qt::CaseInsensitive) == 0;
        };
        const auto cleanDescription = [](QString desc) {
            desc.replace(QStringLiteral(" (LoRA)"), QString{}, Qt::CaseInsensitive);
            desc.replace(QStringLiteral("(LoRA)"), QString{}, Qt::CaseInsensitive);
            return desc.trimmed();
        };

        for (const auto& item : kKnownPresets) {
            const QString id = QString::fromUtf8(item.id);
            if (isRemovedVoice(id)) continue;
            QString label = id;
            if (!presetsObj.isEmpty() && presetsObj.contains(id)) {
                const QJsonObject p = presetsObj.value(id).toObject();
                const QString desc = cleanDescription(p.value(QStringLiteral("description")).toString());
                label = desc.isEmpty() ? id : QStringLiteral("%1 (%2)").arg(id, desc);
            } else {
                label = QStringLiteral("%1 (%2)").arg(id, QString::fromUtf8(item.fallbackDesc));
            }
            list.append(QVariantMap{
                {QStringLiteral("id"), id},
                {QStringLiteral("name"), label}
            });
        }

        // Dynamically include any custom voices added to voices_v3_turbo.json
        for (auto it = presetsObj.begin(); it != presetsObj.end(); ++it) {
            const QString id = it.key();
            if (isRemovedVoice(id)) continue;
            bool alreadyAdded = false;
            for (const auto& item : kKnownPresets) {
                if (id == QString::fromUtf8(item.id)) {
                    alreadyAdded = true;
                    break;
                }
            }
            if (!alreadyAdded) {
                const QJsonObject p = it.value().toObject();
                const QString desc = cleanDescription(p.value(QStringLiteral("description")).toString());
                const QString label = desc.isEmpty() ? QStringLiteral("%1 (Tùy chỉnh)").arg(id)
                                                    : QStringLiteral("%1 (%2)").arg(id, desc);
                list.append(QVariantMap{
                    {QStringLiteral("id"), id},
                    {QStringLiteral("name"), label}
                });
            }
        }
    return list;
}

void Application::setTtsVoice(const QString& voice)
{
    const QString trimmed = voice.trimmed();
    if (trimmed.isEmpty() || settingsManager_.tts().voice == trimmed) return;
    auto settings = settingsManager_.tts();
    settings.voice = trimmed;
    settingsManager_.setTts(settings);
    if (vieNeuTtsBackend_) {
        vieNeuTtsBackend_->setVoice(trimmed);
    }
    if (ttsBackend_ == vieNeuTtsBackend_) {
        ttsStatus_ = ttsAvailable_
            ? QStringLiteral("%1 sẵn sàng").arg(ttsBackend_->backendName())
            : QStringLiteral("Thiếu runtime/model %1").arg(ttsBackend_->backendName());
        emit ttsStatusChanged();
    }
    emit ttsVoiceChanged();
    qCInfo(logTts) << "Selected TTS voice:" << trimmed;
}

QStringList Application::audioOutputDevices() const
{
    QStringList devices;
    for (const QAudioDevice& device : QMediaDevices::audioOutputs()) {
        devices.append(device.description());
    }
    return devices;
}

QString Application::selectedAudioOutput() const
{
    if (!settingsManager_.tts().outputDevice.isEmpty()) {
        return settingsManager_.tts().outputDevice;
    }
    return QMediaDevices::defaultAudioOutput().description();
}

void Application::setAudioOutputDevice(const QString& description)
{
    auto settings = settingsManager_.tts();
    settings.outputDevice = audioOutputDevices().contains(description) ? description : QString{};
    settingsManager_.setTts(settings);
    vieNeuTtsBackend_->setAudioOutputDevice(settings.outputDevice);
    pttSoundOutput_->setDevice(QMediaDevices::defaultAudioOutput());
    for (const QAudioDevice& device : QMediaDevices::audioOutputs()) {
        if (device.description() == settings.outputDevice) {
            pttSoundOutput_->setDevice(device);
            break;
        }
    }
    emit audioOutputChanged();
}

void Application::refreshAudioInputDevices()
{
    QVariantList devices;
    devices.append(QVariantMap{{QStringLiteral("id"), QString{}},
        {QStringLiteral("name"), QStringLiteral("Default (System)")}});
    for (const auto& device : AudioCapture::inputDevices()) {
        devices.append(QVariantMap{{QStringLiteral("id"), stableAudioInputId(device.id)},
            {QStringLiteral("name"), device.description}});
        qCInfo(logAudio) << "Enumerated microphone:" << device.description
                         << "device_id=" << device.id.toHex();
    }
    if (audioInputDevices_ == devices) {
        return;
    }
    audioInputDevices_ = devices;
    emit audioInputChanged();
}

QString Application::selectedAudioInput() const
{
    const auto& settings = settingsManager_.audioInput();
    return settings.deviceId.isEmpty() ? QStringLiteral("Default (System)") : settings.deviceName;
}

QString Application::selectedAudioInputId() const
{
    return settingsManager_.audioInput().deviceId;
}

void Application::setAudioInputDevice(const QString& deviceId)
{
    refreshAudioInputDevices();
    const QString selectedId = deviceId.trimmed();
    AudioInputSettings settings = settingsManager_.audioInput();
    if (selectedId.isEmpty()) {
        settings.deviceId.clear();
        settings.deviceName = QStringLiteral("Default (System)");
    } else {
        bool found = false;
        for (const auto& device : AudioCapture::inputDevices()) {
            if (stableAudioInputId(device.id) != selectedId) {
                continue;
            }
            settings.deviceId = selectedId;
            settings.deviceName = device.description;
            found = true;
            break;
        }
        if (!found) {
            qCWarning(logAudio).noquote()
                << "Refusing unknown microphone selection; retaining current device_id=" << settings.deviceId;
            return;
        }
    }
    settingsManager_.setAudioInput(settings);
    microphoneName_ = settings.deviceName;
    emit microphoneChanged();
    emit audioInputChanged();
    qCInfo(logAudio) << "Selected microphone:" << settings.deviceName
                     << "device_id=" << settings.deviceId;
    emit requestConfigureAudioInput(audioInputIdBytes(settings.deviceId));
}

void Application::setTtsVolume(const double volume)
{
    auto settings = settingsManager_.tts();
    settings.volume = std::clamp(static_cast<float>(volume), 0.0F, 1.0F);
    settingsManager_.setTts(settings);
    vieNeuTtsBackend_->setVolume(settings.volume);
    emit ttsVolumeChanged();
}

void Application::setAudioDuckingEnabled(const bool enabled)
{
    auto settings = settingsManager_.tts();
    if (settings.audioDucking == enabled) return;
    settings.audioDucking = enabled;
    settingsManager_.setTts(settings);
    if (audioDucker_) audioDucker_->setEnabled(enabled);
    updateAudioDuckingState();
    emit audioDuckingEnabledChanged();
}

void Application::retryStartup()
{
    if (startupReady_) return;
    startupError_.clear();
    emit startupChanged();
    if (!sttWarmUpReady_) {
        QMetaObject::invokeMethod(speechRecognizer_, "warmUp", Qt::QueuedConnection);
    }
    if (!ttsWarmUpReady_) {
        ttsBackend_->warmUp();
    }
}

void Application::onSttWarmUpFinished(const bool success, const QString& error)
{
    sttWarmUpReady_ = success;
    if (!success && whisperComputeBackend_ == QStringLiteral("Đang khởi tạo")) {
        whisperComputeBackend_ = QStringLiteral("Không sẵn sàng");
        whisperComputeFallback_ = error;
    }
    emit aiComputeStatusChanged();
    if (!success) {
        startupError_ = QStringLiteral("PhoWhisper STT không khởi tạo được.");
        qCWarning(logApp).noquote() << "Startup STT warm-up failed:" << error;
    }
    finishStartupIfReady();
}

void Application::onTtsWarmUpFinished(const bool success, const QString& error)
{
    if (startupReady_) return;
    ttsWarmUpReady_ = success;
    if (!success && vieNeuComputeBackend_ == QStringLiteral("Chưa khởi tạo")) {
        vieNeuComputeBackend_ = QStringLiteral("Không sẵn sàng");
        vieNeuComputeFallback_ = error;
    }
    emit aiComputeStatusChanged();
    if (!success) {
        startupError_ = QStringLiteral("VieNeu-TTS không khởi tạo được.");
        qCWarning(logApp).noquote() << "Startup TTS warm-up failed:" << error;
    }
    finishStartupIfReady();
}

void Application::finishStartupIfReady()
{
    if (sttWarmUpReady_ && ttsWarmUpReady_) {
        startupReady_ = true;
        startupError_.clear();
        qCInfo(logApp) << "STT and TTS warm-up complete; enabling the main UI";
    }
    emit startupChanged();
}

bool Application::isAudioDucked() const noexcept
{
    return audioDucker_ && audioDucker_->isDucked();
}

void Application::updateAudioDuckingState()
{
    if (!audioDucker_) return;
    const bool shouldDuck = audioDuckingEnabled() && (pushToTalkPressed_ || isSpeaking_);
    audioDucker_->setDucked(shouldDuck);
}

void Application::startDirectInput(const quintptr nativeWindowHandle)
{
    const auto& settings = settingsManager_.pushToTalk();
    emit requestStartDirectInput(nativeWindowHandle);
    emit requestConfigureDirectInput(settings.directInputEnabled,
        settings.deviceGuid, settings.buttonIndex);
}

void Application::setPushToTalkOptions(const bool keyboardEnabled,
    const bool directInputEnabled)
{
    auto settings = settingsManager_.pushToTalk();
    settings.keyboardEnabled = keyboardEnabled;
    settings.directInputEnabled = directInputEnabled;
    settingsManager_.setPushToTalk(settings);
    emit requestConfigureDirectInput(settings.directInputEnabled,
        settings.deviceGuid, settings.buttonIndex);
    emit pttSettingsChanged();
}

void Application::beginDirectInputMapping()
{
    directInputStatus_ = QStringLiteral("Waiting for a DirectInput button…");
    emit directInputStatusChanged();
    emit requestDirectInputMapping();
}

bool Application::mockAvailable() const noexcept
{
#if RACEENGINEER_ENABLE_MOCK
    return true;
#else
    return false;
#endif
}

void Application::setUseMockTelemetry(const bool enabled)
{
    const bool accepted = enabled && mockAvailable();
    if (mockEnabled_ == accepted) {
        return;
    }
    mockEnabled_ = accepted;
    emit mockEnabledChanged();
    emit requestMockTelemetry(mockEnabled_);
}

void Application::setApiReasoning(const bool enabled)
{
    auto settings = settingsManager_.llm();
    if (settings.reasoning == enabled) return;
    settings.reasoning = enabled;
    settingsManager_.setLlm(settings);
    llmManager_->configure(settingsManager_.llm(), apiKey_);
    emit apiSettingsChanged();
}

void Application::setDriverName(const QString& name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || settingsManager_.driverName() == trimmed) return;
    settingsManager_.setDriverName(trimmed);
    if (llmManager_) {
        llmManager_->setDriverName(trimmed);
    }
    emit driverNameChanged();
}

void Application::setResponseStyle(const QString& style)
{
    const QString trimmed = style.trimmed();
    if (trimmed.isEmpty() || settingsManager_.responseStyle() == trimmed) return;
    settingsManager_.setResponseStyle(trimmed);
    if (llmManager_) {
        llmManager_->setResponseStyle(settingsManager_.responseStyle());
    }
    emit responseStyleChanged();
}

void Application::saveAiSettings(const QString& provider, const QString& baseUrl,
    const QString& apiKey, const QString& model, const bool streaming,
    const int timeoutMilliseconds, const int maximumTokens, const double temperature,
    const bool reasoning)
{
    LlmSettings settings = settingsManager_.llm();
    settings.provider = provider.trimmed().isEmpty() ? QStringLiteral("OpenAI Compatible") : provider.trimmed();
    settings.baseUrl = baseUrl.trimmed();
    settings.model = model.trimmed();
    settings.streaming = streaming;
    settings.timeoutMilliseconds = timeoutMilliseconds;
    settings.maximumTokens = maximumTokens;
    settings.temperature = temperature;
    settings.reasoning = reasoning;
    settingsManager_.setLlm(settings);
    if (!CredentialStore::writeApiKey(apiKey.trimmed())) {
        apiState_ = QStringLiteral("Unavailable");
        apiDetail_ = QStringLiteral("Could not update API key in Windows Credential Manager.");
        emit apiStateChanged();
        return;
    }
    apiKey_ = CredentialStore::readApiKey();
    apiConfigured_ = !settingsManager_.llm().baseUrl.trimmed().isEmpty()
        && !settingsManager_.llm().model.trimmed().isEmpty();
    llmManager_->configure(settingsManager_.llm(), apiKey_);
    emit apiSettingsChanged();
    if (apiConfigured_) {
        llmManager_->testConnection();
    }
}

void Application::testApiConnection(const QString& baseUrl, const QString& apiKey,
    const QString& model)
{
    manualConnectionTestActive_ = true;
    streamedResponse_.clear();
    onVoiceStatusChanged(QStringLiteral("Thinking"));
    if (!baseUrl.trimmed().isEmpty() && !model.trimmed().isEmpty()) {
        LlmSettings tempSettings = settingsManager_.llm();
        tempSettings.baseUrl = baseUrl.trimmed();
        tempSettings.model = model.trimmed();
        llmManager_->configure(tempSettings, apiKey.trimmed());
    }
    llmManager_->testConnection();
}

void Application::appendConversationMessage(const QString& role, const QString& text)
{
    if (text.trimmed().isEmpty()) return;
    conversationLog_.append(QVariantMap{{QStringLiteral("role"), role},
        {QStringLiteral("text"), text.trimmed()}});
    while (conversationLog_.size() > 100) conversationLog_.removeFirst();
    emit conversationLogChanged();
}

void Application::updateEngineerMessage(const QString& text)
{
    latestEngineerText_ = text;
    if (!conversationLog_.isEmpty()) {
        QVariantMap message = conversationLog_.last().toMap();
        if (message.value(QStringLiteral("role")).toString() == QStringLiteral("engineer")) {
            message.insert(QStringLiteral("text"), text);
            conversationLog_.last() = message;
            emit conversationLogChanged();
            return;
        }
    }
    appendConversationMessage(QStringLiteral("engineer"), text);
}

void Application::askText(const QString& text)
{
    if (!startupReady_) return;
    if (text.trimmed().isEmpty()) return;
    latestUserText_ = text.trimmed();
    latestEngineerText_.clear();
    streamedResponse_.clear();
    appendConversationMessage(QStringLiteral("driver"), latestUserText_);
    onVoiceStatusChanged(QStringLiteral("Thinking"));
    emit interactionChanged();
    llmManager_->ask(latestUserText_, latestState_, raceHistory_,
        QStringLiteral("Vietnamese by default; use English only if the driver clearly writes in English"),
        pitStrategyToolData());
}

void Application::resetConversation()
{
    llmManager_->resetConversation();
    latestUserText_.clear();
    latestEngineerText_.clear();
    streamedResponse_.clear();
    conversationLog_.clear();
    emit conversationLogChanged();
    emit interactionChanged();
}

void Application::onStateUpdated(const RaceState& state)
{
    const bool previousWasRace = latestState_.sessionType == SessionType::Race;
    const QString previousSessionId = strategySessionId_;
    strategyRecorder_.setSimulatorConnected(state.connected);
    strategyRecorder_.setRaceActive(state.connected && state.sessionType == SessionType::Race);
    latestState_ = state;
    const QString sessionKey = state.connected
        ? QStringLiteral("%1|%2|%3|%4|%5")
              .arg(static_cast<int>(state.simulator))
              .arg(state.track ? utf8(*state.track) : QString{})
              .arg(state.carModel ? utf8(*state.carModel) : QString{})
              .arg(state.totalLaps.value_or(0))
              .arg(static_cast<int>(state.sessionType.value_or(SessionType::Unknown)))
        : QString{};
    const bool sessionChanged = sessionKey != strategySessionKey_
        || (state.currentLap && strategyLastTelemetryLap_ > 0 && *state.currentLap < strategyLastTelemetryLap_);
    if (sessionChanged) {
        if (previousWasRace && strategyRecordedLap_ > 0)
            strategyRecorder_.finishSession(previousSessionId);
        raceHistory_.reset();
        strategySessionKey_ = sessionKey;
        strategySessionId_ = state.connected ? QUuid::createUuid().toString(QUuid::WithoutBraces) : QString{};
        strategyRecordedLap_ = 0;
        strategyLapExcluded_ = true;
        strategyObservedLap_ = 0;
        strategyRequestedLap_ = 0;
        strategyAnnouncedPrepareLap_ = 0;
        strategyAnnouncedPitLap_ = 0;
        strategyPitLap_ = 0;
        strategyLastLegalLap_ = false;
        strategySeenStart_ = false;
        strategyPitted_ = false;
        strategyWasInPit_ = false;
        strategyWasInPitBox_ = false;
        ++strategyRevision_;
        if (settingsManager_.strategyEnabled()) setStrategyState(QStringLiteral("Đang chờ dữ liệu"),
            QStringLiteral("Đang xác định phiên đua và lịch sử vòng."));
    }
    strategyLastTelemetryLap_ = state.currentLap.value_or(0);
    raceHistory_.update(state);
    const bool isInPit = state.pitState && (*state.pitState == PitState::Entering
        || *state.pitState == PitState::PitLane || *state.pitState == PitState::PitBox);
    const bool isInPitBox = state.pitState && *state.pitState == PitState::PitBox;
    const bool enteredPit = isInPit && !strategyWasInPit_;
    const bool excludedSample = !state.pitState || *state.pitState != PitState::Track
        || (state.flag && (*state.flag == FlagState::Yellow || *state.flag == FlagState::Red
            || *state.flag == FlagState::Black));
    strategyLapExcluded_ = strategyLapExcluded_ || excludedSample;
    const bool exitedPit = !isInPit && strategyWasInPit_;
    const bool enteredPitBox = isInPitBox && !strategyWasInPitBox_;
    const bool exitedPitBox = !isInPitBox && strategyWasInPitBox_;
    if (state.connected && state.sessionType == SessionType::Race) {
        if (enteredPit) strategyRecorder_.recordPitEvent(strategySessionId_, state, true);
        if (exitedPitBox) strategyRecorder_.recordPitBoxEvent(strategySessionId_, state, false);
        if (enteredPitBox) strategyRecorder_.recordPitBoxEvent(strategySessionId_, state, true);
        if (exitedPit) strategyRecorder_.recordPitEvent(strategySessionId_, state, false);
    }
    strategyWasInPit_ = isInPit;
    strategyWasInPitBox_ = isInPitBox;
    if (state.connected && state.sessionType == SessionType::Race && state.currentLap
        && !raceHistory_.laps().empty()) {
        const auto& completed = raceHistory_.laps().back();
        if (completed.lapNumber == *state.currentLap - 1
            && completed.lapNumber != strategyRecordedLap_) {
            strategyRecordedLap_ = completed.lapNumber;
            strategyRecorder_.record(strategySessionId_, state, completed, strategyLapExcluded_);
            strategyLapExcluded_ = excludedSample;
        }
    }
    updateStrategy(state);
    auto events = eventEngine_.process(state);
    auto spotterEvents = spotterEngine_.process(state);
    events.insert(events.end(), spotterEvents.begin(), spotterEvents.end());
    for (const auto& event : events) {
        latestEvent_ = utf8(event.message);
        QVariantMap entry;
        entry.insert(QStringLiteral("message"), latestEvent_);
        entry.insert(QStringLiteral("priority"), static_cast<int>(event.priority));
        entry.insert(QStringLiteral("timestamp"),
            QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
        eventLog_.prepend(entry);
        while (eventLog_.size() > 100) {
            eventLog_.removeLast();
        }
        qCInfo(logEvent).noquote() << latestEvent_;
        messageDispatcher_->enqueue(latestEvent_, event.priority);
    }
    if (!events.empty()) {
        emit latestEventChanged();
    }
    telemetry_ = toVariantMap(state);
    if (const auto average = raceHistory_.averageFuelConsumption())
        telemetry_.insert(QStringLiteral("fuelPerLapLiters"), *average);
    if (const auto remaining = raceHistory_.estimatedFuelLapsRemaining(state))
        telemetry_.insert(QStringLiteral("estimatedFuelLaps"), *remaining);
    emit telemetryChanged();
}

void Application::setStrategyEnabled(const bool enabled)
{
    if (settingsManager_.strategyEnabled() == enabled) return;
    settingsManager_.setStrategyEnabled(enabled);
    ++strategyRevision_;
    strategyRequestedLap_ = 0;
    strategyPitLap_ = 0;
    strategyLastLegalLap_ = false;
    strategyAnnouncedPrepareLap_ = 0;
    strategyAnnouncedPitLap_ = 0;
    messageDispatcher_->cancelByPrefix(QStringLiteral("Chuẩn bị vào pit"));
    messageDispatcher_->cancelByPrefix(QStringLiteral("Vào pit cuối vòng"));
    if (enabled) {
        setStrategyState(QStringLiteral("Đang chờ dữ liệu"),
            QStringLiteral("Đang kiểm tra chiến thuật, race profile và telemetry."));
        updateStrategy(latestState_);
    } else {
        setStrategyState(QStringLiteral("Đã tắt"),
            QStringLiteral("Bật để chọn vòng pit khi có dữ liệu chiến thuật đã duyệt."));
    }
}

bool Application::configureStrategySharing(const QString& endpoint, const QString& token)
{
    const QString trimmedEndpoint = endpoint.trimmed();
    const QUrl url(trimmedEndpoint, QUrl::StrictMode);
    if (!url.isValid() || url.scheme() != QStringLiteral("https") || url.host().isEmpty()
        || !url.userInfo().isEmpty() || !url.query().isEmpty() || !url.fragment().isEmpty())
        return false;
    QString savedToken = CredentialStore::readRaceDataShareToken();
    if (!token.trimmed().isEmpty()) {
        savedToken = token.trimmed();
        if (savedToken.size() < 32) return false;
        if (!CredentialStore::writeRaceDataShareToken(savedToken)) return false;
    }
    if (savedToken.size() < 32) return false;
    settingsManager_.setStrategyShareEndpoint(trimmedEndpoint);
    strategyRecorder_.setSharingConfig(trimmedEndpoint, savedToken);
    return true;
}

void Application::setStrategyState(const QString& status, const QString& detail, const int pitLap)
{
    if (strategyStatus_ == status && strategyDetail_ == detail && strategyPitLap_ == pitLap) return;
    strategyStatus_ = status;
    strategyDetail_ = detail;
    strategyPitLap_ = pitLap;
    emit strategyChanged();
}

QJsonObject Application::pitStrategyToolData() const
{
    if (!settingsManager_.strategyEnabled() || !strategyAvailable() || !latestState_.connected
        || strategyStatus_ != QStringLiteral("Sẵn sàng") || strategyPitLap_ <= 0)
        return {{QStringLiteral("available"), false},
            {QStringLiteral("status"), strategyStatus_},
            {QStringLiteral("reason"), strategyDetail_}};
    return {{QStringLiteral("available"), true},
        {QStringLiteral("pit_lap"), strategyPitLap_},
        {QStringLiteral("pit_timing"), QStringLiteral("end_of_lap")}};
}

void Application::announceStrategy(const QString& text, const EventPriority priority)
{
    latestEvent_ = text;
    QVariantMap entry;
    entry.insert(QStringLiteral("message"), text);
    entry.insert(QStringLiteral("priority"), static_cast<int>(priority));
    entry.insert(QStringLiteral("timestamp"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    eventLog_.prepend(entry);
    while (eventLog_.size() > 100) eventLog_.removeLast();
    emit latestEventChanged();
    messageDispatcher_->enqueue(text, priority);
}

void Application::updateStrategy(const RaceState& state)
{
    if (!settingsManager_.strategyEnabled()) return;
    if (!strategyAvailable()) {
        if (strategyPitLap_) {
            messageDispatcher_->cancelByPrefix(QStringLiteral("Chuẩn bị vào pit"));
            messageDispatcher_->cancelByPrefix(QStringLiteral("Vào pit cuối vòng"));
        }
        strategyLastLegalLap_ = false;
        setStrategyState(strategyPredictor_->artifactStatus(),
            QStringLiteral("Đang chờ dữ liệu chiến thuật pit đủ điều kiện để duyệt."));
        return;
    }
    if (!state.connected || !state.currentLap) {
        setStrategyState(QStringLiteral("Đang chờ dữ liệu"),
            QStringLiteral("Đang chờ simulator AC / ACC kết nối."));
        return;
    }
    const int lap = *state.currentLap;
    const bool inPit = state.pitState && (*state.pitState == PitState::Entering
        || *state.pitState == PitState::PitLane || *state.pitState == PitState::PitBox);
    if (lap == 1 && !inPit) strategySeenStart_ = true;
    if (inPit) {
        strategyPitted_ = true;
    }
    if (strategyPitted_) {
        if (strategyPitLap_) {
            messageDispatcher_->cancelByPrefix(QStringLiteral("Chuẩn bị vào pit"));
            messageDispatcher_->cancelByPrefix(QStringLiteral("Vào pit cuối vòng"));
        }
        setStrategyState(QStringLiteral("Đã vào pit"),
            QStringLiteral("Đã phát hiện xe vào pit; chưa có telemetry xác nhận dịch vụ, nên dừng khuyến nghị một stop."));
        return;
    }
    if (!strategySeenStart_) {
        setStrategyState(QStringLiteral("Chưa đủ dữ liệu"),
            QStringLiteral("Cần quan sát cuộc đua từ vòng 1 để xác nhận tuổi stint và chưa pit."));
        return;
    }
    if (lap != strategyObservedLap_) {
        strategyObservedLap_ = lap;
        strategyRequestedLap_ = 0;
        ++strategyRevision_;
    }
    const int stintLaps = lap - 1;
    if (strategyPitLap_ && !strategyPredictor_->stillLegal(state, raceHistory_, stintLaps, strategyPitLap_)) {
        messageDispatcher_->cancelByPrefix(QStringLiteral("Chuẩn bị vào pit"));
        messageDispatcher_->cancelByPrefix(QStringLiteral("Vào pit cuối vòng"));
        strategyPitLap_ = 0;
        strategyLastLegalLap_ = false;
        strategyAnnouncedPrepareLap_ = 0;
        strategyAnnouncedPitLap_ = 0;
        strategyRequestedLap_ = 0;
        ++strategyRevision_;
        setStrategyState(QStringLiteral("Đang tính lại"),
            QStringLiteral("Phương án cũ không còn hợp lệ với nhiên liệu và luật pit."));
    }
    if (strategyRequestedLap_ != lap && !strategyPredictor_->busy()) {
        const quint64 revision = strategyRevision_;
        const QString session = strategySessionKey_;
        const QString status = strategyPredictor_->prepare(state, raceHistory_, stintLaps, revision,
            [this, session](StrategyDecision result) {
                if (!settingsManager_.strategyEnabled() || session != strategySessionKey_
                    || result.revision != strategyRevision_ || !latestState_.currentLap
                    || result.currentLap != *latestState_.currentLap || strategyPitted_) return;
                if (!result.error.isEmpty()) {
                    messageDispatcher_->cancelByPrefix(QStringLiteral("Chuẩn bị vào pit"));
                    messageDispatcher_->cancelByPrefix(QStringLiteral("Vào pit cuối vòng"));
                    strategyLastLegalLap_ = false;
                    setStrategyState(QStringLiteral("Lỗi chiến thuật"), result.error);
                    return;
                }
                if (!strategyPredictor_->stillLegal(latestState_, raceHistory_,
                        *latestState_.currentLap - 1, result.pitLap)) {
                    strategyRequestedLap_ = 0;
                    setStrategyState(QStringLiteral("Đang tính lại"),
                        QStringLiteral("Kết quả đã hết hạn do nhiên liệu hoặc trạng thái đua thay đổi."));
                    return;
                }
                if (result.fromPlanner && !result.decisive) {
                    messageDispatcher_->cancelByPrefix(QStringLiteral("Chuẩn bị vào pit"));
                    messageDispatcher_->cancelByPrefix(QStringLiteral("Vào pit cuối vòng"));
                    strategyLastLegalLap_ = false;
                    strategyAnnouncedPrepareLap_ = 0;
                    strategyAnnouncedPitLap_ = 0;
                    setStrategyState(QStringLiteral("Cửa sổ pit"),
                        QStringLiteral("Các vòng hợp lệ chưa khác biệt đủ rõ để tự gọi pit."));
                    return;
                }
                if (strategyPitLap_ && strategyPitLap_ != result.pitLap) {
                    messageDispatcher_->cancelByPrefix(QStringLiteral("Chuẩn bị vào pit"));
                    messageDispatcher_->cancelByPrefix(QStringLiteral("Vào pit cuối vòng"));
                    strategyAnnouncedPrepareLap_ = 0;
                    strategyAnnouncedPitLap_ = 0;
                }
                strategyLastLegalLap_ = result.fromPlanner && result.lastLegalLap;
                setStrategyState(QStringLiteral("Sẵn sàng"),
                    result.fromPlanner
                        ? (result.lastLegalLap
                            ? QStringLiteral("Vòng %1 là vòng pit cuối còn hợp lệ theo luật và nhiên liệu.").arg(result.pitLap)
                            : QStringLiteral("Dự kiến vào pit cuối vòng %1 để giảm thời gian còn lại.").arg(result.pitLap))
                        : QStringLiteral("XGBoost Ranker chọn vào pit cuối vòng %1 theo profile điều kiện khô.").arg(result.pitLap),
                    result.pitLap);
                updateStrategy(latestState_);
            });
        if (status == QStringLiteral("Đang tính chiến thuật")) {
            strategyRequestedLap_ = lap;
            if (!strategyPitLap_) setStrategyState(status, QStringLiteral("Đang tính các vòng pit hợp lệ."));
        } else if (!strategyPitLap_) {
            setStrategyState(status, status);
        }
    }
    if (!strategyPitLap_) return;
    if (lap == strategyPitLap_ - 1 && strategyAnnouncedPrepareLap_ != strategyPitLap_) {
        strategyAnnouncedPrepareLap_ = strategyPitLap_;
        announceStrategy(QStringLiteral("Chuẩn bị vào pit ở vòng tiếp theo."), EventPriority::Engineer);
    }
    if (lap == strategyPitLap_ && strategyAnnouncedPitLap_ != lap) {
        const auto lapTime = state.currentLapTimeSeconds;
        if (lapTime && *lapTime >= 0.0 && *lapTime <= 5.0) {
            strategyAnnouncedPitLap_ = lap;
            announceStrategy(QStringLiteral("Vào pit cuối vòng này."), EventPriority::Important);
        } else {
            setStrategyState(QStringLiteral("Thông báo muộn"),
                QStringLiteral("Đã qua điểm báo an toàn; không phát lệnh pit gấp."), strategyPitLap_);
        }
    }
}

void Application::onConnectionStatusChanged(const QString& simulator, const bool connected)
{
    const bool changed = simulatorName_ != simulator || connected_ != connected;
    simulatorName_ = simulator;
    connected_ = connected;
    if (changed) {
        emit connectionChanged();
    }
}

void Application::onVoiceStatusChanged(const QString& status)
{
    if (voiceStatus_ != status) {
        voiceStatus_ = status;
        emit voiceStatusChanged();
    }
}

void Application::onUtteranceReady(const QByteArray& pcm16k)
{
    qCInfo(logAudio) << "PTT utterance complete:" << pcm16k.size() / 2 << "samples";
    onVoiceStatusChanged(QStringLiteral("Recognizing"));
    emit requestTranscription(pcm16k, QStringLiteral("vi"));
}

void Application::onTranscriptionReady(const QString& text, const QString& detectedLanguage)
{
    latestUserText_ = text;
    latestEngineerText_.clear();
    streamedResponse_.clear();
    appendConversationMessage(QStringLiteral("driver"), latestUserText_);
    emit interactionChanged();
    if (!apiConfigured_) {
        updateEngineerMessage(QStringLiteral("Configure the LLM server URL and model in AI & Voice."));
        onVoiceStatusChanged(QStringLiteral("Idle"));
        emit interactionChanged();
        return;
    }
    onVoiceStatusChanged(QStringLiteral("Thinking"));
    llmManager_->ask(text, latestState_, raceHistory_,
        detectedLanguage.startsWith(QStringLiteral("en"), Qt::CaseInsensitive)
            ? QStringLiteral("English") : QStringLiteral("Vietnamese"),
        pitStrategyToolData());
}

bool Application::eventFilter(QObject* const watched, QEvent* const event)
{
    Q_UNUSED(watched)
    if (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease) {
        return false;
    }
    const auto* const key = static_cast<QKeyEvent*>(event);
    if (!settingsManager_.pushToTalk().keyboardEnabled || key->key() != Qt::Key_Space
        || !(key->modifiers() & Qt::ControlModifier)
        || key->isAutoRepeat()) {
        return false;
    }
    if (event->type() == QEvent::KeyPress) {
        beginPushToTalk();
    } else {
        endPushToTalk();
    }
    return true;
}

QVariantMap Application::toVariantMap(const RaceState& state)
{
    QVariantMap map;
    map.insert(QStringLiteral("simulator"),
        QString::fromLatin1(::raceengineer::simulatorName(state.simulator)));
    map.insert(QStringLiteral("connected"), state.connected);

    if (state.track) map.insert(QStringLiteral("track"), utf8(*state.track));
    if (state.carModel) map.insert(QStringLiteral("carModel"), utf8(*state.carModel));
    if (state.carCategory) map.insert(QStringLiteral("carCategory"), utf8(*state.carCategory));
    if (state.carSubclass) map.insert(QStringLiteral("carSubclass"), utf8(*state.carSubclass));
    if (state.driverName) map.insert(QStringLiteral("driverName"), utf8(*state.driverName));
    if (state.sessionType) map.insert(QStringLiteral("sessionType"),
        QString::fromLatin1(sessionTypeName(*state.sessionType)));
    addOptional(map, "sessionTimeSeconds", state.sessionTimeSeconds);
    addOptional(map, "timeRemainingSeconds", state.timeRemainingSeconds);
    addOptional(map, "currentLap", state.currentLap);
    addOptional(map, "totalLaps", state.totalLaps);
    addOptional(map, "lapsRemaining", state.lapsRemaining);
    addOptional(map, "position", state.position);
    addOptional(map, "speedKmh", state.speedKmh);
    addOptional(map, "rpm", state.rpm);
    addOptional(map, "gear", state.gear);
    addOptional(map, "throttle", state.throttle);
    addOptional(map, "brake", state.brake);
    addOptional(map, "clutch", state.clutch);
    addOptional(map, "steering", state.steering);
    addOptional(map, "currentLapTimeSeconds", state.currentLapTimeSeconds);
    addOptional(map, "previousLapTimeSeconds", state.previousLapTimeSeconds);
    addOptional(map, "bestLapTimeSeconds", state.bestLapTimeSeconds);
    addOptional(map, "currentDeltaSeconds", state.currentDeltaSeconds);
    addOptional(map, "fuelLiters", state.fuelLiters);
    addOptional(map, "fuelCapacityLiters", state.fuelCapacityLiters);
    addOptional(map, "engineTemperatureCelsius", state.engineTemperatureCelsius);
    addOptional(map, "oilTemperatureCelsius", state.oilTemperatureCelsius);
    addOptional(map, "waterTemperatureCelsius", state.waterTemperatureCelsius);
    addOptional(map, "pitLimiter", state.pitLimiter);
    addOptional(map, "tractionControl", state.tractionControl);
    addOptional(map, "abs", state.abs);
    addOptional(map, "gapAheadSeconds", state.gapAheadSeconds);
    addOptional(map, "gapBehindSeconds", state.gapBehindSeconds);
    if (state.opponentAhead) map.insert(QStringLiteral("opponentAhead"), utf8(*state.opponentAhead));
    if (state.opponentBehind) map.insert(QStringLiteral("opponentBehind"), utf8(*state.opponentBehind));
    if (state.flag) map.insert(QStringLiteral("flag"), QString::fromLatin1(flagName(*state.flag)));
    if (state.pitState) map.insert(QStringLiteral("pitState"), QString::fromLatin1(pitStateName(*state.pitState)));
    if (state.tyreTemperaturesCelsius) map.insert(QStringLiteral("tyreTemperatures"),
        wheelsToList(*state.tyreTemperaturesCelsius));
    if (state.tyrePressuresPsi) map.insert(QStringLiteral("tyrePressures"),
        wheelsToList(*state.tyrePressuresPsi));
    if (state.tyreWear) map.insert(QStringLiteral("tyreWear"), wheelsToList(*state.tyreWear));
    if (state.brakeTemperaturesCelsius) map.insert(QStringLiteral("brakeTemperatures"),
        wheelsToList(*state.brakeTemperaturesCelsius));
    if (state.suspensionDamage) map.insert(QStringLiteral("suspensionDamage"),
        wheelsToList(*state.suspensionDamage));
    return map;
}

} // namespace raceengineer
