#include "app/Application.h"

#include "telemetry/TelemetryManager.h"
#include "audio/VoiceInputController.h"
#include "stt/WhisperRecognizer.h"
#include "config/CredentialStore.h"
#include "llm/LLMManager.h"
#include "audio/MessageDispatcher.h"
#include "tts/GwenTtsBackend.h"
#include "tts/PiperTtsBackend.h"
#include "input/DInputButtonMonitor.h"
#include "utils/Logging.h"

#include <QCoreApplication>
#include <QDir>
#include <QDateTime>
#include <QEvent>
#include <QKeyEvent>
#include <QJsonDocument>
#include <QMetaObject>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QAudioDevice>
#include <QMediaDevices>
#include <QFileInfo>
#include <QUrl>
#include <QVariantList>

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

QString resolveGwenModelPath()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString q4k = QDir(appDir).filePath(QStringLiteral("models/gwen-tts/gwen-tts-0.6b-q4_k.gguf"));
    if (QFileInfo::exists(q4k)) return q4k;
    return QDir(appDir).filePath(QStringLiteral("models/gwen-tts/gwen-tts-0.6b-q8_0.gguf"));
}

QString resolvePiperPythonPath()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString packaged = QDir(appDir).filePath(QStringLiteral("tools/piper/Scripts/python.exe"));
    if (QFileInfo::exists(packaged)) return packaged;
    return QDir(appDir).absoluteFilePath(
        QStringLiteral("../runtime/piper/venv/Scripts/python.exe"));
}

QString resolvePiperModelPath()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString packaged = QDir(appDir).filePath(
        QStringLiteral("models/piper/vi_VN-vais1000-medium.onnx"));
    if (QFileInfo::exists(packaged)) return packaged;
    return QDir(appDir).absoluteFilePath(
        QStringLiteral("../models/piper/vi_VN-vais1000-medium.onnx"));
}

QString resolvePhoWhisperModelPath()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString packaged = QDir(appDir).filePath(
        QStringLiteral("models/ggml-phowhisper-medium-q5_0.bin"));
    if (QFileInfo::exists(packaged)) return packaged;
    return QDir(appDir).absoluteFilePath(
        QStringLiteral("../models/ggml-phowhisper-medium-q5_0.bin"));
}

} // namespace

Application::Application(const bool startWithMock, QObject* const parent)
    : QObject(parent)
    , telemetryManager_(new TelemetryManager)
    , voiceInput_(new VoiceInputController)
    , speechRecognizer_(new WhisperRecognizer(resolvePhoWhisperModelPath()))
    , piperTtsBackend_(new PiperTtsBackend(resolvePiperPythonPath(), resolvePiperModelPath(), this))
    , gwenTtsBackend_(new GwenTtsBackend(
          QDir(QCoreApplication::applicationDirPath()).filePath(
              QStringLiteral("tools/gwen-tts/crispasr.exe")),
          resolveGwenModelPath(),
          QDir(QCoreApplication::applicationDirPath()).filePath(
              QStringLiteral("models/gwen-tts/qwen3-tts-tokenizer-12hz.gguf")),
          QDir(QCoreApplication::applicationDirPath()).filePath(
              QStringLiteral("voices/gwen-tts")), this))
    , ttsBackend_(piperTtsBackend_)
    , directInput_(new DInputButtonMonitor)
    , pttSoundPlayer_(new QMediaPlayer(this))
    , pttSoundOutput_(new QAudioOutput(this))
    , mockEnabled_(startWithMock && mockAvailable())
    , apiKey_(settingsManager_.migratedFromLegacyMistral() ? QString{} : CredentialStore::readApiKey())
    , llmManager_(std::make_unique<LLMManager>(settingsManager_.llm(), apiKey_))
    , messageDispatcher_(std::make_unique<MessageDispatcher>(piperTtsBackend_))
{
    qRegisterMetaType<RaceState>();
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

    connect(&audioThread_, &QThread::started, voiceInput_, &VoiceInputController::start);
    connect(&audioThread_, &QThread::finished, voiceInput_, &QObject::deleteLater);
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
        latestEngineerText_ = error;
        onVoiceStatusChanged(QStringLiteral("Idle"));
        emit microphoneChanged();
        emit interactionChanged();
    }, Qt::QueuedConnection);
    connect(voiceInput_, &VoiceInputController::utteranceReady,
        this, &Application::onUtteranceReady, Qt::QueuedConnection);

    connect(&sttThread_, &QThread::started, speechRecognizer_, &WhisperRecognizer::warmUp);
    connect(&sttThread_, &QThread::finished, speechRecognizer_, &QObject::deleteLater);
    connect(this, &Application::requestTranscription,
        speechRecognizer_, &WhisperRecognizer::transcribe, Qt::QueuedConnection);
    connect(speechRecognizer_, &WhisperRecognizer::recognitionStarted, this, [this] {
        onVoiceStatusChanged(QStringLiteral("Recognizing"));
    }, Qt::QueuedConnection);
    connect(speechRecognizer_, &WhisperRecognizer::transcriptionReady,
        this, &Application::onTranscriptionReady, Qt::QueuedConnection);
    connect(speechRecognizer_, &WhisperRecognizer::recognitionError, this, [this](const QString& error) {
        latestEngineerText_ = error;
        onVoiceStatusChanged(QStringLiteral("Idle"));
        emit interactionChanged();
    }, Qt::QueuedConnection);
    connect(speechRecognizer_, &WhisperRecognizer::recognitionFinished, this, [this] {
        if (voiceStatus_ == QStringLiteral("Recognizing")) {
            latestEngineerText_ = QStringLiteral("Whisper không trả về kết quả nhận dạng.");
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
        latestEngineerText_ = streamedResponse_;
        emit interactionChanged();
    });
    connect(llmManager_.get(), &LLMManager::responseReady, this, [this](const QString& text) {
        latestEngineerText_ = text;
        streamedResponse_.clear();
        onVoiceStatusChanged(QStringLiteral("Idle"));
        emit interactionChanged();
        messageDispatcher_->enqueue(text, EventPriority::Conversation);
    });
    connect(llmManager_.get(), &LLMManager::errorOccurred, this, [this](const QString& message) {
        latestEngineerText_ = message;
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
            latestEngineerText_ = detail;
            streamedResponse_.clear();
            onVoiceStatusChanged(QStringLiteral("Idle"));
            emit interactionChanged();
        });

    if (settingsManager_.tts().backend.compare(QStringLiteral("Gwen-TTS"),
            Qt::CaseInsensitive) == 0) {
        ttsBackend_ = gwenTtsBackend_;
        messageDispatcher_->setBackend(ttsBackend_);
    }
    piperTtsBackend_->setAudioOutputDevice(settingsManager_.tts().outputDevice);
    gwenTtsBackend_->setAudioOutputDevice(settingsManager_.tts().outputDevice);
    ttsAvailable_ = ttsBackend_->isAvailable();
    ttsStatus_ = ttsAvailable_
        ? QStringLiteral("%1 sẵn sàng").arg(ttsBackend_->backendName())
        : QStringLiteral("Thiếu runtime/model %1").arg(ttsBackend_->backendName());
    connect(messageDispatcher_.get(), &MessageDispatcher::speakingChanged, this,
        [this](const bool speaking, const QString&) {
            ttsStatus_ = speaking
                ? QStringLiteral("Đang chuẩn bị · %1…").arg(ttsBackend_->backendName())
                : (ttsAvailable_
                    ? QStringLiteral("%1 sẵn sàng").arg(ttsBackend_->backendName())
                    : QStringLiteral("Thiếu runtime/model %1").arg(ttsBackend_->backendName()));
            if (speaking) onVoiceStatusChanged(QStringLiteral("Speaking"));
            else if (voiceStatus_ == QStringLiteral("Speaking")) onVoiceStatusChanged(QStringLiteral("Idle"));
            emit ttsStatusChanged();
    });
    connect(piperTtsBackend_, &PiperTtsBackend::statusChanged,
        this, [this](const QString& status) {
            if (ttsBackend_ != piperTtsBackend_) return;
            ttsStatus_ = status;
            emit ttsStatusChanged();
        }, Qt::QueuedConnection);
    connect(gwenTtsBackend_, &GwenTtsBackend::statusChanged,
        this, [this](const QString& status) {
            if (ttsBackend_ != gwenTtsBackend_) return;
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
    connectBackendError(piperTtsBackend_);
    connectBackendError(gwenTtsBackend_);
    if (ttsBackend_ == gwenTtsBackend_) gwenTtsBackend_->warmUp();

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
}

Application::~Application()
{
    QCoreApplication::instance()->removeEventFilter(this);
    messageDispatcher_->clear();
    if (inputThread_.isRunning()) {
        QMetaObject::invokeMethod(directInput_, &DInputButtonMonitor::stop,
            Qt::BlockingQueuedConnection);
        inputThread_.quit();
        inputThread_.wait(3000);
    }
    piperTtsBackend_->stop();
    gwenTtsBackend_->shutdown();
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
    if (!pushToTalkPressed_) {
        pushToTalkPressed_ = true;
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

void Application::setTtsBackend(const QString& backend)
{
    const bool useGwen = backend.compare(QStringLiteral("Gwen-TTS"), Qt::CaseInsensitive) == 0;
    ITtsBackend* const selected = useGwen
        ? static_cast<ITtsBackend*>(gwenTtsBackend_)
        : static_cast<ITtsBackend*>(piperTtsBackend_);
    if (selected == ttsBackend_) return;

    messageDispatcher_->clear();
    ttsBackend_->stop();
    if (ttsBackend_ == gwenTtsBackend_) gwenTtsBackend_->shutdown();
    ttsBackend_ = selected;
    messageDispatcher_->setBackend(ttsBackend_);

    TtsSettings settings = settingsManager_.tts();
    settings.backend = useGwen ? QStringLiteral("Gwen-TTS") : QStringLiteral("Piper");
    settingsManager_.setTts(settings);
    ttsAvailable_ = ttsBackend_->isAvailable();
    ttsStatus_ = ttsAvailable_
        ? QStringLiteral("%1 sẵn sàng").arg(ttsBackend_->backendName())
        : QStringLiteral("Thiếu runtime/model %1").arg(ttsBackend_->backendName());
    emit ttsStatusChanged();
    if (ttsBackend_ == gwenTtsBackend_) gwenTtsBackend_->warmUp();
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
    piperTtsBackend_->setAudioOutputDevice(settings.outputDevice);
    gwenTtsBackend_->setAudioOutputDevice(settings.outputDevice);
    pttSoundOutput_->setDevice(QMediaDevices::defaultAudioOutput());
    for (const QAudioDevice& device : QMediaDevices::audioOutputs()) {
        if (device.description() == settings.outputDevice) {
            pttSoundOutput_->setDevice(device);
            break;
        }
    }
    emit audioOutputChanged();
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

void Application::saveAiSettings(const QString& provider, const QString& baseUrl,
    const QString& apiKey, const QString& model, const bool streaming,
    const int timeoutMilliseconds, const int maximumTokens, const double temperature)
{
    LlmSettings settings = settingsManager_.llm();
    settings.provider = provider.trimmed().isEmpty() ? QStringLiteral("OpenAI Compatible") : provider.trimmed();
    settings.baseUrl = baseUrl.trimmed();
    settings.model = model.trimmed();
    settings.streaming = streaming;
    settings.timeoutMilliseconds = timeoutMilliseconds;
    settings.maximumTokens = maximumTokens;
    settings.temperature = temperature;
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
}

void Application::testApiConnection()
{
    streamedResponse_.clear();
    onVoiceStatusChanged(QStringLiteral("Thinking"));
    llmManager_->testConnection();
}

void Application::askText(const QString& text)
{
    if (text.trimmed().isEmpty()) return;
    latestUserText_ = text.trimmed();
    latestEngineerText_.clear();
    streamedResponse_.clear();
    onVoiceStatusChanged(QStringLiteral("Thinking"));
    emit interactionChanged();
    llmManager_->ask(latestUserText_, latestState_, raceHistory_,
        QStringLiteral("Vietnamese by default; use English only if the driver clearly writes in English"));
}

void Application::resetConversation()
{
    llmManager_->resetConversation();
    latestUserText_.clear();
    latestEngineerText_.clear();
    streamedResponse_.clear();
    emit interactionChanged();
}

void Application::onStateUpdated(const RaceState& state)
{
    latestState_ = state;
    raceHistory_.update(state);
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
    emit telemetryChanged();
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
    emit interactionChanged();
    if (!apiConfigured_) {
        latestEngineerText_ = QStringLiteral("Configure the LLM server URL and model in AI & Voice.");
        onVoiceStatusChanged(QStringLiteral("Idle"));
        emit interactionChanged();
        return;
    }
    onVoiceStatusChanged(QStringLiteral("Thinking"));
    llmManager_->ask(text, latestState_, raceHistory_,
        detectedLanguage.startsWith(QStringLiteral("en"), Qt::CaseInsensitive)
            ? QStringLiteral("English") : QStringLiteral("Vietnamese"));
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
    return map;
}

} // namespace raceengineer
