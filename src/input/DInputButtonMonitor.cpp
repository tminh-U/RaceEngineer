#define DIRECTINPUT_VERSION 0x0800
#include "input/DInputButtonMonitor.h"

#include "utils/Logging.h"

#include <QTimer>

#include <algorithm>

#ifdef _WIN32
#include <Windows.h>
#include <dinput.h>
#include <objbase.h>

#include <array>
#include <vector>
#endif

namespace raceengineer {

struct DInputButtonMonitor::Impl {
#ifdef _WIN32
    struct Device {
        IDirectInputDevice8W* handle{nullptr};
        QString guid;
        QString name;
        std::array<bool, 128> previous{};
    };

    IDirectInput8W* directInput{nullptr};
    HWND window{nullptr};
    std::vector<Device> devices;
#endif
    QTimer* pollTimer{nullptr};
    QTimer* scanTimer{nullptr};
    QString configuredGuid;
    int configuredButton{-1};
    bool enabled{false};
    bool mapping{false};
    bool mappedPressed{false};
};

#ifdef _WIN32
namespace {

QString guidText(const GUID& guid)
{
    wchar_t value[40]{};
    StringFromGUID2(guid, value, static_cast<int>(std::size(value)));
    return QString::fromWCharArray(value);
}

BOOL CALLBACK enumerateController(const DIDEVICEINSTANCEW* const instance, void* const context)
{
    auto* const impl = static_cast<DInputButtonMonitor::Impl*>(context);
    IDirectInputDevice8W* device = nullptr;
    if (FAILED(impl->directInput->CreateDevice(instance->guidInstance, &device, nullptr))) {
        return DIENUM_CONTINUE;
    }
    if (FAILED(device->SetDataFormat(&c_dfDIJoystick2))
        || FAILED(device->SetCooperativeLevel(impl->window, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE))) {
        device->Release();
        return DIENUM_CONTINUE;
    }
    device->Acquire();
    DInputButtonMonitor::Impl::Device entry;
    entry.handle = device;
    entry.guid = guidText(instance->guidInstance);
    entry.name = QString::fromWCharArray(instance->tszProductName);
    DIJOYSTATE2 state{};
    device->Poll();
    if (SUCCEEDED(device->GetDeviceState(sizeof(state), &state))) {
        for (int button = 0; button < 128; ++button) {
            entry.previous[static_cast<std::size_t>(button)] = (state.rgbButtons[button] & 0x80U) != 0;
        }
    }
    impl->devices.push_back(std::move(entry));
    return DIENUM_CONTINUE;
}

void releaseDevices(DInputButtonMonitor::Impl& impl)
{
    for (auto& device : impl.devices) {
        if (device.handle != nullptr) {
            device.handle->Unacquire();
            device.handle->Release();
        }
    }
    impl.devices.clear();
}

} // namespace
#endif

DInputButtonMonitor::DInputButtonMonitor(QObject* const parent)
    : QObject(parent), impl_(std::make_unique<Impl>())
{
}

DInputButtonMonitor::~DInputButtonMonitor()
{
    stop();
}

void DInputButtonMonitor::start(const quintptr nativeWindowHandle)
{
    stop();
#ifdef _WIN32
    impl_->window = reinterpret_cast<HWND>(nativeWindowHandle);
    if (impl_->window == nullptr || FAILED(DirectInput8Create(GetModuleHandleW(nullptr),
            DIRECTINPUT_VERSION, IID_IDirectInput8W,
            reinterpret_cast<void**>(&impl_->directInput), nullptr))) {
        emit statusChanged(QStringLiteral("DirectInput unavailable"));
        return;
    }
    impl_->pollTimer = new QTimer(this);
    impl_->pollTimer->setTimerType(Qt::PreciseTimer);
    impl_->pollTimer->setInterval(8);
    connect(impl_->pollTimer, &QTimer::timeout, this, &DInputButtonMonitor::pollDevices);
    impl_->scanTimer = new QTimer(this);
    impl_->scanTimer->setInterval(2000);
    connect(impl_->scanTimer, &QTimer::timeout, this, [this] {
        const bool mappedPresent = std::any_of(impl_->devices.cbegin(), impl_->devices.cend(),
            [this](const Impl::Device& device) {
                return device.guid.compare(impl_->configuredGuid, Qt::CaseInsensitive) == 0;
            });
        if (impl_->devices.empty() || (impl_->enabled && !mappedPresent)) scanDevices();
    });
    scanDevices();
    impl_->pollTimer->start();
    impl_->scanTimer->start();
#else
    Q_UNUSED(nativeWindowHandle)
    emit statusChanged(QStringLiteral("DirectInput is only available on Windows"));
#endif
}

void DInputButtonMonitor::stop()
{
    if (impl_->pollTimer != nullptr) {
        impl_->pollTimer->stop();
        delete impl_->pollTimer;
        impl_->pollTimer = nullptr;
    }
    if (impl_->scanTimer != nullptr) {
        impl_->scanTimer->stop();
        delete impl_->scanTimer;
        impl_->scanTimer = nullptr;
    }
#ifdef _WIN32
    releaseDevices(*impl_);
    if (impl_->directInput != nullptr) {
        impl_->directInput->Release();
        impl_->directInput = nullptr;
    }
    impl_->window = nullptr;
#endif
    if (impl_->mappedPressed) emit buttonPressedChanged(false);
    impl_->mappedPressed = false;
    impl_->mapping = false;
}

void DInputButtonMonitor::configure(const bool enabled, const QString& deviceGuid,
    const int buttonIndex)
{
    impl_->enabled = enabled;
    impl_->configuredGuid = deviceGuid;
    impl_->configuredButton = buttonIndex;
    if (!enabled && impl_->mappedPressed) {
        impl_->mappedPressed = false;
        emit buttonPressedChanged(false);
    }
    if (enabled && (deviceGuid.isEmpty() || buttonIndex < 0)) {
        emit statusChanged(QStringLiteral("Map a DirectInput button first"));
    }
}

void DInputButtonMonitor::beginMapping()
{
    impl_->mapping = true;
    scanDevices();
    emit statusChanged(impl_->devices.empty()
            ? QStringLiteral("No DirectInput controller found")
            : QStringLiteral("Press and release a wheel button…"));
}

void DInputButtonMonitor::cancelMapping()
{
    impl_->mapping = false;
    emit statusChanged(QStringLiteral("Mapping cancelled"));
}

void DInputButtonMonitor::scanDevices()
{
#ifdef _WIN32
    if (impl_->directInput == nullptr) return;
    releaseDevices(*impl_);
    impl_->directInput->EnumDevices(DI8DEVCLASS_GAMECTRL, enumerateController, impl_.get(),
        DIEDFL_ATTACHEDONLY);
    qCInfo(logAudio) << "DirectInput controllers detected:" << impl_->devices.size();
    if (!impl_->mapping) {
        emit statusChanged(impl_->devices.empty() ? QStringLiteral("No DirectInput controller found")
                                                  : QStringLiteral("DirectInput ready"));
    }
#endif
}

void DInputButtonMonitor::pollDevices()
{
#ifdef _WIN32
    bool mappedPressed = false;
    bool mappedDevicePresent = false;
    for (auto& device : impl_->devices) {
        HRESULT result = device.handle->Poll();
        if (FAILED(result)) {
            device.handle->Acquire();
            result = device.handle->Poll();
        }
        DIJOYSTATE2 state{};
        if (FAILED(result) || FAILED(device.handle->GetDeviceState(sizeof(state), &state))) continue;

        for (int button = 0; button < 128; ++button) {
            const bool pressed = (state.rgbButtons[button] & 0x80U) != 0;
            const bool wasPressed = device.previous[static_cast<std::size_t>(button)];
            if (impl_->mapping && pressed && !wasPressed) {
                impl_->mapping = false;
                impl_->configuredGuid = device.guid;
                impl_->configuredButton = button;
                emit mappingCaptured(device.name, device.guid, button);
                emit statusChanged(QStringLiteral("%1 — Button %2").arg(device.name).arg(button + 1));
            }
            device.previous[static_cast<std::size_t>(button)] = pressed;
        }

        if (device.guid.compare(impl_->configuredGuid, Qt::CaseInsensitive) == 0) {
            mappedDevicePresent = true;
            if (impl_->configuredButton >= 0 && impl_->configuredButton < 128) {
                mappedPressed = (state.rgbButtons[impl_->configuredButton] & 0x80U) != 0;
            }
        }
    }
    mappedPressed = impl_->enabled && mappedDevicePresent && mappedPressed;
    if (mappedPressed != impl_->mappedPressed) {
        impl_->mappedPressed = mappedPressed;
        emit buttonPressedChanged(mappedPressed);
    }
#endif
}

} // namespace raceengineer
