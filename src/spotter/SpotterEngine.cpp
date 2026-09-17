#include "spotter/SpotterEngine.h"

namespace raceengineer {

std::vector<RaceEvent> SpotterEngine::process(const RaceState& state) const
{
    // The supplied shared-memory definitions do not expose reliable opponent
    // coordinates. Producing car-left/right calls from gaps would be unsafe.
    (void)state;
    return {};
}

} // namespace raceengineer
