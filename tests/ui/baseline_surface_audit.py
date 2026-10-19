#!/usr/bin/env python3
"""Targeted runtime regressions for baseline visible telemetry fields.

The audit deliberately fails if restored baseline fields stop rendering on
their reachable routes. It writes its evidence JSON before returning failure.
Run from the repository root with:
    QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
      .tooling/ui/bin/python tests/ui/baseline_surface_audit.py
"""

from __future__ import annotations

import hashlib
import json
import re
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

import run_ui


ROOT = Path(__file__).resolve().parents[2]
BASELINE = "f01acc553c0d938af74f4ea6a026dbc6571eb06c"
REPORT = ROOT / "docs/ui-validation/baseline-surface-audit.json"


def _read_baseline() -> str:
    return subprocess.check_output(
        ["git", "show", f"{BASELINE}:qml/Main.qml"], cwd=ROOT, text=True
    )


def _line_for(source: str, snippet: str) -> int | None:
    for index, line in enumerate(source.splitlines(), start=1):
        if snippet in line:
            return index
    return None


def _qml_texts(parent: Any) -> list[str]:
    texts: list[str] = []
    for item in run_ui._items(parent):
        try:
            value = item.property("text")
            if isinstance(value, str) and value:
                texts.append(value)
        except (RuntimeError, TypeError):
            continue
    return texts


def _visible_qml_texts(parent: Any, window_bounds: tuple[float, float, float, float]) -> list[str]:
    texts: list[str] = []
    left, top, right, bottom = window_bounds
    for item in run_ui._items(parent):
        try:
            value = item.property("text")
            if not isinstance(value, str) or not value or not item.isVisible():
                continue
            x, y, width, height = run_ui._scene_rect(item)
            if width > 0 and height > 0 and x + width > left and y + height > top and x < right and y < bottom:
                texts.append(value)
        except (RuntimeError, TypeError):
            continue
    return texts


def _visible_text_items(parent: Any, marker: str) -> list[Any]:
    """Find currently visible rendered text values containing marker."""
    matches: list[Any] = []
    for item in run_ui._items(parent):
        try:
            value = item.property("text")
            if isinstance(value, str) and marker in value and item.isVisible():
                x, y, width, height = run_ui._scene_rect(item)
                if width > 0 and height > 0:
                    matches.append(item)
        except (RuntimeError, TypeError):
            continue
    return matches


def _contains_text(texts: list[str], expected: str) -> bool:
    return any(expected in text for text in texts)


def _read_snapshot_field(engine: Any, page: Any, name: str) -> tuple[Any, list[str]]:
    errors: list[str] = []
    for expression in (f"snapshot.{name}", f"controller.telemetry.{name}", f"page.telemetry.{name}"):
        try:
            return run_ui._qml_item_value(engine, page, expression), errors
        except AssertionError as error:
            errors.append(str(error))
    return None, errors


def audit() -> dict[str, Any]:
    app = run_ui.QGuiApplication.instance() or run_ui.QGuiApplication(
        ["raceengineer-baseline-surface-audit"]
    )
    app.setApplicationName("RaceEngineer baseline surface audit")
    app.setQuitOnLastWindowClosed(False)

    baseline = _read_baseline()
    current_files = sorted((ROOT / "qml").glob("*.qml"))
    current = "\n".join(path.read_text(encoding="utf-8") for path in current_files)
    telemetry_page_source = (ROOT / "qml/TelemetryPage.qml").read_text(encoding="utf-8")
    baseline_fields = sorted(set(re.findall(r"backend\.telemetry\.([A-Za-z_]\w*)", baseline)))
    current_fields: set[str] = set()
    for pattern in (
        r"(?:backend|cockpit|vehicleData|page)\.telemetry\.([A-Za-z_]\w*)",
        r"(?:telemetryScroll|snapshot)\.([A-Za-z_]\w*)",
        r"controller\.telemetry\.([A-Za-z_]\w*)",
    ):
        current_fields.update(re.findall(pattern, current))
    missing_nested_fields = sorted(set(baseline_fields) - current_fields)

    baseline_gap_lines = {
        "ahead_name_and_gap": _line_for(baseline, 'subtitle:backend.telemetry.opponentAhead ? "Xe: "'),
        "behind_name_and_gap": _line_for(baseline, 'subtitle:backend.telemetry.opponentBehind ? "Xe: "'),
    }
    current_gap_lines = {
        "ahead_gap": _line_for(telemetry_page_source, '"opponentAheadGap"'),
        "behind_gap": _line_for(telemetry_page_source, '"opponentBehindGap"'),
    }
    current_opponent_lines = {
        "ahead_name": _line_for(telemetry_page_source, '"opponentAheadName"'),
        "behind_name": _line_for(telemetry_page_source, '"opponentBehindName"'),
        "fallback_expression": _line_for(telemetry_page_source, 'opponent.opponentName || (opponent.ahead ? "Chưa có xe trước"'),
    }
    baseline_direct_input_value_line = _line_for(
        baseline, 'title:"Bộ điều khiển DirectInput";subtitle:root.directInputStatusText();value:backend.directInputBinding'
    )
    system_source = (ROOT / "qml/SystemDevicesPage.qml").read_text(encoding="utf-8")
    engineer_source = (ROOT / "qml/EngineerSpotterPage.qml").read_text(encoding="utf-8")
    direct_input_source_lines = {
        "system_binding_label": _line_for(system_source, 'text: page.controller.directInputBinding || "Chưa gán"'),
        "engineer_binding_row": _line_for(engineer_source, 'value: page.controller.directInputBinding'),
    }

    fake, engine, root, warnings = None, None, None, None
    engine, root, fake, warnings = run_ui._qml_engine(run_ui.NEW_QML)
    root.show()
    root.resize(1280, 800)
    run_ui._pump(80)
    unique_ahead = "AUDIT-AHEAD-OPPONENT-47"
    unique_behind = "AUDIT-BEHIND-OPPONENT-62"
    fake.values["connected"] = True
    fake.values["connectionText"] = "Đã kết nối AC / ACC"
    fake.values["telemetry"] = {
        "track": "Monza",
        "position": 4,
        "currentLap": 7,
        "lapProgress": 0.625,
        "opponentAhead": unique_ahead,
        "opponentBehind": unique_behind,
        "gapAheadSeconds": 1.234,
        "gapBehindSeconds": 2.345,
        "speedKmh": 224,
        "gear": 5,
        "fuelLiters": 38.2,
        "tyreTemperatures": [81.1, 82.2, 83.3, 84.4],
        "tyrePressures": [26.1, 26.2, 26.3, 26.4],
        "brakeTemperatures": [501, 502, 503, 504],
    }
    fake.changed.emit()
    run_ui._route_to(engine, root, 3)
    run_ui._pump(100)
    telemetry_loader = run_ui._find(root, "workspaceTelemetry")
    telemetry_page = telemetry_loader.property("item")
    if telemetry_page is None or not telemetry_page.isVisible():
        raise AssertionError("Telemetry route was not an active, visible page in the QML runtime probe")
    telemetry_data = run_ui._find_scoped(telemetry_page, "telemetryDrivingData")
    telemetry_data_visible = telemetry_data.isVisible()
    telemetry_window_bounds = (0.0, 0.0, float(root.width()), float(root.height()))
    telemetry_texts = _visible_qml_texts(telemetry_page, telemetry_window_bounds)
    snapshot_ahead_raw, snapshot_ahead_errors = _read_snapshot_field(engine, telemetry_page, "opponentAhead")
    snapshot_behind_raw, snapshot_behind_errors = _read_snapshot_field(engine, telemetry_page, "opponentBehind")
    snapshot_ahead = str(snapshot_ahead_raw)
    snapshot_behind = str(snapshot_behind_raw)
    expected_gaps = {"1,234 s", "2,345 s"}
    rendered_gap_values = sorted(expected_gaps.intersection(telemetry_texts))
    rendered_opponent_names = [name for name in (unique_ahead, unique_behind)
                               if any(name in text for text in telemetry_texts)]
    opponent_items = {
        name: run_ui._find_scoped(telemetry_page, name)
        for name in ("opponentAheadName", "opponentBehindName", "opponentAheadGap", "opponentBehindGap")
    }
    opponent_item_visibility = {
        name: item.isVisible() and item.width() > 0 and item.height() > 0
        for name, item in opponent_items.items()
    }
    rendered_fallback_names = [name for name in ("Chưa có xe trước", "Chưa có xe sau")
                               if any(name in text for text in telemetry_texts)]
    telemetry_position_texts = [text for text in telemetry_texts
                                if re.fullmatch(r"P0?4", text.strip())]
    telemetry_lap_texts = [text for text in telemetry_texts
                           if text.strip() == "7" or text.strip() == "Vòng 7"]

    # Change names and gaps on the same active facade. Both the strings and
    # rendered values must update; stale text from the first snapshot fails.
    updated_ahead = "AUDIT-AHEAD-UPDATED-91"
    updated_behind = "AUDIT-BEHIND-UPDATED-38"
    fake.values["telemetry"] = {
        **fake.values["telemetry"],
        "opponentAhead": updated_ahead,
        "opponentBehind": updated_behind,
        "gapAheadSeconds": 3.456,
        "gapBehindSeconds": 4.567,
    }
    fake.changed.emit()
    run_ui._pump(80)
    updated_telemetry_texts = _visible_qml_texts(telemetry_page, telemetry_window_bounds)
    updated_gap_values = [value for value in ("3,456 s", "4,567 s")
                          if _contains_text(updated_telemetry_texts, value)]
    updated_opponent_names = [name for name in (updated_ahead, updated_behind)
                              if _contains_text(updated_telemetry_texts, name)]
    stale_opponent_names = [name for name in (unique_ahead, unique_behind)
                            if _contains_text(updated_telemetry_texts, name)]
    updated_opponent_item_texts = {
        name: str(item.property("text")) for name, item in opponent_items.items()
    }

    # Empty names exercise the exact baseline fallback branch, not merely a
    # null/uninitialized QML state.
    fake.values["telemetry"] = {
        **fake.values["telemetry"],
        "opponentAhead": "",
        "opponentBehind": "",
    }
    fake.changed.emit()
    run_ui._pump(80)
    no_opponent_texts = _visible_qml_texts(telemetry_page, telemetry_window_bounds)
    no_opponent_fallback_labels = [
        label for label in ("Chưa có xe trước", "Chưa có xe sau")
        if _contains_text(no_opponent_texts, label)
    ]
    empty_opponent_item_texts = {
        name: str(opponent_items[name].property("text"))
        for name in ("opponentAheadName", "opponentBehindName")
    }
    fake.values["telemetry"] = {
        key: value for key, value in fake.values["telemetry"].items()
        if key not in {"opponentAhead", "opponentBehind"}
    }
    fake.changed.emit()
    run_ui._pump(60)
    missing_opponent_texts = _visible_qml_texts(telemetry_page, telemetry_window_bounds)
    missing_opponent_fallback_labels = [
        label for label in ("Chưa có xe trước", "Chưa có xe sau")
        if _contains_text(missing_opponent_texts, label)
    ]

    # Position and current lap must remain visible on the baseline Telemetry
    # route after the workspace split. The Cockpit values are checked too.
    telemetry_position_items = {
        name: run_ui._find_scoped(telemetry_page, name, required=False)
        for name in ("telemetryPositionValue", "telemetryCurrentLapValue")
    }
    telemetry_position_objects = {
        name: item is not None and item.isVisible() and item.width() > 0 and item.height() > 0
        for name, item in telemetry_position_items.items()
    }
    telemetry_position_values = {
        name: str(item.property("text")) if item is not None else None
        for name, item in telemetry_position_items.items()
    }
    run_ui._route_to(engine, root, 0)
    cockpit_page = run_ui._find(root, "workspaceCockpit").property("item")
    cockpit_position_objects = {
        name: (run_ui._find_scoped(cockpit_page, name, required=False) is not None)
        for name in ("positionValue", "currentLapValue")
    }
    cockpit_values = {
        "position": str(run_ui._find_scoped(cockpit_page, "positionValue").property("text")),
        "current_lap": str(run_ui._find_scoped(cockpit_page, "currentLapValue").property("text")),
    }

    direct_input_marker = "AUDIT-DIRECTINPUT-WHEEL-BUTTON-17"
    fake.values["directInputBinding"] = direct_input_marker
    fake.values["directInputStatus"] = "DirectInput ready"
    fake.changed.emit()
    run_ui._route_to(engine, root, 5)
    system_page = run_ui._find(root, "workspaceSystemDevices").property("item")
    system_texts = _visible_qml_texts(system_page, (0.0, 0.0, float(root.width()), float(root.height())))
    system_status_items = [text for text in system_texts if "Đã sẵn sàng." in text]
    binding_label_items = _visible_text_items(system_page, direct_input_marker)
    system_binding_item = run_ui._find_scoped(system_page, "systemDirectInputBinding", required=False)
    system_shows_binding = (
        system_binding_item is not None and system_binding_item.isVisible()
        and str(system_binding_item.property("text")) == direct_input_marker
    )
    updated_binding = "AUDIT-DIRECTINPUT-UPDATED-29"
    fake.values["directInputBinding"] = updated_binding
    fake.changed.emit()
    run_ui._pump(80)
    system_texts_updated = _visible_qml_texts(system_page, (0.0, 0.0, float(root.width()), float(root.height())))
    system_binding_updates = (
        system_binding_item is not None and str(system_binding_item.property("text")) == updated_binding
        and _contains_text(system_texts_updated, updated_binding)
    )
    stale_system_binding = _contains_text(system_texts_updated, direct_input_marker)
    run_ui._route_to(engine, root, 2)
    engineer_page = run_ui._find(root, "workspaceEngineerSpotter").property("item")
    engineer_binding_item = run_ui._find_text_item(engineer_page, updated_binding, required=False)
    if engineer_binding_item is not None:
        run_ui._scroll_item_into_view(root, engineer_binding_item)
    engineer_texts = _visible_qml_texts(engineer_page, (0.0, 0.0, float(root.width()), float(root.height())))
    engineer_shows_binding = any(updated_binding in text for text in engineer_texts)

    regression_checks = {
        "initial_gap_values_rendered": rendered_gap_values == sorted(expected_gaps),
        "initial_opponent_names_rendered": sorted(rendered_opponent_names) == sorted([unique_ahead, unique_behind]),
        "opponent_name_and_gap_items_visible": all(opponent_item_visibility.values()),
        "fake_snapshot_received_opponent_names": (snapshot_ahead, snapshot_behind) == (unique_ahead, unique_behind),
        "updated_opponent_names_rendered": sorted(updated_opponent_names) == sorted([updated_ahead, updated_behind]),
        "updated_gap_values_rendered": sorted(updated_gap_values) == ["3,456 s", "4,567 s"],
        "opponent_items_update_dynamically": (
            updated_opponent_item_texts.get("opponentAheadName") == updated_ahead
            and updated_opponent_item_texts.get("opponentBehindName") == updated_behind
            and updated_opponent_item_texts.get("opponentAheadGap") == "3,456 s"
            and updated_opponent_item_texts.get("opponentBehindGap") == "4,567 s"
        ),
        "stale_opponent_names_removed": not stale_opponent_names,
        "empty_names_show_baseline_fallbacks": sorted(no_opponent_fallback_labels) == ["Chưa có xe sau", "Chưa có xe trước"],
        "fallbacks_update_on_same_name_items": empty_opponent_item_texts == {
            "opponentAheadName": "Chưa có xe trước", "opponentBehindName": "Chưa có xe sau"},
        "missing_names_show_baseline_fallbacks": sorted(missing_opponent_fallback_labels) == ["Chưa có xe sau", "Chưa có xe trước"],
        "position_visible_on_telemetry_route": telemetry_position_objects.get("telemetryPositionValue", False)
            and telemetry_position_values.get("telemetryPositionValue") == "P04",
        "current_lap_visible_on_telemetry_route": telemetry_position_objects.get("telemetryCurrentLapValue", False)
            and telemetry_position_values.get("telemetryCurrentLapValue") == "7",
        "position_and_lap_visible_on_cockpit": cockpit_values == {"position": "P04", "current_lap": "7"},
        "direct_input_binding_visible_on_system_route": system_shows_binding,
        "direct_input_binding_updates_dynamically": system_binding_updates and not stale_system_binding,
        "direct_input_binding_item_identity_retained": system_binding_item is not None,
        "direct_input_binding_remains_reachable_on_engineer_route": engineer_shows_binding,
        "no_qml_warnings": not warnings,
    }
    failed_checks = [name for name, passed in regression_checks.items() if not passed]

    audit_sources = [ROOT / "qml/Main.qml", ROOT / "qml/DrivingData.qml",
                     ROOT / "qml/SystemDevicesPage.qml", ROOT / "qml/EngineerSpotterPage.qml",
                     ROOT / "qml/TelemetryPage.qml", Path(__file__).resolve()]
    source_hashes = {
        path.relative_to(ROOT).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in audit_sources if path.exists()
    }
    result = {
        "date": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "baseline_commit": BASELINE,
        "audit_scope": "Baseline nested telemetry fields and active Telemetry route rendering; not a complete app-wide feature inventory.",
        "overall_baseline_feature_parity": not failed_checks,
        "targeted_regression_status": "PASS" if not failed_checks else "FAIL",
        "failed_regression_checks": failed_checks,
        "regression_checks": regression_checks,
        "current_source_sha256": source_hashes,
        "baseline_nested_telemetry_fields": baseline_fields,
        "current_nested_telemetry_fields": sorted(current_fields),
        "baseline_nested_fields_missing_from_all_current_qml": missing_nested_fields,
        "baseline_evidence": {
            "source_file": "qml/Main.qml",
            "telemetry_gap_rows": baseline_gap_lines,
            "expression": "subtitle = 'Xe: ' + opponent name, else 'Chưa có xe trước/sau'",
        },
        "current_source_evidence": {
            "source_file": "qml/TelemetryPage.qml",
            "gap_rows": current_gap_lines,
            "opponent_name_or_fallback_rows": current_opponent_lines,
            "opponent_fields_referenced": sorted(set(re.findall(r"opponent(?:Ahead|Behind)", current))),
            "opponent_fields_referenced_in_telemetry_page": all(
                name in telemetry_page_source for name in ("opponentAhead", "opponentBehind")),
        },
        "dynamic_qml_probe": {
            "route": "Telemetry (route 3, via mode and subnavigation clicks)",
            "window": [int(root.width()), int(root.height())],
            "active_page_object": telemetry_page.objectName(),
            "driving_data_visible": telemetry_data_visible,
            "fake_snapshot_opponent_names": [snapshot_ahead, snapshot_behind],
            "rendered_gap_values": rendered_gap_values,
            "rendered_opponent_names": rendered_opponent_names,
            "opponent_item_visibility": opponent_item_visibility,
            "updated_opponent_item_texts": updated_opponent_item_texts,
            "rendered_no-opponent_fallback_labels": rendered_fallback_names,
            "updated_fake_snapshot_opponent_names": [updated_ahead, updated_behind],
            "updated_rendered_opponent_names": updated_opponent_names,
            "updated_rendered_gap_values": updated_gap_values,
            "stale_opponent_names_after_update": stale_opponent_names,
            "no-opponent_state_rendered_fallback_labels": no_opponent_fallback_labels,
            "empty_name_item_texts": empty_opponent_item_texts,
            "missing_name_fallback_labels": missing_opponent_fallback_labels,
            "missing_name_state_texts": missing_opponent_texts,
            "telemetry_position_value_texts": telemetry_position_texts,
            "telemetry_current_lap_value_texts": telemetry_lap_texts,
            "telemetry_position_items_visible": telemetry_position_objects,
            "telemetry_position_item_values": telemetry_position_values,
            "all_telemetry_page_texts": telemetry_texts,
            "qml_warnings": warnings,
            "snapshot_expression_errors": snapshot_ahead_errors + snapshot_behind_errors,
        },
        "direct_input_binding_locality_probe": {
            "baseline_settings_route_showed_binding": True,
            "baseline_source_line": baseline_direct_input_value_line,
            "current_system_source_line": direct_input_source_lines["system_binding_label"],
            "current_engineer_source_line": direct_input_source_lines["engineer_binding_row"],
            "synthetic_binding": direct_input_marker,
            "current_system_route_status_visible": bool(system_status_items),
            "current_system_route_shows_binding": system_shows_binding,
            "current_system_binding_item_object_name": system_binding_item.objectName() if system_binding_item else None,
            "current_system_binding_label_visible_bounds":
                [list(map(round, run_ui._scene_rect(item))) for item in binding_label_items],
            "updated_synthetic_binding": updated_binding,
            "current_system_route_binding_updates_dynamically": system_binding_updates,
            "stale_system_binding_after_update": stale_system_binding,
            "current_engineer_route_shows_binding": engineer_shows_binding,
            "current_engineer_route_shows_binding_after_reveal": engineer_shows_binding,
            "current_engineer_binding_value_bounds": list(map(round, run_ui._scene_rect(engineer_binding_item))) if engineer_binding_item else None,
            "classification": "restored on System/devices and reachable on Engineer/Spotter" if system_shows_binding and engineer_shows_binding else "one or more settings-route binding surfaces remain unreachable",
        },
        "route_relocation_probe": {
            "baseline_telemetry_page_showed_position_and_current_lap": True,
            "current_telemetry_page_has_position_value_items": telemetry_position_objects,
            "current_cockpit_has_position_and_lap_items": cockpit_position_objects,
            "current_cockpit_rendered_values": cockpit_values,
            "classification": "restored on dedicated Telemetry route and retained on Cockpit" if regression_checks["position_visible_on_telemetry_route"] and regression_checks["current_lap_visible_on_telemetry_route"] else "Telemetry route regression remains",
        },
        "confirmed_gap": {
            "fields": ["opponentAhead", "opponentBehind"],
            "description": "Baseline Telemetry displays each opponent name with its numeric gap, or the matching no-opponent fallback. The active Telemetry route is dynamically checked for initial/update/empty-name states.",
            "status": "restored and dynamically verified" if all(regression_checks[name] for name in ("initial_opponent_names_rendered", "updated_opponent_names_rendered", "updated_gap_values_rendered", "empty_names_show_baseline_fallbacks", "missing_names_show_baseline_fallbacks")) else "regression check failed",
        },
        "limitations": [
            "The runtime uses the strict Python QObject facade and synthetic telemetry; no AC/ACC provider or native C++ QML process is started.",
            "Other native-only Windows, audio, provider, file-picker, renderer, and DirectInput effects are not assessed by this audit.",
        ],
    }
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    # Tear down the QML engine while its context facade is still alive. This
    # avoids shutdown-order errors from Python QObject wrappers at interpreter
    # exit and keeps the probe's warning evidence scoped to the live engine.
    root.hide()
    run_ui.ENGINE_KEEPALIVE[:] = [entry for entry in run_ui.ENGINE_KEEPALIVE
                                  if not entry or entry[0] is not engine]
    engine.deleteLater()
    run_ui.QCoreApplication.sendPostedEvents(None, run_ui.QEvent.Type.DeferredDelete)
    run_ui.QCoreApplication.processEvents()
    return result


if __name__ == "__main__":
    result = audit()
    print(json.dumps(result, ensure_ascii=False, indent=2))
    if result["failed_regression_checks"]:
        sys.exit(1)
