# Race Engineer

Native Windows x64 race engineer for Assetto Corsa and Assetto Corsa Competizione. It combines verified shared-memory telemetry, deterministic race logic, local voice processing, an OpenAI-compatible LLM server, and local Gwen-TTS speech output.

## What works

- AC/ACC auto-detection and read-only shared-memory telemetry
- Normalized `RaceState`, bounded `RaceHistory`, fuel/lap calculations, event transitions and cooldowns
- Mock telemetry in Debug builds
- Microphone capture, 320 ms pre-roll, real TEN VAD, push-to-talk (`Ctrl+Space`)
- Local multilingual Whisper small Q5 transcription (Vietnamese-first, English supported)
- Configurable push-to-talk using `Ctrl+Space` and/or a held DirectInput wheel button
- OpenAI-compatible chat completions for llama.cpp and similar local/LAN/cloud servers, with streaming, cancellation, timeout and one bounded transient retry
- Local telemetry tool calling with bounded conversation history
- Optional API key storage in Windows Credential Manager; the Authorization header is omitted when the key is empty
- Local Gwen-TTS 0.6B GGUF speech with the Vietnamese `khanh_toan` voice, served by a persistent low-priority CrispASR process
- Zero-synthesis-latency static alerts: 15 pre-generated Khánh Toàn WAV variants for each built-in spotter/engineer phrase
- Priority audio dispatch; critical/spotter messages interrupt lower-priority speech
- Dashboard, telemetry viewer, AI settings, API statistics and system tray

The app reads the full AC/ACC graphics page for real lap, position, gap, flag and pit-state data. ACC opponent names, leaderboard position and lap pace use ACC's official UDP Broadcasting interface. Set `updListenerPort` to a non-zero port (for example `9000`) in `Documents/Assetto Corsa Competizione/Config/broadcasting.json` before starting ACC; the connection and command passwords remain supported. If broadcasting is disabled, the core shared-memory telemetry continues working and opponent fields remain unavailable rather than being fabricated.

## Requirements

- Windows 10/11 x64
- Visual Studio 2022 Build Tools with Desktop development with C++
- CMake 3.24+
- Ninja (or the Visual Studio 2022 generator)
- Qt 6.5+ MSVC x64 with Core, GUI, Quick, Quick Controls 2, Network, Multimedia and Widgets
- Python 3 (setup only, used to resample the official Khánh Toàn reference WAV)

The validated local setup is MSVC 17.14, Qt 6.8.3, CMake 4.4.1 and Ninja 1.13.2.

## Runtime models

Models and binary runtimes are intentionally ignored by Git. Install the default Whisper model, the CrispASR Windows Vulkan runtime, Gwen-TTS talker/codec models and the official Vietnamese Khánh Toàn reference voice:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\setup_runtime.ps1
```

This creates:

```text
models/ggml-small-q5_1.bin
models/gwen-tts/gwen-tts-0.6b-q8_0.gguf
models/gwen-tts/qwen3-tts-tokenizer-12hz.gguf
runtime/gwen-tts/crispasr-windows-x86_64-vulkan/crispasr.exe
voices/gwen-tts/khanh_toan.wav
voices/gwen-tts/khanh_toan.txt
```

The Gwen assets use about 1.35 GB before build-directory copies. Reconfigure/rebuild after installation. The app preloads Gwen once and reuses its local HTTP server; the child process runs below normal priority. Missing voice components degrade safely: telemetry and text responses keep working.

Built-in alerts are read from `assets/spotter/manifest.json` and play a non-repeating random WAV without calling Gwen at race time. To regenerate the cache after changing a fixed phrase, run `powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/generate_spotter_cache.ps1 -Variants 15`, then rebuild. Dynamic LLM answers still use Gwen-TTS normally.

## Build with MSVC and Ninja

From an x64 Native Tools Command Prompt:

```powershell
cmake -S . -B build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Deploy Qt dependencies for a standalone build directory:

```powershell
C:\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe `
  --debug --qmldir src\ui --no-translations --compiler-runtime `
  build\RaceEngineer.exe
```

Run normally or with synthetic telemetry:

```powershell
.\build\RaceEngineer.exe
.\build\RaceEngineer.exe --mock
```

## Moza / DirectInput push-to-talk

1. Connect the wheel and open **AI & Voice**.
2. Under **Push-to-talk mapping**, choose **Map button**.
3. Press the radio button on the Moza ES once.
4. Enable **DirectInput wheel button**, optionally disable **Ctrl+Space**, then choose **Apply**.

The binding stores the DirectInput device instance GUID and zero-based button index in normal JSON settings. At runtime the UI displays it as `Button 1`, `Button 2`, etc. Input is polled on a low-priority worker using background, non-exclusive access, so the game can continue reading the same wheel. Holding the mapped button begins capture; releasing it ends the utterance and sends it to Whisper.

## AI setup

Open **AI & Voice** in the app. The development defaults target the LG V60 llama.cpp server over Tailscale:

- Provider: OpenAI Compatible
- Base URL: `http://100.114.125.88:8080/v1`
- Model: `race-engineer`
- API key: empty (optional)
- Streaming: enabled
- Timeout: 30 seconds
- Maximum response: 32 tokens
- Temperature: 0.1

Choose **Save**, then **Test /models**. The test performs `GET /v1/models` and verifies that the configured model ID is available. The base URL and model remain editable, so the application is not tied to Tailscale or this phone. Non-secret configuration is stored as human-readable JSON under the Windows application config directory. If provided, the API key is stored separately by Windows Credential Manager under `RaceEngineer/LLMApiKey`.

Live telemetry questions use OpenAI-style tool calls. Tool results carry deterministic semantic fields such as `fuel_status`, `enough_fuel`, `spare_laps`, `missing_laps`, temperature status and gap trend. These conclusions are authoritative so a small local model does not need to recalculate them.

AI replies prefer Vietnamese by default. Clearly English input still receives an English response; unknown or ambiguous detected language falls back to Vietnamese.

Network/API failure never stops telemetry, local history, event detection or the deterministic audio queue. No automatic LLM request is made per telemetry frame.

## Architecture

```text
AC / ACC shared memory
        ↓
normalized RaceState
        ↓
RaceHistory / EventEngine / SpotterEngine
        ↓
ToolRegistry → LLMManager → OpenAI-compatible API
        ↓
MessageDispatcher → GwenTtsBackend → persistent CrispASR/Gwen-TTS server

Microphone → AudioCapture → TEN VAD → whisper.cpp → LLMManager
```

Main source areas:

```text
src/telemetry/   simulator providers and normalized state
src/race/        bounded history and deterministic analysis
src/events/      transition/cooldown event engine
src/audio/       capture, voice controller and priority dispatcher
src/vad/         TEN VAD wrapper and utterance state machine
src/stt/         whisper.cpp recognizer
src/llm/         providers, conversation manager and telemetry tools
src/tts/         backend abstraction and persistent Gwen-TTS implementation
src/spotter/     deterministic spotter boundary
src/config/      JSON settings and Windows credential storage
src/ui/          Qt Quick interface
tests/           offline tests; no simulator, microphone or internet needed
```

## Telemetry provenance

AC opens `Local\acpmf_physics` and `Local\acpmf_static`; ACC uses the same mapping names with ACC-specific layouts. [`structed_file_AC.h`](structed_file_AC.h) and [`structed_file_ACC.h`](structed_file_ACC.h) are the supplied source of truth. Simulator structs stay inside their providers and never reach the LLM layer.

## Test coverage

The offline test executable covers normalization helpers, mock telemetry, fuel averaging, lap history, event transitions/cooldowns, TEN VAD loading, conversation trimming, telemetry tool JSON, provider endpoint/error parsing and the explicit no-fabrication spotter behavior.
