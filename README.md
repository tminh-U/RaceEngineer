# Race Engineer

Native Windows x64 race engineer for Assetto Corsa and Assetto Corsa Competizione. It combines verified shared-memory telemetry, deterministic race logic, local voice processing, an OpenAI-compatible LLM server, and local Gwen-TTS speech output.

## What works

- AC/ACC auto-detection and read-only shared-memory telemetry
- Normalized `RaceState`, bounded `RaceHistory`, fuel/lap calculations, event transitions and cooldowns
- Mock telemetry in Debug builds
- Microphone capture with 80 ms fixed pre-roll and held push-to-talk (`Ctrl+Space`)
- Local `vinai/PhoWhisper-medium` Q5_0 transcription through whisper.cpp (Vietnamese-first with racing English preserved)
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

Models and binary runtimes are intentionally ignored by Git. Install the `vinai/PhoWhisper-medium` Q5_0 model, the CrispASR Windows Vulkan runtime, Gwen-TTS talker/codec models and the official Vietnamese Khánh Toàn reference voice:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\setup_runtime.ps1
```

This creates:

```text
models/ggml-phowhisper-medium-q5_0.bin
models/gwen-tts/gwen-tts-0.6b-q8_0.gguf
models/gwen-tts/qwen3-tts-tokenizer-12hz.gguf
runtime/gwen-tts/crispasr-windows-x86_64-vulkan/crispasr.exe
voices/gwen-tts/khanh_toan.wav
voices/gwen-tts/khanh_toan.txt
```

PhoWhisper conversion is pinned to the VinAI checkpoint revision and uses the vendored whisper.cpp converter plus `whisper-quantize q5_0`; Python/PyTorch are used only for this one-time conversion, never at runtime. The Gwen assets use about 1.35 GB before build-directory copies. Reconfigure/rebuild after installation. The app preloads Gwen once and reuses its local HTTP server; the child process runs below normal priority. Missing voice components degrade safely: telemetry and text responses keep working.

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

CLI options:

- `.\build\RaceEngineer.exe --help`: xem danh sách tham số
- `.\build\RaceEngineer.exe --map-button`: gán nút Push-to-Talk trên vô lăng (DirectInput)
- `.\build\RaceEngineer.exe --set-key <api_key>`: lưu API key vào Windows Credential Manager
- `.\build\RaceEngineer.exe --test-llm`: kiểm tra kết nối tới LLM server

## Moza / DirectInput push-to-talk

1. Kết nối vô lăng vào máy tính.
2. Chạy lệnh: `.\build\RaceEngineer.exe --map-button`
3. Nhấn nút mong muốn trên vô lăng (ví dụ nút Radio trên Moza ES) một lần. Cấu hình sẽ tự động lưu vào `settings.json`.

Input được quét trên worker độ ưu tiên thấp qua chế độ background non-exclusive, không làm gián đoạn game đọc vô lăng. Giữ nút để bắt đầu ghi âm; nhả nút để gửi ngay lập tức tới PhoWhisper.

## AI setup

Cấu hình được lưu trữ dạng JSON tại `settings.json` trong thư mục AppData của Windows.
API key được lưu trữ an toàn trong Windows Credential Manager (`RaceEngineer/LLMApiKey`).

Để lưu API key từ dòng lệnh:
```powershell
.\build\RaceEngineer.exe --set-key "your_api_key_here"
```

Để kiểm tra kết nối API:
```powershell
.\build\RaceEngineer.exe --test-llm
```

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

Microphone → AudioCapture → held push-to-talk → PhoWhisper-medium / whisper.cpp → LLMManager
```

Main source areas:

```text
src/telemetry/   simulator providers and normalized state
src/race/        bounded history and deterministic analysis
src/events/      transition/cooldown event engine
src/audio/       capture, voice controller and priority dispatcher
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

The offline test executable covers normalization helpers, mock telemetry, fuel averaging, lap history, event transitions/cooldowns, conversation trimming, telemetry tool JSON, provider endpoint/error parsing and the explicit no-fabrication spotter behavior.
