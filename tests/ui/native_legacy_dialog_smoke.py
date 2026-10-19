#!/usr/bin/env python3
"""Check the QML compatibility surface called by the native setup verifier.

Run from any directory with:
    QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
      .tooling/ui/bin/python tests/ui/native_legacy_dialog_smoke.py

This probes Qt meta-object invocation and the dialog property alias against the
same QML window, but does not run the native Windows executable.
"""

from __future__ import annotations

import hashlib
import json
import os
import sys
import time
import traceback
from pathlib import Path
from typing import Any

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QT_QUICK_BACKEND", "software")

from PySide6.QtCore import QCoreApplication, QEvent, QMetaObject, qInstallMessageHandler  # noqa: E402
from PySide6.QtGui import QGuiApplication  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "docs/ui-validation/release-1.0.6/native-legacy-dialog-smoke.json"


def _sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _run() -> dict[str, Any]:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import run_ui as ui  # pylint: disable=import-outside-toplevel

    app = QGuiApplication.instance() or QGuiApplication(["native-legacy-dialog-smoke"])
    app.setQuitOnLastWindowClosed(False)
    ui.QT_LOGS.clear()
    qInstallMessageHandler(ui._qt_message_handler)
    engine, root, fake, warnings = ui._qml_engine(ROOT / "qml/Main.qml")
    root.resize(880, 576)
    root.show()
    ui._pump(60)

    # Mirror main.cpp's setup sequence: setup completed, then reopened; the
    # selected provider is Google and the native verifier invokes this method.
    fake.finishSetup()
    fake.openSetup()
    fake.values.update({"setupStep": 0, "ttsBackend": "Google Gemini API",
                        "startupReady": True, "startupError": ""})
    fake.changed.emit()
    ui._pump(60)
    invoked = bool(QMetaObject.invokeMethod(root, "openGoogleVoiceDialog"))
    ui._pump(120)
    if not invoked:
        raise AssertionError("Qt meta-object could not invoke Main.openGoogleVoiceDialog()")

    ai_loader = ui._find(root, "workspaceAiVoice")
    ai_page = ai_loader.property("item")
    dialog = ui._find(root, "googleVoiceDialog")
    # QML ids inside an inline Component are not available to an external
    # QQmlExpression, so inspect the exported alias itself instead of trying
    # to reference the host's private id.
    alias_object_name = str(ui._qml_value(engine, root, "googleVoiceDialog.objectName"))
    alias_identity = alias_object_name == "googleVoiceDialog"
    alias_visible = bool(ui._qml_value(engine, root, "googleVoiceDialog.visible"))
    route = int(root.property("currentPage"))
    section = int(ai_page.property("sectionIndex")) if ai_page is not None else -1
    dialog_visible = ui._visible(dialog)
    if not alias_identity or not alias_visible or not dialog_visible:
        raise AssertionError("Native dialog alias does not point to a visible QML Dialog")
    if route != 4 or section != 2:
        raise AssertionError(f"Legacy open method did not select AI Google TTS: route={route}, section={section}")

    bounds = {name: float(ui._qml_value(engine, root, f"googleVoiceDialog.{name}"))
              for name in ("x", "y", "width", "height")}
    center_delta = [abs(bounds["x"] + bounds["width"] / 2 - float(root.width()) / 2),
                    abs(bounds["y"] + bounds["height"] / 2 - float(root.height()) / 2)]
    if (center_delta[0] > 1 or center_delta[1] > 1 or bounds["x"] < 0 or bounds["y"] < 0
            or bounds["x"] + bounds["width"] > float(root.width()) + 1
            or bounds["y"] + bounds["height"] > float(root.height()) + 1):
        raise AssertionError(f"Legacy dialog is not centered within the 880x576 window: {bounds}")
    if warnings:
        raise AssertionError(f"QML emitted warnings: {warnings}")

    ui._qml_value(engine, root, "googleVoiceDialog.close()")
    fake.values["setupVisible"] = False
    fake.changed.emit()
    root.hide()
    engine.deleteLater()
    QCoreApplication.sendPostedEvents(None, QEvent.Type.DeferredDelete)
    QCoreApplication.processEvents()
    ui.ENGINE_KEEPALIVE.clear()
    qt_logs = list(ui.QT_LOGS)
    if qt_logs:
        raise AssertionError(f"Qt emitted logs during native compatibility smoke: {qt_logs}")
    return {
        "created_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "status": "PASS",
        "native_source_contract": "src/main.cpp --verify-setup-ui invokes openGoogleVoiceDialog and reads googleVoiceDialog as QObject*",
        "qt_meta_object_invocation": invoked,
        "dialog_property_alias_identity": alias_identity,
        "dialog_alias_object_name": alias_object_name,
        "dialog_alias_visible": alias_visible,
        "dialog_visible": dialog_visible,
        "route": route,
        "ai_voice_section": section,
        "dialog_bounds": {key: round(value, 2) for key, value in bounds.items()},
        "center_delta": [round(value, 3) for value in center_delta],
        "qml_warnings": warnings,
        "qt_logs": qt_logs,
        "scope_limit": "QML window and Qt meta-object probe with Python facade; native Windows executable was not run.",
        "source_sha256": {path: _sha(ROOT / path) for path in
                          ("src/main.cpp", "qml/Main.qml", "qml/GoogleVoiceDialog.qml",
                           "tests/ui/native_legacy_dialog_smoke.py")},
    }


def main() -> int:
    OUT.parent.mkdir(parents=True, exist_ok=True)
    try:
        report = _run()
    except Exception as exc:
        report = {"created_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                  "status": "FAIL", "error": f"{type(exc).__name__}: {exc}",
                  "traceback": traceback.format_exc().splitlines()[-10:]}
        OUT.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        raise
    OUT.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"report": OUT.relative_to(ROOT).as_posix(), **report}, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
