#pragma once

#include "telemetry/common/RaceState.h"

#include <memory>
#include <vector>

namespace raceengineer {

// Lightweight client for ACC's official UDP Broadcasting interface. It is
// optional: shared-memory telemetry keeps working when broadcasting.json has
// updListenerPort set to 0.
class AccBroadcastClient final {
public:
    AccBroadcastClient();
    ~AccBroadcastClient();

    AccBroadcastClient(const AccBroadcastClient&) = delete;
    AccBroadcastClient& operator=(const AccBroadcastClient&) = delete;

    bool start();
    void stop() noexcept;
    void update();
    [[nodiscard]] std::vector<OpponentState> opponents(int playerCarId) const;
    [[nodiscard]] std::optional<int> playerPosition(int playerCarId) const;
    [[nodiscard]] std::optional<std::array<double, 3>> playerSectorTimes(int playerCarId) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace raceengineer
