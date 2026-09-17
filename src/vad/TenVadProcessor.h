#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include <ten_vad.h>

namespace raceengineer {

struct VadDecision final {
    bool speech{false};
    float probability{0.0F};
};

class TenVadProcessor final {
public:
    explicit TenVadProcessor(std::size_t frameSamples = 256, float threshold = 0.5F);
    ~TenVadProcessor();
    TenVadProcessor(const TenVadProcessor&) = delete;
    TenVadProcessor& operator=(const TenVadProcessor&) = delete;

    [[nodiscard]] bool isAvailable() const noexcept { return handle_ != nullptr; }
    [[nodiscard]] std::size_t frameSamples() const noexcept { return frameSamples_; }
    [[nodiscard]] std::string version() const;
    [[nodiscard]] VadDecision process(std::span<const std::int16_t> samples) const noexcept;

private:
    ten_vad_handle_t handle_{nullptr};
    std::size_t frameSamples_;
};

} // namespace raceengineer
