#pragma once

#include "telemetry/common/ISimTelemetryProvider.h"
#include "telemetry/common/WindowsSharedMemory.h"

namespace raceengineer {

class ACTelemetryProvider final : public ISimTelemetryProvider {
public:
    bool start() override;
    void stop() noexcept override;
    [[nodiscard]] bool isConnected() const noexcept override { return connected_; }
    [[nodiscard]] std::string_view simName() const noexcept override { return "Assetto Corsa"; }
    bool update() override;
    [[nodiscard]] RaceState getCurrentState() const override { return state_; }

private:
    WindowsSharedMemory physicsPage_;
    WindowsSharedMemory graphicsPage_;
    WindowsSharedMemory staticPage_;
    RaceState state_;
    bool connected_{false};
};

} // namespace raceengineer
