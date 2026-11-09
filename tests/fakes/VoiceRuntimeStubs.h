#pragma once
#include <atomic>

// Link only into ResourceUsageTests; these are lifecycle/parameter probes, not AI models.
namespace voice_test {
inline std::atomic_int sttLoads{0}, sttFrees{0}, sttThreads{0};
inline std::atomic_int ttsLoads{0}, ttsFrees{0}, ttsThreads{0}, ttsSyntheses{0};
inline std::atomic_bool failTtsInit{false};
}
