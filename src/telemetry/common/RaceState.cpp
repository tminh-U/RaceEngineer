#include "telemetry/common/RaceState.h"

#include <algorithm>

namespace raceengineer {

const char* simulatorName(const Simulator simulator) noexcept
{
    switch (simulator) {
    case Simulator::AssettoCorsa: return "Assetto Corsa";
    case Simulator::AssettoCorsaCompetizione: return "Assetto Corsa Competizione";
    case Simulator::Mock: return "Mock Telemetry";
    case Simulator::None: break;
    }
    return "Not Connected";
}

const char* sessionTypeName(const SessionType session) noexcept
{
    switch (session) {
    case SessionType::Practice: return "Practice";
    case SessionType::Qualifying: return "Qualifying";
    case SessionType::Race: return "Race";
    case SessionType::Hotlap: return "Hotlap";
    case SessionType::TimeAttack: return "Time Attack";
    case SessionType::Drift: return "Drift";
    case SessionType::Drag: return "Drag";
    case SessionType::Unknown: break;
    }
    return "Unknown";
}

const char* flagName(const FlagState flag) noexcept
{
    switch (flag) {
    case FlagState::None: return "None";
    case FlagState::Green: return "Green";
    case FlagState::Yellow: return "Yellow";
    case FlagState::Blue: return "Blue";
    case FlagState::Red: return "Red";
    case FlagState::Black: return "Black";
    case FlagState::Chequered: return "Chequered";
    case FlagState::White: return "White";
    case FlagState::Unknown: break;
    }
    return "Unavailable";
}

const char* pitStateName(const PitState state) noexcept
{
    switch (state) {
    case PitState::Track: return "Track";
    case PitState::Entering: return "Entering";
    case PitState::PitLane: return "Pit Lane";
    case PitState::PitBox: return "Pit Box";
    case PitState::Exiting: return "Exiting";
    case PitState::Unknown: break;
    }
    return "Unavailable";
}

int normalizeAcGear(const int sharedMemoryGear) noexcept
{
    // AC-family shared memory: 0=reverse, 1=neutral, 2=first, ...
    return sharedMemoryGear - 1;
}

double clampUnit(const double value) noexcept
{
    return std::clamp(value, 0.0, 1.0);
}

} // namespace raceengineer
