# RaceEngineer Build Guide

This guide is for developers and release maintainers. End users should install
the generated `RaceEngineer-X.Y.Z-Setup.exe` instead of cloning the repository
and running build scripts.

## Prerequisites

- Inno Setup 6 (`ISCC.exe`) for building the Windows installer.
- Windows x64.
- Visual Studio 2022 Build Tools with the x64 MSVC toolchain.
- CMake 3.24 or newer.
- Ninja.
- Qt 6.5 or newer, built for MSVC x64.
- Vulkan SDK (optional). When `VULKAN_SDK` is available, the build enables the
  whisper.cpp Vulkan backend.

Runtime assets:

- `models/ggml-phowhisper-small-q5_1.bin`
- `models/vieneu-v3/`
- `voices/vieneu/`
- native files under `third_party/vieneu-bin/`

Run commands from an x64 Visual Studio Developer Command Prompt. A normal
PowerShell window may find CMake but still fail to find `cl.exe` or the MSVC
runtime.

## Runtime assets

Runtime models and voices are intentionally ignored by Git. Developer-only
preparation is available through:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\setup_runtime.ps1
```

This may download large model files and perform a one-time PhoWhisper
conversion. Do not run it for a normal end-user installation; the release
installer is expected to contain the required runtime assets.

## Development build

Configure a Release build with Qt:

```powershell
cmake -S . -B build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64
```

Build it:

```powershell
cmake --build build --parallel
```

The Windows CMake configuration automatically runs `windeployqt` after
building `RaceEngineer.exe` when it can find the Qt deployment tool. This
copies Qt DLLs, plugins, QML modules, and the MSVC runtime beside the
executable.

Run the offline tests:

```powershell
ctest --test-dir build --output-on-failure
```

Run the application:

```powershell
.\build\RaceEngineer.exe
.\build\RaceEngineer.exe --help
.\build\RaceEngineer.exe --version
```

Useful command-line options:

```text
--mock                 Use synthetic telemetry in a Debug build
--map-button           Map a DirectInput steering-wheel button
--set-key <api_key>    Store the API key in Windows Credential Manager
--test-llm             Test the configured OpenAI-compatible endpoint
```

## Debug build and mock telemetry

Mock telemetry is compiled into Debug configurations:

```powershell
cmake -S . -B build-debug -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64

cmake --build build-debug --parallel
ctest --test-dir build-debug --output-on-failure
.\build-debug\RaceEngineer.exe --mock
```

Use a new build directory when switching between Debug and Release to avoid
carrying incompatible generated configuration.

## Production build

The production script configures a fresh Release tree, builds, deploys Qt, and
runs the tests:

```powershell
powershell -ExecutionPolicy Bypass -File .\production\scripts\build-release.ps1 `
  -QtPrefix C:\Qt\6.8.3\msvc2022_64 `
  -EnableLtcg
```

The output is written to `build-production/`. Runtime models and voices must
already exist before this step; the script does not download them.

## Create the portable package and installer

Package the production build:

```powershell
powershell -ExecutionPolicy Bypass -File .\production\scripts\package-portable.ps1 `
  -Version 1.0.1 `
  -BuildDirectory .\build-production `
  -QtPrefix C:\Qt\6.8.3\msvc2022_64 `
  -VCRedistPath C:\BuildTools\VC\Redist\MSVC\v143\vc_redist.x64.exe `
  -ISCCPath "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
```

The package step:

1. Runs `cmake --install`.
2. Deploys Qt and the MSVC runtime.
3. Copies spotter audio, models, voices, and the optional AC Python companion.
4. Writes `release-manifest.json` with SHA-256 hashes.
5. Creates a Windows x64 ZIP.
6. Compiles `RaceEngineer-1.0.1-Setup.exe` with Inno Setup.

The installer uses a standard Windows setup wizard, installs per-user under
`%LOCALAPPDATA%\Programs\RaceEngineer`, creates a Start Menu shortcut, and
registers an uninstaller.

The optional Assetto Corsa companion is stored in the package at:

```text
extras\AssettoCorsa\apps\python\RaceEngineer
```

The installer does not write into the user's Assetto Corsa directory. Copy this
folder into the game's `apps\python` directory or install it through Content
Manager when the extra telemetry is required.

## Verify a release package

Run verification against the staging directory:

```powershell
powershell -ExecutionPolicy Bypass -File .\production\scripts\verify-release.ps1 `
  -PackageDirectory .\production\dist\RaceEngineer-1.0.1
```

Verification checks:

- required executable, Qt, and MSVC files;
- model, voice, and spotter assets;
- optional AC Python companion;
- every manifest file exists and matches its SHA-256 hash;
- no `.pdb`, `.ilk`, or user `settings.json` is included;
- the packaged executable accepts `--version`.

## Clean reconfigure

Use a fresh build directory when changing the generator, compiler, Qt version,
or build type:

```powershell
cmake --fresh -S . -B build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64
cmake --build build --parallel
```

For production, `build-release.ps1` already uses a fresh configuration.

## Troubleshooting

### `cl.exe` was not found

Open an x64 Visual Studio Developer Command Prompt and rerun the command.

### Qt DLL is missing when launching the executable

Build from the configured CMake tree so the `windeployqt` post-build step runs.
For a one-off manual deployment:

```powershell
C:\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe `
  --release --qmldir qml --no-translations --compiler-runtime `
  .\build\RaceEngineer.exe
```

### CMake cannot find Qt

Set `-DCMAKE_PREFIX_PATH` to the Qt MSVC directory, for example:

```text
C:\Qt\6.8.3\msvc2022_64
```

### Vulkan is unavailable

The project still builds with the CPU backend when `VULKAN_SDK` is not set.
Install the Vulkan SDK and reopen the Developer Command Prompt to enable its
environment variables.

### Runtime model is missing

Install the developer runtime assets with `scripts\setup_runtime.ps1`, or build
and package from a checkout that already contains the ignored `models/` and
`voices/` directories.

## Release output

Important generated paths are:

```text
build\RaceEngineer.exe
build-production\RaceEngineer.exe
production\dist\RaceEngineer-X.Y.Z\
production\dist\RaceEngineer-X.Y.Z-windows-x64.zip
production\dist\RaceEngineer-X.Y.Z-Setup.exe
```
