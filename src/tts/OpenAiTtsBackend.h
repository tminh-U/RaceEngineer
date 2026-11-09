#pragma once

#include "tts/GoogleTtsBackend.h"
#include <utility>

namespace raceengineer {

// POST /audio/speech with streamed 24 kHz, mono, signed 16-bit little-endian PCM.
// Uses the same bounded playback, interruption and retry implementation as Google.
class OpenAiTtsBackend final : public GoogleTtsBackend {
public:
    explicit OpenAiTtsBackend(QObject* parent = nullptr)
        : GoogleTtsBackend(true, QUrl(QStringLiteral("https://api.openai.com/v1/audio/speech")), parent) {}
    explicit OpenAiTtsBackend(QUrl endpoint, QObject* parent = nullptr)
        : GoogleTtsBackend(true, std::move(endpoint), parent) {}
};

} // namespace raceengineer
