#!/usr/bin/env python3
"""Capture a real Qt software-rendered gallery independently of the full tests.

This lets reviewers inspect every workspace and modal even while a behavioral
assertion in run_ui.py is being repaired.
"""

from __future__ import annotations

import json
import os
import sys
import time
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QT_QUICK_BACKEND", "software")

from PySide6.QtCore import QCoreApplication, qInstallMessageHandler  # noqa: E402
from PySide6.QtGui import QGuiApplication, QImage  # noqa: E402

import run_ui as ui  # noqa: E402


ROOT = ui.ROOT
OUT = ui.VALIDATION_DIR / "review-gallery"
TYRE_TILE_OUT = ui.VALIDATION_DIR / "tyre-tile-closeups"


def _fixture(fake: object) -> None:
    fake.values.update({
        "connected": True,
        "simulatorName": "Assetto Corsa",
        "connectionText": "Đã kết nối Assetto Corsa",
        "telemetry": {
            "track": "Monza",
            "carModel": "Ferrari 296 GT3",
            "speedKmh": 224.0,
            "gear": 5,
            "rpm": 7825.0,
            "throttle": 0.82,
            "brake": 0.12,
            "position": 3,
            "currentLap": 12,
            "currentLapTimeSeconds": 57.84,
            "lapProgress": 0.61,
            "opponentAhead": "Lexus RC F GT3 · #05",
            "opponentBehind": "Porsche 911 GT3 R · #09",
            "fuelLiters": 32.4,
            "fuelPerLapLiters": 2.61,
            "estimatedFuelLaps": 12.4,
            "gapAheadSeconds": 0.682,
            "gapBehindSeconds": 1.264,
            "previousLapTimeSeconds": 95.482,
            "bestLapTimeSeconds": 94.911,
            "tyreTemperatures": [82.0, 83.5, 84.2, 85.1],
            "tyrePressures": [26.9, 27.0, 27.1, 27.2],
            "brakeTemperatures": [512.0, 526.0, 448.0, 453.0],
        },
        "apiConfigured": True,
        "apiState": "Connected",
        "apiBaseUrl": "http://localhost:8080/v1",
        "apiKey": "mock-local-key-present",
        "apiModel": "race-engineer-model",
        "ttsBackend": "Google Gemini API",
        "googleTtsVoices": ["Puck", "Zephyr"],
        "availableTtsVoices": [{"id": "default", "name": "VieNeu default"}],
        "selectedTtsVoice": "default",
        "aiComputeDevices": [{"id": "cpu", "label": "CPU"}, {"id": "gpu", "label": "GPU"}],
        "selectedAiComputeDevice": "gpu",
        "aiComputeStatus": "GPU",
        "gpuRendererEnabled": True,
        "gpuRendererRestartRequired": False,
        "audioInputDevices": [{"id": "mic", "name": "Microphone mock"}],
        "audioOutputDevices": ["Speakers mock"],
        "selectedAudioInputId": "mic",
        "selectedAudioOutput": "Speakers mock",
        "microphoneName": "Microphone mock",
        "voiceStatus": "Idle",
        "latestEvent": "Đối thủ phía trước đang ở trong khoảng cách 0,682 giây.",
        "directInputBinding": "Mock DirectInput Controller — Button 3",
        "isAudioDucked": False,
        "startupReady": True,
        "startupError": "",
        "setupVisible": False,
        "ttsBenchmarkAllowed": True,
        "ttsBenchmarkRunning": False,
        "ttsBenchmarkRestoring": False,
        "ttsBenchmarkStale": False,
        "ttsBenchmarkStatus": "Đã áp dụng kết quả benchmark mẫu.",
        "ttsBenchmarkTimestamp": "18/10/2026 09:30",
        "ttsBenchmarkProgress": 1.0,
        "ttsBenchmarkResults": [
            {"threads": 2, "p95_ms": 865, "cpu_ms": 812,
             "peak_working_set": 402653184, "peak_private_bytes": 201326592},
            {"threads": 3, "p95_ms": 734, "cpu_ms": 691,
             "peak_working_set": 419430400, "peak_private_bytes": 209715200},
            {"threads": 4, "p95_ms": 710, "cpu_ms": 672,
             "peak_working_set": 452984832, "peak_private_bytes": 230686720},
        ],
        "ttsCpuThreads": 3,
        "ttsExternalThreadOverride": False,
        "selectedTtsVoice": "vieneu-default",
        "availableTtsVoices": [{"id": "vieneu-default", "name": "VieNeu mặc định"},
                                {"id": "vieneu-vietnamese", "name": "Tiếng Việt tự nhiên"}],
        "googleTtsApiKeyConfigured": True,
        "googleTtsModel": "gemini-3.8-flash-lite-tts",
        "googleTtsVoice": "Puck",
    })
    fake.changed.emit()


def _capture_route(engine: object, root: object, page: int, label: str,
                   size: tuple[int, int], records: list[dict[str, object]],
                   closeups: list[dict[str, object]]) -> None:
    ui._route_to(engine, root, page, direct=True)
    root.resize(*size)
    ui._pump(100)
    metrics = ui._window_metrics(root, size)
    actual_width, actual_height = metrics["actual_size"]
    loader = ui._find(root, ui.PAGE_NAMES[page])
    page_item = loader.property("item")
    layout = ui._assert_page_geometry(root, loader, page_item, page, actual_width, actual_height)
    tile_grid = None
    tile_geometry = None
    if page == 0:
        cockpit_data = ui._find(root, "cockpitDrivingData")
        tile_grid = ui._find_scoped(cockpit_data, "tyreTileGrid")
        tile_geometry = layout["cockpit_no_scroll"]["tyre_tile_geometry"]
    elif page == 3:
        telemetry_data = ui._find_scoped(page_item, "telemetryDrivingData")
        tile_grid = ui._find_scoped(telemetry_data, "tyreTileGrid")
        tile_geometry = ui._assert_tyre_tile_geometry(telemetry_data, actual_width, actual_height)
        layout["tyre_tile_geometry"] = tile_geometry
        layout["telemetry_primary_geometry"] = ui._assert_telemetry_primary_geometry(
            root, page_item, actual_width, actual_height)
    header = ui._assert_header_layout(root, actual_width, actual_height)
    screenshot_path = OUT / f"{label}-{actual_width}x{actual_height}.png"
    record = ui._capture(root, screenshot_path)
    record.update({"route": page, "logical_size": list(size), "actual_size": [actual_width, actual_height],
                   "window_metrics": metrics, "layout": layout, "header": header})
    records.append(record)
    is_closeup_size = label == "cockpit" and (actual_width, actual_height) in ui.SIZES
    is_telemetry_review_size = (label == "telemetry" and (actual_width, actual_height) == (1280, 800)
                                and ui._rects_inside(ui._scene_rect(tile_grid),
                                                     (0.0, 0.0, float(actual_width), float(actual_height))))
    if tile_grid is not None and (is_closeup_size or is_telemetry_review_size):
        tile_rects = [tile["tile_bounds"] for tile in tile_geometry["tiles"].values()]
        grid_rect = (min(rect[0] for rect in tile_rects), min(rect[1] for rect in tile_rects),
                     max(rect[0] + rect[2] for rect in tile_rects) - min(rect[0] for rect in tile_rects),
                     max(rect[1] + rect[3] for rect in tile_rects) - min(rect[1] for rect in tile_rects))
        margin = 18
        left = max(0, round(grid_rect[0]) - margin)
        top = max(0, round(grid_rect[1]) - margin)
        right = min(actual_width, round(grid_rect[0] + grid_rect[2]) + margin)
        bottom = min(actual_height, round(grid_rect[1] + grid_rect[3]) + margin)
        source = QImage(str(screenshot_path))
        if source.isNull():
            raise AssertionError(f"Could not load rendered screenshot for tyre tile closeup: {screenshot_path}")
        crop = source.copy(left, top, right - left, bottom - top)
        closeup_label = "cockpit" if label == "cockpit" else "telemetry"
        closeup_path = TYRE_TILE_OUT / f"{closeup_label}-{actual_width}x{actual_height}.png"
        TYRE_TILE_OUT.mkdir(parents=True, exist_ok=True)
        if crop.isNull() or not crop.save(str(closeup_path), "PNG"):
            raise AssertionError(f"Could not create tile closeup: {closeup_path}")
        closeups.append({"path": str(closeup_path.relative_to(ui.ROOT)),
                         "source_screenshot": str(screenshot_path.relative_to(ui.ROOT)),
                         "route": page, "actual_size": [actual_width, actual_height],
                         "crop_box": [left, top, right - left, bottom - top],
                         "grid_bounds": list(map(round, grid_rect)),
                         "tile_geometry": tile_geometry})


def run() -> dict[str, object]:
    OUT.mkdir(parents=True, exist_ok=True)
    app = QGuiApplication.instance() or QGuiApplication(["raceengineer-review-gallery"])
    # Mirrors the native QGuiApplication::setApplicationVersion value from
    # the current CMake project version, so the version label is not blank.
    app.setApplicationVersion("1.0.5")
    qInstallMessageHandler(ui._qt_message_handler)
    ui.QT_LOGS.clear()
    compile_report = ui._compile_source_components()
    if not compile_report["all_ready"]:
        raise AssertionError("QML source compilation failed: " + "\n".join(compile_report["errors"]))
    engine, root, fake, warnings = ui._qml_engine(ui.NEW_QML)
    _fixture(fake)
    records: list[dict[str, object]] = []
    closeups: list[dict[str, object]] = []
    for page, label in ((0, "cockpit"), (1, "fuel-strategy"), (2, "engineer-spotter"),
                        (3, "telemetry"), (4, "ai-voice"), (5, "system-devices")):
        for size in ui.SIZES:
            _capture_route(engine, root, page, label, size, records, closeups)
        if page == 0:
            for size in ((1366, 728), (1280, 720)):
                _capture_route(engine, root, page, label, size, records, closeups)

    # Capture Telemetry's lower wheel/fuel supplements in the compact viewport.
    # Full wheel-tile geometry remains available in the separate item capture.
    ui._route_to(engine, root, 3, direct=True)
    root.resize(880, 576)
    ui._pump(100)
    telemetry_page = ui._find(root, "telemetryPage")
    telemetry_supplement = ui._find_scoped(telemetry_page, "telemetryEstimatedLapsValue")
    ui._scroll_item_into_view(root, telemetry_supplement)
    ui._pump(100)
    telemetry_page_bounds = ui._scene_rect(telemetry_page)
    supplement_bounds = ui._scene_rect(telemetry_supplement)
    ui._assert_inside(supplement_bounds, telemetry_page_bounds, "Telemetry fuel supplement at 880x576")
    telemetry_wheels = ui._find_scoped(telemetry_page, "telemetryDrivingData")
    telemetry_flickable = next(ancestor for ancestor in ui.iter_ancestors(telemetry_supplement)
                               if "Flickable" in ancestor.metaObject().className())
    supplement_record = ui._capture(root, OUT / "telemetry-supplements-scrolled-880x576.png")
    supplement_record.update({
        "route": 3, "logical_size": [880, 576], "actual_size": [round(root.width()), round(root.height())],
        "scrolled_supplement": "Telemetry fuel and wheel readings",
        "viewport_bounds": list(map(round, telemetry_page_bounds)),
        "wheel_grid_bounds": list(map(round, ui._scene_rect(ui._find_scoped(telemetry_wheels, "tyreTileGrid")))),
        "fuel_estimated_laps_bounds": list(map(round, supplement_bounds)),
        "scroll_content_y": round(float(telemetry_flickable.property("contentY")), 2),
    })
    records.append(supplement_record)

    # Capture the restored Engineer/Spotter binding value after bringing its
    # real row into view, without opening a native DirectInput mapping flow.
    ui._route_to(engine, root, 2, direct=True)
    root.resize(880, 576)
    ui._pump(100)
    engineer_page = ui._find(root, "engineerSpotterPage")
    binding_value = ui._find_scoped(engineer_page, "engineerDirectInputBinding")
    mapping_button = ui._find_scoped(engineer_page, "engineerMapDirectInputButton")
    # Align the capture on the action below the binding row so both the saved
    # mapping and the real remap affordance are visible together with padding.
    ui._scroll_item_into_view(root, mapping_button)
    # Leave room below the final action too. The generic helper aligns a
    # bottom-most control to a 2px edge tolerance, so use remaining scroll range
    # to move the visible row/button upward for a readable gallery frame.
    engineer_flickable = next(ancestor for ancestor in ui.iter_ancestors(binding_value)
                              if "Flickable" in ancestor.metaObject().className())
    current_content_y = float(engineer_flickable.property("contentY"))
    scroll_max = float(engineer_flickable.property("contentHeight")) - float(engineer_flickable.height())
    engineer_flickable.setProperty("contentY", min(scroll_max, current_content_y + 100.0))
    ui._pump(80)
    binding_bounds = ui._scene_rect(binding_value)
    mapping_button_bounds = ui._scene_rect(mapping_button)
    engineer_page_bounds = ui._scene_rect(engineer_page)
    ui._assert_inside(binding_bounds, engineer_page_bounds, "Engineer DirectInput binding at 880x576")
    ui._assert_inside(mapping_button_bounds, engineer_page_bounds, "Engineer remap button at 880x576")
    engineer_viewport = ui._scene_rect(engineer_flickable)
    ui._assert_inside(binding_bounds, engineer_viewport, "Engineer DirectInput binding viewport")
    ui._assert_inside(mapping_button_bounds, engineer_viewport, "Engineer remap button viewport")
    if min(binding_bounds[1] - engineer_viewport[1],
           engineer_viewport[1] + engineer_viewport[3] - binding_bounds[1] - binding_bounds[3],
           mapping_button_bounds[1] - engineer_viewport[1],
           engineer_viewport[1] + engineer_viewport[3] - mapping_button_bounds[1] - mapping_button_bounds[3]) < 8:
        raise AssertionError(
            "Engineer binding and remap control need visible viewport padding: "
            f"binding={binding_bounds}, button={mapping_button_bounds}, viewport={engineer_viewport}, "
            f"contentY={engineer_flickable.property('contentY')}, "
            f"contentHeight={engineer_flickable.property('contentHeight')}"
        )
    if str(binding_value.property("text")) != str(fake.values["directInputBinding"]):
        raise AssertionError("Engineer DirectInput binding gallery value does not match the fake facade")
    binding_record = ui._capture(root, OUT / "engineer-directinput-binding-scrolled-880x576.png")
    binding_record.update({
        "route": 2, "logical_size": [880, 576], "actual_size": [round(root.width()), round(root.height())],
        "scrolled_control": "engineerDirectInputBinding",
        "binding_text": str(binding_value.property("text")),
        "binding_bounds": list(map(round, binding_bounds)),
        "remap_button_bounds": list(map(round, mapping_button_bounds)),
        "viewport_bounds": list(map(round, engineer_viewport)),
        "page_bounds": list(map(round, engineer_page_bounds)),
    })
    records.append(binding_record)

    ui._route_to(engine, root, 4, direct=True)
    ai_page = ui._find(root, "aiVoicePage")
    for section, label, backend in ((0, "ai-connection", "Google Gemini API"),
                                    (1, "ai-local-voice", "VieNeu-TTS"),
                                    (2, "ai-google-tts", "Google Gemini API")):
        ai_page.setProperty("sectionIndex", section)
        fake.values.update({"ttsBackend": backend, "connected": section != 1,
                            "voiceStatus": "Idle", "ttsBenchmarkRunning": False,
                            "ttsBenchmarkRestoring": False, "ttsBenchmarkAllowed": True})
        fake.changed.emit()
        ui._pump(100)
        for size in ui.SIZES:
            root.resize(*size)
            ui._pump(100)
            page_bounds = ui._scene_rect(ai_page)
            record = ui._capture(root, OUT / f"{label}-{size[0]}x{size[1]}.png")
            record.update({"route": 4, "section": section, "logical_size": list(size),
                           "tts_backend": backend, "page_bounds": list(map(round, page_bounds))})
            records.append(record)

    ui._route_to(engine, root, 1, direct=True)
    strategy_page = ui._find(root, "fuelStrategyPage")
    strategy_page.setProperty("sectionIndex", 1)
    ui._pump(100)
    records.append(ui._capture(root, OUT / "fuel-strategy-data-training-1280x800.png"))

    fake.values.update({"setupVisible": True, "setupStep": 0, "ttsBackend": "VieNeu-TTS",
                        "connected": False, "voiceStatus": "Idle", "ttsBenchmarkAllowed": True})
    fake.changed.emit()
    wizard = ui._find(root, "setupWizard")
    panel = ui._find_scoped(wizard, "setupPanel")
    setup_continue = ui._find_scoped(wizard, "setupContinueButton")
    for size in ui.SIZES:
        root.resize(*size)
        for step in range(4):
            fake.values["setupStep"] = step
            fake.values["ttsBackend"] = "VieNeu-TTS"
            fake.changed.emit()
            ui._pump(100)
            panel_bounds = ui._scene_rect(panel)
            continue_bounds = ui._scene_rect(setup_continue)
            geometry = {"panel": list(map(round, panel_bounds)),
                        "continue": list(map(round, continue_bounds)),
                        "continue_in_panel": (
                            continue_bounds[0] >= panel_bounds[0] - 2
                            and continue_bounds[1] >= panel_bounds[1] - 2
                            and continue_bounds[0] + continue_bounds[2] <= panel_bounds[0] + panel_bounds[2] + 2
                            and continue_bounds[1] + continue_bounds[3] <= panel_bounds[1] + panel_bounds[3] + 2)}
            if step == 2:
                token_slider = ui._find_scoped(wizard, "apiMaximumTokensSlider")
                geometry["llm_token_slider"] = list(map(round, ui._scene_rect(token_slider)))
                geometry["llm_token_slider_has_height"] = token_slider.height() > 1
            record = ui._capture(root, OUT / f"setup-step-{step}-{size[0]}x{size[1]}.png")
            record.update({"route": "setup", "step": step, "logical_size": list(size),
                           "geometry": geometry})
            records.append(record)

    # Exercise the one shared host from the wizard's Voice step, then capture
    # both prompt and WAV surfaces without opening a native file picker.
    root.resize(1280, 800)
    fake.values.update({"setupStep": 1, "ttsBackend": "Google Gemini API",
                        "connected": False, "voiceStatus": "Idle"})
    fake.changed.emit()
    ui._pump(80)
    wizard_add = ui._find_scoped(wizard, "addGoogleVoiceButton")
    ui._click(root, wizard_add)
    records.append(ui._capture(root, OUT / "google-voice-prompt-from-setup-1280x800.png"))
    ui._pump(250)
    dialog = ui._find(root, "googleVoiceDialog")
    mode = dialog.findChild(ui.QObject, "googleVoiceModeTabs")
    if mode is None:
        mode = ui._find(root, "googleVoiceModeTabs")
    mode.setProperty("currentIndex", 1)
    ui._pump(100)
    records.append(ui._capture(root, OUT / "google-voice-wav-from-setup-1280x800.png"))
    dialog.close()

    fake.values.update({"setupVisible": False, "setupStep": 0,
                        "startupReady": False, "startupProgress": 0.42,
                        "startupError": "AI model initialization failed (mock state)"})
    fake.changed.emit()
    ui._pump(100)
    records.append(ui._capture(root, OUT / "startup-error-1280x800.png"))

    ui._route_to(engine, root, 0, direct=True)
    fake.values.update({"connected": True, "simulatorName": "Assetto Corsa",
                        "connectionText": "Đã kết nối Assetto Corsa",
                        "voiceStatus": "Recognizing", "apiConfigured": True,
                        "apiBaseUrl": "http://localhost:8080/v1", "apiKey": "mock-key",
                        "apiModel": "race-engineer-model", "telemetry": {
                            "track": "Monza", "carModel": "Ferrari 296 GT3", "speedKmh": 224.0,
                            "gear": 5, "rpm": 7825.0, "throttle": 0.82, "brake": 0.12,
                            "fuelLiters": 32.4, "previousLapTimeSeconds": 95.482,
                            "bestLapTimeSeconds": 94.911, "tyreTemperatures": [82, 83, 84, 85],
                            "tyrePressures": [26.9, 27.0, 27.1, 27.2],
                            "brakeTemperatures": [512, 526, 448, 453]},
                        "setupVisible": False})
    for api_state, label in (("LLM Server Offline", "offline"),
                             ("AuthenticationError", "authentication-error")):
        fake.values["apiState"] = api_state
        fake.changed.emit()
        for width in (1120, 1280):
            root.resize(width, 800)
            ui._pump(100)
            header = ui._assert_header_layout(root, width, 800)
            record = ui._capture(root, OUT / f"header-stress-{label}-{width}x800.png")
            record.update({"route": 0, "api_state": api_state, "logical_size": [width, 800],
                           "header": header, "voice_state": "Recognizing", "connected": True})
            records.append(record)

    fake.values.update({"startupReady": True, "startupError": "",
                        "conversationLog": [
                            {"role": "driver" if index % 2 == 0 else "engineer",
                             "text": f"Review history item {index}: Monza long-run tyre and fuel notes."}
                            for index in range(14)
                        ]})
    fake.changed.emit()
    ui._route_to(engine, root, 0, direct=True)
    ui._pump(80)
    ui._click(root, ui._find(root, "radioHistoryButton"))
    ui._pump(250)
    records.append(ui._capture(root, OUT / "radio-history-1280x800.png"))

    report = {
        "created_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "renderer": "PySide6 Qt Quick software backend with QGuiApplication offscreen",
        "fake_fixture": "Synthetic Monza / Ferrari 296 GT3 / 224 km/h / gear 5 / distinct tyre and brake readings / fuel / opponent names and gaps / lap times / mock DirectInput binding",
        "native_live_data": False,
        "runtime": {"python": sys.version.split()[0], "pyside6": __import__("PySide6").__version__,
                    "qt": __import__("PySide6.QtCore", fromlist=["qVersion"]).qVersion()},
        "component_compile": compile_report,
        "source_sha256": ui._source_sha256(),
        "qml_warnings": warnings + ui.QT_LOGS,
        "screenshots": records,
        "tyre_tile_closeups": closeups,
    }
    report_path = OUT / "gallery-report.json"
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return report


if __name__ == "__main__":
    output = run()
    print(json.dumps({"screenshot_count": len(output["screenshots"]),
                      "tyre_tile_closeup_count": len(output["tyre_tile_closeups"]),
                      "report": str(OUT / "gallery-report.json"),
                      "screenshots": [item["path"] for item in output["screenshots"]],
                      "tyre_tile_closeups": [item["path"] for item in output["tyre_tile_closeups"]]},
                     ensure_ascii=False, indent=2))
