#pragma once

#include "events/EventEngine.h"
#include "telemetry/common/RaceState.h"

#include <string_view>
#include <vector>

namespace raceengineer {

class SpotterEngine final {
public:
    [[nodiscard]] std::vector<RaceEvent> process(const RaceState& state) const;
    [[nodiscard]] static constexpr std::string_view unavailableReason() noexcept
    {
        return "Normalized AC/ACC data has no reliable left/right overlap coordinates.";
    }
};

} // namespace raceengineer
