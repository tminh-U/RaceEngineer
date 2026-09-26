#include "ai/LocalAiRuntime.h"

#include <QByteArray>
#include <QRegularExpression>
#include <QVariantMap>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <utility>
#include <vector>

#if defined(RACEENGINEER_HAS_VULKAN)
#include <ggml-vulkan.h>
#endif

namespace raceengineer {
namespace {

struct VulkanDeviceInfo final {
    int index{-1};
    QString description;
};

[[nodiscard]] std::vector<VulkanDeviceInfo> vulkanDevices()
{
    std::vector<VulkanDeviceInfo> devices;
#if defined(RACEENGINEER_HAS_VULKAN)
    const int count = std::max(0, ggml_backend_vk_get_device_count());
    devices.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        std::array<char, 512> description{};
        ggml_backend_vk_get_device_description(
            index, description.data(), description.size());
        QString label = QString::fromUtf8(description.data()).trimmed();
        if (label.isEmpty()) {
            label = QStringLiteral("Vulkan device%1").arg(index);
        }
        devices.push_back({index, std::move(label)});
    }
#endif
    return devices;
}

[[nodiscard]] QVariantMap deviceOption(const QString& id,
    const QString& label,
    const QString& backend,
    const bool available,
    const int index = -1,
    const QString& description = {})
{
    QVariantMap option{
        {QStringLiteral("id"), id},
        {QStringLiteral("label"), label},
        {QStringLiteral("backend"), backend},
        {QStringLiteral("available"), available},
    };
    if (index >= 0) {
        option.insert(QStringLiteral("index"), index);
    }
    if (!description.isEmpty()) {
        option.insert(QStringLiteral("description"), description);
    }
    return option;
}

void useCpu(LocalAiRuntimeSelection& selection, const QString& reason = {})
{
    selection.resolvedDeviceId = QStringLiteral("cpu");
    selection.label = QStringLiteral("CPU");
    selection.deviceDescription.clear();
    selection.vulkanDeviceIndex = -1;
    selection.useVulkan = false;
    if (!reason.isEmpty()) {
        selection.fallbackToCpu = true;
        selection.fallbackReason = reason;
    }
}

[[nodiscard]] bool parseDeviceIndex(const QString& deviceId, int* index)
{
    static const QRegularExpression pattern(
        QStringLiteral(R"(^vulkan:(\d+)$)"),
        QRegularExpression::CaseInsensitiveOption);
    const auto match = pattern.match(deviceId.trimmed());
    if (!match.hasMatch()) {
        return false;
    }
    bool ok = false;
    const int parsed = match.captured(1).toInt(&ok);
    if (!ok || parsed < 0) {
        return false;
    }
    *index = parsed;
    return true;
}

void selectVulkan(LocalAiRuntimeSelection& selection,
    const VulkanDeviceInfo& device)
{
    selection.resolvedDeviceId = QStringLiteral("vulkan:%1").arg(device.index);
    selection.label = QStringLiteral("Vulkan — %1").arg(device.description);
    selection.deviceDescription = device.description;
    selection.vulkanDeviceIndex = device.index;
    selection.useVulkan = true;
    selection.fallbackToCpu = false;
    selection.fallbackReason.clear();
}

} // namespace

QVariantList LocalAiRuntime::availableDevices()
{
    QVariantList result;
    result.append(deviceOption(QStringLiteral("auto"),
        QStringLiteral("Tự động"),
        QStringLiteral("auto"),
        true));
    result.append(deviceOption(QStringLiteral("cpu"),
        QStringLiteral("CPU"),
        QStringLiteral("cpu"),
        true));

    for (const auto& device : vulkanDevices()) {
        result.append(deviceOption(
            QStringLiteral("vulkan:%1").arg(device.index),
            QStringLiteral("Vulkan — %1").arg(device.description),
            QStringLiteral("vulkan"),
            true,
            device.index,
            device.description));
    }
    return result;
}

LocalAiRuntimeSelection LocalAiRuntime::resolve(const QString& requestedDeviceId)
{
    LocalAiRuntimeSelection selection;
    selection.requestedDeviceId = requestedDeviceId.trimmed();
    if (selection.requestedDeviceId.isEmpty()) {
        selection.requestedDeviceId = QStringLiteral("auto");
    }

    const auto devices = vulkanDevices();
    if (selection.requestedDeviceId.compare(QStringLiteral("cpu"),
            Qt::CaseInsensitive) == 0) {
        useCpu(selection);
        return selection;
    }

    if (selection.requestedDeviceId.compare(QStringLiteral("auto"),
            Qt::CaseInsensitive) == 0) {
        if (!devices.empty()) {
            selectVulkan(selection, devices.front());
        } else {
            useCpu(selection, QStringLiteral("Không tìm thấy thiết bị Vulkan"));
        }
        return selection;
    }

    int requestedIndex = -1;
    if (parseDeviceIndex(selection.requestedDeviceId, &requestedIndex)) {
        const auto iterator = std::find_if(devices.cbegin(), devices.cend(),
            [requestedIndex](const VulkanDeviceInfo& device) {
                return device.index == requestedIndex;
            });
        if (iterator != devices.cend()) {
            selectVulkan(selection, *iterator);
            return selection;
        }
        useCpu(selection, QStringLiteral("Thiết bị Vulkan device%1 không khả dụng")
            .arg(requestedIndex));
        return selection;
    }

    useCpu(selection, QStringLiteral("Lựa chọn thiết bị AI không hợp lệ"));
    return selection;
}

LocalAiRuntimeSelection LocalAiRuntime::resolveAndApply(
    const QString& requestedDeviceId)
{
    const auto selection = resolve(requestedDeviceId);
    applyEnvironment(selection);
    return selection;
}

void LocalAiRuntime::applyEnvironment(
    const LocalAiRuntimeSelection& selection)
{
    if (selection.useVulkan) {
        qunsetenv("GGML_DISABLE_VULKAN");
        qputenv("VIENEU_GPU_DEVICE",
            selection.deviceDescription.toUtf8());
        qputenv("VIENEU_GPU_LAYERS", QByteArrayLiteral("99"));
        qputenv("VIENEU_ORT_EP", QByteArrayLiteral("cpu"));
        return;
    }

    qputenv("GGML_DISABLE_VULKAN", QByteArrayLiteral("1"));
    qunsetenv("VIENEU_GPU_DEVICE");
    qputenv("VIENEU_GPU_LAYERS", QByteArrayLiteral("0"));
    qputenv("VIENEU_ORT_EP", QByteArrayLiteral("cpu"));
}

} // namespace raceengineer
