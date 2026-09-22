#pragma once

#include "telemetry/common/RaceState.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace raceengineer {

// Optional UDP receiver for Assetto Corsa extended telemetry streamed by the
// companion RaceEngineer Python app in AC / Content Manager. If the Python app
// is not installed, standard AC shared-memory telemetry continues seamlessly.
class AcExtensionClient final {
public:
    AcExtensionClient();
    ~AcExtensionClient();

    AcExtensionClient(const AcExtensionClient&) = delete;
    AcExtensionClient& operator=(const AcExtensionClient&) = delete;

    bool start(uint16_t port = 9996);
    void stop() noexcept;
    void update();

    [[nodiscard]] bool hasData() const noexcept;
    [[nodiscard]] const std::vector<OpponentState>& opponents() const noexcept { return opponents_; }
    [[nodiscard]] std::optional<int> playerPosition() const noexcept { return playerPosition_; }
    [[nodiscard]] std::optional<double> gapAhead() const noexcept { return gapAhead_; }
    [[nodiscard]] std::optional<double> gapBehind() const noexcept { return gapBehind_; }
    [[nodiscard]] const std::optional<std::string>& opponentAhead() const noexcept { return opponentAhead_; }
    [[nodiscard]] const std::optional<std::string>& opponentBehind() const noexcept { return opponentBehind_; }
    [[nodiscard]] const std::optional<std::array<double, 3>>& playerSectors() const noexcept { return playerSectors_; }
    [[nodiscard]] const std::optional<WheelValues>& brakeTemperatures() const noexcept { return brakeTemps_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    std::vector<OpponentState> opponents_;
    std::optional<int> playerPosition_;
    std::optional<double> gapAhead_;
    std::optional<double> gapBehind_;
    std::optional<std::string> opponentAhead_;
    std::optional<std::string> opponentBehind_;
    std::optional<std::array<double, 3>> playerSectors_;
    std::optional<WheelValues> brakeTemps_;
};

} // namespace raceengineer
