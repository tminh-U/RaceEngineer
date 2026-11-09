// Resource policy tests deliberately exclude controller hardware on every platform.
#include "input/DInputButtonMonitor.h"

namespace raceengineer {
struct DInputButtonMonitor::Impl {};
DInputButtonMonitor::DInputButtonMonitor(QObject* parent) : QObject(parent), impl_(std::make_unique<Impl>()) {}
DInputButtonMonitor::~DInputButtonMonitor() = default;
void DInputButtonMonitor::start(quintptr) { emit statusChanged(QStringLiteral("DirectInput test stub")); }
void DInputButtonMonitor::stop() {}
void DInputButtonMonitor::configure(bool, const QString&, int) {}
void DInputButtonMonitor::beginMapping() {}
void DInputButtonMonitor::cancelMapping() {}
void DInputButtonMonitor::scanDevices() {}
void DInputButtonMonitor::pollDevices() {}
} // namespace raceengineer
