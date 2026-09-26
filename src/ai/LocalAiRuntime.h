#pragma once

#include <QVariantList>
#include <QString>

namespace raceengineer {

struct LocalAiRuntimeSelection final {
    QString requestedDeviceId{QStringLiteral("auto")};
    QString resolvedDeviceId{QStringLiteral("cpu")};
    QString label{QStringLiteral("CPU")};
    QString deviceDescription;
    QString fallbackReason;
    int vulkanDeviceIndex{-1};
    bool useVulkan{false};
    bool fallbackToCpu{false};
};

class LocalAiRuntime final {
public:
    [[nodiscard]] static QVariantList availableDevices();
    [[nodiscard]] static LocalAiRuntimeSelection resolve(const QString& requestedDeviceId);
    [[nodiscard]] static LocalAiRuntimeSelection resolveAndApply(const QString& requestedDeviceId);
    static void applyEnvironment(const LocalAiRuntimeSelection& selection);
};

} // namespace raceengineer
