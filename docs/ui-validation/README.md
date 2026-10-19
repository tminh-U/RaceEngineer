# UI validation

The current UI evidence compares the QML surfaces with baseline `f01acc553c0d938af74f4ea6a026dbc6571eb06c`. Rendering and interactions use PySide6 6.8.3 / Qt 6.8.3 with the offscreen Qt Quick software renderer and a strict fake QObject facade.

## Run commands

From the repository root:

```bash
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  .tooling/ui/bin/python tests/ui/run_ui.py
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  .tooling/ui/bin/python tests/ui/baseline_surface_audit.py
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  .tooling/ui/bin/python tests/ui/capture_review.py
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  .tooling/ui/bin/python tests/ui/capture_tyre_tile_items.py
.tooling/ui/bin/python tests/ui/build_validation_bundle.py
```

## Latest results

- Integrated UI suite completed **2026-10-19 04:12:02 UTC**. All 15 QML sources compiled, the six baseline routes loaded, **51/51 baseline-referenced methods** were reached through QML bindings/getters and handlers, and the main QML engine reported **zero warnings and zero unmatched Qt logs**. UI actions used Qt mouse/keyboard events. This call coverage is not, by itself, proof of complete baseline feature parity.
- The targeted baseline surface audit completed **2026-10-19 04:13:38 UTC** with all 19 assertions passing. It checks Telemetry opponent names, gaps, empty/missing-name fallbacks and live updates; position/current lap on Telemetry and Cockpit; and DirectInput binding visibility/update on System and Engineer routes. The targeted audit does not claim an exhaustive audit of every native subsystem or every baseline label.
- Cockpit no-scroll and left-panel/tile-grid geometry passed at responsive sizes from 880×576 through 1600×1000, including 1100×576, 1280×640, 1366×728 and other breakpoint cases. It checks that speed, km/h, gear frame, RPM scale, pedals and tyre grid do not overlap or clip. Telemetry primary columns and lap ledger do not overlap; the ledger fits the initial viewport at 1280×800 and 1600×1000 and remains reachable by scrolling on shorter screens.
- The real Qt gallery was refreshed at **2026-10-19 04:18:33 UTC** with 52 full-window images. Four full FL/FR/RL/RR tile-grid item images were captured with `QQuickItem.grabToImage` at **04:18:56 UTC**. Additional full-window captures show Telemetry's scrolled wheel/fuel section and the scrolled Engineer DirectInput binding at 880×576; the latter frame includes both the saved binding and the “Gán lại nút” action with visible margins.
- The synthetic gallery fixture shows Monza / Ferrari 296 GT3, 224 km/h, gear 5, distinct tyre and brake readings, fuel and lap values, `Lexus RC F GT3 · #05` ahead, `Porsche 911 GT3 R · #09` behind, and `Mock DirectInput Controller — Button 3`. It is fake-facade data, not live Assetto Corsa telemetry or a native wheel mapping.
- Dashboard and common header font metrics remain at their dashboard values. Non-dashboard page and Telemetry text use the rounded +15% contract. Wheel tiles keep the same `theme.canvas` background as the dashboard and show `LỐP`, pressure in psi and `PHANH` in °C. Checks cover corner indices 0–3, tile equality/gaps/containment, accessible names, dynamic updates and zero-versus-missing values.

The integrated suite ran before capture-only updates to `tests/ui/capture_review.py`, which add synthetic names/binding data to the gallery and refine the scrolled Engineer frame. The 04:12 suite report preserves the exact hashes used during that run. Its `run_ui.py` and all QML hashes match current files; its historical capture-helper hash differs from the current one. The latest gallery report contains the final capture-helper hash. No functional suite code or production QML changed after the integrated run.

## Evidence and downloads

- [Integrated UI report](render-report.json)
- [Current baseline surface audit](baseline-surface-audit.json)
- [Baseline-to-current matrix](baseline-parity.md)
- [52-image gallery index](index.html)
- [Gallery source report and hashes](review-gallery/gallery-report.json)
- [Full-window Telemetry scrolled supplements](review-gallery/telemetry-supplements-scrolled-880x576.png)
- [Full-window Engineer DirectInput row](review-gallery/engineer-directinput-binding-scrolled-880x576.png)
- [Full tyre/brake grid images and index](tyre-tile-closeups/index.html)
- [Standalone gallery with embedded PNGs](gallery-standalone.html)
- [Current downloadable ZIP (52 full-window + 4 full-grid PNGs)](RaceEngineer-UI-preview-fix-2026-10-19.zip)
- [ZIP SHA-256](RaceEngineer-UI-preview-fix-2026-10-19.zip.sha256)
- [Bundle manifest](bundle-manifest.json)

The images are original Qt output. The ZIP includes the 52 current full-window images, four complete tyre-grid item captures, the current gallery/report and the baseline audit. Older gallery folders and older ZIPs remain as historical artifacts; the current index and ZIP do not include the obsolete car illustration comparisons.

## Limits

The fake facade checks QML loading, rendering, bindings, geometry, UI state transitions and QML-to-facade calls. Native Windows builds and qrc packaging, live simulator data, tray/startup behavior, physical keyboard routing, DirectInput devices, real audio devices, local model execution, benchmark measurements, provider requests and native file dialogs are not verified. WAV/provider paths use fake selections. No performance percentages or DPI calibration claims are made.
