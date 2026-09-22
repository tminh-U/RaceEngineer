# RaceEngineer production package

This folder contains the repeatable Windows x64 Release build and package flow.

## Build

Run from an x64 MSVC environment with Qt and Ninja available:

```powershell
.\production\scripts\build-release.ps1 `
  -QtPrefix C:\Qt\6.8.3\msvc2022_64 `
  -EnableLtcg
```

The production build is kept in `build-production/`, separate from the
development build. Runtime models and voices must already be installed; the
build does not download them.

## Package and verify

Install Inno Setup 6 and make `ISCC.exe` available on `PATH`, or pass its path
through `-ISCCPath`.

```powershell
.\production\scripts\package-portable.ps1 `
  -Version 1.0.1 `
  -QtPrefix C:\Qt\6.8.3\msvc2022_64 `
  -ISCCPath "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"

.\production\scripts\verify-release.ps1 `
  -PackageDirectory .\production\dist\RaceEngineer-1.0.1
```

The package script:

1. Runs `cmake --install`.
2. Deploys Qt runtime files.
3. Copies spotter audio, models, voices, and the optional AC Python companion.
4. Bundles the MSVC runtime files and `vc_redist.x64.exe`.
5. Writes `release-manifest.json` with SHA-256 hashes.
6. Creates a Windows x64 portable ZIP.
7. Compiles `RaceEngineer-1.0.1-Setup.exe` with Inno Setup.

The Inno Setup installer installs per-user under
`%LOCALAPPDATA%\Programs\RaceEngineer`, creates a Start Menu shortcut, and
registers a standard Windows uninstaller. It can also run the bundled Visual
C++ x64 runtime installer with elevation when available.

Do not put API keys or user `settings.json` files in this folder. API keys are
stored through Windows Credential Manager.

## Optional Assetto Corsa addon

The package includes the optional Python companion at
`extras\AssettoCorsa\apps\python\RaceEngineer`. The installer does not write into
the game directory; copy that folder to Assetto Corsa or install it through
Content Manager when extra telemetry is needed.
