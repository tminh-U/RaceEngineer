#include "telemetry/common/WindowsSharedMemory.h"

#ifdef _WIN32
#include <memoryapi.h>
#endif

namespace raceengineer {

WindowsSharedMemory::~WindowsSharedMemory()
{
    close();
}

bool WindowsSharedMemory::open(const wchar_t* const mappingName, const std::size_t bytes) noexcept
{
    close();
#ifdef _WIN32
    handle_ = OpenFileMappingW(FILE_MAP_READ, FALSE, mappingName);
    if (handle_ == nullptr) {
        return false;
    }
    view_ = MapViewOfFile(handle_, FILE_MAP_READ, 0, 0, bytes);
    if (view_ == nullptr) {
        CloseHandle(handle_);
        handle_ = nullptr;
        return false;
    }
    size_ = bytes;
    return true;
#else
    (void)mappingName;
    (void)bytes;
    return false;
#endif
}

void WindowsSharedMemory::close() noexcept
{
#ifdef _WIN32
    if (view_ != nullptr) {
        UnmapViewOfFile(view_);
    }
    if (handle_ != nullptr) {
        CloseHandle(handle_);
    }
    handle_ = nullptr;
#endif
    view_ = nullptr;
    size_ = 0;
}

bool WindowsSharedMemory::isOpen() const noexcept
{
    return view_ != nullptr;
}

} // namespace raceengineer
