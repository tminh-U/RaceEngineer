# Race Engineer

Native Windows x64 race engineer for Assetto Corsa and Assetto Corsa Competizione. It combines verified shared-memory telemetry, deterministic race logic, local voice processing, an OpenAI-compatible LLM server, and local native VieNeu-TTS C++ speech output.

## What works

- AC/ACC auto-detection and read-only shared-memory telemetry
- Normalized `RaceState`, bounded `RaceHistory`, fuel/lap calculations, event transitions and cooldowns
- Mock telemetry in Debug builds
- Microphone capture with 80 ms fixed pre-roll and held push-to-talk (`Ctrl+Space`)
- Local `vinai/PhoWhisper-small` Q5_1 transcription through whisper.cpp (Vietnamese-first with racing English preserved)
- Configurable push-to-talk using `Ctrl+Space` and/or a held DirectInput wheel button
- OpenAI-compatible chat completions for llama.cpp and similar local/LAN/cloud servers, with streaming, cancellation, timeout and one bounded transient retry
- Local telemetry tool calling with bounded conversation history
- Optional API key storage in Windows Credential Manager; the Authorization header is omitted when the key is empty
- Local native VieNeu-TTS v3 Turbo C++ speech with Vietnamese voice fine-tuning (Minh Quân LoRA model & studio presets) accelerated on AMD Radeon 680M via Vulkan
- Piper TTS backend as fallback and low-latency pre-generated spotter alerts
- Priority audio dispatch; critical/spotter messages interrupt lower-priority speech
- Dashboard, telemetry viewer, AI settings, API statistics and system tray

The app reads the full AC/ACC graphics page for real lap, position, gap, flag and pit-state data. ACC opponent names, leaderboard position and lap pace use ACC's official UDP Broadcasting interface. Set `updListenerPort` to a non-zero port (for example `9000`) in `Documents/Assetto Corsa Competizione/Config/broadcasting.json` before starting ACC; the connection and command passwords remain supported. If broadcasting is disabled, the core shared-memory telemetry continues working and opponent fields remain unavailable rather than being fabricated.

## Requirements

- Windows 10/11 x64
- Visual Studio 2022 Build Tools with Desktop development with C++
- CMake 3.24+
- Ninja (or the Visual Studio 2022 generator)
- Qt 6.5+ MSVC x64 with Core, GUI, Quick, Quick Controls 2, Network, Multimedia and Widgets
- Python 3 (setup only, used for the one-time PhoWhisper conversion and reference WAV resampling)
- LunarG Vulkan SDK (optional but recommended for the Radeon 680M path; without it the Release build uses the measured CPU fallback)

The validated local setup is MSVC 17.14, Qt 6.8.3, CMake 4.4.1 and Ninja 1.13.2.

## Runtime models

Models and binary runtimes are intentionally ignored by Git. Install the `vinai/PhoWhisper-small` Q5_1 model, Piper ONNX model, and the native `VieNeu-TTS v3 Turbo` assets:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\setup_runtime.ps1
```

This sets up:

```text
models/ggml-phowhisper-small-q5_1.bin
models/piper/vi_VN-vais1000-medium.onnx
models/vieneu-v3/backbone.gguf
models/vieneu-v3/config.json
models/vieneu-v3/tokenizer.json
models/vieneu-v3/vieneu_v3_heads.npz
models/vieneu-v3/acoustic/vieneu_acoustic_weights.npz
models/vieneu-v3/codec/moss_audio_tokenizer_decode_full.onnx
models/vieneu-v3/voices_v3_turbo.json
```

PhoWhisper conversion is pinned to the VinAI `PhoWhisper-small` checkpoint revision and uses the vendored whisper.cpp converter plus `whisper-quantize q5_1`; Python/PyTorch are used only for this one-time conversion, never at runtime. VieNeu-TTS v3 Turbo runs natively in C++ through `third_party/vieneu.cpp`, offloading backbone operations to AMD Radeon 680M via Vulkan, with in-memory PCM playback to `QAudioSink` and zero temporary WAV files. Missing voice components degrade safely: telemetry and text responses keep working.

Built-in alerts are read from `assets/spotter/manifest.json` and play a non-repeating random WAV for zero-latency spotter callouts. Dynamic LLM answers synthesize through native VieNeu-TTS.

## Build with MSVC and Ninja

From an x64 Native Tools Command Prompt:

```powershell
cmake -S . -B build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

When `VULKAN_SDK` is set, CMake enables whisper.cpp's Vulkan backend and the app selects Vulkan device 0 by default (the validated machine exposes the Radeon 680M there). If no SDK is available, the same source builds with the measured 8-thread CPU fallback.

To reproduce the local STT measurements, configure with `-DRACEENGINEER_BUILD_STT_BENCHMARK=ON`, then run the harness with `--threads 4|6|8`, `--no-gpu`, `--no-flash`, or `--no-warmup` as needed. It reuses one loaded context and prints model load, warm-up, encoder, decoder and backend-overhead timings for each WAV input.

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
MessageDispatcher → VieNeuTtsBackend (native C++ / Vulkan) / PiperTtsBackend

Microphone → AudioCapture → held push-to-talk → PhoWhisper-small Q5_1 / whisper.cpp → LLMManager
```

Main source areas:

```text
src/telemetry/   simulator providers and normalized state
src/race/        bounded history and deterministic analysis
src/events/      transition/cooldown event engine
src/audio/       capture, voice controller and priority dispatcher
src/stt/         whisper.cpp recognizer
src/llm/         providers, conversation manager and telemetry tools
src/tts/         backend abstraction, native VieNeu-TTS and Piper implementations
src/spotter/     deterministic spotter boundary
src/config/      JSON settings and Windows credential storage
src/ui/          Qt Quick interface
tests/           offline tests; no simulator, microphone or internet needed
```

## Telemetry provenance

AC opens `Local\acpmf_physics` and `Local\acpmf_static`; ACC uses the same mapping names with ACC-specific layouts. [`structed_file_AC.h`](structed_file_AC.h) and [`structed_file_ACC.h`](structed_file_ACC.h) are the supplied source of truth. Simulator structs stay inside their providers and never reach the LLM layer.

## Test coverage

The offline test executable covers normalization helpers, mock telemetry, fuel averaging, lap history, event transitions/cooldowns, conversation trimming, telemetry tool JSON, provider endpoint/error parsing and the explicit no-fabrication spotter behavior.
