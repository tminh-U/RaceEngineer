#include "telemetry/common/WindowsSharedMemory.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
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

bool WindowsSharedMemory::createOrOpen(const wchar_t* const mappingName, const std::size_t bytes) noexcept
{
    close();
#ifdef _WIN32
    SECURITY_DESCRIPTOR sd;
    InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
    SetSecurityDescriptorDacl(&sd, TRUE, nullptr, FALSE);
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = &sd;
    sa.bInheritHandle = FALSE;

    handle_ = CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0,
        static_cast<DWORD>(bytes), mappingName);
    if (handle_ == nullptr) {
        handle_ = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, mappingName);
    }
    if (handle_ == nullptr) {
        return false;
    }
    view_ = MapViewOfFile(handle_, FILE_MAP_ALL_ACCESS, 0, 0, bytes);
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
