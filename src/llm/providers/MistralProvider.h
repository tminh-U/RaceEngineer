#pragma once

#include "llm/providers/OpenAICompatibleProvider.h"

namespace raceengineer {

class MistralProvider final : public OpenAICompatibleProvider {
    Q_OBJECT

public:
    explicit MistralProvider(const ProviderConfiguration& configuration, QObject* parent = nullptr)
        : OpenAICompatibleProvider(configuration, parent)
    {
    }
};

} // namespace raceengineer
