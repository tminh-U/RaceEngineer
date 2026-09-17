#include "telemetry/common/WindowsProcess.h"

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#include <cwchar>
#endif

namespace raceengineer {

RunningSimulator detectRunningSimulator() noexcept
{
#ifdef _WIN32
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return RunningSimulator::None;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool foundAc = false;
    if (Process32FirstW(snapshot, &entry) != FALSE) {
        do {
            if (_wcsicmp(entry.szExeFile, L"AC2-Win64-Shipping.exe") == 0) {
                CloseHandle(snapshot);
                return RunningSimulator::AssettoCorsaCompetizione;
            }
            if (_wcsicmp(entry.szExeFile, L"acs.exe") == 0
                || _wcsicmp(entry.szExeFile, L"acs_x86.exe") == 0) {
                foundAc = true;
            }
        } while (Process32NextW(snapshot, &entry) != FALSE);
    }
    CloseHandle(snapshot);
    return foundAc ? RunningSimulator::AssettoCorsa : RunningSimulator::None;
#else
    return RunningSimulator::None;
#endif
}

} // namespace raceengineer
