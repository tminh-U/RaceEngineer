#include "telemetry/TelemetryManager.h"

#include "telemetry/ac/ACTelemetryProvider.h"
#include "telemetry/acc/ACCTelemetryProvider.h"
#include "telemetry/common/ISimTelemetryProvider.h"
#include "telemetry/common/WindowsProcess.h"
#include "telemetry/mock/MockTelemetryProvider.h"

#include <QTimer>

namespace raceengineer {

TelemetryManager::TelemetryManager(QObject* const parent)
    : QObject(parent)
    , ac_(std::make_unique<ACTelemetryProvider>())
    , acc_(std::make_unique<ACCTelemetryProvider>())
    , mock_(std::make_unique<MockTelemetryProvider>())
{
}

TelemetryManager::~TelemetryManager()
{
    stop();
}

void TelemetryManager::start()
{
    if (started_) {
        return;
    }
    started_ = true;

    pollTimer_ = new QTimer(this);
    pollTimer_->setTimerType(Qt::PreciseTimer);
    pollTimer_->setInterval(20); // Cheap shared-memory reads; UI publication is throttled below.
    connect(pollTimer_, &QTimer::timeout, this, &TelemetryManager::pollTelemetry);

    detectionTimer_ = new QTimer(this);
    detectionTimer_->setTimerType(Qt::VeryCoarseTimer);
    detectionTimer_->setInterval(1000);
    connect(detectionTimer_, &QTimer::timeout, this, &TelemetryManager::detectSimulator);

    uiPublishClock_.start();
    detectSimulator();
    detectionTimer_->start();
}

void TelemetryManager::stop()
{
    if (pollTimer_ != nullptr) {
        pollTimer_->stop();
    }
    if (detectionTimer_ != nullptr) {
        detectionTimer_->stop();
    }
    if (active_ != nullptr) {
        active_->stop();
    }
    active_ = nullptr;
    selected_ = SelectedProvider::None;
    started_ = false;
}

void TelemetryManager::setUseMockTelemetry(const bool enabled)
{
#if RACEENGINEER_ENABLE_MOCK
    useMock_ = enabled;
#else
    useMock_ = false;
    (void)enabled;
#endif
    detectSimulator();
}

void TelemetryManager::detectSimulator()
{
    if (useMock_) {
        selectProvider(SelectedProvider::Mock);
        return;
    }

    switch (detectRunningSimulator()) {
    case RunningSimulator::AssettoCorsaCompetizione:
        selectProvider(SelectedProvider::ACC);
        break;
    case RunningSimulator::AssettoCorsa:
        selectProvider(SelectedProvider::AC);
        break;
    case RunningSimulator::None:
        selectProvider(SelectedProvider::None);
        break;
    }
}

void TelemetryManager::pollTelemetry()
{
    if (active_ == nullptr || !active_->isConnected()) {
        return;
    }
    if (!active_->update()) {
        return;
    }
    if (uiPublishClock_.elapsed() >= 100) {
        emit stateUpdated(active_->getCurrentState());
        uiPublishClock_.restart();
    }
}

void TelemetryManager::selectProvider(const SelectedProvider selection)
{
    if (selection == SelectedProvider::None && selected_ == SelectedProvider::None
        && active_ == nullptr) {
        return;
    }
    if (selection == selected_ && active_ != nullptr && active_->isConnected()) {
        return;
    }

    if (active_ != nullptr) {
        active_->stop();
        active_ = nullptr;
    }
    if (pollTimer_ != nullptr) {
        pollTimer_->stop();
    }
    selected_ = selection;

    if (selection == SelectedProvider::None) {
        publishDisconnected();
        return;
    }

    auto* const candidate = providerFor(selection);
    if (candidate != nullptr && candidate->start()) {
        active_ = candidate;
        if (pollTimer_ != nullptr) {
            pollTimer_->start();
        }
        emit connectionStatusChanged(QString::fromUtf8(candidate->simName().data(),
                                         static_cast<qsizetype>(candidate->simName().size())),
            true);
        emit stateUpdated(candidate->getCurrentState());
        uiPublishClock_.restart();
        return;
    }

    // Keep the selection so the next detection tick retries while the game creates its mappings.
    publishDisconnected();
}

ISimTelemetryProvider* TelemetryManager::providerFor(const SelectedProvider selection) const noexcept
{
    switch (selection) {
    case SelectedProvider::AC: return ac_.get();
    case SelectedProvider::ACC: return acc_.get();
    case SelectedProvider::Mock: return mock_.get();
    case SelectedProvider::None: break;
    }
    return nullptr;
}

void TelemetryManager::publishDisconnected()
{
    RaceState state;
    state.capturedAt = std::chrono::steady_clock::now();
    emit connectionStatusChanged(QStringLiteral("Not Connected"), false);
    emit stateUpdated(state);
}

} // namespace raceengineer
