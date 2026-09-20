#pragma once

#include <QString>

namespace raceengineer {

class RacingTextNormalizer {
public:
    [[nodiscard]] static QString normalize(const QString& text);
};

} // namespace raceengineer
