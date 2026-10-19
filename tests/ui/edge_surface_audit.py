#!/usr/bin/env python3
"""Focused UI edge audit for long text, numeric extremes, keyboard, and lifecycle.

This is deliberately separate from the completed integrated suite and gallery.
It writes only tests/ui/artifacts/edge_surface_audit.json and screenshots for
observed failures; it never changes production sources or existing validation
evidence.
"""

from __future__ import annotations

import hashlib
import json
import re
import sys
import time
import traceback
from pathlib import Path
from typing import Any, Callable

from PySide6.QtCore import QPoint, Qt, qInstallMessageHandler
from PySide6.QtGui import QAccessible, QGuiApplication
from PySide6.QtTest import QTest

import run_ui as ui
from capture_review import _fixture


ROOT = ui.ROOT
OUT = Path(__file__).resolve().parent / "artifacts"
SCREENSHOTS = OUT / "edge-failures"
SIZES = ((880, 576), (1100, 576), (1280, 576), (1280, 720), (1280, 800), (1600, 1000))
RESULTS: dict[str, dict[str, Any]] = {}
WARNINGS: list[str] = []


def _message_handler(mode: Any, context: Any, message: str) -> None:
    WARNINGS.append(str(message))


def _sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _source_lines(path: str, needles: list[str]) -> dict[str, dict[str, Any]]:
    lines = (ROOT / path).read_text(encoding="utf-8").splitlines()
    result: dict[str, dict[str, Any]] = {}
    for needle in needles:
        found = next(((index, line.strip()) for index, line in enumerate(lines, 1) if needle in line), None)
        if found:
            result[needle] = {"line": found[0], "text": found[1]}
    return result


def _nearest_flickable(item: Any) -> Any | None:
    return next((ancestor for ancestor in ui.iter_ancestors(item)
                 if "Flickable" in ancestor.metaObject().className()), None)


def _text(item: Any) -> str:
    return str(item.property("text") or "")


def _accessible_name(item: Any) -> str:
    interface = QAccessible.queryAccessibleInterface(item)
    return str(interface.text(QAccessible.Text.Name)) if interface else ""


def _accessibility_value(item: Any) -> dict[str, str]:
    return {"text": _text(item), "accessible_name": _accessible_name(item)}


def _rect(item: Any) -> list[float]:
    return list(ui._scene_rect(item))


def _assert_full_text_accessible(item: Any, expected: str, label: str) -> dict[str, Any]:
    actual_text = _text(item)
    accessible = _accessible_name(item)
    if actual_text != expected:
        raise AssertionError(f"{label} text changed/truncated: {len(actual_text)} chars, expected {len(expected)}")
    if accessible != expected:
        raise AssertionError(f"{label} accessible name is not the full value: {len(accessible)} chars, expected {len(expected)}")
    if not item.isVisible() or item.width() <= 1 or item.height() <= 1:
        raise AssertionError(f"{label} is not on a visible, non-empty QML item: {item.isVisible()} {_rect(item)}")
    return {"object_name": item.objectName(), "chars": len(expected),
            "bounds": list(map(round, _rect(item))), "text_full": True,
            "accessible_name_full": True,
            "implicit_width": round(float(item.property("implicitWidth") or 0), 2),
            "width": round(float(item.width()), 2),
            "implicit_height": round(float(item.property("implicitHeight") or 0), 2),
            "height": round(float(item.height()), 2)}


def _assert_full_visible_text(item: Any, expected: str, label: str) -> dict[str, Any]:
    actual = _text(item)
    if actual != expected:
        raise AssertionError(f"{label} QML text changed/truncated: {len(actual)} chars, expected {len(expected)}")
    if not item.isVisible() or item.width() <= 1 or item.height() <= 1:
        raise AssertionError(f"{label} is not visible/non-empty: {_rect(item)}")
    painted_height = float(item.property("paintedHeight") or 0)
    if painted_height > float(item.height()) + 2:
        raise AssertionError(f"{label} painted text exceeds its Text item: painted={painted_height} height={item.height()}")
    return {"object_name": item.objectName(), "chars": len(expected),
            "bounds": list(map(round, _rect(item))),
            "painted_height": round(painted_height, 2),
            "height": round(float(item.height()), 2),
            "accessible_name": _accessible_name(item),
            "accessible_name_present": bool(_accessible_name(item))}


def _edge_scrolls(window: Any, item: Any, label: str) -> dict[str, Any]:
    flickable = _nearest_flickable(item)
    if flickable is None or flickable.height() <= 1:
        raise AssertionError(f"{label} has no reachable ScrollView/Flickable viewport")
    view = ui._scene_rect(flickable)
    content_height = float(flickable.property("contentHeight"))
    height = float(flickable.height())
    origin = float(flickable.property("originY"))
    max_y = max(origin, content_height - height)
    before = float(flickable.property("contentY"))
    item_rect = ui._scene_rect(item)
    # Bring the first edge into view, then the last edge. A long wrapped Text
    # may itself be taller than the viewport, so require each edge to be
    # reachable instead of requiring the whole long item to fit at once.
    top_target = max(origin, min(max_y, before + item_rect[1] - view[1]))
    flickable.setProperty("contentY", top_target)
    ui._pump(30)
    top_rect = ui._scene_rect(item)
    top_edge = (top_rect[0], top_rect[1], top_rect[2], min(8.0, top_rect[3]))
    if not ui._rects_inside(top_edge, ui._scene_rect(flickable), tolerance=2.0):
        raise AssertionError(f"{label} top line is clipped/unreachable: item={top_rect} view={ui._scene_rect(flickable)}")

    item_rect = ui._scene_rect(item)
    view = ui._scene_rect(flickable)
    current = float(flickable.property("contentY"))
    bottom_target = max(origin, min(max_y, current + item_rect[1] + item_rect[3] - view[1] - view[3]))
    flickable.setProperty("contentY", bottom_target)
    ui._pump(30)
    bottom_rect = ui._scene_rect(item)
    bottom_edge = (bottom_rect[0], bottom_rect[1] + max(0.0, bottom_rect[3] - 8.0),
                   bottom_rect[2], min(8.0, bottom_rect[3]))
    if not ui._rects_inside(bottom_edge, ui._scene_rect(flickable), tolerance=2.0):
        raise AssertionError(f"{label} final line is clipped/unreachable: item={bottom_rect} view={ui._scene_rect(flickable)} maxY={max_y}")
    return {"viewport_bounds": list(map(round, ui._scene_rect(flickable))),
            "item_height": round(float(item.height()), 2),
            "content_height": round(content_height, 2),
            "top_line_bounds": list(map(round, top_edge)),
            "bottom_line_bounds": list(map(round, bottom_edge)),
            "top_content_y": round(top_target, 2), "bottom_content_y": round(bottom_target, 2),
            "max_content_y": round(max_y, 2), "top_reachable": True, "bottom_reachable": True}


def _set_values(fake: Any, values: dict[str, Any]) -> None:
    fake.values.update(values)
    fake.changed.emit()
    ui._pump(60)


def _capture_failure(name: str, window: Any) -> str | None:
    try:
        SCREENSHOTS.mkdir(parents=True, exist_ok=True)
        path = SCREENSHOTS / (re.sub(r"[^a-zA-Z0-9_-]+", "-", name).strip("-") + ".png")
        ui._capture(window, path)
        return str(path.relative_to(ROOT))
    except Exception as exc:  # capture is secondary evidence, not the assertion
        return f"capture failed: {exc}"


def _run_case(name: str, window: Any, body: Callable[[], dict[str, Any]]) -> None:
    try:
        value = body()
        RESULTS[name] = {"status": "PASS", "evidence": value}
    except Exception as exc:
        RESULTS[name] = {"status": "FAIL", "error": f"{type(exc).__name__}: {exc}",
                         "traceback": traceback.format_exc().splitlines()[-8:],
                         "screenshot": _capture_failure(name, window)}


def _long_payloads() -> dict[str, Any]:
    track = "AUTODROMO-DEL-MUGELLO-24-HOURS-OF-ENDURANCE-" + "NORTH-STRAIGHT-" * 5
    car = "Ferrari 296 GT3 Evo Endurance Specification — Team Corse Clienti / #296 — " + "Aero Package Revision 2026 " * 4
    ahead = "Lexus RC F GT3 · #05 · Endurance team with a deliberately long display name — " + "Driver One / Driver Two " * 5
    behind = "Porsche 911 GT3 R · #09 · Long running entry — " + "Driver Three / Driver Four " * 5
    event = ("Race control reports a full course caution in sector three; the leading car is maintaining pace and "
             "the engineer recommends staying on the current tyre strategy. ") * 5
    direct = "Mock Fanatec ClubSport Formula V2.5 wheel — Steering input / Button 3 — " + "Long device description — " * 7
    return {"track": track, "carModel": car, "opponentAhead": ahead, "opponentBehind": behind,
            "latestEvent": event, "directInputBinding": direct}


def _extreme_telemetry(payload: dict[str, Any]) -> dict[str, Any]:
    return {
        "track": payload["track"], "carModel": payload["carModel"],
        "speedKmh": 500.0, "gear": 8, "rpm": 19999.0, "throttle": 1.0, "brake": 1.0,
        "position": 99, "currentLap": 999, "currentLapTimeSeconds": 999.999,
        "lapProgress": 1.0, "opponentAhead": payload["opponentAhead"],
        "opponentBehind": payload["opponentBehind"], "fuelLiters": 999.9,
        "fuelPerLapLiters": 99.9, "estimatedFuelLaps": 99.9,
        "gapAheadSeconds": 123.456, "gapBehindSeconds": 98.765,
        "previousLapTimeSeconds": 999.999, "bestLapTimeSeconds": 888.888,
        "tyreTemperatures": [120.0, 119.0, 118.0, 117.0],
        "tyrePressures": [45.0, 44.0, 43.0, 42.0],
        "brakeTemperatures": [1100.0, 1090.0, 1080.0, 1070.0],
    }


def _test_long_telemetry(engine: Any, root: Any, fake: Any,
                         requested: tuple[int, int]) -> dict[str, Any]:
    payload = _long_payloads()
    _set_values(fake, {"connected": True, "simulatorName": "Assetto Corsa", "connectionText": "AC / ACC connected",
                       "telemetry": _extreme_telemetry(payload), "latestEvent": payload["latestEvent"],
                       "startupReady": True, "startupError": ""})
    root.resize(*requested)
    ui._pump(60)
    size = (round(root.width()), round(root.height()))
    ui._route_to(engine, root, 0, direct=True)
    cockpit = ui._find(root, "cockpit")
    data = ui._find_scoped(cockpit, "cockpitDrivingData")
    track = ui._find_scoped(cockpit, "trackValue")
    car = ui._find_scoped(cockpit, "carValue")
    expected_track = payload["track"].upper()
    text_records = {"track": _assert_full_text_accessible(track, expected_track, f"Cockpit track {size}"),
                    "car": _assert_full_text_accessible(car, payload["carModel"], f"Cockpit car {size}")}
    viewport = ui._find_scoped(cockpit, "cockpitDataViewport")
    if viewport.width() <= 1 or viewport.height() <= 1:
        raise AssertionError(f"Cockpit telemetry viewport collapsed at {size}: {_rect(viewport)}")
    speed = ui._find_scoped(data, "speedValue")
    gear = ui._find_scoped(data, "gearValue")
    if not speed.isVisible() or not gear.isVisible():
        raise AssertionError(f"Cockpit numeric values hidden at {size}: speed={speed.isVisible()} gear={gear.isVisible()}")
    text_records["speed"] = {"text": _text(speed), "bounds": list(map(round, _rect(speed))),
                              "content_width": round(float(speed.property("contentWidth") or 0), 2),
                              "width": round(float(speed.width()), 2)}
    if _text(speed) != "500" or float(speed.property("contentWidth") or 0) > speed.width() + 3:
        raise AssertionError(f"Cockpit large speed text is wrong/overflows its item at {size}: {text_records['speed']}")
    if ui._rects_overlap(_rect(track), _rect(car), gap=0):
        raise AssertionError(f"Long cockpit track and car masthead items overlap at {size}: {_rect(track)}, {_rect(car)}")

    ui._route_to(engine, root, 3, direct=True)
    page = ui._find(root, "telemetryPage")
    names = [ui._find_scoped(page, name) for name in ("opponentAheadName", "opponentBehindName")]
    event = ui._find_scoped(page, "telemetryLatestEvent")
    text_checks = []
    for name_item, expected_name, label in (
        (names[0], payload["opponentAhead"], "Telemetry ahead name"),
        (names[1], payload["opponentBehind"], "Telemetry behind name"),
    ):
        if _text(name_item) != expected_name or expected_name not in _accessible_name(name_item):
            raise AssertionError(f"{label} lost text/accessibility at {size}: {_accessibility_value(name_item)}")
        text_checks.append({"object_name": name_item.objectName(), "text_chars": len(expected_name),
                            "accessible_name": _accessible_name(name_item), "bounds": list(map(round, _rect(name_item)))})
    text_checks.append(_assert_full_visible_text(event, payload["latestEvent"], f"Telemetry latest event {size}"))
    page_flick = _nearest_flickable(names[0])
    if page_flick is None:
        raise AssertionError(f"Telemetry route has no vertical scroll viewport at {size}")
    if float(page_flick.property("contentWidth")) > float(page_flick.width()) + 2:
        raise AssertionError(f"Telemetry has horizontal content overflow at {size}: content={page_flick.property('contentWidth')} width={page_flick.width()}")
    scroll_checks = {}
    for label, item in (("ahead", names[0]), ("behind", names[1]), ("latest_event", event)):
        scroll_checks[label] = _edge_scrolls(root, item, f"Telemetry {label} {size}")
    if float(event.property("implicitHeight") or 0) > event.height() + 2:
        raise AssertionError(f"Telemetry event text is clipped inside its Text item at {size}: implicit={event.property('implicitHeight')} height={event.height()}")
    numeric = {}
    expected = {"telemetrySpeedValue": "500", "telemetryGearValue": "8",
                "telemetryPositionValue": "P99", "telemetryCurrentLapValue": "999"}
    for name in ("telemetrySpeedValue", "telemetryGearValue", "telemetryPositionValue", "telemetryCurrentLapValue",
                 "opponentAheadGap", "opponentBehindGap", "telemetryCurrentLapTime", "telemetryPreviousLapTime",
                 "telemetryBestLapTime"):
        item = ui._find_scoped(page, name)
        if not item.isVisible() or item.width() <= 1 or item.height() <= 1:
            raise AssertionError(f"Telemetry numeric field {name} is not visible/reachable at {size}: {_rect(item)}")
        numeric[name] = {"text": _text(item), "bounds": list(map(round, _rect(item))),
                         "content_width": round(float(item.property("contentWidth") or 0), 2),
                         "width": round(float(item.width()), 2)}
        if name in expected and _text(item) != expected[name]:
            raise AssertionError(f"Telemetry large numeric value {name} rendered {_text(item)!r}, expected {expected[name]!r}")
        content_width = float(item.property("contentWidth") or 0)
        if content_width > item.width() + 4:
            raise AssertionError(f"Telemetry numeric text {name} extends beyond its QML item at {size}: {numeric[name]}")
    wheel_data = ui._find_scoped(page, "telemetryDrivingData")
    wheel_checks = {}
    for corner, temperature, pressure, brake in zip(
            ("FL", "FR", "RL", "RR"), (120, 119, 118, 117), (45.0, 44.0, 43.0, 42.0), (1100, 1090, 1080, 1070)):
        values = {
            "tyre": ui._find_scoped(wheel_data, "wheelTemperature" + corner),
            "pressure": ui._find_scoped(wheel_data, "wheelPressure" + corner),
            "brake": ui._find_scoped(wheel_data, "wheelBrakeTemperature" + corner),
        }
        expected_values = {"tyre": f"{temperature}°C", "pressure": f"{pressure:.1f}".replace(".", ",") + " psi",
                           "brake": f"{brake}°C"}
        for key, item in values.items():
            if _text(item) != expected_values[key]:
                raise AssertionError(f"Telemetry {corner} {key} index/value mismatch at {size}: {_text(item)!r} != {expected_values[key]!r}")
            if not item.isVisible() or item.width() <= 1 or item.height() <= 1:
                raise AssertionError(f"Telemetry {corner} {key} item is collapsed at {size}: {_rect(item)}")
        wheel_checks[corner] = {key: {"text": _text(item), "bounds": list(map(round, _rect(item)))}
                                for key, item in values.items()}
    return {"requested_size": list(requested), "actual_size": list(size),
            "cockpit_text_and_numeric": text_records,
            "cockpit_masthead_nonoverlap": True,
            "telemetry": {"long_text": text_checks, "scroll_edges": scroll_checks,
                          "numeric_values": numeric,
                          "wheel_tile_values": wheel_checks,
                          "horizontal_content_width": round(float(page_flick.property("contentWidth")), 2),
                          "horizontal_view_width": round(float(page_flick.width()), 2)}}


def _test_engineer_readonly_row(engine: Any, root: Any, fake: Any,
                                requested: tuple[int, int]) -> dict[str, Any]:
    payload = _long_payloads()
    _set_values(fake, {"directInputBinding": payload["directInputBinding"], "startupReady": True,
                       "ttsBenchmarkRunning": False, "ttsBenchmarkRestoring": False})
    root.resize(*requested)
    ui._pump(40)
    ui._route_to(engine, root, 2, direct=True)
    page = ui._find(root, "engineerSpotterPage")
    value = ui._find_scoped(page, "engineerDirectInputBinding")
    mapping = ui._find_scoped(page, "engineerMapDirectInputButton")
    ui._scroll_item_into_view(root, value)
    ui._pump(20)
    if _text(value) != payload["directInputBinding"]:
        raise AssertionError(f"Engineer DirectInput ReadonlyRow lost full text/accessibility at {requested}: {_accessibility_value(value)}")
    flick = _nearest_flickable(value)
    if flick is None:
        raise AssertionError("Engineer page no longer has a scrollable container")
    value_rect = ui._scene_rect(value)
    if value.width() <= 1 or value.height() <= 1 or value_rect[2] <= 1:
        raise AssertionError(f"Engineer DirectInput ReadonlyRow collapsed at {requested}: {value_rect}")
    visible_text = _assert_full_visible_text(value, payload["directInputBinding"], f"Engineer DirectInput row {requested}")
    scroll = _edge_scrolls(root, value, f"Engineer DirectInput row {requested}")
    ui._scroll_item_into_view(root, mapping)
    ui._pump(20)
    if not mapping.isEnabled() or not mapping.isVisible() or mapping.width() <= 1 or mapping.height() <= 1:
        raise AssertionError(f"Engineer remap button is unavailable at {requested}: enabled={mapping.isEnabled()}, bounds={_rect(mapping)}")
    mapping_rect = ui._scene_rect(mapping)
    if not ui._rects_inside(mapping_rect, ui._scene_rect(flick), tolerance=2):
        raise AssertionError(f"Engineer remap button cannot be brought into viewport at {requested}: {mapping_rect}, view={ui._scene_rect(flick)}")
    before = len([call for call in fake.calls if call[0] == "beginDirectInputMapping"])
    ui._click(root, mapping)
    after = len([call for call in fake.calls if call[0] == "beginDirectInputMapping"])
    if after != before + 1:
        raise AssertionError(f"Engineer remap button did not invoke the facade at {requested}: {before} -> {after}")
    return {"requested_size": list(requested), "actual_size": [round(root.width()), round(root.height())],
            "value_text_chars": len(_text(value)), "value_bounds": list(map(round, value_rect)),
            "value_text": _text(value), "value_accessibility": visible_text, "value_scroll": scroll,
            "remap_button_bounds": list(map(round, mapping_rect)), "remap_invocations": after - before}


def _startup_error_payload() -> str:
    return ("Voice model initialization failed while loading the selected Vietnamese runtime. "
            "The model directory is unavailable; check the configured installation path, "
            "disk permissions, and whether another process is updating the model. " * 6)


def _api_error_payload() -> str:
    return ("TLS handshake failed while contacting https://race-engineer.example.invalid/v1/models. "
            "The peer closed the connection during certificate verification; verify the configured base URL, "
            "system clock, certificate chain, proxy settings, and server availability. " * 5)


def _test_startup_error(root: Any, fake: Any, requested: tuple[int, int]) -> dict[str, Any]:
    startup_error = _startup_error_payload()
    root.resize(*requested)
    _set_values(fake, {"startupReady": False, "startupProgress": 0.42, "startupError": startup_error,
                       "apiDetail": "", "apiConfigured": False})
    ui._pump(40)
    notice = ui._find(root, "startupNotice")
    label = ui._find_scoped(notice, "startupErrorLabel")
    title = ui._find_scoped(notice, "startupErrorTitle")
    scroll = ui._find_scoped(notice, "startupErrorScroll")
    retry = ui._find_scoped(notice, "startupRetryButton")
    close = ui._find_scoped(notice, "startupCloseButton")
    notice_rect = ui._scene_rect(notice)
    label_rect = ui._scene_rect(label)
    window_rect = (0.0, 0.0, float(root.width()), float(root.height()))
    if not notice.isVisible() or not ui._rects_inside(notice_rect, window_rect, tolerance=2):
        raise AssertionError(f"Startup notice escapes the visible window at {requested}: notice={notice_rect} window={window_rect}")
    if _text(label) != startup_error or not label.isVisible() or label.height() < 1:
        raise AssertionError(f"Long startup error is not shown in full at {requested}: {_accessibility_value(label)}")
    if not ui._rects_inside(ui._scene_rect(scroll), notice_rect, tolerance=2):
        raise AssertionError(f"Startup error ScrollView escapes its card at {requested}: scroll={_rect(scroll)} notice={notice_rect}")
    if not ui._rects_inside(label_rect, ui._scene_rect(scroll), tolerance=2):
        # The Text item may extend past the ScrollView while its contents scroll;
        # only its first and last lines need to be reachable through the viewport.
        if _nearest_flickable(label) is None:
            raise AssertionError(f"Startup error is outside a scrollable viewport at {requested}: label={label_rect} scroll={_rect(scroll)}")
    if not title.isVisible() or not retry.isVisible() or not close.isVisible():
        raise AssertionError(f"Startup title/footer controls are hidden at {requested}")
    title_before = _rect(title)
    retry_before = _rect(retry)
    close_before = _rect(close)
    for name, item in (("title", title), ("retry", retry), ("close", close)):
        if not ui._rects_inside(_rect(item), notice_rect, tolerance=2):
            raise AssertionError(f"Startup {name} is outside the fixed card at {requested}: {_rect(item)} notice={notice_rect}")
    scroll_evidence = _edge_scrolls(root, label, f"Startup error {requested}")
    for name, before, item in (("title", title_before, title), ("retry", retry_before, retry), ("close", close_before, close)):
        after = _rect(item)
        if any(abs(a - b) > 1 for a, b in zip(before, after)):
            raise AssertionError(f"Startup {name} moved while scrolling the error text at {requested}: before={before}, after={after}")
    return {"requested_size": list(requested), "actual_size": [round(root.width()), round(root.height())],
            "chars": len(startup_error), "notice_bounds": list(map(round, notice_rect)),
            "scroll_bounds": list(map(round, _rect(scroll))), "error_bounds": list(map(round, label_rect)),
            "title_bounds": list(map(round, ui._scene_rect(title))),
            "retry_bounds": list(map(round, ui._scene_rect(retry))),
            "close_bounds": list(map(round, ui._scene_rect(close))),
            "title_retry_close_fixed_while_scrolling": True, "error_text_full": True,
            "scroll_edges": scroll_evidence}


def _test_api_detail(engine: Any, root: Any, fake: Any,
                     requested: tuple[int, int], *, html_like: bool = False) -> dict[str, Any]:
    api_detail = ("<b>Upstream error</b> · <model>Qwen</model> &amp; certificate check failed at "
                  "https://race-engineer.example.invalid/v1/models") if html_like else _api_error_payload()
    root.resize(*requested)
    _set_values(fake, {"startupReady": True, "startupError": "", "apiConfigured": True,
                       "apiState": "ProviderError", "apiDetail": api_detail,
                       "apiBaseUrl": "http://localhost:8080/v1", "apiModel": "race-engineer-model",
                       "apiKey": "mock-key", "ttsBackend": "VieNeu-TTS"})
    ui._route_to(engine, root, 4, direct=True)
    ai = ui._find(root, "aiVoicePage")
    ai.setProperty("sectionIndex", 0)
    ui._pump(30)
    detail = ui._find_scoped(ai, "apiStatusDetailLabel")
    strip = ui._find_scoped(ai, "apiStatusStrip")
    summary = ui._find_scoped(ai, "apiStatusSummaryLabel")
    endpoint = ui._find_scoped(ai, "apiEndpointField")
    if _text(detail) != api_detail or _text(summary).find("Lỗi API") < 0:
        raise AssertionError(f"API error detail/summary was lost at {requested}: detail={len(_text(detail))}/{len(api_detail)} summary={_text(summary)}")
    if float(detail.property("implicitHeight") or 0) > detail.height() + 2:
        raise AssertionError(f"API detail is clipped inside its Text at {requested}: implicit={detail.property('implicitHeight')} height={detail.height()}")
    if not html_like:
        scroll_evidence = _edge_scrolls(root, detail, f"API error detail {requested}")
    else:
        scroll_evidence = None
    if not ui._rects_inside(_rect(detail), _rect(strip), tolerance=2):
        raise AssertionError(f"API diagnostic text escapes status strip at {requested}: detail={_rect(detail)} strip={_rect(strip)}")
    flick = _nearest_flickable(detail)
    if flick is None or float(flick.property("contentWidth")) > float(flick.width()) + 2:
        raise AssertionError(f"Long API detail causes horizontal overflow/no scroll container at {requested}")
    ui._scroll_item_into_view(root, endpoint)
    if not ui._rects_inside(_rect(endpoint), _rect(flick), tolerance=2):
        raise AssertionError(f"API endpoint field cannot be reached after long status detail at {requested}: {_rect(endpoint)} view={_rect(flick)}")
    text_format = int(ui._qml_item_value(engine, detail, "textFormat"))
    auto_text = int(ui._qml_component_item_value(detail, "Text.AutoText"))
    plain_text = int(ui._qml_component_item_value(detail, "Text.PlainText"))
    rich_text = int(ui._qml_component_item_value(detail, "Text.RichText"))
    format_names = {auto_text: "AutoText", plain_text: "PlainText", rich_text: "RichText"}
    accessible = _accessible_name(detail)
    return {"requested_size": list(requested), "actual_size": [round(root.width()), round(root.height())],
            "chars": len(api_detail), "detail_bounds": list(map(round, _rect(detail))),
            "strip_bounds": list(map(round, _rect(strip))), "summary": _text(summary),
            "detail_text_full": True, "detail_accessible_name": accessible,
            "text_format_enum_value": text_format, "text_format": format_names.get(text_format, "unknown"),
            "html_like_provider_error": html_like,
            "html_like_text_retained_verbatim_in_qml_property": _text(detail) == api_detail if html_like else None,
            "scroll_edges": scroll_evidence, "endpoint_reachable_after_status": True,
            "content_width": round(float(flick.property("contentWidth")), 2),
            "viewport_width": round(float(flick.width()), 2)}


def _test_unavailable_model_detail(engine: Any, root: Any, fake: Any) -> dict[str, Any]:
    detail_text = "Connected, but model 'missing-model' is unavailable"
    _set_values(fake, {"startupReady": True, "startupError": "", "apiConfigured": True,
                       "apiState": "ProviderError", "apiDetail": detail_text,
                       "apiBaseUrl": "http://localhost:8080/v1", "apiModel": "missing-model",
                       "apiKey": "mock-key"})
    ui._route_to(engine, root, 4, direct=True)
    ai = ui._find(root, "aiVoicePage")
    ai.setProperty("sectionIndex", 0)
    ui._pump(30)
    detail = ui._find_scoped(ai, "apiStatusDetailLabel")
    summary = ui._find_scoped(ai, "apiStatusSummaryLabel")
    displayed = _text(detail)
    falsely_claims_available = "mô hình khả dụng" in displayed
    if not falsely_claims_available:
        return {"provider_detail": detail_text, "displayed_detail": displayed,
                "provider_summary": _text(summary), "false_availability_claim": False}
    raise AssertionError(f"Provider says the model is unavailable, but UI claims it is available: provider={detail_text!r}, displayed={displayed!r}")


def _test_keyboard_and_lifecycle(engine: Any, root: Any, fake: Any) -> dict[str, Any]:
    root.resize(1280, 800)
    _set_values(fake, {"startupReady": True, "startupError": "", "ttsBenchmarkRunning": False,
                       "ttsBenchmarkRestoring": False, "connected": True})
    ui._route_to(engine, root, 0, direct=True)
    command = ui._find(root, "radioCommand")
    send = ui._find(root, "radioSend")
    ptt = ui._find(root, "radioPtt")
    cockpit = ui._find(root, "cockpit")
    ui._replace_text(root, command, "keyboard Enter audit")
    command.forceActiveFocus()
    QTest.keyClick(root, Qt.Key.Key_Return)
    ui._pump(40)
    ask_calls = [args for name, args in fake.calls if name == "askText"]
    if not ask_calls or ask_calls[-1] != ("keyboard Enter audit",):
        raise AssertionError(f"Enter did not send the focused radio composer text: {ask_calls[-1:]}")

    ui._replace_text(root, command, "keyboard Tab Space audit")
    command.forceActiveFocus()
    QTest.keyClick(root, Qt.Key.Key_Tab)
    ui._pump(20)
    focused = root.activeFocusItem()
    focus_name = focused.objectName() if focused else ""
    if focused is not send:
        raise AssertionError(f"Tab from the composer did not focus the adjacent Send button: {focus_name!r}")
    before_space = len(ask_calls)
    QTest.keyClick(root, Qt.Key.Key_Space)
    ui._pump(30)
    ask_calls = [args for name, args in fake.calls if name == "askText"]
    if len(ask_calls) != before_space + 1 or ask_calls[-1] != ("keyboard Tab Space audit",):
        raise AssertionError(f"Space on focused Send button did not send exactly once: {ask_calls[before_space:]}")

    # Space is tested as Qt Quick's keyboard activation on the focused PTT
    # button; native Ctrl+Space event-filter dispatch is outside this fake.
    ptt.forceActiveFocus()
    begin_before = sum(1 for name, _ in fake.calls if name == "beginPushToTalk")
    end_before = sum(1 for name, _ in fake.calls if name == "endPushToTalk")
    QTest.keyPress(root, Qt.Key.Key_Space)
    ui._pump(20)
    begin_during = sum(1 for name, _ in fake.calls if name == "beginPushToTalk")
    if begin_during != begin_before + 1 or not bool(cockpit.property("pttHeld")):
        raise AssertionError(f"Space did not begin PTT on focused radio button: begin {begin_before}->{begin_during}, held={cockpit.property('pttHeld')}")
    ui._route_to(engine, root, 1, direct=True)
    ui._pump(30)
    end_after_route = sum(1 for name, _ in fake.calls if name == "endPushToTalk")
    if end_after_route != end_before + 1 or bool(cockpit.property("pttHeld")):
        raise AssertionError(f"Route change did not cancel keyboard-held PTT exactly once: end {end_before}->{end_after_route}")
    QTest.keyRelease(root, Qt.Key.Key_Space)
    ui._pump(30)
    end_after_release = sum(1 for name, _ in fake.calls if name == "endPushToTalk")
    if end_after_release != end_after_route:
        raise AssertionError(f"PTT release after route cancellation duplicated end call: {end_after_route}->{end_after_release}")

    # Hidden Cockpit must stop reading telemetry, then reattach to the newest
    # controller snapshot on return.
    _set_values(fake, {"telemetry": {"track": "BEFORE-HIDDEN", "speedKmh": 224, "gear": 5}})
    ui._route_to(engine, root, 0, direct=True)
    track = ui._find(root, "trackValue")
    if _text(track) != "BEFORE-HIDDEN":
        raise AssertionError(f"Cockpit did not show the current telemetry before route change: {_text(track)}")
    ui._route_to(engine, root, 2, direct=True)
    _set_values(fake, {"telemetry": {"track": "AFTER-HIDDEN", "speedKmh": 301, "gear": 6}})
    hidden_track = _text(track)
    hidden_live = bool(cockpit.property("live"))
    if hidden_live or hidden_track != "CHỜ PHIÊN":
        raise AssertionError(f"Cockpit did not freeze while its route was inactive: live={hidden_live}, track={hidden_track!r}")
    ui._route_to(engine, root, 0, direct=True)
    restored_track = _text(track)
    if restored_track != "AFTER-HIDDEN":
        raise AssertionError(f"Cockpit did not refresh from current telemetry after route restore: {restored_track!r}")

    # Minimize while active, update the facade, and confirm live bindings
    # reattach to the latest value on restore.
    root.showMinimized()
    ui._pump(100)
    minimize_state = str(root.visibility())
    minimized_live = bool(cockpit.property("live"))
    _set_values(fake, {"telemetry": {"track": "AFTER-MINIMIZE", "speedKmh": 309, "gear": 7}})
    minimized_track = _text(track)
    root.showNormal()
    root.show()
    ui._pump(100)
    restored_minimized_track = _text(track)
    if "Minimized" not in minimize_state and minimized_live:
        raise AssertionError(f"QWindow did not enter minimized/inactive state in offscreen test: visibility={minimize_state}")
    if minimized_live or minimized_track != "CHỜ PHIÊN":
        raise AssertionError(f"Cockpit did not freeze while minimized: live={minimized_live}, track={minimized_track!r}")
    if restored_minimized_track != "AFTER-MINIMIZE" or not bool(cockpit.property("live")):
        raise AssertionError(f"Cockpit did not refresh latest telemetry after restore: track={restored_minimized_track!r}, live={cockpit.property('live')}")

    # The dedicated Telemetry page uses the same inactive/minimized contract.
    _set_values(fake, {"telemetry": {"track": "TELEMETRY-BEFORE-HIDE", "currentLap": 20, "speedKmh": 224}})
    ui._route_to(engine, root, 3, direct=True)
    telemetry_page = ui._find(root, "telemetryPage")
    lap = ui._find_scoped(telemetry_page, "telemetryCurrentLapValue")
    if _text(lap) != "20":
        raise AssertionError(f"Telemetry starts with stale current lap: {_text(lap)}")
    ui._route_to(engine, root, 0, direct=True)
    _set_values(fake, {"telemetry": {"track": "TELEMETRY-AFTER-HIDE", "currentLap": 21, "speedKmh": 225}})
    if bool(telemetry_page.property("live")) or _text(lap) != "—":
        raise AssertionError(f"Telemetry bindings did not freeze when route became inactive: live={telemetry_page.property('live')}, lap={_text(lap)!r}")
    ui._route_to(engine, root, 3, direct=True)
    if _text(lap) != "21":
        raise AssertionError(f"Telemetry did not refresh on route restore: {_text(lap)!r}")
    root.showMinimized()
    ui._pump(80)
    telemetry_minimized_live = bool(telemetry_page.property("live"))
    _set_values(fake, {"telemetry": {"track": "TELEMETRY-AFTER-MINIMIZE", "currentLap": 22, "speedKmh": 226}})
    minimized_lap = _text(lap)
    root.showNormal(); root.show(); ui._pump(80)
    restored_lap = _text(lap)
    if telemetry_minimized_live or minimized_lap != "—" or restored_lap != "22":
        raise AssertionError(f"Telemetry minimized refresh contract failed: live={telemetry_minimized_live}, hidden={minimized_lap!r}, restored={restored_lap!r}")
    return {"enter_sends_text_once": ask_calls[-2:], "tab_focus_target": focus_name,
            "space_sends_once": True, "space_ptt_route_cancel_begin_end": [begin_during - begin_before, end_after_route - end_before],
            "space_release_after_cancel_extra_end_calls": end_after_release - end_after_route,
            "cockpit_inactive_route": {"live": hidden_live, "hidden_text": hidden_track, "restored_text": restored_track},
            "cockpit_minimize": {"visibility": minimize_state, "live_while_minimized": minimized_live,
                                 "text_while_minimized": minimized_track, "restored_text": restored_minimized_track},
            "telemetry_inactive_minimize": {"restored_after_route_lap": "21", "live_while_minimized": telemetry_minimized_live,
                                            "text_while_minimized": minimized_lap, "restored_after_minimize_lap": restored_lap},
            "native_ctrl_space_verified": False}


def _test_voice_shortcut(engine: Any, root: Any, fake: Any) -> dict[str, Any]:
    result: dict[str, Any] = {}
    mismatches = []
    for backend_name, expected_section in (("VieNeu-TTS", 1), ("Google Gemini API", 2)):
        root.resize(1280, 800)
        _set_values(fake, {"startupReady": True, "startupError": "", "ttsBackend": backend_name,
                           "ttsBenchmarkRunning": False, "ttsBenchmarkRestoring": False})
        ui._route_to(engine, root, 2, direct=True)
        engineer = ui._find(root, "engineerSpotterPage")
        shortcut = ui._find_scoped(engineer, "openAiVoicePageButton")
        if not shortcut.isVisible() or not shortcut.isEnabled() or shortcut.width() <= 1:
            raise AssertionError(f"Voice configuration shortcut is not reachable for backend {backend_name}: visible={shortcut.isVisible()} enabled={shortcut.isEnabled()} bounds={_rect(shortcut)}")
        ui._click(root, shortcut)
        ai = ui._find(root, "aiVoicePage")
        actual_section = int(ai.property("sectionIndex"))
        result[backend_name] = {"current_page": int(root.property("currentPage")),
                                "section_index": actual_section,
                                "selected_backend": backend_name,
                                "expected_voice_section": expected_section,
                                "shortcut_bounds": list(map(round, _rect(shortcut)))}
        if actual_section != expected_section:
            mismatches.append({"backend": backend_name, "actual_section": actual_section,
                               "expected_section": expected_section})
    if mismatches:
        raise AssertionError(f"'Cấu hình giọng nói' opened the wrong AI section: {mismatches}; evidence={result}")
    return result


def run() -> dict[str, Any]:
    app = QGuiApplication.instance() or QGuiApplication(["raceengineer-edge-surface-audit"])
    app.setQuitOnLastWindowClosed(False)
    app.setApplicationVersion("1.0.5")
    qInstallMessageHandler(_message_handler)
    ui.QT_LOGS.clear()
    engine, root, fake, qml_warnings = ui._qml_engine(ui.NEW_QML)
    _fixture(fake)
    ui._pump(100)

    for requested in SIZES:
        label = f"{requested[0]}x{requested[1]}"
        _run_case(f"long_telemetry_and_numeric_extremes_{label}", root,
                  lambda requested=requested: _test_long_telemetry(engine, root, fake, requested))
        _run_case(f"long_engineer_binding_row_and_remap_action_{label}", root,
                  lambda requested=requested: _test_engineer_readonly_row(engine, root, fake, requested))
        _run_case(f"long_api_error_detail_{label}", root,
                  lambda requested=requested: _test_api_detail(engine, root, fake, requested))
    for requested in ((880, 576), (1280, 720), (1600, 1000)):
        label = f"{requested[0]}x{requested[1]}"
        _run_case(f"long_startup_error_{label}", root,
                  lambda requested=requested: _test_startup_error(root, fake, requested))
    _run_case("html_like_api_error_detail", root,
              lambda: _test_api_detail(engine, root, fake, (1280, 800), html_like=True))
    _run_case("unavailable_model_error_text", root,
              lambda: _test_unavailable_model_detail(engine, root, fake))
    _run_case("keyboard_and_inactive_minimized_refresh", root,
              lambda: _test_keyboard_and_lifecycle(engine, root, fake))
    _run_case("engineer_voice_shortcut_selected_backend", root,
              lambda: _test_voice_shortcut(engine, root, fake))

    report = {
        "created_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "baseline_commit": ui.BASELINE,
        "scope": "Focused independent UI edge audit; no whole-suite or gallery rerun.",
        "renderer": {"python": sys.version.split()[0], "pyside6": "6.8.3", "qt": "6.8.3",
                     "platform": "offscreen", "quick_backend": "software"},
        "requested_sizes": [list(size) for size in SIZES],
        "source_sha256": {
            name: _sha(ROOT / name) for name in (
                "qml/Main.qml", "qml/Cockpit.qml", "qml/DrivingData.qml", "qml/TelemetryPage.qml",
                "qml/EngineerSpotterPage.qml", "qml/StartupNotice.qml", "qml/LlmConnectionForm.qml",
                "src/llm/providers/OpenAICompatibleProvider.cpp",
                "qml/AiVoicePage.qml", "tests/ui/run_ui.py", "tests/ui/fake_application.py",
                "tests/ui/capture_review.py", "tests/ui/edge_surface_audit.py")
        },
        "source_evidence": {
            "engineer_voice_shortcut": _source_lines("qml/Main.qml", [
                "onOpenAiVoiceRequested: {",
                "aiVoiceLoader.item.sectionIndex = backend.ttsBackend === \"Google Gemini API\" ? 2 : 1",
            ]) | {"ai_voice_default_section": _source_lines("qml/AiVoicePage.qml", [
                "property alias sectionIndex: sectionTabs.currentIndex",
                "id: sectionTabs",
                'TabButton { text: "Kết nối AI"', 'TabButton { text: "Giọng cục bộ"', 'TabButton { text: "Google TTS"',
            ])},
            "long_startup_notice": _source_lines("qml/StartupNotice.qml", [
                "implicitHeight: contents.implicitHeight + 28",
                "height: parent ? Math.min(implicitHeight, Math.max(0, parent.height - 28)) : implicitHeight",
                'objectName: "startupErrorScroll"', "contentHeight: errorLabel.implicitHeight",
                "text: String(root.controller.startupError)",
            ]) | {"main_bottom_anchor": _source_lines("qml/Main.qml", [
                'objectName: "startupNotice"', "anchors.bottom: parent.bottom",
            ])},
            "api_error_detail": _source_lines("qml/LlmConnectionForm.qml", [
                "readonly property string statusDetail", 'objectName: "apiStatusStrip"',
                'objectName: "apiStatusDetailLabel"',
            ]) | {"label_text_component": _source_lines("qml/LlmConnectionForm.qml", [
                "component LabelText: Text", "wrapMode: Text.Wrap",
            ])},
            "provider_unavailable_model": _source_lines("src/llm/providers/OpenAICompatibleProvider.cpp", [
                "Connected, but model",
            ]),
            "engineer_binding_and_shortcut": _source_lines("qml/EngineerSpotterPage.qml", [
                'valueObjectName: "engineerDirectInputBinding"',
                'objectName: "engineerMapDirectInputButton"',
                'objectName: "openAiVoicePageButton"',
                'text: "Cấu hình giọng nói"',
            ]),
        },
        "qml_engine_warnings": qml_warnings,
        "qt_log_warnings": WARNINGS + ui.QT_LOGS,
        "cases": RESULTS,
        "failure_count": sum(1 for item in RESULTS.values() if item["status"] == "FAIL"),
        "native_limits": [
            "Native C++ Ctrl+Space event-filter dispatch is not loaded into this QML fake; standard Qt Quick Space key activation is exercised.",
            "No real simulator, Windows minimize/tray, file dialog, audio device, provider, or DirectInput device is invoked.",
        ],
    }
    OUT.mkdir(parents=True, exist_ok=True)
    report_path = OUT / "edge_surface_audit.json"
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return {"report": str(report_path), "failures": report["failure_count"],
            "cases": {name: item["status"] for name, item in RESULTS.items()},
            "warnings": report["qml_engine_warnings"] + report["qt_log_warnings"]}


if __name__ == "__main__":
    print(json.dumps(run(), ensure_ascii=False, indent=2))
