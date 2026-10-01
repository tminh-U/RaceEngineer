#include "audio/AudioDucker.h"
#include "utils/Logging.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <psapi.h>
#pragma comment(lib, "ole32.lib")
#endif

#include <algorithm>
#include <cctype>
#include <map>
#include <QElapsedTimer>
#include <string>
#include <vector>

namespace raceengineer {

namespace {

#ifdef _WIN32
std::string getProcessBaseName(const DWORD pid)
{
    if (pid == 0) {
        return {};
    }
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess) {
        return {};
    }
    char path[MAX_PATH] = {0};
    DWORD size = MAX_PATH;
    if (QueryFullProcessImageNameA(hProcess, 0, path, &size)) {
        CloseHandle(hProcess);
        std::string fullPath(path);
        const size_t pos = fullPath.find_last_of("\\/");
        std::string base = (pos != std::string::npos) ? fullPath.substr(pos + 1) : fullPath;
        std::transform(base.begin(), base.end(), base.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return base;
    }
    CloseHandle(hProcess);
    return {};
}

bool isKnownSimGame(const std::string& name)
{
    // Recognizes Assetto Corsa, ACC, and major racing simulators
    static const std::vector<std::string> kSimExes = {
        "acs.exe",
        "assettocorsa.exe",
        "acs_x86.exe",
        "ac2-win64-shipping.exe",
        "acc.exe",
        "iracingsim64dx11.exe",
        "iracing.exe",
        "ams2avx.exe",
        "ams2.exe",
        "rfactor2.exe",
        "lemansultimate.exe",
        "f1_24.exe",
        "f1_23.exe",
        "f1_22.exe",
        "forzamotorsport.exe",
        "dirt4.exe",
        "dirt2_s.exe"
    };
    return std::find(kSimExes.begin(), kSimExes.end(), name) != kSimExes.end();
}

bool isIgnoredProcess(const std::string& name, const DWORD pid)
{
    if (pid == GetCurrentProcessId() || pid == 0) {
        return true;
    }
    static const std::vector<std::string> kIgnored = {
        "discord.exe",
        "teamspeak.exe",
        "teamspeak3.exe",
        "slack.exe",
        "teams.exe",
        "ms-teams.exe",
        "devenv.exe"
    };
    return std::find(kIgnored.begin(), kIgnored.end(), name) != kIgnored.end();
}
#endif

} // namespace

struct AudioDucker::WorkerState {
    void initialize()
    {
#ifdef _WIN32
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        comReady_ = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
        shouldUninitialize_ = SUCCEEDED(hr);
#endif
    }

    void shutdown()
    {
        performUnduck();
#ifdef _WIN32
        if (shouldUninitialize_) {
            CoUninitialize();
        }
        shouldUninitialize_ = false;
        comReady_ = false;
#endif
    }

    void performDuck(float duckFactor);
    void performUnduck();

    std::map<unsigned long, float> savedVolumes_;
#ifdef _WIN32
    bool comReady_{false};
    bool shouldUninitialize_{false};
#endif
};

AudioDucker::AudioDucker(QObject* parent)
    : QObject(parent)
    , workerContext_(new QObject)
    , workerState_(std::make_shared<WorkerState>())
{
    workerContext_->moveToThread(&workerThread_);
    connect(&workerThread_, &QThread::finished, workerContext_, &QObject::deleteLater);
    workerThread_.start();
    const auto state = workerState_;
    QMetaObject::invokeMethod(workerContext_, [state] { state->initialize(); }, Qt::QueuedConnection);
}

AudioDucker::~AudioDucker()
{
    if (workerThread_.isRunning()) {
        const auto state = workerState_;
        QMetaObject::invokeMethod(workerContext_, [state] { state->shutdown(); },
                                  Qt::BlockingQueuedConnection);
        workerThread_.quit();
        workerThread_.wait();
    } else if (workerState_) {
        workerState_->shutdown();
    }
}

void AudioDucker::setEnabled(const bool enabled)
{
    if (enabled_ == enabled) {
        return;
    }
    enabled_ = enabled;
    if (!enabled_ && ducked_) {
        setDucked(false);
    }
    emit enabledChanged(enabled_);
}

void AudioDucker::setDuckFactor(const float factor)
{
    duckFactor_ = std::clamp(factor, 0.05f, 0.95f);
}

void AudioDucker::setDucked(const bool ducked)
{
    if (!enabled_ && ducked) {
        return;
    }
    if (ducked_ == ducked) {
        return;
    }
    ducked_ = ducked;
    const auto state = workerState_;
    if (ducked_) {
        const float factor = duckFactor_;
        QMetaObject::invokeMethod(workerContext_, [state, factor] { state->performDuck(factor); },
                                  Qt::QueuedConnection);
    } else {
        QMetaObject::invokeMethod(workerContext_, [state] { state->performUnduck(); },
                                  Qt::QueuedConnection);
    }
    emit duckedChanged(ducked_);
}

void AudioDucker::WorkerState::performDuck(const float duckFactor)
{
#ifdef _WIN32
    QElapsedTimer timer;
    timer.start();
    if (!comReady_) {
        return;
    }

    IMMDeviceEnumerator* pEnum = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&pEnum));
    if (FAILED(hr) || !pEnum) {
        return;
    }

    IMMDevice* pDevice = nullptr;
    hr = pEnum->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    if (FAILED(hr) || !pDevice) {
        pEnum->Release();
        return;
    }

    IAudioSessionManager2* pMgr = nullptr;
    hr = pDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                           reinterpret_cast<void**>(&pMgr));
    if (FAILED(hr) || !pMgr) {
        pDevice->Release();
        pEnum->Release();
        return;
    }

    IAudioSessionEnumerator* pSessions = nullptr;
    hr = pMgr->GetSessionEnumerator(&pSessions);
    if (FAILED(hr) || !pSessions) {
        pMgr->Release();
        pDevice->Release();
        pEnum->Release();
        return;
    }

    int count = 0;
    pSessions->GetCount(&count);

    // Pass 1: detect if any known sim game is currently active in the session list
    bool hasKnownSim = false;
    for (int i = 0; i < count; ++i) {
        IAudioSessionControl* pCtrl = nullptr;
        if (SUCCEEDED(pSessions->GetSession(i, &pCtrl)) && pCtrl) {
            IAudioSessionControl2* pCtrl2 = nullptr;
            if (SUCCEEDED(pCtrl->QueryInterface(__uuidof(IAudioSessionControl2),
                                                reinterpret_cast<void**>(&pCtrl2))) && pCtrl2) {
                DWORD pid = 0;
                pCtrl2->GetProcessId(&pid);
                if (pid != 0) {
                    const std::string name = getProcessBaseName(pid);
                    if (isKnownSimGame(name)) {
                        hasKnownSim = true;
                    }
                }
                pCtrl2->Release();
            }
            pCtrl->Release();
        }
        if (hasKnownSim) {
            break;
        }
    }

    // Pass 2: duck target audio sessions
    for (int i = 0; i < count; ++i) {
        IAudioSessionControl* pCtrl = nullptr;
        if (SUCCEEDED(pSessions->GetSession(i, &pCtrl)) && pCtrl) {
            IAudioSessionControl2* pCtrl2 = nullptr;
            if (SUCCEEDED(pCtrl->QueryInterface(__uuidof(IAudioSessionControl2),
                                                reinterpret_cast<void**>(&pCtrl2))) && pCtrl2) {
                DWORD pid = 0;
                pCtrl2->GetProcessId(&pid);
                if (pid != 0) {
                    const std::string name = getProcessBaseName(pid);
                    const bool shouldDuckSession = hasKnownSim ? isKnownSimGame(name)
                                                               : !isIgnoredProcess(name, pid);
                    if (shouldDuckSession) {
                        ISimpleAudioVolume* pVol = nullptr;
                        if (SUCCEEDED(pCtrl->QueryInterface(__uuidof(ISimpleAudioVolume),
                                                            reinterpret_cast<void**>(&pVol))) && pVol) {
                            float currentVol = 1.0f;
                            pVol->GetMasterVolume(&currentVol);
                            if (savedVolumes_.find(pid) == savedVolumes_.end()) {
                                savedVolumes_[pid] = currentVol;
                            }
                            const float duckedVol = currentVol * duckFactor;
                            pVol->SetMasterVolume(duckedVol, nullptr);
                            pVol->Release();
                        }
                    }
                }
                pCtrl2->Release();
            }
            pCtrl->Release();
        }
    }

    pSessions->Release();
    pMgr->Release();
    pDevice->Release();
    pEnum->Release();
    qCInfo(logAudio) << "Core Audio duck operation took" << timer.elapsed() << "ms";
#endif
}

void AudioDucker::WorkerState::performUnduck()
{
#ifdef _WIN32
    QElapsedTimer timer;
    timer.start();
    if (savedVolumes_.empty()) {
        return;
    }

    if (!comReady_) {
        savedVolumes_.clear();
        return;
    }

    IMMDeviceEnumerator* pEnum = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&pEnum));
    if (FAILED(hr) || !pEnum) {
        savedVolumes_.clear();
        return;
    }

    IMMDevice* pDevice = nullptr;
    hr = pEnum->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    if (FAILED(hr) || !pDevice) {
        pEnum->Release();
        savedVolumes_.clear();
        return;
    }

    IAudioSessionManager2* pMgr = nullptr;
    hr = pDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                           reinterpret_cast<void**>(&pMgr));
    if (FAILED(hr) || !pMgr) {
        pDevice->Release();
        pEnum->Release();
        savedVolumes_.clear();
        return;
    }

    IAudioSessionEnumerator* pSessions = nullptr;
    hr = pMgr->GetSessionEnumerator(&pSessions);
    if (FAILED(hr) || !pSessions) {
        pMgr->Release();
        pDevice->Release();
        pEnum->Release();
        savedVolumes_.clear();
        return;
    }

    int count = 0;
    pSessions->GetCount(&count);
    for (int i = 0; i < count; ++i) {
        IAudioSessionControl* pCtrl = nullptr;
        if (SUCCEEDED(pSessions->GetSession(i, &pCtrl)) && pCtrl) {
            IAudioSessionControl2* pCtrl2 = nullptr;
            if (SUCCEEDED(pCtrl->QueryInterface(__uuidof(IAudioSessionControl2),
                                                reinterpret_cast<void**>(&pCtrl2))) && pCtrl2) {
                DWORD pid = 0;
                pCtrl2->GetProcessId(&pid);
                auto it = savedVolumes_.find(pid);
                if (it != savedVolumes_.end()) {
                    ISimpleAudioVolume* pVol = nullptr;
                    if (SUCCEEDED(pCtrl->QueryInterface(__uuidof(ISimpleAudioVolume),
                                                        reinterpret_cast<void**>(&pVol))) && pVol) {
                        pVol->SetMasterVolume(it->second, nullptr);
                        pVol->Release();
                    }
                }
                pCtrl2->Release();
            }
            pCtrl->Release();
        }
    }

    savedVolumes_.clear();
    pSessions->Release();
    pMgr->Release();
    pDevice->Release();
    pEnum->Release();
    qCInfo(logAudio) << "Core Audio restore operation took" << timer.elapsed() << "ms";
#endif
}

} // namespace raceengineer
