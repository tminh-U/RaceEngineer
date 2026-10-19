#!/usr/bin/env python3
"""Audit the shipped monochrome logo files and the QML header image.

Run from any directory with:
    QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
      .tooling/ui/bin/python tests/ui/logo_asset_audit.py

The script writes a standalone JSON report under docs/ui-validation/release-1.0.6.
"""

from __future__ import annotations

import hashlib
import json
import os
import re
import sys
import time
from pathlib import Path
from typing import Any

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QT_QUICK_BACKEND", "software")

from PySide6.QtCore import QCoreApplication, QEvent, QUrl  # noqa: E402
from PySide6.QtGui import QGuiApplication, QImageReader  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
EXPECTED_PNGS = {
    "final_icon_16.png": (16, 16),
    "final_icon_24.png": (24, 24),
    "final_icon_32.png": (32, 32),
    "final_icon_48.png": (48, 48),
    "final_icon_64.png": (64, 64),
    "final_icon_128.png": (128, 128),
    "final_icon_180.png": (180, 180),
    "final_icon_192.png": (192, 192),
    "final_icon_256.png": (256, 256),
    "final_icon_512.png": (512, 512),
}
ICO_REQUIRED_SIZES = {(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)}
_APP: QGuiApplication | None = None


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _image_record(path: Path) -> dict[str, Any]:
    reader = QImageReader(str(path))
    reader.setAutoTransform(True)
    frame_count = max(1, int(reader.imageCount()))
    frames: list[dict[str, Any]] = []
    non_gray_pixels = 0
    for frame_index in range(frame_count):
        if frame_index and not reader.jumpToImage(frame_index):
            raise AssertionError(f"Cannot read frame {frame_index} from {path}")
        image = reader.read()
        if image.isNull():
            raise AssertionError(f"Qt could not decode {path}: {reader.errorString()}")
        frame_non_gray = 0
        for y in range(image.height()):
            for x in range(image.width()):
                pixel = image.pixelColor(x, y)
                if pixel.alpha() and (pixel.red() != pixel.green() or pixel.green() != pixel.blue()):
                    frame_non_gray += 1
        non_gray_pixels += frame_non_gray
        frames.append({"size": [image.width(), image.height()],
                       "nontransparent_non_gray_pixels": frame_non_gray})
    return {"path": path.relative_to(ROOT).as_posix(), "sha256": _sha256(path),
            "frame_count": frame_count, "frames": frames,
            "grayscale": non_gray_pixels == 0,
            "nontransparent_non_gray_pixels": non_gray_pixels}


def _logo_asset_checks() -> dict[str, Any]:
    assets_dir = ROOT / "assets"
    records = {name: _image_record(assets_dir / name) for name in EXPECTED_PNGS}
    for name, expected_size in EXPECTED_PNGS.items():
        record = records[name]
        if record["frames"][0]["size"] != list(expected_size):
            raise AssertionError(f"{name} dimensions {record['frames'][0]['size']} != {expected_size}")
        if not record["grayscale"]:
            raise AssertionError(f"{name} has {record['nontransparent_non_gray_pixels']} chromatic pixels")

    ico = _image_record(assets_dir / "final_icon.ico")
    ico_sizes = {tuple(frame["size"]) for frame in ico["frames"]}
    if not ICO_REQUIRED_SIZES.issubset(ico_sizes):
        raise AssertionError(f"ICO sizes {sorted(ico_sizes)} do not include {sorted(ICO_REQUIRED_SIZES)}")
    if not ico["grayscale"]:
        raise AssertionError(f"ICO has {ico['nontransparent_non_gray_pixels']} chromatic pixels")

    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    resource_block = re.search(r"set\(RACEENGINEER_ICON_RESOURCES(.*?)\n\)", cmake, re.S)
    if not resource_block:
        raise AssertionError("CMakeLists.txt does not declare RACEENGINEER_ICON_RESOURCES")
    registered = set(re.findall(r"assets/[^\s)]+", resource_block.group(1)))
    expected_registered = {f"assets/{name}" for name in EXPECTED_PNGS} | {"assets/final_icon.ico"}
    missing_resources = sorted(expected_registered - registered)
    if missing_resources:
        raise AssertionError(f"CMake icon resource list omits: {missing_resources}")
    rc = (assets_dir / "final_icon.rc").read_text(encoding="utf-8")
    if '"final_icon.ico"' not in rc:
        raise AssertionError("Windows resource file does not refer to final_icon.ico")

    readme_links: dict[str, list[str]] = {}
    for relative in ("README.md", "docs/readme_vie.md"):
        text = (ROOT / relative).read_text(encoding="utf-8")
        links = re.findall(r'<img\b[^>]*\bsrc="([^"]+)"[^>]*\balt="[^"]*logo[^"]*"', text, re.I)
        if not links:
            raise AssertionError(f"No logo image link found in {relative}")
        for link in links:
            target = (ROOT / relative).parent / link
            if not target.is_file():
                raise AssertionError(f"README logo link does not resolve: {relative} -> {link}")
            if target.suffix.lower() != ".png" or not _image_record(target)["grayscale"]:
                raise AssertionError(f"README logo link is not a grayscale PNG: {relative} -> {link}")
        readme_links[relative] = links

    version_text = cmake.splitlines()[2]
    if "VERSION 1.0.6" not in version_text:
        raise AssertionError(f"CMake project version is not 1.0.6: {version_text!r}")
    return {"pngs": records, "ico": ico,
            "ico_required_sizes": [list(size) for size in sorted(ICO_REQUIRED_SIZES)],
            "cmake_registered_resources": sorted(registered),
            "windows_resource_file": "assets/final_icon.rc",
            "readme_logo_links": readme_links,
            "cmake_project_version": "1.0.6"}


def _runtime_logo_check() -> dict[str, Any]:
    global _APP
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import run_ui  # pylint: disable=import-outside-toplevel

    _APP = QGuiApplication.instance() or QGuiApplication(["raceengineer-logo-audit"])
    engine, root, _fake, warnings = run_ui._qml_engine(ROOT / "qml" / "Main.qml")
    run_ui._pump(180)
    logo = run_ui._find(root, "raceEngineerLogo")
    source = logo.property("source")
    source_url = source.toString() if isinstance(source, QUrl) else str(source)
    status = int(run_ui._qml_item_value(engine, logo, "status"))
    geometry = run_ui._scene_rect(logo)
    if not logo.isVisible() or status != 1:
        raise AssertionError(f"QML logo is not loaded and visible: visible={logo.isVisible()}, status={status}, source={source_url}")
    if geometry[2] <= 1 or geometry[3] <= 1:
        raise AssertionError(f"QML logo has empty geometry: {geometry}")
    if warnings:
        raise AssertionError(f"Main emitted QML warnings while loading logo: {warnings}")
    # Tear the engine down while the application and facade are still alive;
    # otherwise Python finalization can null the context object first and emit
    # irrelevant binding errors after the audit report has been written.
    root.setVisible(False)
    engine.deleteLater()
    QCoreApplication.sendPostedEvents(None, QEvent.Type.DeferredDelete)
    QCoreApplication.processEvents()
    run_ui.ENGINE_KEEPALIVE.clear()
    return {"object_name": "raceEngineerLogo", "visible": True, "status": "Ready",
            "source_url": source_url, "scene_bounds": [round(value, 2) for value in geometry],
            "qml_warnings": warnings, "source_sha256": _sha256(ROOT / "qml" / "Main.qml")}


def main() -> int:
    output_dir = ROOT / "docs" / "ui-validation" / "release-1.0.6"
    output_dir.mkdir(parents=True, exist_ok=True)
    report = {"created_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
              "assets": _logo_asset_checks(), "runtime_logo": _runtime_logo_check()}
    report["source_sha256"] = {
        path: _sha256(ROOT / path) for path in
        ("CMakeLists.txt", "README.md", "docs/readme_vie.md", "qml/Main.qml",
         "tests/ui/run_ui.py", "tests/ui/logo_asset_audit.py")
    }
    destination = output_dir / "logo-asset-audit.json"
    destination.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"report": destination.relative_to(ROOT).as_posix(),
                      "png_count": len(report["assets"]["pngs"]),
                      "ico_frames": report["assets"]["ico"]["frame_count"],
                      "runtime_logo": report["runtime_logo"]}, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
