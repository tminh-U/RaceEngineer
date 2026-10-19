#!/usr/bin/env python3
"""Refresh the downloadable UI gallery, standalone HTML, manifest, and ZIP."""

from __future__ import annotations

import base64
import hashlib
import html
import json
import time
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VALIDATION = ROOT / "docs" / "ui-validation"
GALLERY = VALIDATION / "review-gallery"
TILE_DIR = VALIDATION / "tyre-tile-closeups"
ZIP_NAME = "RaceEngineer-UI-preview-fix-2026-10-19.zip"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def write_html(path: Path, source: str) -> None:
    path.write_text(source, encoding="utf-8")


def validation_relative(repo_relative_path: str) -> str:
    return (ROOT / repo_relative_path).relative_to(VALIDATION).as_posix()


def main() -> None:
    gallery_report_path = GALLERY / "gallery-report.json"
    gallery_report = read_json(gallery_report_path)
    screenshots = gallery_report["screenshots"]
    tiles = gallery_report["tyre_tile_closeups"]
    if len(screenshots) != 52 or len(tiles) != 4:
        raise SystemExit(f"Unexpected current evidence count: {len(screenshots)} screenshots, {len(tiles)} tile grabs")
    if gallery_report.get("qml_warnings") or gallery_report.get("tile_capture_qml_warnings") or gallery_report.get("tile_capture_qt_logs"):
        raise SystemExit("Current gallery report contains QML/Qt warnings or logs")

    index_cards: list[str] = []
    standalone_cards: list[str] = []
    tile_cards: list[str] = []
    for item in screenshots:
        image_path = ROOT / item["path"]
        if not image_path.is_file():
            raise SystemExit(f"Missing screenshot: {image_path}")
        actual = item.get("actual_size", item.get("size", []))
        label = Path(item["path"]).stem.replace("-", " ")
        caption = f"{label} · {actual[0]}×{actual[1]}"
        rel = image_path.relative_to(VALIDATION).as_posix()
        escaped_caption = html.escape(caption)
        index_cards.append(
            f'<figure><a href="{html.escape(rel)}"><img loading="lazy" src="{html.escape(rel)}" '
            f'alt="{escaped_caption}"></a><figcaption>{escaped_caption}</figcaption></figure>'
        )
        encoded = base64.b64encode(image_path.read_bytes()).decode("ascii")
        standalone_cards.append(
            f'<figure><img loading="lazy" src="data:image/png;base64,{encoded}" '
            f'alt="{escaped_caption}"><figcaption>{escaped_caption}</figcaption></figure>'
        )

    for item in tiles:
        image_path = ROOT / item["path"]
        if not image_path.is_file():
            raise SystemExit(f"Missing full tile-grid capture: {image_path}")
        size = item.get("item_image_size", [])
        source = Path(item["source_screenshot"]).name
        label = f"{source.rsplit('-', 1)[0]} full tyre/brake grid · {size[0]}×{size[1]}"
        escaped_caption = html.escape(label)
        rel = image_path.relative_to(VALIDATION).as_posix()
        tile_cards.append(
            f'<figure><a href="{html.escape(rel)}"><img loading="lazy" src="{html.escape(rel)}" '
            f'alt="{escaped_caption}"></a><figcaption>{escaped_caption}<br>'
            f'<a href="{html.escape(validation_relative(item["source_screenshot"]))}">'
            "Full-screen view</a></figcaption></figure>"
        )
        encoded = base64.b64encode(image_path.read_bytes()).decode("ascii")
        standalone_cards.append(
            f'<figure><img loading="lazy" src="data:image/png;base64,{encoded}" '
            f'alt="{escaped_caption}"><figcaption>{escaped_caption}</figcaption></figure>'
        )

    common_css = (
        "body{margin:0;background:#101214;color:#eee;font:15px/1.45 system-ui,sans-serif}"
        "main{max-width:1500px;margin:auto;padding:24px}h1,h2{font-weight:650}section{margin:30px 0}"
        "figure{display:inline-block;vertical-align:top;background:#1c1e21;padding:10px;margin:8px;"
        "border:1px solid #45494f;border-radius:6px;max-width:min(45%,680px)}"
        "img{display:block;max-width:100%;max-height:680px;width:auto;height:auto}"
        "figcaption{color:#ccd0d4;margin-top:8px}a{color:#9ed4ff}.tiles figure{max-width:min(45%,500px)}"
        ".tiles img{max-height:760px}"
    )
    meta = (
        "Actual Qt 6.8.3 software-rendered images from an offscreen QGuiApplication with a synthetic facade. "
        "The fixture is not live Assetto Corsa/ACC data and does not invoke native DirectInput or file dialogs. "
        "All PNGs are original Qt output, not edited."
    )
    index = (
        '<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" '
        'content="width=device-width,initial-scale=1"><title>RaceEngineer current UI gallery</title>'
        f"<style>{common_css}</style><main><h1>RaceEngineer current UI gallery</h1>"
        f"<p>{html.escape(meta)} Gallery: {html.escape(gallery_report['created_utc'])} UTC. "
        f"Full tile grabs: {html.escape(gallery_report.get('tile_capture_created_utc',''))} UTC.</p>"
        '<nav><a href="#tiles">Full tyre/brake grids</a> · <a href="#screens">52 full-screen captures</a> · '
        '<a href="render-report.json">Integrated UI report</a> · '
        '<a href="baseline-surface-audit.json">Targeted feature audit</a> · '
        '<a href="baseline-parity.md">Baseline matrix</a> · '
        f'<a href="{ZIP_NAME}">Download ZIP</a></nav>'
        f'<section id="tiles"><h2>Full tile-grid item captures</h2><p>Live '
        '<code>QQuickItem.grabToImage</code> captures include lower Telemetry tiles outside its scroll viewport.</p>'
        f'<div class="tiles">{"".join(tile_cards)}</div></section>'
        f'<section id="screens"><h2>Full-window Qt captures</h2>{"".join(index_cards)}</section></main></html>'
    )
    write_html(VALIDATION / "index.html", index)

    tile_index = (
        '<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" '
        'content="width=device-width,initial-scale=1"><title>Full tyre and brake tile captures</title>'
        f"<style>{common_css}</style><main><h1>Full tyre/brake tile captures</h1>"
        f"<p>Captured from the live QML item with QQuickItem.grabToImage. See the "
        f'<a href="../index.html">full gallery</a>.</p><div class="tiles">{"".join(tile_cards)}</div></main></html>'
    )
    write_html(TILE_DIR / "index.html", tile_index)

    standalone = (
        '<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" '
        'content="width=device-width,initial-scale=1"><title>Standalone RaceEngineer UI gallery</title>'
        f"<style>{common_css}</style><main><h1>Standalone RaceEngineer UI gallery</h1>"
        f"<p>{html.escape(meta)} All 56 current images are embedded in this file.</p>"
        f'<section><h2>Full tile grids</h2><div class="tiles">{"".join(standalone_cards[52:])}</div></section>'
        f'<section><h2>Full-screen captures</h2>{"".join(standalone_cards[:52])}</section></main></html>'
    )
    write_html(VALIDATION / "gallery-standalone.html", standalone)

    bundle_paths = [
        VALIDATION / "README.md",
        VALIDATION / "baseline-parity.md",
        VALIDATION / "baseline-surface-audit.json",
        VALIDATION / "render-report.json",
        VALIDATION / "index.html",
        VALIDATION / "gallery-standalone.html",
        VALIDATION / "review-gallery" / "gallery-report.json",
        VALIDATION / "tyre-tile-closeups" / "index.html",
        *[ROOT / item["path"] for item in screenshots],
        *[ROOT / item["path"] for item in tiles],
    ]
    for path in bundle_paths:
        if not path.is_file():
            raise SystemExit(f"Missing bundle input: {path}")

    manifest = {
        "artifact": "RaceEngineer current UI gallery — Telemetry and DirectInput preview fixes",
        "created_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "baseline_commit": "f01acc553c0d938af74f4ea6a026dbc6571eb06c",
        "integrated_run_utc": "2026-10-19T04:12:02Z",
        "targeted_audit_utc": "2026-10-19T04:13:38Z",
        "gallery_capture_utc": gallery_report["created_utc"],
        "tile_item_capture_utc": gallery_report.get("tile_capture_created_utc"),
        "renderer": "PySide6 6.8.3 / Qt 6.8.3 software renderer, offscreen QGuiApplication",
        "full_window_screenshot_count": len(screenshots),
        "full_tile_grid_item_capture_count": len(tiles),
        "qml_warnings": gallery_report.get("qml_warnings", []),
        "tile_capture_qml_warnings": gallery_report.get("tile_capture_qml_warnings", []),
        "tile_capture_qt_logs": gallery_report.get("tile_capture_qt_logs", []),
        "native_live_data": False,
        "obsolete_vehicle_illustration_images_included": False,
        "source_sha256": gallery_report.get("source_sha256", {}),
        "bundled_files": [
            {"path": p.relative_to(VALIDATION).as_posix(), "sha256": sha256(p), "bytes": p.stat().st_size}
            for p in bundle_paths
        ],
    }
    manifest_path = VALIDATION / "bundle-manifest.json"
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    archive = VALIDATION / ZIP_NAME
    root_name = archive.stem
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as out:
        for path in bundle_paths + [manifest_path]:
            rel = path.relative_to(VALIDATION).as_posix()
            out.write(path, f"{root_name}/{rel}")

    archive_hash = sha256(archive)
    (VALIDATION / f"{ZIP_NAME}.sha256").write_text(f"{archive_hash}  {ZIP_NAME}\n", encoding="ascii")
    print(json.dumps({
        "archive": str(archive),
        "archive_sha256": archive_hash,
        "screenshots": len(screenshots),
        "tile_captures": len(tiles),
        "standalone_images": len(standalone_cards),
        "gallery_capture_utc": gallery_report["created_utc"],
        "tile_item_capture_utc": gallery_report.get("tile_capture_created_utc"),
    }, indent=2))


if __name__ == "__main__":
    main()
