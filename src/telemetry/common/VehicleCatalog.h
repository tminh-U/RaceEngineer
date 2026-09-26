#pragma once

#include "telemetry/common/RaceState.h"

#include <string>
#include <string_view>

namespace raceengineer {

struct VehicleClass final {
    std::string category{"unknown"};
    std::string subclass;
};

// Exact in-game carModel lookup. Unknown IDs must never select a strategy model.
[[nodiscard]] VehicleClass classifyVehicle(Simulator simulator, std::string_view carModel);

} // namespace raceengineer
