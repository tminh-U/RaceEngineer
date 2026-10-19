# RaceEngineer 1.0.6 — release verification

## Frontend and branding

- [Full Qt UI suite](render-report.json): 15 QML components compile, 51/51 baseline facade methods exercised, 12 Cockpit breakpoints and 20 window captures, no QML/Qt warnings.
- [Focused fixes](focused-fix/focused-fix-regressions.json): 3/3 groups pass, including startup error scrolling/actions, API diagnostics and voice navigation with draft preservation.
- [Edge cases](edge-audit/edge_surface_audit.json): 25/25 pass across six window sizes.
- [Branding audit](logo-asset-audit.json): ten PNG sizes and seven ICO frames are grayscale, README/CMake/Windows resource references resolve, and the header logo loads in Qt.
- [Legacy native dialog contract](native-legacy-dialog-smoke.json): Qt meta-object dispatch and the dialog alias remain valid, visible and centered at 880×576. This uses the fake facade; it is not a Windows binary test.
- Root reviewed the new Cockpit screenshot used in both READMEs, the header logo, and the generated monochrome identity. [Cockpit 1280×800](screenshots/page-0-1280x800.png).

UI captures use sample telemetry and a fake application facade. They verify presentation and UI calls, not a real simulator or audio device.

## Native release gate

The Windows workflow builds from the current source, runs CTest, packages with the existing scripts, verifies the manifest and executable version, and renders the packaged setup UI. A tagged build prepares a draft release; publishing follows review of those results. The previous release supplies only unchanged model/voice runtime assets, checked against its published archive SHA-256. The executable, Qt package and installer are built again.

[Windows workflow 39297911475](https://github.com/tminh-U/RaceEngineer/actions/runs/39297911475) succeeded: 6/6 CTests, executable version/manifest verification, built and packaged UI assertions, and all seven source ICO frames embedded in both app and installer. Root independently checked the downloaded app EXE and all eleven packaged logo files against source, matched installer/ZIP hashes to GitHub asset digests, and reviewed all seven Windows UI captures. [Verification record](native/release-verification.json), [embedded icons](native/embedded-icons.json), [first setup](native/setup-ui-check/step-0.png), [Google voice dialog](native/setup-ui-check/step-6.png).

[RaceEngineer v1.0.6](https://github.com/tminh-U/RaceEngineer/releases/tag/v1.0.6) is published as the latest stable release, with installer, portable ZIP and SHA256SUMS. Its binaries come from commit `a5a5fca8e3758b393243c93a7748cbfed07c60fe`. The UI gate requires the executable's own `Setup UI 880x576: passed` marker and rejects a crash during its exit grace period. If native model warmup/shutdown still runs after that period, the harness terminates only the test instance and records this in JSON; this does not verify shutdown during model warmup. App backend code is unchanged.

The GPU-less Windows runner registers its existing Chrome SwiftShader ICD only for CI; this driver is not bundled or installed by RaceEngineer. The Vulkan-enabled release requires a working Vulkan runtime/driver, including for CPU compute. Release CPU code targets AVX2 instead of inheriting AVX-512 from the build host.

Real AC/ACC sessions, microphones, audio, DirectInput hardware and live-track calibration are separate checks. No measured CPU/GPU performance improvement is claimed.
