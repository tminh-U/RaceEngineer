// Tests inject RaceState into the real Application; never read a running user's game.
// Real simulator I/O is covered separately, not by these resource policy tests.
#include "telemetry/ac/ACTelemetryProvider.h"
#include "telemetry/acc/ACCTelemetryProvider.h"

namespace raceengineer {
bool ACTelemetryProvider::start() { return false; }
void ACTelemetryProvider::stop() noexcept { connected_ = false; }
bool ACTelemetryProvider::update() { return false; }
bool ACCTelemetryProvider::start() { return false; }
void ACCTelemetryProvider::stop() noexcept { connected_ = false; }
bool ACCTelemetryProvider::update() { return false; }
} // namespace raceengineer
