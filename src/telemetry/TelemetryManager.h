#pragma once

#include "telemetry/common/RaceState.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>

#include <memory>

class QTimer;

namespace raceengineer {

class ISimTelemetryProvider;
class ACTelemetryProvider;
class ACCTelemetryProvider;
class MockTelemetryProvider;

class TelemetryManager final : public QObject {
    Q_OBJECT

public:
    explicit TelemetryManager(QObject* parent = nullptr);
    ~TelemetryManager() override;

public slots:
    void start();
    void stop();
    void setUseMockTelemetry(bool enabled);

signals:
    void stateUpdated(const raceengineer::RaceState& state);
    void connectionStatusChanged(const QString& simulator, bool connected);

private slots:
    void detectSimulator();
    void pollTelemetry();

private:
    enum class SelectedProvider { None, AC, ACC, Mock };

    void selectProvider(SelectedProvider selection);
    ISimTelemetryProvider* providerFor(SelectedProvider selection) const noexcept;
    void publishDisconnected();

    std::unique_ptr<ACTelemetryProvider> ac_;
    std::unique_ptr<ACCTelemetryProvider> acc_;
    std::unique_ptr<MockTelemetryProvider> mock_;
    ISimTelemetryProvider* active_{nullptr};
    QTimer* pollTimer_{nullptr};
    QTimer* detectionTimer_{nullptr};
    QElapsedTimer uiPublishClock_;
    SelectedProvider selected_{SelectedProvider::None};
    bool useMock_{false};
    bool started_{false};
};

} // namespace raceengineer
