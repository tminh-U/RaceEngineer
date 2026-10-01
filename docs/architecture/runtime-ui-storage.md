# Runtime, UI, Settings and Release Architecture

[Architecture index](../architect.md)

## Entry point and ownership

[main.cpp](../../src/main.cpp) creates `QApplication`, sets application
identity/version and Qt Quick's Basic style, then parses CLI options.
`PROJECT_VERSION` in [CMakeLists.txt](../../CMakeLists.txt) supplies the version.
`--version` exits before creating Application, workers or models.

[Application](../../src/app/Application.h) is the QObject facade exposed to QML
as `backend`. It owns settings, current normalized state, race history,
event/spotter engines, LLM manager, radio dispatcher, strategy predictor,
recorder and audio ducking. Simulator readers, capture, recognition and
controller monitoring use queued signals rather than direct QML access.

Normal startup loads `RaceEngineer/Main` from the compiled QML module.
`--mock` works only in Debug builds. `--map-button` captures a DirectInput
binding; `--set-key` stores the credential; both then exit. `--test-llm`
tests the configured endpoint without loading QML, but can use the network.

## Startup and shutdown

1. Settings load/migrate; `LocalAiRuntime::resolveAndApply()` selects the
   compute device before voice backends are constructed.
2. Application resolves model paths beside the executable, falling back to the
   parent source directory in development. It configures recording, sharing,
   strategy, LLM, devices and persisted feature switches.
3. Signals are wired, telemetry/capture/STT/DirectInput threads start and TTS
   warms up on its backend worker. Configured API endpoints are tested
   asynchronously after 500 ms.
4. Main selects Qt Quick's software graphics API when the saved renderer switch
   is off, loads QML and passes the window handle to DirectInput. UI graphics
   selection is separate from local AI CPU/Vulkan selection.
5. `finishStartupIfReady()` requires successful STT **and** TTS warm-ups.
   QML's startup overlay displays progress/errors/retry; PTT, text requests and
   pending lap-summary speech are gated on `startupReady`.

Telemetry starts before warm-up finishes. Missing voice assets therefore do
not stop polling, but the current overlay and `askText()` gate prevent normal
UI/text interaction. The older claim that missing STT leaves text chat
immediately usable does not describe the current implementation.

Shutdown finishes an observed Race recording session, removes the keyboard
filter, restores game volume and clears radio messages. It stops DirectInput,
shuts down TTS, cancels STT, stops capture/telemetry and quits/joins the threads.
Backend, predictor and recorder destructors clean up their workers; the
recorder kills outstanding Python work and waits for its disk pool.
With minimize-on-close enabled, closing the window hides it rather than running
shutdown; tray Exit quits the application.

## Thread boundaries

| Execution context | Work | Boundary |
| --- | --- | --- |
| UI: Application/QML/SettingsManager | Snapshots, bounded history/events/spotter, summaries, tools, settings and display models | Queued `RaceState` copies in; properties/signals/invokables for QML. |
| `TelemetryWorker`, low priority | Detection/provider polling | Detect every 1 s; poll every 33 ms; publication at most about 30 Hz. |
| `AudioCaptureWorker`, low priority | Capture, resampling, PTT assembly | Completed mono 16 kHz PCM and level/status signals. |
| `SpeechRecognitionWorker`, normal priority | Whisper warm-up/inference | Queued transcription in; text/status/errors out. |
| `DirectInputWorker`, low priority | Discovery, mapping and button polling | Window/configuration in; presses/binding/status out. |
| VieNeu worker | Native initialization/synthesis/cache preparation | PCM/status return to facade; Qt playback uses facade. |
| StrategyPredictor pool, one low-priority worker | Approved planner or XGBoost C API | Captured state/history/revision; stale decisions rejected. |
| StrategyRecorder pool, one low-priority worker | JSONL writes, rotation, share copies | Bounded pending writes; QObject status updates. |
| Python subprocess | Process snapshots/train research pace/tyre models | Disconnected-only, cancelled on reconnect, asynchronous JSON status. |
| Qt network event loop | LLM HTTP/SSE and optional uploads | `QNetworkAccessManager` callbacks, without blocking HTTP calls. |

Deterministic analysis stays short and bounded in
`Application::onStateUpdated()` on the UI thread. Audio inference/residency
details are in the [voice chapter](voice-stt-tts.md).

## State publication and session lifecycle

`Application::onStateUpdated()` performs the integration:

1. Notify recorder of simulator/Race activity and copy the latest state.
2. Compare simulator, track, car, race lap count and session type to the session
   key; lap-counter rollback also starts a new session.
3. On change, finish the previous observed Race session, reset history and
   event/spotter baselines, cancel proximity/summary speech, assign a new
   recording ID and advance strategy revision.
4. Update history, summary, pit transitions, lap recording and strategy;
   generate events and independently run proximity.
5. Filter disabled categories before event logging/dispatch. Publish current
   spotter overlap to the dispatcher for speech revalidation.
6. Convert available fields to `QVariantMap`, add available native fuel
   estimates and emit `telemetryChanged`.

Session tracking runs even with strategy disabled. Disconnect publishes an
empty disconnected state, resetting live session data. It does not implicitly
erase the user's conversation history.

## UI and controls

[qml/Main.qml](../../qml/Main.qml) contains Dashboard, Analysis & Strategy,
Engineer, Telemetry, AI & Voice and Settings pages, selected through a `Loader`.
QML handles presentation/gestures; native setters check availability, persist
choices and cancel affected work. Derived race conclusions originate in C++.

Engineer controls cover fuel, tyre overheating, lap delta/new best, proximity,
flags and damage; summary has a separate opt-in switch. Allowlisted LLM tools
use the same setters. Sharing consent and setup realism confirmation remain
direct user actions; see the [LLM chapter](llm-prompts-tools.md).

The frameless window supports dragging/resizing. `QSystemTrayIcon` offers
Show/Exit; minimize-to-tray/minimize-on-close apply only when a tray exists.
Start with Windows uses
`HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, separate from JSON.
Renderer/AI compute changes report when restart is required.

MiSans Latin is embedded only if present at CMake configure time, with Segoe UI
fallback and Xiaomi attribution. See [font setup](../build.md#runtime-assets).

## Settings, credentials and storage

[SettingsManager](../../src/config/SettingsManager.cpp) writes `settings.json`
under `QStandardPaths::AppConfigLocation`. Current schema is **13**; migrations
and defaults preserve older settings. Native setters normalize API limits,
button index, volume and compute selection.

| Data | Owner/location | Behavior |
| --- | --- | --- |
| Provider URL/model/options, PTT, microphone, TTS, feature/window switches, driver/style | SettingsManager / app config | JSON persistence; LLM key excluded. |
| LLM API key | CredentialStore / Windows Credential Manager `RaceEngineer/LLMApiKey` | Generic credential read/write/delete. |
| Legacy sharing token | CredentialStore / `RaceEngineer/RaceDataShareToken` | Read for compatibility. |
| Sharing endpoint/token | Executable-adjacent `.env`, overridden by process environment; legacy config fallback | Receiver configuration does not grant consent. |
| Raw lap/pit logs | `AppLocalDataLocation/pit_strategy_laps.jsonl` | Rotate at 20 MiB into `recordings/`; user removes archives. |
| Snapshots/research models/reports | Local data / `training/` | Immutable process snapshots, separate runs, no implicit pit deployment. |
| Pending sanitized uploads | Local data / `share_queue/` | Deleted on consent revocation; raw logs remain. |
| UI event/chat/tool lists | Application memory | 100 events, 100 chat display entries, 50 tool entries; separate from LLM context. |
| Diagnostics | [Logging.cpp](../../src/utils/Logging.cpp) | Qt app/provider/event/spotter/audio/STT/LLM/API/tool/TTS categories; distinct from training logs. |

Use **Open data folder** to locate actual paths. Runtime models/user data are
ignored by Git. [Strategy/training](strategy-xgboost.md) covers recording fields,
pending-write bounds and upload lifecycle.

## Build and release boundary

[CMake](../../CMakeLists.txt) builds Qt code, statically links whisper.cpp and
imports `vieneu-tts` DLL/library from `third_party/vieneu-bin/`.
`VULKAN_SDK` enables Whisper Vulkan; CUDA is disabled. Post-build steps copy
available local models, presets, native DLLs and spotter audio.

[Production scripts](../../production/README.md) keep Release in
`build-production/`. `build-release.ps1` configures afresh, builds, deploys Qt
and runs CTest. `package-portable.ps1` installs to staging, bundles runtime
assets/AC companion, hashes files, creates a portable ZIP and runs Inno Setup.
Model preparation is separate; release build/package do not train models.

Strategy copy/install requires `deployment_ready`. The packaging marker alone
is insufficient: StrategyPredictor still checks profiles, metrics, schema,
hashes and parity. Planner calibration has a separate gate.

`verify-release.ps1` checks required files, SHA-256 hashes, forbidden debug/user
files, obsolete installer payloads and executable version against the manifest.
The per-user installer does not copy/update the companion inside AC; users must
install the packaged companion separately, including geometry updates.

## Verification

Run existing offline C++ tests in an already built tree:

```powershell
ctest --test-dir build --output-on-failure
```

- [RaceStateTests](../../tests/RaceStateTests.cpp): state/history, events,
  spotter geometry, settings, tools/prompts/provider parsing and dispatcher.
- [StrategyRecorderTests](../../tests/StrategyRecorderTests.cpp): recording,
  excluded-lap metadata, pit events, refusal to process while connected and
  rotation preserving archives/legacy logs. It does not test upload consent,
  realism filtering or a completed Python job.
- [test_local_training.py](../../tests/test_local_training.py): separate Python
  pipeline checks; run `python tests/test_local_training.py`. Model-fitting
  cases require installed numpy/xgboost and otherwise skip.

CTest does not register the Python suite. Offline checks do not prove live
simulator directions, microphone/acoustic quality, provider compatibility or
strategy readiness. See the [build guide](../build.md) for builds/release checks
and the [telemetry chapter](telemetry-radar-spotter.md) for live calibration limits.
