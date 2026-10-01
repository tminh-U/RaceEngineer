#include "app/Application.h"

#include "telemetry/TelemetryManager.h"
#include "audio/AudioCapture.h"
#include "audio/VoiceInputController.h"
#include "stt/WhisperRecognizer.h"
#include "config/CredentialStore.h"
#include "llm/LLMManager.h"
#include "audio/MessageDispatcher.h"
#include "audio/AudioDucker.h"
#include "tts/GoogleTtsBackend.h"
#include "tts/VieNeuTtsBackend.h"
#include "tts/TtsBenchmark.h"
#include "input/DInputButtonMonitor.h"
#include "utils/Logging.h"

#include <QCoreApplication>
#include <QDir>
#include <QDateTime>
#include <QEvent>
#include <QFile>
#include <QKeyEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaObject>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QAudioDevice>
#include <QMediaDevices>
#include <QSettings>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QFileInfo>
#include <QUrl>
#include <QVariantList>
#include <QUuid>

#include <algorithm>
#include <cmath>

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

QString lapTimeText(const double seconds)
{
    if (!std::isfinite(seconds) || seconds <= 0.0) return {};
    const auto milliseconds = static_cast<qint64>(std::llround(seconds * 1000.0));
    return QStringLiteral("%1:%2.%3")
        .arg(milliseconds / 60000)
        .arg((milliseconds / 1000) % 60, 2, 10, QLatin1Char('0'))
        .arg(milliseconds % 1000, 3, 10, QLatin1Char('0'));
}

MessageSource eventMessageSource(const EventType type)
{
    switch (type) {
    case EventType::CarLeft:
    case EventType::CarRight:
        return MessageSource::ProximitySpotter;
    case EventType::FuelLow:
    case EventType::FuelCritical: return MessageSource::FuelAlerts;
    case EventType::EngineHot:
    case EventType::EngineCritical: return MessageSource::EngineAlerts;
    case EventType::TyreOverheating: return MessageSource::TyreAlerts;
    case EventType::NewBestLap:
    case EventType::LapDelta: return MessageSource::LapDelta;
    case EventType::YellowFlag:
    case EventType::BlueFlag:
    case EventType::GreenFlag:
    case EventType::RedFlag:
    case EventType::BlackFlag:
    case EventType::WhiteFlag:
    case EventType::ChequeredFlag: return MessageSource::FlagAlerts;
    case EventType::DamageDetected: return MessageSource::DamageAlerts;
    default:
        return MessageSource::General;
    }
}

QString cachedEventSpeech(const EventType type)
{
    switch (type) {
    case EventType::FuelLow: return QStringLiteral("Nhiên liệu sắp hết.");
    case EventType::FuelCritical: return QStringLiteral("Nhiên liệu nguy cấp.");
    case EventType::EngineHot: return QStringLiteral("Nhiệt độ động cơ cao.");
    case EventType::EngineCritical: return QStringLiteral("Động cơ quá nhiệt.");
    case EventType::YellowFlag: return QStringLiteral("Cờ vàng.");
    case EventType::BlueFlag: return QStringLiteral("Cờ xanh dương.");
    case EventType::GreenFlag: return QStringLiteral("Cờ xanh lá.");
    case EventType::RedFlag: return QStringLiteral("Cờ đỏ.");
    case EventType::BlackFlag: return QStringLiteral("Cờ đen.");
    case EventType::WhiteFlag: return QStringLiteral("Cờ trắng.");
    case EventType::ChequeredFlag: return QStringLiteral("Cờ ca rô.");
    case EventType::CarLeft: return QStringLiteral("Có xe bên trái.");
    case EventType::CarRight: return QStringLiteral("Có xe bên phải.");
    case EventType::TyreOverheating: return QStringLiteral("Lốp quá nóng.");
    case EventType::DamageDetected: return QStringLiteral("Phát hiện hư hại xe.");
    default: return {};
    }
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
          resolveVieNeuModelPath(), settingsManager_.tts().voice, aiRuntimeSelection_,
        settingsManager_.tts().cpuThreads, this))
    , googleTtsBackend_(new GoogleTtsBackend(this))
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
    setupVisible_ = !settingsManager_.setupCompleted();
    benchmarkFingerprint_ = ttsBenchmarkFingerprint(resolveVieNeuModelPath(),
        settingsManager_.tts().voice, selectedAiComputeDevice());
    ttsBenchmark_ = std::make_unique<TtsBenchmark>();
    connect(ttsBenchmark_.get(), &TtsBenchmark::changed, this, &Application::ttsBenchmarkChanged);
    connect(vieNeuTtsBackend_, &VieNeuTtsBackend::modelReleased, this, [this] {
        localContextReleased_ = true;
        if (ttsBenchmarkRunning()) {
            ttsBenchmark_->start(QDir(QCoreApplication::applicationDirPath()).filePath("RaceEngineerTtsBench.exe"),
                {"--model-dir", resolveVieNeuModelPath(), "--voice", settingsManager_.tts().voice,
                    "--device", aiRuntimeSelection_.requestedDeviceId});
        } else if (ttsBackend_ == vieNeuTtsBackend_) {
            vieNeuTtsBackend_->setCpuThreads(settingsManager_.tts().cpuThreads);
            localContextReleased_ = false;
            vieNeuTtsBackend_->warmUp();
        }
    });
    connect(ttsBenchmark_.get(), &TtsBenchmark::finished, this, [this](int selected) {
        if (selected) {
            auto settings = settingsManager_.tts();
            settings.cpuThreads = selected;
            settingsManager_.setTts(settings);
            ++featureSettingsRevision_;
            emit aiComputeSettingsChanged();
        }
        settingsManager_.setTtsBenchmark({{"timestamp", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
            {"fingerprint", benchmarkFingerprint_}, {"voice", settingsManager_.tts().voice},
            {"device", aiRuntimeSelection_.requestedDeviceId}, {"corpus_version", ttsBenchmarkVersion},
            {"complete", selected != 0}, {"selected_threads", selected},
            {"status", ttsBenchmark_->status()}, {"results", ttsBenchmark_->results()}});
        if (localContextReleased_ && ttsBackend_ == vieNeuTtsBackend_) {
            vieNeuTtsBackend_->setCpuThreads(settingsManager_.tts().cpuThreads);
            localContextReleased_ = false;
            vieNeuTtsBackend_->warmUp();
        }
        emit ttsBenchmarkChanged();
    });
    strategyRecorder_.setEnabled(settingsManager_.strategyRecordingEnabled());
    connect(&strategyRecorder_, &StrategyRecorder::enabledChanged, this,
            [this](bool enabled) {
                settingsManager_.setStrategyRecordingEnabled(enabled);
                ++featureSettingsRevision_;
            });
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
    llmManager_->setFeatureControlHandlers(
        [this] { return featureSettings(); },
        [this](const QString& feature, const bool enabled, const quint64 revision) {
            return setFeatureEnabled(feature, enabled, revision);
        });
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
                streamedResponse_.clear();
                onVoiceStatusChanged(QStringLiteral("Idle"));
                emit interactionChanged();
            }
        });

    const auto& ttsSettings = settingsManager_.tts();
    const QString googleKey = CredentialStore::readGoogleTtsApiKey();
    googleTtsApiKeyConfigured_ = !googleKey.trimmed().isEmpty();
    googleTtsBackend_->setApiKey(googleKey);
    googleTtsBackend_->setModel(ttsSettings.googleModel);
    googleTtsBackend_->setVoice(ttsSettings.googleVoice, ttsSettings.googleVoiceId);
    googleTtsBackend_->setAudioOutputDevice(ttsSettings.outputDevice);
    googleTtsBackend_->setVolume(ttsSettings.volume);
    vieNeuTtsBackend_->setVoice(ttsSettings.voice);
    vieNeuTtsBackend_->setAudioOutputDevice(ttsSettings.outputDevice);
    vieNeuTtsBackend_->setVolume(ttsSettings.volume);
    messageDispatcher_->setLocalBackend(vieNeuTtsBackend_);
    ttsBackend_ = ttsSettings.backend == QStringLiteral("Google Gemini API")
        ? static_cast<ITtsBackend*>(googleTtsBackend_)
        : static_cast<ITtsBackend*>(vieNeuTtsBackend_);
    messageDispatcher_->setBackend(ttsBackend_);

    const auto connectWarmUp = [this](ITtsBackend* const backend) {
        connect(backend, &ITtsBackend::warmUpFinished, this,
            [this, backend](const bool success, const QString& error) {
                if (backend == ttsBackend_) onTtsWarmUpFinished(success, error);
            }); // Backends own GUI-thread state; do not queue a stale warm-up completion again.
    };
    connectWarmUp(vieNeuTtsBackend_);
    connectWarmUp(googleTtsBackend_);
    connect(vieNeuTtsBackend_, &VieNeuTtsBackend::runtimeBackendChanged, this,
        [this](const QString& backend, const QString& fallbackReason) {
            vieNeuComputeBackend_ = backend;
            vieNeuComputeFallback_ = fallbackReason;
            emit aiComputeStatusChanged();
            emit ttsBenchmarkChanged();
        }, Qt::QueuedConnection);
    ttsAvailable_ = ttsBackend_->isAvailable();
    ttsStatus_ = ttsAvailable_
        ? QStringLiteral("%1 sẵn sàng").arg(ttsBackend_->backendName())
        : QStringLiteral("Thiếu cấu hình/runtime %1").arg(ttsBackend_->backendName());
    connect(messageDispatcher_.get(), &MessageDispatcher::speakingChanged, this,
        [this](const bool speaking, const QString&) {
            isSpeaking_ = speaking;
            emit ttsBenchmarkChanged();
            updateAudioDuckingState();
            if (speaking) {
                ttsStatus_ = QStringLiteral("Đang chuẩn bị · %1…").arg(ttsBackend_->backendName());
                onVoiceStatusChanged(QStringLiteral("Speaking"));
            } else {
                ttsStatus_ = ttsAvailable_
                    ? QStringLiteral("%1 sẵn sàng").arg(ttsBackend_->backendName())
                    : QStringLiteral("Thiếu cấu hình/runtime %1").arg(ttsBackend_->backendName());
                if (voiceStatus_ == QStringLiteral("Speaking")) onVoiceStatusChanged(QStringLiteral("Idle"));
            }
            emit ttsStatusChanged();
        });
    connect(vieNeuTtsBackend_, &VieNeuTtsBackend::statusChanged, this,
        [this](const QString& status) {
            if (ttsBackend_ != vieNeuTtsBackend_) return;
            ttsStatus_ = status;
            emit ttsStatusChanged();
        }, Qt::QueuedConnection);
    const auto connectBackendError = [this](ITtsBackend* const backend) {
        connect(backend, &ITtsBackend::errorOccurred, this,
            [this, backend](const QString& error) {
                qCWarning(logTts).noquote() << error;
                if (ttsBackend_ != backend) return;
                QTimer::singleShot(0, this, [this, backend, error] {
                    if (ttsBackend_ != backend) return;
                    ttsStatus_ = error;
                    emit ttsStatusChanged();
                });
                if (backend == googleTtsBackend_ && !googleTtsFailureAnnounced_
                    && (connected_ || !setupVisible_)) {
                    googleTtsFailureAnnounced_ = true;
                    messageDispatcher_->enqueue(QStringLiteral("Google TTS hiện không khả dụng."),
                        EventPriority::Conversation, MessageSource::LocalSystem);
                }
            }, Qt::QueuedConnection);
    };
    connectBackendError(vieNeuTtsBackend_);
    connectBackendError(googleTtsBackend_);
    connect(googleTtsBackend_, &GoogleTtsBackend::speakingStarted, this,
        [this](const QString&) { googleTtsFailureAnnounced_ = false; }, Qt::QueuedConnection);
    connect(googleTtsBackend_, &GoogleTtsBackend::voiceCreated, this,
        [this](const QString& name, const QString& voiceId) {
            auto settings = settingsManager_.tts();
            settings.googleVoiceName = name;
            settings.googleVoiceId = voiceId;
            settings.googleCustomVoices.append(QJsonObject{{"name", name}, {"id", voiceId}});
            settingsManager_.setTts(settings);
            googleTtsBackend_->setVoice(name, voiceId);
            ++featureSettingsRevision_;
            emit googleTtsSettingsChanged();
        }, Qt::QueuedConnection);
    connect(googleTtsBackend_, &GoogleTtsBackend::voiceOperationFinished, this,
        [this](const bool, const QString& message) {
            if (ttsBackend_ == googleTtsBackend_) {
                ttsStatus_ = message;
                emit ttsStatusChanged();
            }
        }, Qt::QueuedConnection);

    connect(googleTtsBackend_, &GoogleTtsBackend::retryScheduled, this, [this](int attempt, int delayMs) {
        if (ttsBackend_ != googleTtsBackend_) return;
        ttsStatus_ = QStringLiteral("TTS đang thử kết nối lại %1/2 sau %2 ms…").arg(attempt).arg(delayMs);
        emit ttsStatusChanged();
    });
    connect(googleTtsBackend_, &GoogleTtsBackend::connectionRecovered, this, [this] {
        if (ttsBackend_ != googleTtsBackend_) return;
        ttsStatus_ = QStringLiteral("Đã kết nối lại Google TTS; yêu cầu kế tiếp sẽ thử phát audio.");
        emit ttsStatusChanged();
    });
    connect(googleTtsBackend_, &GoogleTtsBackend::firstAudioReceived, this, [this](qint64 milliseconds) {
        googleTtsFirstAudioMs_ = milliseconds;
        if (ttsBackend_ == googleTtsBackend_) {
            ttsStatus_ = QStringLiteral("Google TTS sẵn sàng.");
            emit ttsStatusChanged();
        }
        emit googleTtsSettingsChanged();
    });
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
            ++featureSettingsRevision_;
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
    ttsBenchmark_->disconnect(this);
    ttsBenchmark_.reset(); // Stop the child before restoring/freeing the application's native backend.
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
        sttThread_.wait(); // Model initialization may still be running when setup is closed early.
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
    if (!startupReady_ || ttsBenchmarkRunning() || ttsBenchmarkRestoring_) return;
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
    if (ttsBenchmarkRunning() || ttsBenchmarkRestoring_) return;
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
    benchmarkFingerprint_ = ttsBenchmarkFingerprint(resolveVieNeuModelPath(), settingsManager_.tts().voice, selectedAiComputeDevice());
    emit ttsBenchmarkChanged();
    emit aiComputeSettingsChanged();
    emit aiComputeDevicesChanged();
    emit aiComputeStatusChanged();
}

void Application::setTtsBackend(const QString& backend)
{
    if (ttsBenchmarkRunning() || ttsBenchmarkRestoring_) return;
    ITtsBackend* const selected = backend == QStringLiteral("Google Gemini API")
        ? static_cast<ITtsBackend*>(googleTtsBackend_)
        : static_cast<ITtsBackend*>(vieNeuTtsBackend_);
    if (selected == ttsBackend_) return;
    auto settings = settingsManager_.tts();
    settings.backend = selected == googleTtsBackend_
        ? QStringLiteral("Google Gemini API") : QStringLiteral("VieNeu-TTS");
    settingsManager_.setTts(settings);
    messageDispatcher_->setBackend(selected);
    const bool switchingToGoogle = selected == googleTtsBackend_;
    ttsBackend_ = selected;
    ttsAvailable_ = ttsBackend_->isAvailable();
    ttsWarmUpReady_ = false;
    startupError_.clear();
    ttsStatus_ = ttsAvailable_
        ? QStringLiteral("%1 sẵn sàng").arg(ttsBackend_->backendName())
        : QStringLiteral("Thiếu cấu hình/runtime %1").arg(ttsBackend_->backendName());
    if (switchingToGoogle) {
        vieNeuTtsBackend_->releaseModel();
        googleTtsBackend_->warmUp();
    } else {
        vieNeuTtsBackend_->warmUp();
    }
    ++featureSettingsRevision_;
    emit ttsStatusChanged();
    emit googleTtsSettingsChanged();
    emit startupChanged();
    emit ttsBenchmarkChanged();
}

void Application::setGoogleTtsApiKey(const QString& apiKey)
{
    const QString trimmed = apiKey.trimmed();
    if (trimmed.size() > 4096) return;
    if (!CredentialStore::writeGoogleTtsApiKey(trimmed)) {
        ttsStatus_ = QStringLiteral("Could not update Google TTS key in Windows Credential Manager.");
        emit ttsStatusChanged();
        return;
    }
    googleTtsApiKeyConfigured_ = !trimmed.isEmpty();
    googleTtsBackend_->setApiKey(trimmed);
    ++featureSettingsRevision_;
    emit googleTtsSettingsChanged();
    if (ttsBackend_ == googleTtsBackend_) googleTtsBackend_->warmUp();
}

void Application::setGoogleTtsModel(const QString& model)
{
    auto settings = settingsManager_.tts();
    settings.googleModel = model;
    settingsManager_.setTts(settings);
    googleTtsBackend_->setModel(settingsManager_.tts().googleModel);
    ++featureSettingsRevision_;
    emit googleTtsSettingsChanged();
}

QString Application::googleTtsVoice() const
{
    const auto& settings = settingsManager_.tts();
    return settings.googleVoiceId.isEmpty() ? settings.googleVoice : settings.googleVoiceName;
}

QStringList Application::googleTtsVoices() const
{
    QStringList voices = GoogleTtsBackend::builtInVoices();
    const auto& settings = settingsManager_.tts();
    for (const auto& value : settings.googleCustomVoices) {
        const auto name = value.toObject().value("name").toString();
        if (!voices.contains(name)) voices.append(name);
    }
    return voices;
}

void Application::setGoogleTtsVoice(const QString& voice)
{
    const QString selected = voice.trimmed();
    if (selected.isEmpty()) return;
    auto settings = settingsManager_.tts();
    const auto custom = std::find_if(settings.googleCustomVoices.cbegin(),
        settings.googleCustomVoices.cend(), [&selected](const QJsonValue& value) {
            return value.toObject().value("name").toString() == selected;
        });
    if (custom != settings.googleCustomVoices.cend()) {
        settings.googleVoiceName = selected;
        settings.googleVoiceId = custom->toObject().value("id").toString();
        googleTtsBackend_->setVoice(selected, settings.googleVoiceId);
    } else {
        const auto voices = GoogleTtsBackend::builtInVoices();
        const auto match = std::find_if(voices.cbegin(), voices.cend(), [&selected](const QString& item) {
            return item.compare(selected, Qt::CaseInsensitive) == 0;
        });
        if (match == voices.cend()) return;
        settings.googleVoice = *match;
        settings.googleVoiceId.clear();
        settings.googleVoiceName.clear();
        googleTtsBackend_->setVoice(settings.googleVoice);
    }
    settingsManager_.setTts(settings);
    ++featureSettingsRevision_;
    emit googleTtsSettingsChanged();
}

void Application::createGoogleTtsVoice(const QString& name, const QString& sourceWavPath,
    const QString& consentWavPath)
{
    if (ttsBenchmarkRunning() || ttsBenchmarkRestoring_) return;
    if (googleTtsVoices().contains(name.trimmed()) || settingsManager_.tts().googleCustomVoices.size() >= 200) {
        ttsStatus_ = QStringLiteral("Tên giọng đã tồn tại hoặc danh sách giọng đã đầy.");
        emit ttsStatusChanged();
        return;
    }
    const QFileInfo sourceInfo(sourceWavPath);
    const QFileInfo consentInfo(consentWavPath);
    constexpr qint64 maximumWavBytes = 5 * 1024 * 1024;
    if (!sourceInfo.isFile() || !consentInfo.isFile()
        || sourceInfo.size() > maximumWavBytes || consentInfo.size() > maximumWavBytes) {
        ttsStatus_ = QStringLiteral("Choose WAV files under 5 MiB each.");
        emit ttsStatusChanged();
        return;
    }
    QFile source(sourceInfo.absoluteFilePath());
    QFile consent(consentInfo.absoluteFilePath());
    if (!source.open(QIODevice::ReadOnly) || !consent.open(QIODevice::ReadOnly)) {
        ttsStatus_ = QStringLiteral("Could not read the selected voice WAV files.");
        emit ttsStatusChanged();
        return;
    }
    googleTtsBackend_->createReplicatedVoice(name, source.readAll(), consent.readAll());
}

QString Application::bonoVoiceDescription() const { return GoogleTtsBackend::bonoVoiceDescription(); }

void Application::createGooglePromptedVoice(const QString& name, const QString& description)
{
    if (ttsBenchmarkRunning() || ttsBenchmarkRestoring_) return;
    if (googleTtsVoices().contains(name.trimmed()) || settingsManager_.tts().googleCustomVoices.size() >= 200) {
        ttsStatus_ = QStringLiteral("Tên giọng đã tồn tại hoặc danh sách giọng đã đầy.");
        emit ttsStatusChanged();
        return;
    }
    ttsStatus_ = QStringLiteral("Đang tạo giọng %1 từ mô tả…").arg(name.trimmed());
    emit ttsStatusChanged();
    googleTtsBackend_->createPromptedVoice(name, description);
}

void Application::previewGoogleTtsVoice()
{
    if (ttsBenchmarkRunning() || ttsBenchmarkRestoring_) return;
    if (ttsBackend_ == googleTtsBackend_)
    messageDispatcher_->enqueue(QStringLiteral("Kiểm tra giọng kỹ sư."), EventPriority::Conversation);
}

void Application::setTtsCpuThreads(const int threads)
{
    if (ttsBenchmarkRunning() || ttsBenchmarkRestoring_ || isSpeaking_ || connected_ || VieNeuTtsBackend::hasExternalThreadOverride()) return;
    if (threads != 2 && threads != 3 && threads != 4) return;
    if (settingsManager_.tts().cpuThreads == threads) return;
    auto settings = settingsManager_.tts();
    settings.cpuThreads = threads;
    settingsManager_.setTts(settings);
    localContextReleased_ = false;
    vieNeuTtsBackend_->releaseModel();
    ++featureSettingsRevision_;
    emit aiComputeSettingsChanged();
    emit ttsBenchmarkChanged();
}

bool Application::ttsExternalThreadOverride() const { return VieNeuTtsBackend::hasExternalThreadOverride(); }


void Application::openSetup()
{
    setupVisible_ = true;
    settingsManager_.setSetupState(false, 0);
    emit setupChanged();
}
void Application::setSetupStep(int step)
{
    if (!setupVisible_ || ttsBenchmarkRunning()) return;
    settingsManager_.setSetupState(false, step);
    emit setupChanged();
}
void Application::finishSetup()
{
    if (ttsBenchmarkRunning()) cancelTtsBenchmark();
    setupVisible_ = false;
    settingsManager_.setSetupState(true, settingsManager_.setupStep());
    emit setupChanged();
}
bool Application::ttsBenchmarkRunning() const { return ttsBenchmark_ && ttsBenchmark_->running(); }
double Application::ttsBenchmarkProgress() const { return ttsBenchmark_ ? ttsBenchmark_->progress() : 0; }
QString Application::ttsBenchmarkStatus() const
{
    if (ttsBenchmark_ && !ttsBenchmark_->status().isEmpty()) return ttsBenchmark_->status();
    return settingsManager_.ttsBenchmark().value("status").toString();
}
QVariantList Application::ttsBenchmarkResults() const
{
    const auto rows = ttsBenchmark_ && !ttsBenchmark_->status().isEmpty()
        ? ttsBenchmark_->results() : settingsManager_.ttsBenchmark().value("results").toArray();
    return rows.toVariantList();
}
bool Application::ttsBenchmarkStale() const
{
    const auto previous = settingsManager_.ttsBenchmark();
    const auto rows = previous.value("results").toArray();
    return !previous.isEmpty() && (previous.value("fingerprint").toString() != benchmarkFingerprint_
        || (!rows.isEmpty() && vieNeuComputeBackend_.contains(QStringLiteral("backbone"))
            && rows[0].toObject().value("runtime").toString() != vieNeuComputeBackend_));
}
QString Application::ttsBenchmarkUnavailableReason() const
{
    if (ttsBenchmarkRestoring_ && !ttsBenchmarkRunning()) return QStringLiteral("Đang khôi phục VieNeu…");
    if (settingsManager_.tts().backend == QStringLiteral("Google Gemini API"))
        return QStringLiteral("Không cần benchmark TTS local.");
    if (VieNeuTtsBackend::hasExternalThreadOverride())
        return QStringLiteral("OMP_NUM_THREADS bên ngoài đang ghi đè setting; tự chọn bị vô hiệu hóa.");
    if (connected_ || latestState_.connected) return QStringLiteral("Ngắt kết nối AC / ACC trước khi đo.");
    if (isSpeaking_ || pushToTalkPressed_ || voiceStatus_ != QStringLiteral("Idle"))
        return QStringLiteral("Chờ radio và hội thoại kết thúc trước khi đo.");
    if (aiComputeRestartRequired()) return QStringLiteral("Khởi động lại để áp dụng GPU đã chọn trước khi đo.");
    if (!vieNeuTtsBackend_->isAvailable()) return QStringLiteral("Thiếu mô hình VieNeu; có thể bỏ qua bước này.");
    return {};
}
bool Application::ttsBenchmarkAllowed() const
{
    return !ttsBenchmarkRunning() && ttsBenchmarkUnavailableReason().isEmpty();
}
void Application::startTtsBenchmark()
{
    if (!ttsBenchmarkAllowed()) return;
    benchmarkFingerprint_ = ttsBenchmarkFingerprint(resolveVieNeuModelPath(), settingsManager_.tts().voice, selectedAiComputeDevice());
    messageDispatcher_->setConversationSuppressed(true);
    ttsBenchmarkRestoring_ = true;
    localContextReleased_ = false;
    ttsBenchmark_->prepare();
    vieNeuTtsBackend_->releaseModel();
}
void Application::cancelTtsBenchmark() { if (ttsBenchmark_) ttsBenchmark_->cancel(); }
void Application::previewTtsVoice()
{
    if (ttsBenchmarkRunning() || ttsBenchmarkRestoring_ || connected_ || isSpeaking_) return;
    messageDispatcher_->enqueue(QStringLiteral("Kiểm tra giọng kỹ sư."), EventPriority::Conversation);
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
    if (ttsBenchmarkRunning() || ttsBenchmarkRestoring_) return;
    const QString trimmed = voice.trimmed();
    if (trimmed.isEmpty() || settingsManager_.tts().voice == trimmed) return;
    auto settings = settingsManager_.tts();
    settings.voice = trimmed;
    settingsManager_.setTts(settings);
    benchmarkFingerprint_ = ttsBenchmarkFingerprint(resolveVieNeuModelPath(), settings.voice, selectedAiComputeDevice());
    emit ttsBenchmarkChanged();
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
    googleTtsBackend_->setAudioOutputDevice(settings.outputDevice);
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
    googleTtsBackend_->setVolume(settings.volume);
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
    ++featureSettingsRevision_;
    emit audioDuckingEnabledChanged();
}

bool Application::startWithWindows() const
{
#ifdef _WIN32
    QSettings registry(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                       QSettings::NativeFormat);
    return registry.value(QStringLiteral("RaceEngineer")).toString()
        == QStringLiteral("\"%1\"").arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
#else
    return false;
#endif
}

void Application::setStartWithWindows(const bool enabled)
{
#ifdef _WIN32
    QSettings registry(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                       QSettings::NativeFormat);
    if (enabled) {
        registry.setValue(QStringLiteral("RaceEngineer"),
            QStringLiteral("\"%1\"").arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath())));
    } else {
        registry.remove(QStringLiteral("RaceEngineer"));
    }
    registry.sync();
    if (registry.status() != QSettings::NoError) {
        qCWarning(logApp) << "Could not update Windows startup registry entry";
        return;
    }
    emit generalSettingsChanged();
#else
    Q_UNUSED(enabled);
#endif
}

bool Application::trayAvailable() const
{
    return QSystemTrayIcon::isSystemTrayAvailable();
}

void Application::setMinimizeToTray(const bool enabled)
{
    if (settingsManager_.minimizeToTray() == enabled || (enabled && !trayAvailable())) return;
    settingsManager_.setMinimizeToTray(enabled);
    emit generalSettingsChanged();
}

void Application::setMinimizeOnClose(const bool enabled)
{
    if (settingsManager_.minimizeOnClose() == enabled || (enabled && !trayAvailable())) return;
    settingsManager_.setMinimizeOnClose(enabled);
    emit generalSettingsChanged();
}

void Application::setGpuRendererEnabled(const bool enabled)
{
    if (settingsManager_.gpuRendererEnabled() == enabled) return;
    settingsManager_.setGpuRendererEnabled(enabled);
    emit generalSettingsChanged();
}

void Application::retryStartup()
{
    if (ttsBenchmarkRunning()) return;
    if (startupReady_ && startupError_.isEmpty()) return;
    startupError_.clear();
    emit startupChanged();
    if (!sttWarmUpReady_) QMetaObject::invokeMethod(speechRecognizer_, "warmUp", Qt::QueuedConnection);
    if (!ttsWarmUpReady_) ttsBackend_->warmUp();
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
    if (ttsBenchmarkRestoring_ && !ttsBenchmarkRunning()) {
        ttsBenchmarkRestoring_ = false;
        messageDispatcher_->setConversationSuppressed(false);
        emit ttsBenchmarkChanged();
    }
    ttsWarmUpReady_ = success;
    ttsAvailable_ = success && ttsBackend_->isAvailable();
    if (!success && ttsBackend_ == vieNeuTtsBackend_
        && vieNeuComputeBackend_ == QStringLiteral("Chưa khởi tạo")) {
        vieNeuComputeBackend_ = QStringLiteral("Không sẵn sàng");
        vieNeuComputeFallback_ = error;
    }
    emit aiComputeStatusChanged();
    ttsStatus_ = success
        ? QStringLiteral("%1 sẵn sàng").arg(ttsBackend_->backendName()) : error;
    if (!success) {
        startupError_ = error;
        qCWarning(logTts).noquote() << "TTS warm-up failed:" << error;
    }
    emit ttsStatusChanged();
    finishStartupIfReady();
}

void Application::finishStartupIfReady()
{
    if (sttWarmUpReady_ && ttsWarmUpReady_) {
        startupReady_ = true;
        startupError_.clear();
        QTimer::singleShot(0, this, &Application::speakPendingLapSummary);
        qCInfo(logApp) << "STT and TTS warm-up complete; enabling the main UI";
    } else if (!startupError_.isEmpty()) {
        // Failed optional model/API setup must not block telemetry, settings, or text interaction.
        startupReady_ = true;
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
    if (settings.keyboardEnabled == keyboardEnabled
        && settings.directInputEnabled == directInputEnabled) return;
    settings.keyboardEnabled = keyboardEnabled;
    settings.directInputEnabled = directInputEnabled;
    settingsManager_.setPushToTalk(settings);
    emit requestConfigureDirectInput(settings.directInputEnabled,
        settings.deviceGuid, settings.buttonIndex);
    ++featureSettingsRevision_;
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
    if (!startupReady_ || ttsBenchmarkRunning() || ttsBenchmarkRestoring_) return;
    if (text.trimmed().isEmpty()) return;
    latestUserText_ = text.trimmed();
    latestEngineerText_.clear();
    streamedResponse_.clear();
    appendConversationMessage(QStringLiteral("driver"), latestUserText_);
    onVoiceStatusChanged(QStringLiteral("Thinking"));
    emit interactionChanged();
    llmManager_->ask(latestUserText_, latestState_, raceHistory_,
        QStringLiteral("Vietnamese by default; use English only if the driver clearly writes in English"),
        pitStrategyToolData(), featureSettingsRevision_);
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
    if (state.connected && ttsBenchmarkRunning())
        ttsBenchmark_->cancel(QStringLiteral("Đã kết nối game; hủy benchmark và giữ cấu hình cũ."));
    const bool previousWasRace = latestState_.sessionType == SessionType::Race;
    const QString previousSessionId = strategySessionId_;
    strategyRecorder_.setSimulatorConnected(state.connected);
    strategyRecorder_.setRaceActive(state.connected && state.sessionType == SessionType::Race);
    latestState_ = state;
    updateLapSummaryStatus();
    updateSpotterStatus();
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
        eventEngine_.reset();
        spotterEngine_.reset();
        messageDispatcher_->cancelBySource(MessageSource::ProximitySpotter);
        messageDispatcher_->setProximitySpotterState(false, false);
        if (previousWasRace && strategyRecordedLap_ > 0)
            strategyRecorder_.finishSession(previousSessionId);
        raceHistory_.reset();
        lapSummaryObservedLap_ = state.connected ? state.currentLap.value_or(0) : 0;
        lapSummaryLastLap_ = 0;
        lapSummaryLastLapSeconds_ = 0.0;
        lapSummaryBoundaryFuel_.reset();
        lapSummaryLastLapComparable_ = false;
        lapSummaryLapHadIssue_ = false;
        lapSummaryLapContextComplete_ = false;
        lapSummaryPending_ = false;
        if (!latestLapSummary_.isEmpty()) {
            latestLapSummary_.clear();
            emit lapSummaryChanged();
        }
        messageDispatcher_->cancelBySource(MessageSource::LapSummary);
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
    updateLapSummary(state);
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
    const bool spotterEnabled = settingsManager_.spotterEnabled();
    spotterEngine_.setLateralWarningGap(settingsManager_.spotterWarningGapMeters());
    auto spotterEvents = spotterEnabled ? spotterEngine_.process(state) : std::vector<RaceEvent>{};
    if (!spotterEnabled) spotterEngine_.reset();
    messageDispatcher_->setProximitySpotterState(spotterEnabled && spotterEngine_.hasLeft(),
        spotterEnabled && spotterEngine_.hasRight());
    updateSpotterStatus();
    if (spotterEnabled)
        events.insert(events.end(), spotterEvents.begin(), spotterEvents.end());
    for (const auto& event : events) {
        if (!settingsManager_.eventEnabled(event.type)) continue;
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
        const auto source = eventMessageSource(event.type);
            const QString cachedSpeech = cachedEventSpeech(event.type);
            messageDispatcher_->enqueue(cachedSpeech.isEmpty() ? latestEvent_ : cachedSpeech,
                event.priority, source, event.type);
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
    if (enabled && !strategyAvailable()) return;
    if (settingsManager_.strategyEnabled() == enabled) return;
    settingsManager_.setStrategyEnabled(enabled);
    ++featureSettingsRevision_;
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

void Application::setSpotterEnabled(const bool enabled)
{
    if (settingsManager_.spotterEnabled() == enabled) return;
    settingsManager_.setSpotterEnabled(enabled);
    ++featureSettingsRevision_;
    spotterEngine_.reset();
    messageDispatcher_->setProximitySpotterState(false, false);
    if (!enabled) messageDispatcher_->cancelBySource(MessageSource::ProximitySpotter);
    updateSpotterStatus();
    emit spotterSettingsChanged();
}

void Application::setSpotterWarningGapMeters(const double meters)
{
    const double clamped = std::clamp(meters, 0.5, 2.5);
    if (settingsManager_.spotterWarningGapMeters() == clamped) return;
    settingsManager_.setSpotterWarningGapMeters(clamped);
    ++featureSettingsRevision_;
    emit spotterSettingsChanged();
}

void Application::setFuelAlertsEnabled(const bool enabled)
{
    if (settingsManager_.fuelAlertsEnabled() == enabled) return;
    settingsManager_.setFuelAlertsEnabled(enabled);
    ++featureSettingsRevision_;
    if (!enabled) messageDispatcher_->cancelBySource(MessageSource::FuelAlerts);
    emit engineerSettingsChanged();
}

void Application::setTyreAlertsEnabled(const bool enabled)
{
    if (settingsManager_.tyreAlertsEnabled() == enabled) return;
    settingsManager_.setTyreAlertsEnabled(enabled);
    ++featureSettingsRevision_;
    if (!enabled) messageDispatcher_->cancelBySource(MessageSource::TyreAlerts);
    emit engineerSettingsChanged();
}

void Application::setLapDeltaEnabled(const bool enabled)
{
    if (settingsManager_.lapDeltaEnabled() == enabled) return;
    settingsManager_.setLapDeltaEnabled(enabled);
    ++featureSettingsRevision_;
    if (!enabled) messageDispatcher_->cancelBySource(MessageSource::LapDelta);
    emit engineerSettingsChanged();
}

void Application::setFlagAlertsEnabled(const bool enabled)
{
    if (settingsManager_.flagAlertsEnabled() == enabled) return;
    settingsManager_.setFlagAlertsEnabled(enabled);
    ++featureSettingsRevision_;
    if (!enabled) messageDispatcher_->cancelBySource(MessageSource::FlagAlerts);
    emit engineerSettingsChanged();
}

void Application::setDamageAlertsEnabled(const bool enabled)
{
    if (settingsManager_.damageAlertsEnabled() == enabled) return;
    settingsManager_.setDamageAlertsEnabled(enabled);
    ++featureSettingsRevision_;
    if (!enabled) messageDispatcher_->cancelBySource(MessageSource::DamageAlerts);
    emit engineerSettingsChanged();
}

void Application::setLapSummaryEnabled(const bool enabled)
{
    if (settingsManager_.lapSummaryEnabled() == enabled) return;
    settingsManager_.setLapSummaryEnabled(enabled);
    ++featureSettingsRevision_;
    lapSummaryLastLap_ = 0;
    lapSummaryLastLapComparable_ = false;
    lapSummaryLapContextComplete_ = false;
    if (!enabled) {
        lapSummaryPending_ = false;
        messageDispatcher_->cancelBySource(MessageSource::LapSummary);
    }
    updateLapSummaryStatus();
    emit lapSummaryChanged();
}

void Application::updateLapSummaryStatus()
{
    QString status;
    if (!settingsManager_.lapSummaryEnabled()) {
        status = QStringLiteral("Đã tắt");
    } else if (!latestState_.connected) {
        status = QStringLiteral("Chờ AC / ACC");
    } else if (latestState_.simulator != Simulator::AssettoCorsa
        && latestState_.simulator != Simulator::AssettoCorsaCompetizione) {
        status = QStringLiteral("Chờ AC / ACC");
    } else if (!latestState_.sessionType
        || (*latestState_.sessionType != SessionType::Race
            && *latestState_.sessionType != SessionType::Practice
            && *latestState_.sessionType != SessionType::Qualifying
            && *latestState_.sessionType != SessionType::Hotlap
            && *latestState_.sessionType != SessionType::TimeAttack)) {
        status = QStringLiteral("Phiên này chưa có tổng kết vòng");
    } else if (!latestState_.currentLap) {
        status = QStringLiteral("Chờ dữ liệu vòng");
    } else {
        status = QStringLiteral("Đang hoạt động");
    }
    if (lapSummaryStatus_ == status) return;
    lapSummaryStatus_ = status;
    emit lapSummaryChanged();
}

void Application::updateSpotterStatus()
{
    QString status;
    if (!settingsManager_.spotterEnabled()) {
        status = QStringLiteral("Đã tắt");
    } else if (!latestState_.connected) {
        status = QStringLiteral("Chờ kết nối Assetto Corsa");
    } else if (latestState_.simulator != Simulator::AssettoCorsa) {
        status = QStringLiteral("Chưa có hình học xe cho simulator này");
    } else if (latestState_.spotterGeometryStale) {
        status = QStringLiteral("Dữ liệu hình học AC đã cũ");
    } else if (!latestState_.spotterGeometryFresh || !latestState_.spotterWheelContactPoints) {
        status = QStringLiteral("Cần cập nhật AC companion");
    } else if (spotterEngine_.hasInvalidGeometry()
        && spotterEngine_.usableOpponentCount() == 0) {
        status = QStringLiteral("Hình học đối thủ không hợp lệ");
    } else if (spotterEngine_.usableOpponentCount() == 0) {
        status = QStringLiteral("Đang chờ dữ liệu xe đối thủ");
    } else {
        status = QStringLiteral("Đang theo dõi %1 xe đối thủ")
            .arg(spotterEngine_.usableOpponentCount());
    }
    if (spotterStatus_ == status) return;
    spotterStatus_ = status;
    emit spotterStatusChanged();
}

void Application::updateLapSummary(const RaceState& state)
{
    if (!state.connected || !state.currentLap || *state.currentLap <= 0) {
        lapSummaryObservedLap_ = 0;
        lapSummaryLapHadIssue_ = false;
        lapSummaryLapContextComplete_ = false;
        lapSummaryBoundaryFuel_.reset();
        updateLapSummaryStatus();
        return;
    }

    const bool sampleHasIssue = (state.pitState && *state.pitState != PitState::Track)
        || (state.flag && (*state.flag == FlagState::Yellow || *state.flag == FlagState::Red
            || *state.flag == FlagState::Black));
    const int lap = *state.currentLap;
    if (lapSummaryObservedLap_ <= 0) {
        lapSummaryObservedLap_ = lap;
        lapSummaryLapHadIssue_ = sampleHasIssue;
        return;
    }

    if (lap > lapSummaryObservedLap_) {
        const bool consecutive = lap == lapSummaryObservedLap_ + 1;
        const int completedLapNumber = lap - 1;
        const bool completedLapClean = consecutive && lapSummaryLapContextComplete_
            && !lapSummaryLapHadIssue_;
        if (!raceHistory_.laps().empty()) {
            const auto& completed = raceHistory_.laps().back();
            if (completed.lapNumber == completedLapNumber && completed.lapNumber > lapSummaryLastLap_) {
                const QString completedTime = lapTimeText(completed.lapTimeSeconds);
                const bool supportedSimulator = state.simulator == Simulator::AssettoCorsa
                    || state.simulator == Simulator::AssettoCorsaCompetizione;
                const bool supportedSession = state.sessionType
                    && (*state.sessionType == SessionType::Race
                        || *state.sessionType == SessionType::Practice
                        || *state.sessionType == SessionType::Qualifying
                        || *state.sessionType == SessionType::Hotlap
                        || *state.sessionType == SessionType::TimeAttack);
                if (settingsManager_.lapSummaryEnabled() && supportedSimulator
                    && supportedSession && !completedTime.isEmpty()) {
                    QString summary = QStringLiteral("Vòng %1: %2")
                        .arg(completed.lapNumber).arg(completedTime);
                    if (completedLapClean && lapSummaryLastLapComparable_
                        && lapSummaryLastLap_ + 1 == completed.lapNumber) {
                        const double delta = completed.lapTimeSeconds - lapSummaryLastLapSeconds_;
                        summary += delta < 0.0
                            ? QStringLiteral(", nhanh hơn vòng trước %1 s").arg(-delta, 0, 'f', 3)
                            : QStringLiteral(", chậm hơn vòng trước %1 s").arg(delta, 0, 'f', 3);
                    } else if (!completedLapClean) {
                        summary += QStringLiteral(". Không so pace vì dữ liệu vòng chưa đầy đủ hoặc có pit/cờ.");
                    }
                    if (completedLapClean && lapSummaryBoundaryFuel_ && state.fuelLiters
                        && std::isfinite(*lapSummaryBoundaryFuel_) && std::isfinite(*state.fuelLiters)
                        && *lapSummaryBoundaryFuel_ >= *state.fuelLiters) {
                        summary += QStringLiteral(" Dùng %1 l xăng.")
                            .arg(*lapSummaryBoundaryFuel_ - *state.fuelLiters, 0, 'f', 1);
                    }
                    if (state.position) summary += QStringLiteral(" P%1.").arg(*state.position);
                    if (state.gapAheadSeconds && std::isfinite(*state.gapAheadSeconds)) {
                        summary += QStringLiteral(" Cách xe trước %1 s.")
                            .arg(*state.gapAheadSeconds, 0, 'f', 2);
                    }
                    latestLapSummary_ = summary;
                    QVariantMap entry;
                    entry.insert(QStringLiteral("message"), summary);
                    entry.insert(QStringLiteral("priority"), static_cast<int>(EventPriority::Conversation));
                    entry.insert(QStringLiteral("timestamp"),
                        QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
                    eventLog_.prepend(entry);
                    while (eventLog_.size() > 100) eventLog_.removeLast();
                    qCInfo(logEvent).noquote() << summary;
                    emit lapSummaryChanged();
                    emit latestEventChanged();
                    lapSummaryPending_ = true;
                    QTimer::singleShot(0, this, &Application::speakPendingLapSummary);
                }
                lapSummaryLastLap_ = completed.lapNumber;
                lapSummaryLastLapSeconds_ = completed.lapTimeSeconds;
                lapSummaryLastLapComparable_ = settingsManager_.lapSummaryEnabled()
                    && completedLapClean && !completedTime.isEmpty();
            }
        }
        lapSummaryObservedLap_ = lap;
        lapSummaryLapHadIssue_ = false;
        lapSummaryLapContextComplete_ = consecutive;
        lapSummaryBoundaryFuel_.reset();
        if (state.fuelLiters && std::isfinite(*state.fuelLiters))
            lapSummaryBoundaryFuel_ = state.fuelLiters;
    }
    lapSummaryLapHadIssue_ = lapSummaryLapHadIssue_ || sampleHasIssue;
    updateLapSummaryStatus();
}

QJsonObject Application::featureSettings() const
{
    const auto& ptt = settingsManager_.pushToTalk();
    const bool directInputReady = !ptt.deviceGuid.trimmed().isEmpty() && ptt.buttonIndex >= 0;
    QJsonArray features;
    const auto append = [&features](const QString& id, const bool enabled,
                              const bool canEnable, const QString& reason = QString{}) {
        QJsonObject item{{QStringLiteral("feature"), id},
            {QStringLiteral("enabled"), enabled}, {QStringLiteral("can_enable"), canEnable}};
        if (!reason.isEmpty()) item.insert(QStringLiteral("reason"), reason);
        features.append(item);
    };
    append(QStringLiteral("spotter"), spotterEnabled(), true);
    append(QStringLiteral("fuel_alerts"), fuelAlertsEnabled(), true);
    append(QStringLiteral("tyre_alerts"), tyreAlertsEnabled(), true);
    append(QStringLiteral("lap_delta"), lapDeltaEnabled(), true);
    append(QStringLiteral("flag_alerts"), flagAlertsEnabled(), true);
    append(QStringLiteral("damage_alerts"), damageAlertsEnabled(), true);
    append(QStringLiteral("lap_summary"), lapSummaryEnabled(), true);
    append(QStringLiteral("audio_ducking"), audioDuckingEnabled(), true);
    append(QStringLiteral("tts_api"), ttsBackend_ == googleTtsBackend_,
        googleTtsApiKeyConfigured_, googleTtsApiKeyConfigured_ ? QString{} : QStringLiteral("Cần lưu Google AI Studio API key."));
    append(QStringLiteral("pit_strategy"), strategyEnabled(), strategyAvailable(),
        strategyAvailable() ? QString{} : QStringLiteral("Chưa có model chiến thuật khả dụng."));
    append(QStringLiteral("ptt_keyboard"), keyboardPttEnabled(), true);
    append(QStringLiteral("ptt_directinput"), directInputPttEnabled(), directInputReady,
        directInputReady ? QString{} : QStringLiteral("Chưa gán nút DirectInput."));
    append(QStringLiteral("race_recording"), strategyRecorder_.enabled(), true);
    return {{QStringLiteral("available"), true},
        {QStringLiteral("revision"), static_cast<double>(featureSettingsRevision_)},
        {QStringLiteral("features"), features}};
}

QJsonObject Application::setFeatureEnabled(const QString& feature, const bool enabled,
    const quint64 expectedRevision)
{
    const auto settings = featureSettings();
    const auto features = settings.value(QStringLiteral("features")).toArray();
    QJsonObject current;
    for (const auto& value : features) {
        const auto candidate = value.toObject();
        if (candidate.value(QStringLiteral("feature")).toString() == feature) {
            current = candidate;
            break;
        }
    }
    const auto failure = [this, &feature](const QString& reason) {
        return QJsonObject{{QStringLiteral("available"), true},
            {QStringLiteral("success"), false}, {QStringLiteral("feature"), feature},
            {QStringLiteral("reason"), reason}, {QStringLiteral("message"), reason},
            {QStringLiteral("revision"), static_cast<double>(featureSettingsRevision_)}};
    };
    QString label;
    if (feature == QStringLiteral("spotter")) label = QStringLiteral("Spotter");
    else if (feature == QStringLiteral("fuel_alerts")) label = QStringLiteral("cảnh báo nhiên liệu");
    else if (feature == QStringLiteral("tyre_alerts")) label = QStringLiteral("cảnh báo lốp");
    else if (feature == QStringLiteral("lap_delta")) label = QStringLiteral("phân tích Delta vòng đua");
    else if (feature == QStringLiteral("flag_alerts")) label = QStringLiteral("cảnh báo cờ hiệu");
    else if (feature == QStringLiteral("damage_alerts")) label = QStringLiteral("cảnh báo hư hại");
    else if (feature == QStringLiteral("lap_summary")) label = QStringLiteral("tổng kết vòng");
    else if (feature == QStringLiteral("audio_ducking")) label = QStringLiteral("tự hạ âm game");
    else if (feature == QStringLiteral("tts_api")) label = QStringLiteral("Google Gemini TTS");
    else if (feature == QStringLiteral("pit_strategy")) label = QStringLiteral("chiến thuật Pit");
    else if (feature == QStringLiteral("ptt_keyboard")) label = QStringLiteral("PTT bàn phím");
    else if (feature == QStringLiteral("ptt_directinput")) label = QStringLiteral("PTT DirectInput");
    else if (feature == QStringLiteral("race_recording")) label = QStringLiteral("ghi dữ liệu đua");
    if (current.isEmpty()) return failure(QStringLiteral("Tính năng này không được hỗ trợ."));
    if (current.value(QStringLiteral("enabled")).toBool() == enabled) {
        if (enabled && !current.value(QStringLiteral("can_enable")).toBool())
            return failure(current.value(QStringLiteral("reason")).toString(
                QStringLiteral("Tính năng chưa sẵn sàng để bật.")));
        return {{QStringLiteral("available"), true}, {QStringLiteral("success"), true},
            {QStringLiteral("changed"), false}, {QStringLiteral("feature"), feature},
            {QStringLiteral("enabled"), enabled},
            {QStringLiteral("revision"), static_cast<double>(featureSettingsRevision_)},
            {QStringLiteral("message"), QStringLiteral("%1 đã ở trạng thái yêu cầu.").arg(label)}};
    }
    if (expectedRevision != featureSettingsRevision_) {
        return failure(QStringLiteral("Cài đặt vừa được thay đổi trên giao diện; không áp dụng lệnh cũ."));
    }
    if (enabled && !current.value(QStringLiteral("can_enable")).toBool()) {
        return failure(current.value(QStringLiteral("reason")).toString(
            QStringLiteral("Tính năng chưa sẵn sàng để bật.")));
    }

    if (feature == QStringLiteral("spotter")) setSpotterEnabled(enabled);
    else if (feature == QStringLiteral("fuel_alerts")) setFuelAlertsEnabled(enabled);
    else if (feature == QStringLiteral("tyre_alerts")) setTyreAlertsEnabled(enabled);
    else if (feature == QStringLiteral("lap_delta")) setLapDeltaEnabled(enabled);
    else if (feature == QStringLiteral("flag_alerts")) setFlagAlertsEnabled(enabled);
    else if (feature == QStringLiteral("damage_alerts")) setDamageAlertsEnabled(enabled);
    else if (feature == QStringLiteral("lap_summary")) setLapSummaryEnabled(enabled);
    else if (feature == QStringLiteral("audio_ducking")) setAudioDuckingEnabled(enabled);
    else if (feature == QStringLiteral("tts_api"))
        setTtsBackend(enabled ? QStringLiteral("Google Gemini API") : QStringLiteral("VieNeu-TTS"));
    else if (feature == QStringLiteral("pit_strategy")) setStrategyEnabled(enabled);
    else if (feature == QStringLiteral("ptt_keyboard"))
        setPushToTalkOptions(enabled, directInputPttEnabled());
    else if (feature == QStringLiteral("ptt_directinput"))
        setPushToTalkOptions(keyboardPttEnabled(), enabled);
    else if (feature == QStringLiteral("race_recording")) strategyRecorder_.setEnabled(enabled);

    bool applied = false;
    for (const auto& value : featureSettings().value(QStringLiteral("features")).toArray()) {
        const auto item = value.toObject();
        if (item.value(QStringLiteral("feature")).toString() == feature) {
            applied = item.value(QStringLiteral("enabled")).toBool() == enabled;
            break;
        }
    }
    if (!applied) return failure(QStringLiteral("Không áp dụng được thay đổi tính năng."));
    return {{QStringLiteral("available"), true}, {QStringLiteral("success"), true},
        {QStringLiteral("changed"), true}, {QStringLiteral("feature"), feature},
        {QStringLiteral("enabled"), enabled},
        {QStringLiteral("revision"), static_cast<double>(featureSettingsRevision_)},
        {QStringLiteral("message"), QStringLiteral("Đã %1 %2.")
            .arg(enabled ? QStringLiteral("bật") : QStringLiteral("tắt"), label)}};
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
    if (connected && ttsBenchmarkRunning())
        ttsBenchmark_->cancel(QStringLiteral("Đã kết nối game; hủy benchmark và giữ cấu hình cũ."));
    const bool changed = simulatorName_ != simulator || connected_ != connected;
    simulatorName_ = simulator;
    connected_ = connected;
    if (changed) {
        emit connectionChanged();
        emit ttsBenchmarkChanged();
    }
}

void Application::speakPendingLapSummary()
{
    if (!lapSummaryPending_ || !settingsManager_.lapSummaryEnabled() || !startupReady_
        || pushToTalkPressed_
        || (voiceStatus_ != QStringLiteral("Idle") && voiceStatus_ != QStringLiteral("Speaking"))) return;
    lapSummaryPending_ = false;
    messageDispatcher_->enqueue(latestLapSummary_, EventPriority::Conversation, MessageSource::LapSummary);
}

void Application::onVoiceStatusChanged(const QString& status)
{
    if (voiceStatus_ != status) {
        voiceStatus_ = status;
        emit voiceStatusChanged();
        emit ttsBenchmarkChanged();
        if (status == QStringLiteral("Idle")) {
            QTimer::singleShot(0, this, &Application::speakPendingLapSummary);
        }
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
    if (ttsBenchmarkRunning() || ttsBenchmarkRestoring_) return;
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
        pitStrategyToolData(), featureSettingsRevision_);
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
