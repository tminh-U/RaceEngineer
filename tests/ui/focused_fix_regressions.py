#!/usr/bin/env python3
"""Focused regression checks for the three UI fixes requested on 2026-10-19.

This stays separate from the historical 22/25 edge audit and the full suite.
It writes a new JSON report and targeted screenshots under
docs/ui-validation/followup-fix/.
"""

from __future__ import annotations

import hashlib
import json
import os
import re
import sys
import time
import traceback
from pathlib import Path
from typing import Any, Callable

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QT_QUICK_BACKEND", "software")

from PySide6.QtCore import QCoreApplication, qInstallMessageHandler  # noqa: E402
from PySide6.QtGui import QGuiApplication  # noqa: E402
from PySide6.QtTest import QTest  # noqa: E402

import run_ui as ui  # noqa: E402
from capture_review import _fixture  # noqa: E402


ROOT = ui.ROOT
OUT = ROOT / "docs/ui-validation/followup-fix"
REPORT_PATH = OUT / "focused-fix-regressions.json"
SCREENSHOT_DIR = OUT
WINDOW_SIZE = (880, 576)
LONG_STARTUP_ERROR = (
    "Voice model initialization failed while loading the selected Vietnamese runtime. "
    "The model directory is unavailable; check the configured installation path, "
    "disk permissions, and whether another process is updating the model. " * 6
)
WARNINGS: list[str] = []
VOICE_WARNINGS: list[str] = []
RESULTS: dict[str, dict[str, Any]] = {}
SUCCESS_SHOTS: dict[str, str] = {}


def _message_handler(mode: Any, context: Any, message: str) -> None:
    WARNINGS.append(str(message))


def _sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _set_values(fake: Any, values: dict[str, Any]) -> None:
    fake.values.update(values)
    fake.changed.emit()
    ui._pump(50)


def _text(item: Any) -> str:
    return str(item.property("text") or "")


def _rect(item: Any) -> list[float]:
    return list(ui._scene_rect(item))


def _inside(item: Any, parent: Any, label: str, tolerance: float = 2.0) -> None:
    if not ui._rects_inside(ui._scene_rect(item), ui._scene_rect(parent), tolerance=tolerance):
        raise AssertionError(f"{label} escapes parent: item={_rect(item)} parent={_rect(parent)}")


def _window_bounds(root: Any) -> tuple[float, float, float, float]:
    return 0.0, 0.0, float(root.width()), float(root.height())


def _scroll_ancestor(item: Any) -> Any | None:
    return next((ancestor for ancestor in ui.iter_ancestors(item)
                 if "Flickable" in ancestor.metaObject().className()), None)


def _capture(root: Any, name: str) -> str:
    OUT.mkdir(parents=True, exist_ok=True)
    path = SCREENSHOT_DIR / name
    ui._capture(root, path)
    rel = path.relative_to(ROOT).as_posix()
    return rel


def _run_case(name: str, root: Any, body: Callable[[], dict[str, Any]]) -> None:
    try:
        RESULTS[name] = {"status": "PASS", "evidence": body()}
    except Exception as exc:
        filename = re.sub(r"[^a-zA-Z0-9_-]+", "-", name).strip("-") + "-failure.png"
        shot = None
        try:
            shot = _capture(root, filename)
        except Exception as capture_error:
            shot = f"capture failed: {capture_error}"
        RESULTS[name] = {"status": "FAIL", "error": f"{type(exc).__name__}: {exc}",
                         "traceback": traceback.format_exc().splitlines()[-8:],
                         "screenshot": shot}


def _start_engine(backend_name: str = "VieNeu-TTS") -> tuple[Any, Any, Any, list[str]]:
    engine, root, fake, qml_warnings = ui._qml_engine(ui.NEW_QML)
    _fixture(fake)
    _set_values(fake, {"startupReady": True, "startupError": "", "ttsBackend": backend_name,
                       "ttsBenchmarkRunning": False, "ttsBenchmarkRestoring": False,
                       "connected": True, "apiConfigured": True,
                       "apiBaseUrl": "http://localhost:8080/v1", "apiModel": "race-engineer-model",
                       "apiKey": "mock-key", "apiState": "Connected", "apiDetail": "Connected"})
    root.resize(*WINDOW_SIZE)
    root.show()
    ui._pump(80)
    return engine, root, fake, qml_warnings


def _assert_fixed_item(root: Any, notice: Any, item: Any, name: str) -> list[int]:
    if item is None or not item.isVisible() or item.width() <= 1 or item.height() <= 1:
        raise AssertionError(f"Startup {name} is hidden or collapsed: {None if item is None else _rect(item)}")
    _inside(item, notice, f"Startup {name}")
    if not ui._rects_inside(ui._scene_rect(item), _window_bounds(root), tolerance=2):
        raise AssertionError(f"Startup {name} is outside the window: {_rect(item)}")
    return list(map(round, _rect(item)))


def _startup_notice_case(root: Any, fake: Any) -> dict[str, Any]:
    root.resize(*WINDOW_SIZE)
    _set_values(fake, {"startupReady": False, "startupProgress": 0.42,
                       "startupError": LONG_STARTUP_ERROR})
    notice = ui._find(root, "startupNotice")
    notice.setProperty("dismissed", False)
    ui._pump(50)
    actual_size = [round(root.width()), round(root.height())]
    if actual_size != list(WINDOW_SIZE):
        raise AssertionError(f"Test window was clamped; requested {WINDOW_SIZE}, actual {actual_size}")
    notice_rect = ui._scene_rect(notice)
    if not ui._rects_inside(notice_rect, _window_bounds(root), tolerance=2):
        raise AssertionError(f"Long startup notice is outside the window: notice={notice_rect}, window={_window_bounds(root)}")

    title = ui._find_scoped(notice, "startupErrorTitle")
    scroll = ui._find_scoped(notice, "startupErrorScroll")
    label = ui._find_scoped(notice, "startupErrorLabel")
    retry = ui._find_scoped(notice, "startupRetryButton")
    close = ui._find_scoped(notice, "startupCloseButton")
    if _text(label) != LONG_STARTUP_ERROR:
        raise AssertionError(f"Startup error label changed: {len(_text(label))}/{len(LONG_STARTUP_ERROR)} chars")
    if not scroll.isVisible() or not ui._rects_inside(_rect(scroll), notice_rect, tolerance=2):
        raise AssertionError(f"Startup error ScrollView is unavailable/outside card: {_rect(scroll)}")

    title_before = _assert_fixed_item(root, notice, title, "title")
    retry_before = _assert_fixed_item(root, notice, retry, "retry button")
    close_before = _assert_fixed_item(root, notice, close, "close button")
    flickable = _scroll_ancestor(label)
    if flickable is None:
        raise AssertionError("Long startup error is not inside a scrollable viewport")
    viewport = ui._scene_rect(flickable)
    content_height = float(flickable.property("contentHeight"))
    viewport_height = float(flickable.height())
    origin_y = float(flickable.property("originY"))
    max_y = max(origin_y, content_height - viewport_height)
    if max_y <= origin_y + 10:
        raise AssertionError(f"Long error produced no vertical scroll range: content={content_height}, viewport={viewport_height}")

    flickable.setProperty("contentY", origin_y)
    ui._pump(30)
    first_line = ui._scene_rect(label)
    first_edge = (first_line[0], first_line[1], first_line[2], min(14.0, first_line[3]))
    if not ui._rects_inside(first_edge, ui._scene_rect(flickable), tolerance=3):
        raise AssertionError(f"First startup error line is not reachable: first={first_edge}, viewport={ui._scene_rect(flickable)}")
    startup_top = _capture(root, "startup-error-long-880x576.png")

    flickable.setProperty("contentY", max_y)
    ui._pump(30)
    last_line = ui._scene_rect(label)
    last_edge = (last_line[0], last_line[1] + max(0.0, last_line[3] - 14.0), last_line[2], min(14.0, last_line[3]))
    if not ui._rects_inside(last_edge, ui._scene_rect(flickable), tolerance=3):
        raise AssertionError(f"Last startup error line is not reachable: last={last_edge}, viewport={ui._scene_rect(flickable)}, maxY={max_y}")

    title_after = list(map(round, _rect(title)))
    retry_after = list(map(round, _rect(retry)))
    close_after = list(map(round, _rect(close)))
    if (title_after, retry_after, close_after) != (title_before, retry_before, close_before):
        raise AssertionError("Startup title/retry/close moved when scrolling the error text")

    ui._click(root, close)
    if not bool(notice.property("dismissed")) or notice.isVisible():
        raise AssertionError("Close button did not dismiss the startup notice")

    notice.setProperty("dismissed", False)
    ui._pump(20)
    calls_before = sum(name == "retryStartup" for name, _ in fake.calls)
    ui._click(root, retry)
    calls_after = sum(name == "retryStartup" for name, _ in fake.calls)
    if calls_after != calls_before + 1:
        raise AssertionError(f"Retry button did not call retryStartup once: {calls_before}->{calls_after}")

    # Short error and no-error startup states should not inherit the tall card.
    notice.setProperty("dismissed", False)
    _set_values(fake, {"startupReady": True, "startupError": "Model directory not found."})
    ui._pump(30)
    short_height = float(notice.height())
    short_retry = ui._find_scoped(notice, "startupRetryButton")
    _assert_fixed_item(root, notice, short_retry, "short-error retry button")
    short_rect = ui._scene_rect(notice)
    if short_height > 360 or not ui._rects_inside(short_rect, _window_bounds(root), tolerance=2):
        raise AssertionError(f"Short startup error is not compact/in-window: height={short_height}, rect={short_rect}")

    _set_values(fake, {"startupReady": False, "startupError": ""})
    ui._pump(30)
    no_error_height = float(notice.height())
    if no_error_height > 300 or no_error_height >= short_height:
        raise AssertionError(f"No-error startup notice should remain compact: no_error={no_error_height}, short_error={short_height}")
    if bool(ui._find_scoped(notice, "startupRetryButton").isVisible()):
        raise AssertionError("Retry button should be hidden when there is no startup error")
    if not ui._find_scoped(notice, "startupCloseButton").isVisible():
        raise AssertionError("Close button should remain available during no-error startup progress")
    return {"actual_window_size": actual_size, "long_error_chars": len(LONG_STARTUP_ERROR),
            "notice_bounds": list(map(round, notice_rect)), "scroll_view_bounds": list(map(round, _rect(scroll))),
            "scroll_content_height": round(content_height, 2), "scroll_view_height": round(viewport_height, 2),
            "first_line_reachable": True, "last_line_reachable": True,
            "fixed_bounds_before_after_scroll": {"title": [title_before, title_after],
                                                   "retry": [retry_before, retry_after],
                                                   "close": [close_before, close_after]},
            "retry_invocations": calls_after - calls_before, "close_dismissed": True,
            "short_error_height": round(short_height, 2), "no_error_height": round(no_error_height, 2),
            "success_screenshots": [startup_top]}


def _api_status_cases(engine: Any, root: Any, fake: Any) -> dict[str, Any]:
    root.resize(1280, 800)
    ui._pump(30)
    _set_values(fake, {"startupReady": True, "startupError": "", "apiConfigured": True,
                       "apiBaseUrl": "http://localhost:8080/v1", "apiModel": "missing-model",
                       "apiKey": "mock-key"})
    ui._route_to(engine, root, 4, direct=True)
    ai = ui._find(root, "aiVoicePage")
    ai.setProperty("sectionIndex", 0)
    summary = ui._find_scoped(ai, "apiStatusSummaryLabel")
    detail = ui._find_scoped(ai, "apiStatusDetailLabel")
    status_strip = ui._find_scoped(ai, "apiStatusStrip")
    observed: dict[str, Any] = {}

    for api_state, provider_detail, expected_detail in (
        ("Connected", "Connected", "Máy chủ đã phản hồi; mô hình khả dụng."),
        ("Connected", "Model available", "Máy chủ đã phản hồi; mô hình khả dụng."),
        ("Connected", "Connected — model available", "Máy chủ đã phản hồi; mô hình khả dụng."),
        ("ProviderError", "Connected, but model 'missing-model' is unavailable",
         "Connected, but model 'missing-model' is unavailable"),
        ("ProviderError", "Connected to host, but model discovery timed out",
         "Connected to host, but model discovery timed out"),
    ):
        _set_values(fake, {"apiState": api_state, "apiDetail": provider_detail})
        actual = _text(detail)
        if actual != expected_detail:
            raise AssertionError(f"API detail mapping mismatch for {api_state}/{provider_detail!r}: {actual!r} != {expected_detail!r}")
        observed[f"{api_state}:{provider_detail}"] = {"summary": _text(summary), "displayed_detail": actual}
        if "unavailable" in provider_detail.lower() and "mô hình khả dụng" in actual:
            raise AssertionError(f"Unavailable model error was translated as success: {provider_detail!r} => {actual!r}")
        if provider_detail.startswith("Connected to host") and actual != provider_detail:
            raise AssertionError(f"Provider error beginning with Connected was swallowed: {provider_detail!r} => {actual!r}")
    _set_values(fake, {"apiState": "ProviderError",
                       "apiDetail": "Connected, but model 'missing-model' is unavailable"})
    unavailable_shot = _capture(root, "api-unavailable-model-1280x800.png")

    _set_values(fake, {"apiState": "Connected", "apiDetail": "Connected"})
    if _text(detail) != "Máy chủ đã phản hồi; mô hình khả dụng.":
        raise AssertionError(f"Canonical Connected response was not localized as success: {_text(detail)!r}")
    success_shot = _capture(root, "api-connected-success-1280x800.png")
    if not status_strip.isVisible():
        raise AssertionError("API status strip is not visible")
    return {"cases": observed, "unavailable_model_screenshot": unavailable_shot,
            "success_detail": _text(detail),
            "success_screenshot": success_shot,
            "status_strip_bounds": list(map(round, _rect(status_strip)))}


def _voice_case_for_backend(backend_name: str) -> dict[str, Any]:
    expected = 2 if backend_name == "Google Gemini API" else 1
    engine, root, fake, qml_warnings = _start_engine(backend_name)
    VOICE_WARNINGS.extend(qml_warnings)
    loader = ui._find(root, "workspaceAiVoice")
    if loader.property("item") is not None:
        raise AssertionError(f"AI page was expected to be unloaded on first voice-shortcut visit for {backend_name}")

    ui._route_to(engine, root, 2, direct=True)
    engineer = ui._find(root, "engineerSpotterPage")
    shortcut = ui._find_scoped(engineer, "openAiVoicePageButton")
    ui._click(root, shortcut)
    ai = ui._find(root, "aiVoicePage")
    first_index = int(ai.property("sectionIndex"))
    if int(root.property("currentPage")) != 4 or first_index != expected:
        raise AssertionError(f"First voice shortcut for {backend_name} selected tab {first_index}; expected {expected}")
    first_shot = _capture(root, f"voice-shortcut-first-{expected}-{backend_name.replace(' ', '-')}.png")

    ai.setProperty("sectionIndex", 0)
    ui._pump(20)
    endpoint = ui._find_scoped(ai, "apiEndpointField")
    draft = f"http://unsaved-{expected}.local/v1"
    ui._replace_text(root, endpoint, draft)
    if _text(endpoint) != draft:
        raise AssertionError(f"Could not create the AI endpoint draft before cached shortcut: {_text(endpoint)!r}")

    ui._route_to(engine, root, 2, direct=True)
    _set_values(fake, {"ttsBackend": backend_name})
    engineer = ui._find(root, "engineerSpotterPage")
    shortcut = ui._find_scoped(engineer, "openAiVoicePageButton")
    ui._click(root, shortcut)
    loaded_index = int(ai.property("sectionIndex"))
    if int(root.property("currentPage")) != 4 or loaded_index != expected:
        raise AssertionError(f"Cached voice shortcut for {backend_name} selected tab {loaded_index}; expected {expected}")
    ai.setProperty("sectionIndex", 0)
    ui._pump(20)
    if _text(endpoint) != draft:
        raise AssertionError(f"Cached voice shortcut discarded the API endpoint draft: {_text(endpoint)!r} != {draft!r}")
    ai.setProperty("sectionIndex", expected)
    ui._pump(20)
    loaded_shot = _capture(root, f"voice-shortcut-cached-{expected}-{backend_name.replace(' ', '-')}.png")
    return {"backend": backend_name, "expected_section": expected,
            "first_entry_section": first_index, "cached_entry_section": loaded_index,
            "first_ai_page_was_lazy": True, "cached_endpoint_draft_preserved": draft,
            "first_visit_screenshot": first_shot, "cached_visit_screenshot": loaded_shot,
            "qml_warnings": qml_warnings}


def _voice_shortcuts() -> dict[str, Any]:
    return {backend: _voice_case_for_backend(backend)
            for backend in ("VieNeu-TTS", "Google Gemini API")}


def _write_report(qml_warnings: list[str]) -> dict[str, Any]:
    OUT.mkdir(parents=True, exist_ok=True)
    source_names = (
        "qml/Main.qml", "qml/StartupNotice.qml", "qml/LlmConnectionForm.qml",
        "qml/EngineerSpotterPage.qml", "qml/AiVoicePage.qml", "tests/ui/run_ui.py",
        "tests/ui/fake_application.py", "tests/ui/focused_fix_regressions.py",
    )
    report = {
        "created_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "scope": "Focused regressions for startup error layout, API diagnostic mapping, and Engineer voice shortcut routing.",
        "historical_audit": "tests/ui/artifacts/edge_surface_audit.json (pre-fix 22/25; preserved)",
        "renderer": {"python": sys.version.split()[0], "pyside6": "6.8.3", "qt": "6.8.3",
                     "platform": "offscreen", "quick_backend": "software"},
        "window_size": list(WINDOW_SIZE),
        "source_sha256": {name: _sha(ROOT / name) for name in source_names},
        "qml_engine_warnings": qml_warnings + VOICE_WARNINGS,
        "qt_log_warnings": WARNINGS + ui.QT_LOGS,
        "cases": RESULTS,
        "success_screenshots": SUCCESS_SHOTS,
        "failure_count": sum(item["status"] == "FAIL" for item in RESULTS.values()),
        "native_limits": [
            "This run exercises QML against the repository's strict Python facade; it does not call real provider services, Windows startup/shutdown handlers, audio, or simulator devices.",
        ],
    }
    REPORT_PATH.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return {"report": str(REPORT_PATH), "failure_count": report["failure_count"],
            "cases": {name: item["status"] for name, item in RESULTS.items()},
            "warnings": report["qml_engine_warnings"] + report["qt_log_warnings"]}


def run() -> dict[str, Any]:
    app = QGuiApplication.instance() or QGuiApplication(["raceengineer-focused-fix-regressions"])
    app.setQuitOnLastWindowClosed(False)
    app.setApplicationVersion("1.0.5")
    qInstallMessageHandler(_message_handler)
    ui.QT_LOGS.clear()

    engine, root, fake, qml_warnings = _start_engine()
    _run_case("startup_notice_scroll_and_pinned_actions", root,
              lambda: _startup_notice_case(root, fake))
    _run_case("api_diagnostic_mapping", root,
              lambda: _api_status_cases(engine, root, fake))

    try:
        SUCCESS_SHOTS["startup"] = RESULTS["startup_notice_scroll_and_pinned_actions"].get("evidence", {}).get("success_screenshots", [None])[0]
        if SUCCESS_SHOTS["startup"] is None:
            SUCCESS_SHOTS.pop("startup", None)
    except Exception:
        pass
    try:
        SUCCESS_SHOTS["api_success"] = RESULTS["api_diagnostic_mapping"].get("evidence", {}).get("success_screenshot")
        if SUCCESS_SHOTS["api_success"] is None:
            SUCCESS_SHOTS.pop("api_success", None)
    except Exception:
        pass

    _run_case("voice_shortcut_first_and_cached_draft_preservation", root, _voice_shortcuts)
    return _write_report(qml_warnings)


if __name__ == "__main__":
    print(json.dumps(run(), ensure_ascii=False, indent=2))
