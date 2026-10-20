#!/usr/bin/env python3
"""Focused QML regression for connected telemetry with late AC/ACC metadata.

Run from the repository root:
    QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
      .tooling/ui/bin/python tests/ui/metadata_regression.py

The fake facade verifies QML rendering/bindings only; native provider coverage
is in tests/TelemetryMetadataTests.cpp and must run on Windows.
"""

from __future__ import annotations

import hashlib
import json
import os
import sys
import time
from pathlib import Path
from typing import Any

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QT_QUICK_BACKEND", "software")

from PySide6.QtCore import qInstallMessageHandler  # noqa: E402
from PySide6.QtGui import QGuiApplication  # noqa: E402
from PySide6.QtQuick import QQuickItem, QQuickWindow  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_ui as ui  # noqa: E402

OUT = ROOT / "docs/ui-validation/release-1.0.6/metadata-reissue-2026-10-20"


def _hash(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _save(window: QQuickWindow, path: Path) -> dict[str, Any]:
    image = window.grabWindow()
    if image.isNull():
        raise AssertionError(f"Qt returned an empty render for {path.name}")
    if not image.save(str(path), "PNG"):
        raise AssertionError(f"Could not save rendered screenshot: {path}")
    return {"path": str(path.relative_to(ROOT)), "width": image.width(), "height": image.height(),
            "sha256": _hash(path)}


def _text(item: QQuickItem) -> str:
    return str(item.property("text") or "")


def _visible_bounds(item: QQuickItem, window: QQuickWindow, label: str) -> list[float]:
    if not item.isVisible() or item.width() <= 1 or item.height() <= 1:
        raise AssertionError(f"{label} is hidden or has empty bounds")
    x, y, width, height = ui._scene_rect(item)
    if x < -1 or y < -1 or x + width > window.width() + 1 or y + height > window.height() + 1:
        raise AssertionError(f"{label} lies outside the window: {(x, y, width, height)}")
    return [round(x, 2), round(y, 2), round(width, 2), round(height, 2)]


def _set_fixture(fake: Any, connected: bool, telemetry: dict[str, Any]) -> None:
    fake.values.update({
        "connected": connected,
        "simulatorName": "Assetto Corsa" if connected else "",
        "connectionText": "Đã kết nối Assetto Corsa" if connected else "Chưa kết nối AC / ACC",
        "telemetry": dict(telemetry),
        "startupReady": True,
        "ttsBenchmarkRunning": False,
        "ttsBenchmarkRestoring": False,
    })
    fake.changed.emit()
    ui._pump(70)


def _verify_state(window: QQuickWindow, cockpit: QQuickItem, fake: Any,
                  connected: bool, telemetry: dict[str, Any], expected_track: str,
                  expected_car: str, screenshot: Path) -> dict[str, Any]:
    _set_fixture(fake, connected, telemetry)
    track = ui._find_scoped(cockpit, "trackValue")
    car = ui._find_scoped(cockpit, "carValue")
    speed = ui._find_scoped(cockpit, "speedValue")
    gear = ui._find_scoped(cockpit, "gearValue")
    fuel = ui._find_scoped(cockpit, "fuelValue")
    screenshot_record = _save(window, screenshot)

    actual_track, actual_car = _text(track), _text(car)
    if actual_track != expected_track:
        raise AssertionError(f"Connected={connected}: track text {actual_track!r}, expected {expected_track!r}")
    if actual_car != expected_car:
        raise AssertionError(f"Connected={connected}: car text {actual_car!r}, expected {expected_car!r}")
    if connected:
        if not _text(speed) or "—" in _text(speed) or not any(ch.isdigit() for ch in _text(speed)):
            raise AssertionError(f"Live speed disappeared while metadata was missing: {_text(speed)!r}")
        if _text(gear) != "5":
            raise AssertionError(f"Live gear was not rendered: {_text(gear)!r}")
        if not any(ch.isdigit() for ch in _text(fuel)):
            raise AssertionError(f"Live fuel was not rendered: {_text(fuel)!r}")

    result = {
        "connected": connected,
        "telemetry": telemetry,
        "rendered": {"track": actual_track, "car": actual_car, "speed": _text(speed),
                     "gear": _text(gear), "fuel": _text(fuel)},
        "bounds": {"track": _visible_bounds(track, window, "trackValue"),
                   "car": _visible_bounds(car, window, "carValue"),
                   "speed": _visible_bounds(speed, window, "speedValue"),
                   "gear": _visible_bounds(gear, window, "gearValue"),
                   "fuel": _visible_bounds(fuel, window, "fuelValue")},
        "screenshot": screenshot_record,
    }
    return result


def main() -> dict[str, Any]:
    OUT.mkdir(parents=True, exist_ok=True)
    app = QGuiApplication.instance() or QGuiApplication(["RaceEngineer-metadata-regression"])
    _ = app
    qt_logs: list[str] = []
    qInstallMessageHandler(lambda _mode, _context, message: qt_logs.append(message))
    engine, root, fake, warnings = ui._qml_engine(ui.NEW_QML)
    if not isinstance(root, QQuickWindow) or root.objectName() != "appWindow":
        raise AssertionError("Main did not load as the RaceEngineer appWindow")
    root.show()
    ui._pump(100)
    cockpit = ui._find(root, "cockpit")

    live_without_metadata = {
        "speedKmh": 224.0, "gear": 5, "rpm": 7825.0, "fuelLiters": 32.4,
        "throttle": 0.82, "brake": 0.12,
    }
    track_only = {**live_without_metadata, "track": "Monza"}
    car_only = {**live_without_metadata, "carModel": "Ferrari 296 GT3"}
    late_metadata = {**live_without_metadata, "track": "Monza", "carModel": "Ferrari 296 GT3"}
    changed_metadata = {**live_without_metadata, "track": "Silverstone", "carModel": "Porsche 992 GT3 R"}
    stale_disconnected = {**live_without_metadata, "track": "Stale Track", "carModel": "Stale Car Model"}
    cases: list[dict[str, Any]] = []
    sizes = ((880, 576), (1280, 800))

    for width, height in sizes:
        root.resize(width, height)
        ui._pump(90)
        actual_size = [round(root.width()), round(root.height())]
        if actual_size != [width, height]:
            raise AssertionError(f"Requested size {(width, height)} was clamped to {actual_size}")
        size_cases = []
        prefix = f"metadata-{width}x{height}"
        size_cases.append(_verify_state(
            root, cockpit, fake, True, live_without_metadata,
            "ĐÃ KẾT NỐI", "Chưa có thông tin xe từ simulator",
            OUT / f"{prefix}-connected-metadata-missing.png"))
        size_cases.append(_verify_state(
            root, cockpit, fake, True, track_only,
            "MONZA", "Chưa có thông tin xe từ simulator",
            OUT / f"{prefix}-track-only-metadata.png"))
        size_cases.append(_verify_state(
            root, cockpit, fake, True, car_only,
            "ĐÃ KẾT NỐI", "Ferrari 296 GT3",
            OUT / f"{prefix}-car-only-metadata.png"))
        size_cases.append(_verify_state(
            root, cockpit, fake, True, late_metadata,
            "MONZA", "Ferrari 296 GT3",
            OUT / f"{prefix}-metadata-arrived.png"))
        size_cases.append(_verify_state(
            root, cockpit, fake, True, changed_metadata,
            "SILVERSTONE", "Porsche 992 GT3 R",
            OUT / f"{prefix}-metadata-changed.png"))
        size_cases.append(_verify_state(
            root, cockpit, fake, False, stale_disconnected,
            "CHỜ PHIÊN", "Khởi động simulator để xem dữ liệu xe",
            OUT / f"{prefix}-disconnected-stale-names.png"))
        cases.append({"requested_size": [width, height], "actual_size": actual_size, "states": size_cases})

    if warnings or qt_logs:
        raise AssertionError(f"QML warnings/logs during metadata UI regression: {warnings + qt_logs}")
    result = {
        "created_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "result": "PASS",
        "scope": "Qt/PySide QML binding and rendered text with synthetic facade telemetry",
        "runtime": {"python": sys.version.split()[0], "pyside6": __import__("PySide6").__version__,
                    "qt": __import__("PySide6.QtCore", fromlist=["qVersion"]).qVersion(),
                    "platform": os.environ.get("QT_QPA_PLATFORM"),
                    "quick_backend": os.environ.get("QT_QUICK_BACKEND")},
        "sizes_and_states": cases,
        "warnings": warnings,
        "qt_logs": qt_logs,
        "source_sha256": {
            "qml/Cockpit.qml": _hash(ROOT / "qml/Cockpit.qml"),
            "qml/Main.qml": _hash(ROOT / "qml/Main.qml"),
            "tests/ui/metadata_regression.py": _hash(Path(__file__).resolve()),
            "tests/TelemetryMetadataTests.cpp": _hash(ROOT / "tests/TelemetryMetadataTests.cpp"),
            "tests/ui/fake_application.py": _hash(ROOT / "tests/ui/fake_application.py"),
            "src/telemetry/ac/ACTelemetryProvider.cpp": _hash(ROOT / "src/telemetry/ac/ACTelemetryProvider.cpp"),
            "src/telemetry/acc/ACCTelemetryProvider.cpp": _hash(ROOT / "src/telemetry/acc/ACCTelemetryProvider.cpp"),
        },
        "limitations": [
            "The UI facade supplies synthetic telemetry and metadata; this does not run AC/ACC or their shared-memory providers.",
            "Native provider regression is in tests/TelemetryMetadataTests.cpp and must execute on Windows.",
            "No real simulator, UDP companion, graphics driver, or game-session behavior is verified here.",
        ],
    }
    report = OUT / "metadata-ui-report.json"
    report.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return result


if __name__ == "__main__":
    print(json.dumps(main(), ensure_ascii=False, indent=2))
