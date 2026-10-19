# RaceEngineer 1.0.6 — release verification

## Frontend and branding

- [Full Qt UI suite](render-report.json): 15 QML components compile, 51/51 baseline facade methods exercised, 12 Cockpit breakpoints and 20 window captures, no QML/Qt warnings.
- [Focused fixes](focused-fix/focused-fix-regressions.json): 3/3 groups pass, including startup error scrolling/actions, API diagnostics and voice navigation with draft preservation.
- [Edge cases](edge-audit/edge_surface_audit.json): 25/25 pass across six window sizes.
- [Branding audit](logo-asset-audit.json): ten PNG sizes and seven ICO frames are grayscale, README/CMake/Windows resource references resolve, and the header logo loads in Qt.
- Root reviewed the new Cockpit screenshot used in both READMEs, the header logo, and the generated monochrome identity. [Cockpit 1280×800](screenshots/page-0-1280x800.png).

UI captures use sample telemetry and a fake application facade. They verify presentation and UI calls, not a real simulator or audio device.

## Native release gate

The Windows workflow builds from the current source, runs CTest, packages with the existing scripts, verifies the manifest and executable version, and renders the packaged setup UI. A tagged build prepares a draft release; publishing follows review of those results. The previous release supplies only unchanged model/voice runtime assets, checked against its published archive SHA-256. The executable, Qt package and installer are built again.

Native Windows verification is pending until that workflow succeeds. Real AC/ACC sessions, microphones, audio, DirectInput hardware and live-track calibration are separate checks. No measured CPU/GPU performance improvement is claimed.
