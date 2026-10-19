#!/usr/bin/env python3
"""Capture the actual 2×2 tyre GridLayout subtree, including off-viewport tiles.

Run after capture_review.py. Telemetry can scroll vertically, so its rendered
screen screenshot may show only part of the tile grid. QQuickItem.grabToImage
captures the same live item subtree without resizing or changing production UI.
"""

from __future__ import annotations

import hashlib
import json
import os
import sys
import time
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QT_QUICK_BACKEND", "software")

from PySide6.QtCore import QCoreApplication, qInstallMessageHandler  # noqa: E402
from PySide6.QtGui import QGuiApplication  # noqa: E402
from PySide6.QtTest import QTest  # noqa: E402

import run_ui as ui  # noqa: E402
import capture_review  # noqa: E402


ROOT = ui.ROOT
GALLERY = ui.VALIDATION_DIR / "review-gallery" / "gallery-report.json"
OUT = ui.VALIDATION_DIR / "tyre-tile-closeups"


def _grab(item: object) -> object:
    grab = item.grabToImage()
    image = grab.image()
    for _ in range(30):
        if not image.isNull():
            return image
        QCoreApplication.processEvents()
        QTest.qWait(40)
        image = grab.image()
    raise AssertionError(f"QQuickItem.grabToImage returned no image for {item.objectName()!r}")


def run() -> dict[str, object]:
    if not GALLERY.is_file():
        raise AssertionError("Run tests/ui/capture_review.py first to create the full screen gallery")
    app = QGuiApplication.instance() or QGuiApplication(["raceengineer-tyre-tile-item-capture"])
    app.setQuitOnLastWindowClosed(False)
    app.setApplicationVersion("1.0.5")
    qInstallMessageHandler(ui._qt_message_handler)
    ui.QT_LOGS.clear()
    compile_report = ui._compile_source_components()
    if not compile_report["all_ready"]:
        raise AssertionError("QML source compilation failed: " + "\n".join(compile_report["errors"]))
    engine, root, fake, warnings = ui._qml_engine(ui.NEW_QML)
    capture_review._fixture(fake)

    captures: list[dict[str, object]] = []
    specs = ((0, "cockpit", size) for size in ui.SIZES)
    for page, label, requested_size in [*specs, (3, "telemetry", (1280, 800))]:
        ui._route_to(engine, root, page, direct=True)
        root.resize(*requested_size)
        ui._pump(100)
        metrics = ui._window_metrics(root, requested_size)
        width, height = metrics["actual_size"]
        page_item = ui._find(root, "cockpit" if page == 0 else "telemetryPage")
        data = ui._find_scoped(page_item, "cockpitDrivingData" if page == 0 else "telemetryDrivingData")
        geometry = ui._assert_tyre_tile_geometry(data, width, height)
        grid = ui._find_scoped(data, "tyreTileGrid")
        image = _grab(grid)
        if image.width() <= 1 or image.height() <= 1:
            raise AssertionError(f"Tyre tile subtree returned empty image at {width}x{height}")
        out = OUT / f"{label}-{width}x{height}.png"
        OUT.mkdir(parents=True, exist_ok=True)
        if not image.save(str(out), "PNG"):
            raise AssertionError(f"Could not save tyre tile item image {out}")
        source_name = f"{label}-{width}x{height}.png"
        if label == "cockpit":
            source_name = f"cockpit-{width}x{height}.png"
        else:
            source_name = f"telemetry-{width}x{height}.png"
        source_path = ui.VALIDATION_DIR / "review-gallery" / source_name
        if not source_path.is_file():
            raise AssertionError(f"Missing corresponding screen render {source_path}")
        captures.append({
            "path": str(out.relative_to(ROOT)),
            "source_screenshot": str(source_path.relative_to(ROOT)),
            "route": page,
            "actual_window_size": [width, height],
            "item_object_name": grid.objectName(),
            "item_image_size": [image.width(), image.height()],
            "item_scene_bounds": list(map(round, ui._scene_rect(grid))),
            "capture_method": "QQuickItem.grabToImage on the live tyreTileGrid subtree",
            "source_screenshot_sha256": hashlib.sha256(source_path.read_bytes()).hexdigest(),
            "item_png_sha256": hashlib.sha256(out.read_bytes()).hexdigest(),
            "tile_geometry": geometry,
        })

    if warnings or ui.QT_LOGS:
        raise AssertionError(f"Tile item capture emitted QML/Qt messages: {warnings + ui.QT_LOGS}")
    report = json.loads(GALLERY.read_text(encoding="utf-8"))
    report["tyre_tile_closeups"] = captures
    report["tile_closeup_method"] = (
        "QQuickItem.grabToImage captures the live tyreTileGrid subtree. "
        "This preserves off-viewport Telemetry tiles without changing application geometry."
    )
    report["tile_capture_script_sha256"] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    report["tile_capture_created_utc"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
    report["tile_capture_qml_warnings"] = []
    report["tile_capture_qt_logs"] = []
    GALLERY.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return {"closeup_count": len(captures), "paths": [c["path"] for c in captures],
            "gallery_report": str(GALLERY), "qml_warnings": [], "qt_logs": []}


if __name__ == "__main__":
    print(json.dumps(run(), ensure_ascii=False, indent=2))
