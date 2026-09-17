#include "vad/TenVadProcessor.h"

namespace raceengineer {

TenVadProcessor::TenVadProcessor(const std::size_t frameSamples, const float threshold)
    : frameSamples_(frameSamples)
{
    if (ten_vad_create(&handle_, frameSamples_, threshold) != 0) {
        handle_ = nullptr;
    }
}

TenVadProcessor::~TenVadProcessor()
{
    if (handle_ != nullptr) {
        ten_vad_destroy(&handle_);
    }
}

std::string TenVadProcessor::version() const
{
    const char* const value = ten_vad_get_version();
    return value == nullptr ? std::string{} : std::string(value);
}

VadDecision TenVadProcessor::process(const std::span<const std::int16_t> samples) const noexcept
{
    if (handle_ == nullptr || samples.size() != frameSamples_) {
        return {};
    }
    float probability = 0.0F;
    int flag = 0;
    if (ten_vad_process(handle_, samples.data(), samples.size(), &probability, &flag) != 0) {
        return {};
    }
    return {flag != 0, probability};
}

} // namespace raceengineer
