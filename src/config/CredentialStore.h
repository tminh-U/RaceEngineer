#pragma once

#include <QString>

namespace raceengineer {

class CredentialStore final {
public:
    [[nodiscard]] static QString readApiKey();
    static bool writeApiKey(const QString& apiKey);
    static bool clearApiKey();
};

} // namespace raceengineer
