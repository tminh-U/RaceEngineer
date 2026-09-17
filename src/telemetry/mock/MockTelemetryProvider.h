#pragma once

#include "telemetry/common/ISimTelemetryProvider.h"

#include <chrono>

namespace raceengineer {

class MockTelemetryProvider final : public ISimTelemetryProvider {
public:
    bool start() override;
    void stop() noexcept override;
    [[nodiscard]] bool isConnected() const noexcept override { return connected_; }
    [[nodiscard]] std::string_view simName() const noexcept override { return "Mock Telemetry"; }
    bool update() override;
    [[nodiscard]] RaceState getCurrentState() const override { return state_; }

private:
    RaceState state_;
    std::chrono::steady_clock::time_point startedAt_{};
    bool connected_{false};
};

} // namespace raceengineer
