#pragma once

#include "telemetry/common/RaceState.h"

#include <string_view>

namespace raceengineer {

class ISimTelemetryProvider {
public:
    virtual ~ISimTelemetryProvider() = default;

    virtual bool start() = 0;
    virtual void stop() noexcept = 0;
    [[nodiscard]] virtual bool isConnected() const noexcept = 0;
    [[nodiscard]] virtual std::string_view simName() const noexcept = 0;
    virtual bool update() = 0;
    [[nodiscard]] virtual RaceState getCurrentState() const = 0;
};

} // namespace raceengineer
