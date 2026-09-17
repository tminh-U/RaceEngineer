#pragma once

#include <cstddef>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

namespace raceengineer {

class WindowsSharedMemory final {
public:
    WindowsSharedMemory() = default;
    ~WindowsSharedMemory();
    WindowsSharedMemory(const WindowsSharedMemory&) = delete;
    WindowsSharedMemory& operator=(const WindowsSharedMemory&) = delete;

    bool open(const wchar_t* mappingName, std::size_t bytes) noexcept;
    void close() noexcept;
    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] const void* data() const noexcept { return view_; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }

    template <typename T>
    bool copyTo(T& value) const noexcept
    {
        if (view_ == nullptr || size_ < sizeof(T)) {
            return false;
        }
        std::memcpy(&value, view_, sizeof(T));
        return true;
    }

private:
#ifdef _WIN32
    HANDLE handle_{nullptr};
#endif
    const void* view_{nullptr};
    std::size_t size_{0};
};

} // namespace raceengineer
