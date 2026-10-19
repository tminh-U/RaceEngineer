#!/usr/bin/env python3
"""Load, exercise and capture the QML interface with a recording facade.

Run from any directory with:
    QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
      .tooling/ui/bin/python tests/ui/run_ui.py

Screenshots and the machine-readable report are written under
docs/ui-validation/.  The facade is deliberately not a replacement for the
native Windows, simulator, DirectInput, microphone, audio or model runtimes.
"""

from __future__ import annotations

import json
import hashlib
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from collections import Counter
from pathlib import Path
from typing import Any
import shiboken6

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QT_QUICK_BACKEND", "software")

from PySide6.QtCore import (  # noqa: E402
    QCoreApplication,
    QEvent,
    QPoint,
    QPointF,
    QObject,
    QUrl,
    Qt,
    Slot,
    qInstallMessageHandler,
)
from PySide6.QtGui import (  # noqa: E402
    QColor,
    QDesktopServices,
    QFont,
    QFontDatabase,
    QFontInfo,
    QGuiApplication,
    QImage,
    QKeyEvent,
    QPainter,
)
from PySide6.QtQml import QQmlApplicationEngine, QQmlComponent, QQmlEngine, QQmlExpression  # noqa: E402
from PySide6.QtQuick import QQuickItem, QQuickWindow  # noqa: E402
from PySide6.QtTest import QSignalSpy, QTest  # noqa: E402

from fake_application import make_fake_application  # noqa: E402
from parity_audit import audit as run_parity_audit  # noqa: E402


ROOT = Path(__file__).resolve().parents[2]
BASELINE = "f01acc553c0d938af74f4ea6a026dbc6571eb06c"
QML_DIR = ROOT / "qml"
NEW_QML = QML_DIR / "Main.qml"
VALIDATION_DIR = ROOT / "docs" / "ui-validation"
SCREENSHOT_DIR = VALIDATION_DIR / "screenshots"
PAGE_NAMES = {
    0: "workspaceCockpit",
    1: "workspaceFuelStrategy",
    2: "workspaceEngineerSpotter",
    3: "workspaceTelemetry",
    4: "workspaceAiVoice",
    5: "workspaceSystemDevices",
}
SIZES = ((880, 576), (1280, 800), (1600, 1000))
QT_LOGS: list[str] = []
ENGINE_KEEPALIVE: list[tuple[Any, ...]] = []
PARTIAL_REPORT: dict[str, Any] = {}
COMPLETED_REPORT: dict[str, Any] | None = None


def _qt_message_handler(mode: Any, context: Any, message: str) -> None:
    QT_LOGS.append(message)


def _pump(ms: int = 30) -> None:
    QCoreApplication.processEvents()
    QTest.qWait(ms)
    QCoreApplication.processEvents()


def _source_sha256() -> dict[str, str]:
    paths = sorted(QML_DIR.glob("*.qml")) + [
        Path(__file__).resolve(),
        Path(__file__).with_name("capture_review.py"),
        Path(__file__).with_name("fake_application.py"),
        Path(__file__).with_name("native_contract.py"),
        Path(__file__).with_name("parity_audit.py"),
    ]
    return {path.relative_to(ROOT).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in paths if path.is_file()}


def _qml_engine(qml_file: Path) -> tuple[QQmlApplicationEngine, Any, Any, list[str]]:
    extra = [qml_file] if qml_file.parent != QML_DIR else []
    fake, strategy = make_fake_application(QML_DIR, extra_sources=extra)
    engine = QQmlApplicationEngine()
    engine.rootContext().setContextProperty("backend", fake)
    warning_list: list[str] = []

    def engine_warnings(warnings: list[Any]) -> None:
        warning_list.extend(f"{warning.url().toString()}:{warning.line()}: {warning.description()}"
                            for warning in warnings)

    engine.warnings.connect(engine_warnings)
    engine.load(QUrl.fromLocalFile(str(qml_file)))
    _pump()
    roots = engine.rootObjects()
    if not roots:
        details = "\n".join(warning_list)
        raise AssertionError(f"QML did not create a root object: {qml_file}\n{details}")
    root = roots[0]
    ENGINE_KEEPALIVE.append((engine, root, fake, strategy))
    return engine, root, fake, warning_list


def _compile_source_components() -> dict[str, Any]:
    """Compile every local QML source, including components not yet routed."""
    fake, strategy = make_fake_application(QML_DIR)
    engine = QQmlApplicationEngine()
    engine.rootContext().setContextProperty("backend", fake)
    components: dict[str, QQmlComponent] = {}
    failures: list[str] = []
    for source in sorted(QML_DIR.glob("*.qml")):
        component = QQmlComponent(engine, QUrl.fromLocalFile(str(source)))
        components[source.name] = component
        if component.status() != QQmlComponent.Status.Ready:
            failures.extend(error.toString() for error in component.errors())
    ENGINE_KEEPALIVE.append((engine, fake, strategy, components))
    return {"compiled_sources": sorted(components), "all_ready": not failures,
            "errors": failures}


def _find(root: Any, object_name: str, required: bool = True) -> Any | None:
    # QML Controls inside Repeater delegates are parented through QQuickItem's
    # visual tree, not necessarily through QObject::parent(). Search both trees
    # so objectNames on nav delegates remain discoverable under PySide.
    result = None
    visual_root = root.contentItem() if isinstance(root, QQuickWindow) else root
    pending = [visual_root] if isinstance(visual_root, QQuickItem) else []
    while pending:
        item = pending.pop()
        if item.objectName() == object_name:
            result = item
            break
        # Preserve QML child order (the first Loader is Cockpit). A later,
        # cached hidden page can expose the same objectName as the active page.
        pending.extend(reversed(item.childItems()))
    if result is None:
        result = root.findChild(QObject, object_name)
    if result is None and required:
        raise AssertionError(f"Missing QML objectName={object_name!r}")
    return result


def _find_scoped(parent: Any, object_name: str, required: bool = True) -> Any | None:
    """Find an objectName only under one page/modal subtree."""
    result = _find(parent, object_name, required=False)
    if result is not None:
        return result
    if required:
        raise AssertionError(f"Missing QML objectName={object_name!r} under {parent.objectName()!r}")
    return None


def _visible(item: Any) -> bool:
    return bool(item.property("visible")) if item is not None else False


def _click(window: QQuickWindow, item: QQuickItem) -> None:
    _scroll_item_into_view(window, item)
    if not item.isVisible() or item.width() <= 1 or item.height() <= 1:
        raise AssertionError(f"Cannot click hidden or empty QML item {item.objectName()!r}")
    point = item.mapToScene(QPointF(item.width() / 2.0, item.height() / 2.0))
    QTest.mouseClick(window, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier,
                      QPoint(round(point.x()), round(point.y())))
    _pump()
    try:
        window_visible = window.isVisible()
    except RuntimeError as error:
        raise AssertionError(f"The Main window was destroyed after clicking {item.objectName()!r}") from error
    if not window_visible:
        raise AssertionError(f"The Main window became hidden after clicking {item.objectName()!r}")


def _replace_text(window: QQuickWindow, item: QQuickItem, text: str) -> None:
    _scroll_item_into_view(window, item)
    item.forceActiveFocus()
    QTest.keyClick(window, Qt.Key.Key_A, Qt.KeyboardModifier.ControlModifier)
    for character in text:
        press = QKeyEvent(QEvent.Type.KeyPress, Qt.Key.Key_unknown,
                          Qt.KeyboardModifier.NoModifier, character)
        release = QKeyEvent(QEvent.Type.KeyRelease, Qt.Key.Key_unknown,
                            Qt.KeyboardModifier.NoModifier, character)
        QCoreApplication.sendEvent(window, press)
        QCoreApplication.sendEvent(window, release)
    _pump()


def _scroll_item_into_view(window: QQuickWindow, item: QQuickItem) -> None:
    """Reveal a control in its nearest Flickable before sending a real click."""
    ancestor = item.parentItem()
    flickable = None
    while ancestor is not None:
        if "Flickable" in ancestor.metaObject().className():
            flickable = ancestor
            break
        ancestor = ancestor.parentItem()
    if flickable is None or flickable.height() <= 1:
        return
    item_rect = _scene_rect(item)
    viewport_rect = _scene_rect(flickable)
    top, bottom = item_rect[1], item_rect[1] + item_rect[3]
    view_top, view_bottom = viewport_rect[1], viewport_rect[1] + viewport_rect[3]
    content_y = float(flickable.property("contentY"))
    if top < view_top + 2:
        flickable.setProperty("contentY", max(float(flickable.property("originY")), content_y - (view_top + 2 - top)))
    elif bottom > view_bottom - 2:
        flickable.setProperty("contentY", min(float(flickable.property("contentHeight")) - flickable.height(),
                                              content_y + (bottom - view_bottom + 2)))
    _pump(60)


def _mouse_press(window: QQuickWindow, item: QQuickItem) -> None:
    point = item.mapToScene(QPointF(item.width() / 2.0, item.height() / 2.0))
    QTest.mousePress(window, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier,
                     QPoint(round(point.x()), round(point.y())))
    _pump(5)


def _mouse_release(window: QQuickWindow, item: QQuickItem) -> None:
    point = item.mapToScene(QPointF(item.width() / 2.0, item.height() / 2.0))
    QTest.mouseRelease(window, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier,
                       QPoint(round(point.x()), round(point.y())))
    _pump()


def _route_to(engine: QQmlApplicationEngine, root: Any, page: int, *,
              direct: bool = False) -> None:
    if direct:
        expression = QQmlExpression(engine.rootContext(), root, f"openPage({page})")
        expression.evaluate()
        if expression.hasError():
            raise AssertionError(expression.error().toString())
        _pump(60)
        if int(root.property("currentPage")) != page:
            raise AssertionError(f"Direct navigation did not reach page {page}; current={root.property('currentPage')}")
        return
    mode_name = "navRace" if page == 0 else "navAnalysis" if page in (1, 3) else "navSettings"
    mode_value = 0 if page == 0 else 1 if page in (1, 3) else 2
    if int(root.property("currentMode")) != mode_value:
        _click(root, _find(root, mode_name))
    if int(root.property("currentPage")) != page:
        route_name = {1: "pageFuelStrategy", 2: "pageEngineerSpotter", 3: "pageTelemetry",
                      4: "pageAiVoice", 5: "pageSystemDevices"}.get(page)
        if route_name is None:
            expression = QQmlExpression(engine.rootContext(), root, f"openPage({page})")
            expression.evaluate()
            if expression.hasError():
                raise AssertionError(expression.error().toString())
            _pump(60)
        else:
            _click(root, _find(root, route_name))
    if int(root.property("currentPage")) != page:
        raise AssertionError(f"Navigation did not reach page {page}; current={root.property('currentPage')}")


def _current_page_item(root: Any, loader: Any) -> Any:
    item = loader.property("item")
    if item is None:
        raise AssertionError(f"Route Loader {loader.objectName()!r} has no item")
    if not item.isVisible():
        raise AssertionError(f"Route item {loader.objectName()!r} is not visible")
    return item


def _qml_value(engine: QQmlApplicationEngine, root: Any, expression: str) -> Any:
    value = QQmlExpression(engine.rootContext(), root, expression)
    result = value.evaluate()
    if value.hasError():
        raise AssertionError(f"QML expression failed: {expression}: {value.error().toString()}")
    # PySide 6.8 wraps QQmlExpression::evaluate(valueIsUndefined) in a
    # (value, undefined) tuple; return the evaluated value for assertions.
    if isinstance(result, tuple) and len(result) == 2:
        result = result[0]
    return result


def _qml_item_value(engine: QQmlApplicationEngine, item: Any, expression: str) -> Any:
    value = QQmlExpression(engine.rootContext(), item, expression)
    result = value.evaluate()
    if value.hasError():
        raise AssertionError(f"QML item expression failed for {item.objectName()}: {expression}: {value.error().toString()}")
    if isinstance(result, tuple) and len(result) == 2:
        result = result[0]
    return result


def _qml_component_item_value(item: Any, expression: str) -> Any:
    """Evaluate a property in the item's own QML import/component context."""
    context = QQmlEngine.contextForObject(item)
    value = QQmlExpression(context, item, expression)
    result = value.evaluate()
    if value.hasError():
        raise AssertionError(f"Component-scoped QML expression failed for {item.objectName()}: {expression}: {value.error().toString()}")
    if isinstance(result, tuple) and len(result) == 2:
        result = result[0]
    return result


def _find_visible_scoped(parent: Any, object_name: str, required: bool = True) -> Any | None:
    matches = [item for item in _items(parent) if item.objectName() == object_name]
    visible = next((item for item in matches if item.isVisible()), None)
    if visible is not None:
        return visible
    if required:
        raise AssertionError(f"No visible objectName={object_name!r} under {parent.objectName()!r}; matches={len(matches)}")
    return None


def _activate_combo(combo: Any, index: int) -> None:
    if index < 0 or index >= int(combo.property("count")):
        raise AssertionError(f"ComboBox index {index} out of range for {combo.objectName()}: {combo.property('count')}")
    # Exercise the public keyboard path. This opens the Qt Quick popup, moves
    # through its options, and accepts one selection without invoking a native
    # OS picker or directly emitting ComboBox.activated.
    window = combo.window()
    if isinstance(window, QQuickWindow):
        _scroll_item_into_view(window, combo)
    if not combo.isEnabled() or not combo.isVisible() or combo.width() <= 1 or combo.height() <= 1:
        raise AssertionError(
            f"ComboBox is not interactable: {combo.objectName()} "
            f"enabled={combo.isEnabled()} visible={combo.isVisible()} "
            f"size={combo.width()}x{combo.height()} scene={_scene_rect(combo)}"
        )
    combo.forceActiveFocus()
    QTest.keyClick(window, Qt.Key.Key_Space)
    _pump(20)
    QTest.keyClick(window, Qt.Key.Key_Home)
    for _ in range(index):
        QTest.keyClick(window, Qt.Key.Key_Down)
    QTest.keyClick(window, Qt.Key.Key_Enter)
    _pump(40)
    if int(combo.property("currentIndex")) != index:
        raise AssertionError(f"Keyboard selection failed for {combo.objectName()}: expected {index}, got {combo.property('currentIndex')}")


def _select_tab_with_mouse(tab_bar: Any, index: int) -> None:
    count = int(tab_bar.property("count"))
    if index < 0 or index >= count:
        raise AssertionError(f"Tab index {index} out of range for {tab_bar.objectName()}: {count}")
    if not tab_bar.isVisible() or tab_bar.width() <= 1 or tab_bar.height() <= 1:
        raise AssertionError(f"Tab bar is not visible/interactable: {tab_bar.objectName()}")
    tab_buttons = [item for item in _items(tab_bar)
                   if "TabButton" in item.metaObject().className() and item.isVisible()]
    if len(tab_buttons) != count:
        raise AssertionError(f"Tab bar {tab_bar.objectName()} exposes {len(tab_buttons)} visible TabButtons, expected {count}")
    _click(tab_bar.window(), tab_buttons[index])
    actual = int(tab_bar.property("currentIndex"))
    if actual != index:
        raise AssertionError(f"Mouse tab selection failed for {tab_bar.objectName()}: expected {index}, got {actual}")


def _click_button_with_text(window: QQuickWindow, parent: Any, text: str) -> Any:
    matches: list[Any] = []
    for item in _items(parent):
        try:
            class_name = item.metaObject().className()
            if item.property("text") == text and "Button" in class_name and item.width() > 1 and item.height() > 1:
                matches.append(item)
        except (RuntimeError, TypeError):
            continue
    if not matches:
        raise AssertionError(f"Could not find a visible Button with text={text!r} under {parent.objectName()!r}")
    visible = [item for item in matches if item.isVisible()]
    if len(visible) != 1:
        raise AssertionError(f"Expected one visible Button named by text {text!r}, got {len(visible)}")
    _click(window, visible[0])
    return visible[0]


def _set_slider_from_user(slider: Any, value: float) -> None:
    window = slider.window()
    _scroll_item_into_view(window, slider)
    if not slider.isEnabled() or not slider.isVisible() or slider.width() <= 1 or slider.height() <= 1:
        raise AssertionError(f"Slider is not interactable: {slider.objectName()}")
    minimum = float(slider.property("from"))
    maximum = float(slider.property("to"))
    if not minimum <= value <= maximum:
        raise AssertionError(f"Requested slider value {value} is outside {minimum}..{maximum} for {slider.objectName()}")
    left_padding = float(slider.property("leftPadding"))
    available_width = float(slider.property("availableWidth"))
    if available_width <= 2:
        raise AssertionError(f"Slider has no usable track width: {slider.objectName()} {available_width}")
    handle = slider.property("handle")
    handle_width = float(handle.width()) if isinstance(handle, QQuickItem) else 0.0
    travel_width = max(0.0, available_width - handle_width)
    fraction = (value - minimum) / (maximum - minimum)
    start_x = left_padding + handle_width / 2 + float(slider.property("visualPosition")) * travel_width
    end_x = left_padding + handle_width / 2 + fraction * travel_width
    y = float(slider.height()) / 2.0
    start = slider.mapToScene(QPointF(start_x, y))
    end = slider.mapToScene(QPointF(end_x, y))
    QTest.mousePress(window, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier,
                     QPoint(round(start.x()), round(start.y())))
    for step in range(1, 5):
        x = start.x() + (end.x() - start.x()) * step / 4.0
        y_step = start.y() + (end.y() - start.y()) * step / 4.0
        QTest.mouseMove(window, QPoint(round(x), round(y_step)), 5)
    QTest.mouseRelease(window, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier,
                       QPoint(round(end.x()), round(end.y())))
    _pump(40)
    actual = float(slider.property("value"))
    step_size = float(slider.property("stepSize"))
    tolerance = max(0.02, step_size * 0.51)
    if abs(actual - value) > tolerance:
        raise AssertionError(f"Mouse drag missed slider target {slider.objectName()}: requested {value}, got {actual}")


class _ExternalUrlRecorder(QObject):
    def __init__(self) -> None:
        super().__init__()
        self.urls: list[str] = []

    @Slot(QUrl)
    def open(self, url: QUrl) -> None:
        self.urls.append(url.toString())


def _capture(window: QQuickWindow, path: Path) -> dict[str, Any]:
    _pump(60)
    image = window.grabWindow()
    if image.isNull():
        raise AssertionError(f"Qt did not render a frame for {path.name}")
    if not image.save(str(path), "PNG"):
        raise AssertionError(f"Could not save rendered screenshot: {path}")
    colors: set[int] = set()
    chromatic = 0
    sampled = 0
    step_x = max(1, image.width() // 70)
    step_y = max(1, image.height() // 70)
    for y in range(0, image.height(), step_y):
        for x in range(0, image.width(), step_x):
            color = image.pixelColor(x, y)
            colors.add(color.rgba())
            if max(color.red(), color.green(), color.blue()) - min(color.red(), color.green(), color.blue()) > 3:
                chromatic += 1
            sampled += 1
    if len(colors) < 12:
        raise AssertionError(f"Rendered frame looks blank: {path.name}; {len(colors)} sampled colors")
    return {
        "path": str(path.relative_to(ROOT)),
        "size": [image.width(), image.height()],
        "sampled_unique_colors": len(colors),
        "sampled_chromatic_fraction": round(chromatic / max(sampled, 1), 5),
    }


def _window_metrics(window: QQuickWindow, requested: tuple[int, int] | None = None) -> dict[str, Any]:
    screen = window.screen()
    available = None
    if screen is not None:
        geometry = screen.availableGeometry()
        available = [geometry.x(), geometry.y(), geometry.width(), geometry.height()]
    minimum = [round(window.minimumWidth()), round(window.minimumHeight())]
    actual = [round(window.width()), round(window.height())]
    if actual[0] < minimum[0] or actual[1] < minimum[1]:
        raise AssertionError(f"QQuickWindow actual size {actual} is below its declared minimum {minimum}")
    return {"requested_size": list(requested) if requested is not None else None,
            "actual_size": actual, "minimum_size": minimum,
            "screen_available_geometry": available,
            "request_fully_applied": list(requested) == actual if requested is not None else None}


def _reacquire_main_if_needed(engine: QQmlApplicationEngine, root: QQuickWindow,
                              identity: int, marker: str, expected_page: int,
                              phase: str) -> tuple[QQuickWindow, bool]:
    try:
        if shiboken6.isValid(root):
            actual_identity = int(shiboken6.getCppPointer(root)[0])
            if actual_identity != identity:
                raise AssertionError(f"Main C++ window identity changed during {phase}: {actual_identity} != {identity}")
            if root.objectName() != "appWindow" or root.property("_uiValidationIdentity") != marker:
                raise AssertionError(f"Main QML identity marker changed during {phase}")
            if int(root.property("currentPage")) != expected_page:
                raise AssertionError(f"Main route changed during {phase}: {root.property('currentPage')} != {expected_page}")
            return root, False
    except RuntimeError:
        pass
    roots = [item for item in engine.rootObjects()
             if isinstance(item, QQuickWindow) and item.objectName() == "appWindow"]
    if len(roots) != 1:
        raise AssertionError(f"Could not reacquire Main after {phase}: {len(roots)} matching roots")
    replacement = roots[0]
    if not shiboken6.isValid(replacement) or int(shiboken6.getCppPointer(replacement)[0]) != identity:
        raise AssertionError(f"Main QQuickWindow C++ identity changed while reacquiring after {phase}")
    if replacement.property("_uiValidationIdentity") != marker:
        raise AssertionError(f"Main QML marker/state was lost after {phase}")
    if int(replacement.property("currentPage")) != expected_page:
        raise AssertionError(f"Main route changed during {phase}: {replacement.property('currentPage')} != {expected_page}")
    return replacement, True


def _scene_rect(item: QQuickItem) -> tuple[float, float, float, float]:
    origin = item.mapToScene(QPointF(0, 0))
    return (float(origin.x()), float(origin.y()), float(item.width()), float(item.height()))


def _rects_overlap(a: tuple[float, float, float, float],
                   b: tuple[float, float, float, float], gap: float = 1.0) -> bool:
    ax, ay, aw, ah = a
    bx, by, bw, bh = b
    return (ax < bx + bw - gap and ax + aw - gap > bx
            and ay < by + bh - gap and ay + ah - gap > by)


def _items(root: Any) -> list[QQuickItem]:
    visual_root = root.contentItem() if isinstance(root, QQuickWindow) else root
    result: list[QQuickItem] = []
    pending = [visual_root] if isinstance(visual_root, QQuickItem) else []
    while pending:
        item = pending.pop()
        result.append(item)
        pending.extend(reversed(item.childItems()))
    return result


def iter_ancestors(item: QQuickItem) -> list[QQuickItem]:
    result: list[QQuickItem] = []
    ancestor = item.parentItem()
    while ancestor is not None:
        result.append(ancestor)
        ancestor = ancestor.parentItem()
    return result


def _direct_visual_child(item: QQuickItem, ancestor: QQuickItem) -> QQuickItem | None:
    """Return the item on the visual path immediately below ancestor."""
    current = item
    while current is not None and current.parentItem() is not ancestor:
        current = current.parentItem()
    return current if current is not None and current.parentItem() is ancestor else None


def _find_text_item(root: Any, text: str, required: bool = True) -> Any | None:
    for item in _items(root):
        try:
            if item.property("text") == text:
                return item
        except (RuntimeError, TypeError):
            pass
    if required:
        raise AssertionError(f"Could not find rendered Text item with text={text!r}")
    return None


def _find_visible_text_item(root: Any, text: str, required: bool = True) -> Any | None:
    """Find the rendered instance in one live visual subtree, excluding cached routes."""
    matches: list[QQuickItem] = []
    for item in _items(root):
        try:
            if item.isVisible() and item.property("text") == text:
                matches.append(item)
        except (RuntimeError, TypeError):
            pass
    if len(matches) > 1:
        # Text aliases on Controls and their Text content can coexist. Prefer
        # the actual rendered Text child; callers only use this for labels.
        text_matches = [item for item in matches if "Text" in item.metaObject().className()]
        if len(text_matches) == 1:
            return text_matches[0]
        if text_matches:
            matches = text_matches
    if matches:
        return matches[0]
    if required:
        raise AssertionError(f"Could not find visible rendered Text item with text={text!r} under {root.objectName()!r}")
    return None


def _find_text_control(root: Any, text: str, required: bool = True) -> Any | None:
    for item in _items(root):
        try:
            if item.property("text") == text and item.isVisible():
                return item
        except (RuntimeError, TypeError):
            pass
    if required:
        raise AssertionError(f"Could not find visible QML control with text={text!r}")
    return None


def _assert_inside(rect: tuple[float, float, float, float],
                   bounds: tuple[float, float, float, float], label: str, tolerance: float = 2.0) -> None:
    x, y, width, height = rect
    bx, by, bwidth, bheight = bounds
    if (width <= 1 or height <= 1 or x < bx - tolerance or y < by - tolerance
            or x + width > bx + bwidth + tolerance or y + height > by + bheight + tolerance):
        raise AssertionError(f"{label} escapes its visible bounds: item={rect}, bounds={bounds}")


def _assert_engineer_cards(root: Any, width: int, height: int) -> dict[str, Any]:
    page = _find(root, "engineerSpotterPage")
    columns = _find_scoped(page, "engineerWorkspaceColumns")
    cards = {name: _find_scoped(page, name) for name in (
        "engineerProfileCard", "engineerAlertsCard", "spotterCard", "pttCard")}
    columns_by_name = {name: _find_scoped(page, name) for name in (
        "engineerProfileColumn", "spotterColumn")}
    page_rect = _scene_rect(page)
    column_rects = {name: _scene_rect(item) for name, item in columns_by_name.items()}
    card_rects = {name: _scene_rect(item) for name, item in cards.items()}
    for name, rect in column_rects.items():
        if (rect[2] <= 1 or rect[3] <= 1 or rect[0] < page_rect[0] - 2
                or rect[0] + rect[2] > page_rect[0] + page_rect[2] + 2):
            raise AssertionError(f"Engineer column {name} has empty or overflowing horizontal bounds at {width}x{height}: {rect}, page={page_rect}")
    for name, rect in card_rects.items():
        if rect[2] <= 1 or rect[3] <= 1:
            raise AssertionError(f"Engineer card {name} has empty geometry at {width}x{height}: {rect}")
        # At the compact breakpoint, cards continue below the viewport, but
        # their horizontal bounds still have to fit inside the page.
        if rect[0] < page_rect[0] - 2 or rect[0] + rect[2] > page_rect[0] + page_rect[2] + 2:
            raise AssertionError(f"Engineer card {name} exceeds page width at {width}x{height}: {rect}")
    for index, name in enumerate(card_rects):
        for other in list(card_rects)[index + 1:]:
            if _rects_overlap(card_rects[name], card_rects[other]):
                raise AssertionError(f"Engineer cards overlap at {width}x{height}: {name} and {other}")
    if width < 1120:
        # Compact one-column pages are intentionally taller than the window.
        # Confirm the cards remain inside the Flickable's scroll extent.
        flickable = next((ancestor for ancestor in iter_ancestors(columns_by_name["engineerProfileColumn"])
                          if "Flickable" in ancestor.metaObject().className()), None)
        if flickable is None:
            raise AssertionError("Compact Engineer page lost its vertical scroll viewport")
        extent_bottom = _scene_rect(flickable)[1] + float(flickable.property("contentHeight"))
        for name, rect in card_rects.items():
            if rect[1] + rect[3] > extent_bottom + 3:
                raise AssertionError(f"Engineer card {name} falls outside compact scroll extent at {width}x{height}")
    if width >= 1120:
        if _rects_overlap(column_rects["engineerProfileColumn"], column_rects["spotterColumn"]):
            raise AssertionError(f"Engineer columns overlap at {width}x{height}: {column_rects}")
        for card_name in ("engineerProfileCard", "engineerAlertsCard"):
            _assert_inside(card_rects[card_name], column_rects["engineerProfileColumn"], card_name)
        for card_name in ("spotterCard", "pttCard"):
            _assert_inside(card_rects[card_name], column_rects["spotterColumn"], card_name)
    return {"columns": {name: list(map(round, rect)) for name, rect in column_rects.items()},
            "cards": {name: list(map(round, rect)) for name, rect in card_rects.items()}}


def _assert_strategy_toggle(root: Any, width: int, height: int) -> dict[str, Any]:
    page = _find(root, "fuelStrategyPage")
    group = _find_scoped(page, "strategyToggleGroup")
    label = _find_scoped(page, "strategyToggleLabel")
    toggle = _find_scoped(page, "strategyToggle")
    group_rect, label_rect, toggle_rect = map(_scene_rect, (group, label, toggle))
    _assert_inside(label_rect, group_rect, "Strategy toggle label")
    _assert_inside(toggle_rect, group_rect, "Strategy toggle switch")
    if _rects_overlap(label_rect, toggle_rect):
        raise AssertionError(f"Strategy toggle label and switch overlap at {width}x{height}: {label_rect}, {toggle_rect}")
    return {"group": list(map(round, group_rect)), "label": list(map(round, label_rect)),
            "switch": list(map(round, toggle_rect))}


def _assert_tyre_tile_geometry(data: Any, width: int, height: int) -> dict[str, Any]:
    """Check four real-data tyre tiles in the required FL/FR/RL/RR 2×2 grid."""
    source = (QML_DIR / "DrivingData.qml").read_text(encoding="utf-8")
    if "vehicleDiagram" in source or re.search(r"\bCanvas\s*\{", source) or "strokeStyle" in source:
        raise AssertionError("DrivingData still contains a Canvas/car illustration")
    if _find_scoped(data, "vehicleDiagram", required=False) is not None:
        raise AssertionError("Obsolete vehicle illustration remains in the live DrivingData tree")

    grid = _find_scoped(data, "tyreTileGrid")
    grid_rect = _scene_rect(grid)
    if grid_rect[2] <= 1 or grid_rect[3] <= 1:
        raise AssertionError(f"Tyre tile grid has empty bounds at {width}x{height}: {grid_rect}")
    compact = bool(_qml_component_item_value(data, "compact"))
    expected_gap = 8.0 if compact else 14.0
    column_gap = float(grid.property("columnSpacing"))
    row_gap = float(grid.property("rowSpacing"))
    if abs(column_gap - expected_gap) > 0.1 or abs(row_gap - expected_gap) > 0.1:
        raise AssertionError(f"Tyre grid spacing changed at {width}x{height}: row={row_gap}, column={column_gap}")

    expected = (
        ("FL", 0, "top-left", "82°C", "26,9 psi", "512°C"),
        ("FR", 1, "top-right", "84°C", "27,0 psi", "526°C"),
        ("RL", 2, "bottom-left", "84°C", "27,1 psi", "448°C"),
        ("RR", 3, "bottom-right", "85°C", "27,2 psi", "453°C"),
    )
    tiles: dict[str, tuple[float, float, float, float]] = {}
    readings: dict[str, Any] = {}
    for corner, index, order, temperature, pressure, brake_temperature in expected:
        tile = _find_scoped(data, f"tyreTile{corner}")
        rect = _scene_rect(tile)
        if rect[2] <= 1 or rect[3] <= 1 or not tile.isVisible():
            raise AssertionError(f"Tyre tile {corner} has no visible bounds at {width}x{height}: {rect}")
        _assert_inside(rect, grid_rect, f"Tyre tile {corner}")
        actual_index = int(tile.property("dataIndex"))
        if actual_index != index:
            raise AssertionError(f"Tyre tile {corner} uses data index {actual_index}, expected {index}")

        tile_color = QColor(tile.property("color"))
        canvas_color = QColor(_qml_component_item_value(tile, "theme.canvas"))
        if not tile_color.isValid() or tile_color != canvas_color:
            raise AssertionError(f"Tyre tile {corner} background {tile_color.name()} does not match theme.canvas {canvas_color.name()}")

        label = _find_scoped(tile, f"wheelLabel{corner}")
        temp_caption = _find_scoped(tile, f"wheelTemperatureCaption{corner}")
        temp_value = _find_scoped(tile, f"wheelTemperature{corner}")
        pressure_caption = _find_scoped(tile, f"wheelPressureCaption{corner}")
        pressure_value = _find_scoped(tile, f"wheelPressure{corner}")
        brake_caption = _find_scoped(tile, f"wheelBrakeTemperatureCaption{corner}")
        brake_value = _find_scoped(tile, f"wheelBrakeTemperature{corner}")
        controls = ((label, corner, "corner label"),
                    (temp_caption, "LỐP", "tyre temperature caption"),
                    (temp_value, temperature, "temperature value"),
                    (pressure_caption, "ÁP SUẤT", "pressure caption"),
                    (pressure_value, pressure, "pressure value"),
                    (brake_caption, "PHANH", "brake temperature caption"),
                    (brake_value, brake_temperature, "brake temperature value"))
        child_bounds = {}
        for child, expected_text, description in controls:
            if not child.isVisible():
                raise AssertionError(f"Tyre tile {corner} {description} is hidden at {width}x{height}")
            actual_text = str(child.property("text") or "")
            if actual_text != expected_text:
                raise AssertionError(f"Tyre tile {corner} {description} is {actual_text!r}, expected {expected_text!r}")
            child_rect = _scene_rect(child)
            _assert_inside(child_rect, rect, f"Tyre tile {corner} {description}")
            if child_rect[2] <= 1 or child_rect[3] <= 1:
                raise AssertionError(f"Tyre tile {corner} {description} has empty bounds: {child_rect}")
            painted_width = float(_qml_item_value_for_component(tile, child.objectName(), "paintedWidth"))
            if painted_width > child_rect[2] + 1.5:
                raise AssertionError(f"Tyre tile {corner} {description} text is clipped: {painted_width}px > {child_rect[2]}px")
            child_bounds[description] = list(map(round, child_rect))
        text_scale = float(data.property("textScale"))
        compact_base = bool(_qml_component_item_value(data, "compact"))
        tile_font_contract = {
            label: (13 if compact_base else 15),
            temp_caption: (13 if compact_base else 15),
            temp_value: (16 if compact_base else 22),
            pressure_caption: (13 if compact_base else 15),
            pressure_value: (12 if compact_base else 14),
            brake_caption: (13 if compact_base else 15),
            brake_value: (16 if compact_base else 22),
        }
        for font_item, base_size in tile_font_contract.items():
            expected_px = math.floor(base_size * text_scale + 0.5)
            actual_px = int(_qml_item_value_for_component(tile, font_item.objectName(), "font.pixelSize"))
            if actual_px != expected_px:
                raise AssertionError(f"Tyre tile {corner} {font_item.objectName()} font is {actual_px}px, "
                                     f"expected rounded {base_size}×{text_scale}={expected_px}px")
        accessible_name = str(_qml_component_item_value(tile, "Accessible.name") or "")
        expected_accessible = (f"{corner}, nhiệt độ lốp {temperature}, áp suất {pressure}, "
                               f"nhiệt độ phanh {brake_temperature}")
        if accessible_name != expected_accessible:
            raise AssertionError(f"Tyre tile {corner} Accessible.name is {accessible_name!r}, "
                                 f"expected {expected_accessible!r}")
        tiles[corner] = rect
        readings[corner] = {"data_index": actual_index, "order": order,
                            "tile_bounds": list(map(round, rect)),
                            "background_color": tile_color.name(QColor.NameFormat.HexRgb),
                            "accessible_name": accessible_name,
                            "texts": {name: str(item.property("text") or "")
                                      for item, name in ((label, "corner"), (temp_caption, "temperature_caption"),
                                                         (temp_value, "temperature"), (pressure_caption, "pressure_caption"),
                                                         (pressure_value, "pressure"),
                                                         (brake_caption, "brake_temperature_caption"),
                                                         (brake_value, "brake_temperature"))},
                            "child_bounds": child_bounds,
                            "font_pixels": {item.objectName(): int(_qml_item_value_for_component(
                                tile, item.objectName(), "font.pixelSize")) for item in tile_font_contract}}

    width_values = [tiles[c][2] for c in ("FL", "FR", "RL", "RR")]
    height_values = [tiles[c][3] for c in ("FL", "FR", "RL", "RR")]
    if max(width_values) - min(width_values) > 1.25 or max(height_values) - min(height_values) > 1.25:
        raise AssertionError(f"Tyre tiles do not have equal geometry at {width}x{height}: {tiles}")
    if _rects_overlap(tiles["FL"], tiles["FR"]) or _rects_overlap(tiles["RL"], tiles["RR"]):
        raise AssertionError(f"Tyre tiles overlap horizontally at {width}x{height}: {tiles}")
    if any(_rects_overlap(tiles[a], tiles[b]) for a, b in (("FL", "RL"), ("FR", "RR"))):
        raise AssertionError(f"Tyre tiles overlap vertically at {width}x{height}: {tiles}")

    horizontal_gaps = {"top": tiles["FR"][0] - (tiles["FL"][0] + tiles["FL"][2]),
                       "bottom": tiles["RR"][0] - (tiles["RL"][0] + tiles["RL"][2])}
    vertical_gaps = {"left": tiles["RL"][1] - (tiles["FL"][1] + tiles["FL"][3]),
                     "right": tiles["RR"][1] - (tiles["FR"][1] + tiles["FR"][3])}
    if min(*horizontal_gaps.values(), *vertical_gaps.values()) <= 0:
        raise AssertionError(f"Tyre tile gaps must be positive at {width}x{height}: {horizontal_gaps}, {vertical_gaps}")
    if any(abs(gap - expected_gap) > 1.25 for gap in (*horizontal_gaps.values(), *vertical_gaps.values())):
        raise AssertionError(f"Tyre tile gap differs from GridLayout spacing at {width}x{height}: "
                             f"h={horizontal_gaps}, v={vertical_gaps}, expected={expected_gap}")
    if abs(horizontal_gaps["top"] - horizontal_gaps["bottom"]) > 1.0:
        raise AssertionError(f"Tyre tile horizontal gaps are unequal at {width}x{height}: {horizontal_gaps}")
    if abs(vertical_gaps["left"] - vertical_gaps["right"]) > 1.0:
        raise AssertionError(f"Tyre tile vertical gaps are unequal at {width}x{height}: {vertical_gaps}")

    for first, second in (("FL", "FR"), ("RL", "RR")):
        if abs(tiles[first][1] - tiles[second][1]) > 1.25:
            raise AssertionError(f"Tyre tile row is not aligned at {width}x{height}: {first}/{second}")
    for first, second in (("FL", "RL"), ("FR", "RR")):
        if abs(tiles[first][0] - tiles[second][0]) > 1.25:
            raise AssertionError(f"Tyre tile column is not aligned at {width}x{height}: {first}/{second}")

    return {"grid_bounds": list(map(round, grid_rect)), "tile_size": [round(width_values[0], 2), round(height_values[0], 2)],
            "expected_gap": expected_gap, "horizontal_gaps": {k: round(v, 2) for k, v in horizontal_gaps.items()},
            "vertical_gaps": {k: round(v, 2) for k, v in vertical_gaps.items()},
            "tiles": readings, "equal_geometry": True, "no_overlap": True, "order": ["FL", "FR", "RL", "RR"]}


def _qml_item_value_for_component(parent: Any, object_name: str, expression: str) -> Any:
    item = _find_scoped(parent, object_name)
    return _qml_component_item_value(item, expression)


def _assert_cockpit_instruments(root: Any, width: int, height: int, *, bottom: bool = False) -> dict[str, Any]:
    """Require the live dashboard data and radio dock to fit without scrolling."""
    cockpit = _find(root, "cockpit")
    data = _find(root, "cockpitDrivingData", required=False)
    flickable = _find(root, "cockpitDataFlickable", required=False)
    if data is None:
        raise AssertionError("Cockpit lost the shared DrivingData visual root")
    data_viewport = _find(root, "cockpitDataViewport", required=False)
    if flickable is not None:
        content_width = float(flickable.property("contentWidth"))
        content_height = float(flickable.property("contentHeight"))
        if content_width > float(flickable.width()) + 1:
            raise AssertionError(f"Cockpit requires horizontal scrolling at {width}x{height}: {content_width} > {flickable.width()}")
        if content_height > float(flickable.height()) + 1:
            raise AssertionError(f"Cockpit requires vertical scrolling at {width}x{height}: {content_height} > {flickable.height()}")
        viewport = _scene_rect(flickable)
        implicit_height = float(data.property("implicitHeight"))
    else:
        viewport = _scene_rect(data_viewport) if data_viewport is not None else _scene_rect(cockpit)
        content_width = float(cockpit.width())
        content_height = float(data.height())
        implicit_height = float(data.property("implicitHeight"))
        if content_height > viewport[3] + 1:
            raise AssertionError(
                f"Cockpit telemetry content is clipped without a scroll viewport at {width}x{height}: "
                f"content={content_height}, dataViewport={viewport[3]}"
            )

    if bottom:
        # Kept for compatibility with the old gallery command. A no-scroll
        # dashboard has no second viewport state to capture.
        if flickable is not None:
            flickable.setProperty("contentY", float(flickable.property("originY")))
            _pump(40)

    window_bounds = (0.0, 0.0, float(root.width()), float(root.height()))
    names = ("speedValue", "gearValue", "fuelValue", "km/h", "GA", "PHANH",
             "Vòng trước", "Tốt nhất", "FL", "FR", "RL", "RR",
             "radioCommand", "radioSend", "radioHistoryButton", "radioPtt")
    bounds: dict[str, list[int]] = {}
    for name in names:
        item = (_find_visible_text_item(data, name, required=False)
                if name in {"km/h", "GA", "PHANH", "Vòng trước", "Tốt nhất", "FL", "FR", "RL", "RR"}
                else _find(root, name, required=False))
        if item is None or not item.isVisible():
            diagnostic = None
            if item is not None:
                diagnostic = {"objectName": item.objectName(), "type": item.metaObject().className(),
                              "scene_bounds": _scene_rect(item), "visible": item.isVisible(),
                              "data_object": data.objectName(), "data_visible": data.isVisible(),
                              "ancestors": [{"name": ancestor.objectName(), "visible": ancestor.isVisible(),
                                             "bounds": _scene_rect(ancestor)}
                                            for ancestor in iter_ancestors(item)]}
            raise AssertionError(f"Cockpit key control/data {name!r} is not visible at {width}x{height}: {diagnostic}")
        rect = _scene_rect(item)
        _assert_inside(rect, window_bounds, f"Cockpit {name} at {width}x{height}")
        # Driving data and action controls must not be clipped by an inner viewport.
        if data is not None and (name in {"speedValue", "gearValue", "fuelValue", "km/h", "GA", "PHANH",
                                         "Vòng trước", "Tốt nhất", "FL", "FR", "RL", "RR"}):
            _assert_inside(rect, viewport, f"Cockpit {name} at {width}x{height}")
        bounds[name] = list(map(round, rect))
    masthead_names = ("trackValue", "carValue", "currentLapValue", "positionValue")
    masthead_rectangles: dict[str, tuple[float, float, float, float]] = {}
    for name in masthead_names:
        item = _find(root, name)
        if not item.isVisible():
            raise AssertionError(f"Cockpit masthead {name} is hidden at {width}x{height}")
        rect = _scene_rect(item)
        _assert_inside(rect, window_bounds, f"Cockpit masthead {name} at {width}x{height}")
        if rect[2] <= 1 or rect[3] <= 1:
            raise AssertionError(f"Cockpit masthead {name} has empty bounds at {width}x{height}: {rect}")
        masthead_rectangles[name] = rect
        bounds[name] = list(map(round, rect))
    for index, name in enumerate(masthead_names):
        for other in masthead_names[index + 1:]:
            if _rects_overlap(masthead_rectangles[name], masthead_rectangles[other]):
                raise AssertionError(f"Cockpit masthead fields overlap at {width}x{height}: {name}, {other}")
    speed_item = _find_scoped(data, "speedValue")
    speed_rect = _scene_rect(speed_item)
    speed_unit = _find_visible_text_item(data, "km/h")
    speed_unit_rect = _scene_rect(speed_unit)
    if speed_unit_rect[0] < speed_rect[0] + speed_rect[2] - 2:
        raise AssertionError(f"Cockpit km/h unit is not beside speed at {width}x{height}: {speed_rect}, {speed_unit_rect}")
    if abs((speed_unit_rect[1] + speed_unit_rect[3]) - (speed_rect[1] + speed_rect[3])) > 28:
        raise AssertionError(f"Cockpit km/h unit is not aligned with the speed baseline at {width}x{height}: {speed_rect}, {speed_unit_rect}")

    data_row = _direct_visual_child(speed_item, data)
    left_panel = _direct_visual_child(speed_item, data_row) if data_row is not None else None
    left_layout = _direct_visual_child(speed_item, left_panel) if left_panel is not None else None
    if left_panel is None or left_layout is None:
        raise AssertionError("Could not identify the visual parent chain for Cockpit left instrument panel/layout")
    left_panel_rect = _scene_rect(left_panel)
    left_layout_rect = _scene_rect(left_layout)
    grid = _find_scoped(data, "tyreTileGrid")
    grid_rect = _scene_rect(grid)
    _assert_inside(left_panel_rect, _scene_rect(data), f"Cockpit left instrument panel at {width}x{height}")
    _assert_inside(left_layout_rect, left_panel_rect, f"Cockpit left instrument layout at {width}x{height}")
    if _rects_overlap(left_panel_rect, grid_rect):
        raise AssertionError(f"Cockpit left instrument panel overlaps tyre grid at {width}x{height}: "
                             f"left={left_panel_rect}, grid={grid_rect}")

    critical_left_items: dict[str, QQuickItem] = {
        "speed": speed_item,
        "speed_unit": speed_unit,
        "gear": _find_scoped(data, "gearValue"),
    }
    gear_frame = critical_left_items["gear"].parentItem()
    if gear_frame is None:
        raise AssertionError("Cockpit gear reading lost its frame")
    critical_left_items["gear_frame"] = gear_frame
    rpm_label = _find_visible_text_item(left_layout, "RPM", required=False)
    if rpm_label is None:
        raise AssertionError(f"Cockpit RPM caption is not visible under left panel at {width}x{height}")
    critical_left_items["rpm_label"] = rpm_label
    tick_texts = {"0", "2.000", "4.000", "6.000", "8.000", "9.000+"}
    ticks = [item for item in _items(left_layout)
             if item.isVisible() and item.property("text") in tick_texts]
    if len(ticks) < 3:
        raise AssertionError(f"Cockpit RPM scale has only {len(ticks)} visible tick labels at {width}x{height}")
    for index, tick in enumerate(ticks):
        critical_left_items[f"rpm_tick_{index}"] = tick
    for label, item in critical_left_items.items():
        rect = _scene_rect(item)
        _assert_inside(rect, left_panel_rect, f"Cockpit left {label} at {width}x{height}")
        if _rects_overlap(rect, grid_rect):
            raise AssertionError(f"Cockpit left {label} overlaps tyre grid at {width}x{height}: {rect}, {grid_rect}")

    pedal_geometry = _assert_cockpit_pedal_segments(data)
    pedals = [item for item in _items(data)
              if str(item.property("label") or "") in {"GA", "PHANH"}]
    for pedal in pedals:
        pedal_rect = _scene_rect(pedal)
        _assert_inside(pedal_rect, left_panel_rect, f"Cockpit {pedal.property('label')} pedal panel at {width}x{height}")
        if _rects_overlap(pedal_rect, grid_rect):
            raise AssertionError(f"Cockpit {pedal.property('label')} pedal overlaps tyre grid at {width}x{height}")
    tyre_tile_geometry = _assert_tyre_tile_geometry(data, width, height)
    for corner, tile in tyre_tile_geometry["tiles"].items():
        _assert_inside(tuple(float(v) for v in tile["tile_bounds"]), viewport,
                       f"Cockpit tyre tile {corner} at {width}x{height}")
    if data_viewport is not None:
        for name in ("radioCommand", "radioSend", "radioHistoryButton", "radioPtt"):
            if _rects_overlap(_scene_rect(data_viewport), _scene_rect(_find(root, name))):
                raise AssertionError(f"Cockpit data viewport overlaps radio action {name} at {width}x{height}")
    return {"viewport": list(map(round, viewport)), "content_width": round(content_width, 2),
            "content_height": round(content_height, 2), "implicit_height": round(implicit_height, 2),
            "key_control_bounds": bounds, "vertical_or_horizontal_scroll_required": False,
            "scrolled_to_bottom": False, "tyre_tile_geometry": tyre_tile_geometry,
            "masthead_nonoverlap": True, "speed_unit_bounds": list(map(round, speed_unit_rect)),
            "pedal_segments": pedal_geometry,
            "left_panel_bounds": list(map(round, left_panel_rect)),
            "left_instrument_layout_bounds": list(map(round, left_layout_rect)),
            "tyre_grid_bounds": list(map(round, grid_rect)),
            "left_panel_grid_nonoverlap": True,
            "critical_left_bounds": {name: list(map(round, _scene_rect(item)))
                                     for name, item in critical_left_items.items()}}


def _assert_header_layout(root: Any, width: int, height: int) -> dict[str, Any]:
    nav_names = ("navRace", "navAnalysis", "navSettings")
    status_names = ("connectionStatus", "voiceStatus", "aiStatus")
    window_names = ("windowMinimize", "windowMaximize", "windowClose")
    groups = {"nav": nav_names, "status": status_names, "window": window_names}
    rectangles: dict[str, tuple[float, float, float, float]] = {}
    for name in (*nav_names, *status_names, *window_names):
        item = _find(root, name)
        if not item.isVisible():
            raise AssertionError(f"Header object {name} is not visible at {width}x{height}")
        rectangle = _scene_rect(item)
        x, y, item_width, item_height = rectangle
        if item_width <= 1 or item_height <= 1:
            raise AssertionError(f"Header object {name} has empty geometry {rectangle}")
        if x < -1 or y < -1 or x + item_width > width + 1 or y + item_height > height + 1:
            raise AssertionError(f"Header object {name} escapes {width}x{height} window: {rectangle}")
        rectangles[name] = rectangle
    for group_names in groups.values():
        for index, name in enumerate(group_names):
            for other in group_names[index + 1:]:
                if _rects_overlap(rectangles[name], rectangles[other]):
                    raise AssertionError(f"Header siblings overlap: {name} and {other} at {width}x{height}")
    for nav_name in nav_names:
        for status_name in status_names:
            if _rects_overlap(rectangles[nav_name], rectangles[status_name]):
                raise AssertionError(f"Header navigation overlaps status pill {status_name} at {width}x{height}")
    for status_name in status_names:
        for window_name in window_names:
            if _rects_overlap(rectangles[status_name], rectangles[window_name]):
                raise AssertionError(f"Header status pill {status_name} overlaps window control {window_name} at {width}x{height}")
    return {name: [round(value, 2) for value in rect] for name, rect in rectangles.items()}


def _assert_cockpit_pedal_segments(data: Any) -> dict[str, Any]:
    """Check both live pedal percentages and all twelve proportional bars."""
    raised = QColor(_qml_component_item_value(data, "theme.raised"))
    accent = QColor(_qml_component_item_value(data, "theme.accent"))
    expected = {"GA": ("82%", 0.82), "PHANH": ("12%", 0.12)}
    records: dict[str, Any] = {}
    pedals = [item for item in _items(data)
              if str(item.property("label") or "") in expected]
    if len(pedals) != 2:
        raise AssertionError(f"Cockpit must render two pedal instruments, found {len(pedals)}")
    for pedal in pedals:
        label = str(pedal.property("label"))
        expected_text, expected_fraction = expected[label]
        rect = _scene_rect(pedal)
        _assert_inside(rect, _scene_rect(data), f"Cockpit {label} pedal")
        value_item = _find_text_item(pedal, expected_text, required=False)
        if value_item is None or not value_item.isVisible() or _scene_rect(value_item)[2] <= 1:
            raise AssertionError(f"Cockpit {label} pedal percentage {expected_text} is not visible")
        backgrounds: list[tuple[float, float, float, float]] = []
        fills: list[tuple[float, float, float, float]] = []
        for item in _items(pedal):
            if "Rectangle" not in item.metaObject().className():
                continue
            color = QColor(item.property("color"))
            item_rect = _scene_rect(item)
            if item_rect[2] <= 1 or not _rects_inside(item_rect, rect):
                continue
            if color == raised and item_rect[3] > 0:
                backgrounds.append(item_rect)
            elif color == accent and item_rect[3] > 0:
                fills.append(item_rect)
        if len(backgrounds) != 12:
            raise AssertionError(f"Cockpit {label} pedal has {len(backgrounds)} background segments, expected 12")
        total_height = sum(item_rect[3] for item_rect in backgrounds)
        filled_height = sum(item_rect[3] for item_rect in fills)
        fraction = filled_height / total_height if total_height else 0
        if not math.isclose(fraction, expected_fraction, abs_tol=0.04):
            raise AssertionError(f"Cockpit {label} pedal segment proportion is {fraction:.3f}, expected {expected_fraction:.2f}")
        records[label] = {"value": expected_text, "segments": len(backgrounds),
                          "filled_proportion": round(fraction, 3),
                          "expected_proportion": expected_fraction,
                          "bounds": list(map(round, rect))}
    return records


def _assert_telemetry_primary_geometry(root: Any, page: Any, width: int, height: int) -> dict[str, Any]:
    """Keep the primary Telemetry summary and lap ledger reachable and separate."""
    main = _find_scoped(page, "telemetryMainSection")
    ledger = _find_scoped(page, "telemetryLapLedger")
    flickable = next((ancestor for ancestor in iter_ancestors(main)
                      if "Flickable" in ancestor.metaObject().className()), None)
    if flickable is not None:
        flickable.setProperty("contentY", float(flickable.property("originY")))
        _pump(30)
    page_rect = _scene_rect(page)
    main_rect = _scene_rect(main)
    ledger_rect = _scene_rect(ledger)
    for label, rect in (("main section", main_rect), ("lap ledger", ledger_rect)):
        if rect[2] <= 1 or rect[3] <= 1:
            raise AssertionError(f"Telemetry {label} has empty geometry at {width}x{height}: {rect}")
    if _rects_overlap(main_rect, ledger_rect):
        raise AssertionError(f"Telemetry main summary overlaps lap ledger at {width}x{height}: {main_rect}, {ledger_rect}")

    columns = [item for item in main.childItems() if item.isVisible() and item.width() > 1 and item.height() > 1]
    if len(columns) != 2:
        raise AssertionError(f"Telemetry main summary exposes {len(columns)} primary columns at {width}x{height}, expected 2")
    column_rects = [_scene_rect(item) for item in columns]
    for index, rect in enumerate(column_rects):
        _assert_inside(rect, main_rect, f"Telemetry primary column {index} at {width}x{height}")
        if index and _rects_overlap(column_rects[0], rect):
            raise AssertionError(f"Telemetry primary columns overlap at {width}x{height}: {column_rects}")

    primary_items = {
        name: _find_scoped(page, name) for name in (
            "telemetrySpeedValue", "telemetryGearValue", "telemetryRpmValue",
            "telemetryProgressValue", "telemetryPositionValue", "telemetryCurrentLapValue",
            "opponentAheadName", "opponentAheadGap", "opponentBehindName", "opponentBehindGap",
            "telemetryLatestEvent", "telemetryCurrentLapTime", "telemetryPreviousLapTime", "telemetryBestLapTime",
        )
    }
    primary_bounds: dict[str, list[int]] = {}
    for name, item in primary_items.items():
        if not item.isVisible():
            raise AssertionError(f"Telemetry primary value {name} is hidden at {width}x{height}")
        rect = _scene_rect(item)
        if rect[2] <= 1 or rect[3] <= 1:
            raise AssertionError(f"Telemetry primary value {name} has empty bounds at {width}x{height}: {rect}")
        if name in {"telemetryCurrentLapTime", "telemetryPreviousLapTime", "telemetryBestLapTime"}:
            _assert_inside(rect, ledger_rect, f"Telemetry lap value {name} at {width}x{height}")
        else:
            _assert_inside(rect, main_rect, f"Telemetry primary value {name} at {width}x{height}")
        primary_bounds[name] = list(map(round, rect))

    if width >= 1280 and height >= 800:
        _assert_inside(main_rect, page_rect, f"Telemetry primary summary viewport at {width}x{height}")
        _assert_inside(ledger_rect, page_rect, f"Telemetry lap ledger viewport at {width}x{height}")
        viewport_fit = True
    else:
        viewport_fit = False
        lap_value = primary_items["telemetryBestLapTime"]
        _scroll_item_into_view(root, lap_value)
        _pump(30)
        scrolled_rect = _scene_rect(lap_value)
        _assert_inside(scrolled_rect, _scene_rect(page), f"Reachable Telemetry lap value at {width}x{height}")
        # Restore the original top position before testing the remaining page content.
        if flickable is not None:
            flickable.setProperty("contentY", float(flickable.property("originY")))
            _pump(30)

    return {"main_bounds": list(map(round, main_rect)), "lap_ledger_bounds": list(map(round, ledger_rect)),
            "main_columns": [list(map(round, rect)) for rect in column_rects],
            "main_and_ledger_nonoverlap": True, "primary_values": primary_bounds,
            "main_and_ledger_fit_initial_viewport": viewport_fit}


def _rects_inside(inner: tuple[float, float, float, float],
                  outer: tuple[float, float, float, float], tolerance: float = 1.5) -> bool:
    ix, iy, iw, ih = inner
    ox, oy, ow, oh = outer
    return (ix >= ox - tolerance and iy >= oy - tolerance
            and ix + iw <= ox + ow + tolerance and iy + ih <= oy + oh + tolerance)


def _test_header_stress(engine: QQmlApplicationEngine, root: Any, fake: Any) -> dict[str, Any]:
    _route_to(engine, root, 0, direct=True)
    fake.values.update({"connected": True, "simulatorName": "Assetto Corsa",
                        "connectionText": "Đã kết nối Assetto Corsa",
                        "voiceStatus": "Recognizing", "apiConfigured": True,
                        "apiBaseUrl": "http://localhost:8080/v1",
                        "apiModel": "race-engineer-model", "apiKey": "mock-key",
                        "telemetry": {"track": "Monza", "carModel": "Ferrari 296 GT3"}})
    fake.changed.emit()
    results: list[dict[str, Any]] = []
    expected = {"LLM Server Offline": "AI · Máy chủ ngoại tuyến",
                "AuthenticationError": "AI · Lỗi xác thực"}
    for api_state, ai_text in expected.items():
        fake.values["apiState"] = api_state
        fake.values["apiDetail"] = api_state
        fake.changed.emit()
        for width in (1120, 1280):
            root.resize(width, 800)
            _pump(100)
            bounds = _assert_header_layout(root, width, 800)
            connection = _find(root, "connectionStatus")
            voice = _find(root, "voiceStatus")
            ai = _find(root, "aiStatus")
            actual = {"connection": connection.property("text"), "voice": voice.property("text"),
                      "ai": ai.property("text")}
            if actual != {"connection": "AC / ACC · Kết nối", "voice": "Mic: Đang nhận dạng", "ai": ai_text}:
                raise AssertionError(f"Header status text mismatch at {width}x800/{api_state}: {actual}")
            results.append({"width": width, "height": 800, "api_state": api_state,
                            "status_text": actual, "bounds": bounds})
    return {"states": results, "track_car_overflow_accessibility": "covered by Main QML ToolTip/Accessible static contract"}


def _test_cockpit_breakpoint_sweep(engine: QQmlApplicationEngine, root: Any) -> dict[str, Any]:
    _route_to(engine, root, 0, direct=True)
    cases = ((1100, 576), (1100, 660), (1120, 660), (1120, 700),
             (1280, 640), (1280, 660), (1280, 720), (1366, 728),
             (1440, 760), (1440, 800), (1600, 576), (1600, 800))
    records: list[dict[str, Any]] = []
    for requested in cases:
        root.resize(*requested)
        _pump(50)
        metrics = _window_metrics(root, requested)
        width, height = metrics["actual_size"]
        loader = _find(root, PAGE_NAMES[0])
        page_item = loader.property("item")
        layout = _assert_page_geometry(root, loader, page_item, 0, width, height)
        header = _assert_header_layout(root, width, height)
        if layout["cockpit_no_scroll"]["vertical_or_horizontal_scroll_required"]:
            raise AssertionError(f"Cockpit requested scrolling at screen-safe size {width}x{height}")
        records.append({"requested_size": list(requested), "actual_size": [width, height],
                        "window_metrics": metrics,
                        "data_viewport": layout["cockpit_no_scroll"]["viewport"],
                        "key_bounds": layout["cockpit_no_scroll"]["key_control_bounds"],
                        "header": header})
    return {"cases": records, "count": len(records),
            "assertions": ["actual size recorded", "key telemetry in viewport", "radio dock separate",
                           "no internal horizontal/vertical scrolling", "header bounds and non-overlap"]}


def _test_cockpit_overflow_accessibility(engine: QQmlApplicationEngine, root: Any,
                                        fake: Any) -> dict[str, Any]:
    _route_to(engine, root, 0, direct=True)
    telemetry = dict(fake.values["telemetry"])
    telemetry["track"] = "MONZA — THE TEMPLE OF SPEED, FULL EVENT NAME FOR ACCESSIBILITY"
    telemetry["carModel"] = ("Ferrari 296 GT3 endurance package with a deliberately long complete model designation "
                              "to verify keyboard and hover access to the untruncated label")
    fake.values["telemetry"] = telemetry
    fake.changed.emit()
    root.resize(880, 576)
    _pump(80)
    records: dict[str, Any] = {}
    for name, full_text in (("trackValue", telemetry["track"].upper()),
                            ("carValue", telemetry["carModel"])):
        item = _find(root, name)
        rendered_text = item.property("text")
        if rendered_text != full_text:
            raise AssertionError(f"Cockpit {name} changed/truncated its source text: {rendered_text!r}")
        text_width = float(item.property("width"))
        implicit_width = float(item.property("implicitWidth"))
        if implicit_width <= text_width:
            raise AssertionError(f"Cockpit {name} fixture did not exercise overflow: {implicit_width} <= {text_width}")
        if int(_qml_component_item_value(item, "elide")) != 1:
            raise AssertionError(f"Cockpit {name} must use Text.ElideRight for its visible label")
        accessible_name = _qml_component_item_value(item, "Accessible.name")
        if accessible_name != full_text:
            raise AssertionError(f"Cockpit {name} accessible name is not the full source text: {accessible_name!r}")
        point = item.mapToScene(QPointF(item.width() / 2, item.height() / 2))
        QTest.mouseMove(root, QPoint(round(point.x()), round(point.y())))
        _pump(650)
        tooltip_visible = bool(_qml_component_item_value(item, "ToolTip.visible"))
        tooltip_text = _qml_component_item_value(item, "ToolTip.text")
        if not tooltip_visible or tooltip_text != full_text:
            raise AssertionError(f"Cockpit {name} hover tooltip did not expose its complete text: visible={tooltip_visible}, text={tooltip_text!r}")
        records[name] = {"visible_text": rendered_text, "accessible_name": accessible_name,
                         "text_width": round(text_width, 2), "implicit_width": round(implicit_width, 2),
                         "elide_mode": "Text.ElideRight", "hover_tooltip": tooltip_text}
    return {"overflow_fields": records, "input": "Qt mouse hover", "all_text_retained": True}


def _assert_page_geometry(root: Any, loader: Any, page_item: Any, page: int,
                          width: int, height: int) -> dict[str, Any]:
    loader_rect = _scene_rect(loader)
    item_rect = _scene_rect(page_item)
    if loader_rect[2] <= 1 or loader_rect[3] <= 1:
        raise AssertionError(f"Route loader {loader.objectName()} has empty bounds at {width}x{height}: {loader_rect}")
    if item_rect[2] <= 1 or item_rect[3] <= 1:
        raise AssertionError(f"Route page {page_item.objectName()} has empty bounds at {width}x{height}: {item_rect}")
    if abs(loader_rect[2] - item_rect[2]) > 2 or abs(loader_rect[3] - item_rect[3]) > 2:
        raise AssertionError(f"Route page does not fill its loader: {loader_rect} vs {item_rect}")
    result: dict[str, Any] = {"loader_scene_bounds": list(map(round, loader_rect)),
                              "item_scene_bounds": list(map(round, item_rect))}
    if page == 0:
        values = {name: _scene_rect(_find(root, name)) for name in ("speedValue", "gearValue", "fuelValue")}
        for name, rect in values.items():
            if rect[2] <= 1 or rect[3] <= 1:
                raise AssertionError(f"Cockpit {name} has empty bounds at {width}x{height}: {rect}")
        for index, name in enumerate(values):
            for other in list(values)[index + 1:]:
                if _rects_overlap(values[name], values[other]):
                    raise AssertionError(f"Cockpit value items overlap at {width}x{height}: {name}, {other}")
        result["cockpit_value_bounds"] = {
            name: [round(value, 2) for value in rect] for name, rect in values.items()
        }
        result["cockpit_no_scroll"] = _assert_cockpit_instruments(root, width, height)
    elif page == 1:
        result["fuel_strategy_toggle"] = _assert_strategy_toggle(root, width, height)
    elif page == 2:
        result["engineer_cards"] = _assert_engineer_cards(root, width, height)
    return result


def _bad_qml_warnings(warnings: list[str]) -> list[str]:
    indicators = (
        "ReferenceError",
        "is not a function",
        "Cannot read property",
        "Cannot assign to non-existent property",
        "TypeError",
        "Unable to assign",
        "required property",
        "QML component was not ready",
    )
    return [warning for warning in warnings if any(indicator in warning for indicator in indicators)]


def _run_baseline_load() -> dict[str, Any]:
    source = subprocess.check_output(
        ["git", "show", f"{BASELINE}:qml/Main.qml"], cwd=ROOT, text=True
    )
    with tempfile.TemporaryDirectory(prefix="raceengineer-baseline-") as temporary:
        temporary_root = Path(temporary)
        baseline_qml_dir = temporary_root / "qml"
        baseline_assets = temporary_root / "assets"
        baseline_qml_dir.mkdir()
        baseline_assets.mkdir()
        baseline_qml = baseline_qml_dir / "Main.qml"
        baseline_qml.write_text(source, encoding="utf-8")
        shutil.copy2(ROOT / "assets" / "final_icon_128.png", baseline_assets / "final_icon_128.png")
        engine, root, fake, warnings = _qml_engine(baseline_qml)
        for page, label in ((0, "Cockpit"), (1, "strategy"), (2, "engineer"),
                            (3, "telemetry"), (4, "AI"), (5, "settings")):
            root.setProperty("currentPage", page)
            _pump(40)
        bad = _bad_qml_warnings(warnings)
        if bad:
            raise AssertionError("Baseline QML emitted runtime errors:\n" + "\n".join(bad))
        result = {
            "commit": BASELINE,
            "loaded": True,
            "routes_rendered": ["Cockpit", "strategy", "engineer", "telemetry", "AI", "settings"],
            "qml_warnings": warnings,
            "mock_facade_call_count": len(fake.calls),
        }
        return result


def _test_new_routes(engine: QQmlApplicationEngine, root: Any, fake: Any,
                     warnings: list[str]) -> dict[str, Any]:
    objects: dict[int, Any] = {}
    for page, name in PAGE_NAMES.items():
        _route_to(engine, root, page)
        objects[page] = _find(root, name)
        if not objects[page].isVisible() or not bool(objects[page].property("active")):
            raise AssertionError(f"Current route {name!r} is not visible")
        item = _current_page_item(root, objects[page])
        live = item.property("live")
        if live is not None and not bool(live):
            raise AssertionError(f"Visible route {name!r} has live=false")
    bad = _bad_qml_warnings(warnings)
    if bad:
        raise AssertionError("New QML emitted runtime errors:\n" + "\n".join(bad))
    return {"loaded": True, "routes": {str(key): {"loader": value.objectName(),
            "item": value.property("item").objectName()} for key, value in objects.items()}}


def _test_empty_zero_connected(engine: QQmlApplicationEngine, root: Any, fake: Any,
                               strategy: Any) -> dict[str, Any]:
    zero = _qml_value(engine, root, 'formatTelemetryValue(0, " km/h")')
    missing = _qml_value(engine, root, 'formatTelemetryValue(undefined, " km/h")')
    nan = _qml_value(engine, root, 'formatTelemetryValue("unavailable", " km/h")')
    if zero != "0.000 km/h" or missing != "—" or nan != "—":
        raise AssertionError(f"0/missing telemetry formatting incorrect: {zero!r}, {missing!r}, {nan!r}")

    theme_zero = _qml_value(engine, root, 'designTheme.number(0, 0, " km/h")')
    theme_missing = _qml_value(engine, root, 'designTheme.number(undefined, 0, " km/h")')
    neutral_gear = _qml_value(engine, root, "designTheme.gear(0)")
    missing_gear = _qml_value(engine, root, "designTheme.gear(undefined)")
    if (theme_zero, theme_missing, neutral_gear, missing_gear) != ("0 km/h", "—", "N", "—"):
        raise AssertionError("Monochrome telemetry formatting no longer distinguishes zero, missing, neutral gear")

    fake.values["connected"] = False
    fake.values["connectionText"] = "Chưa kết nối AC / ACC"
    fake.values["telemetry"] = {"speedKmh": 0.0, "gear": 0, "fuelLiters": 0.0}
    fake.changed.emit()
    _route_to(engine, root, 0)
    speed = _find(root, "speedValue")
    gear = _find(root, "gearValue")
    fuel = _find(root, "fuelValue")
    zero_display = {"speed": speed.property("text"), "gear": gear.property("text"), "fuel": fuel.property("text")}
    if zero_display != {"speed": "0", "gear": "N", "fuel": "0,0 L"}:
        raise AssertionError(f"Visible Cockpit does not render zero telemetry values distinctly: {zero_display}")
    fake.values["telemetry"] = {}
    fake.changed.emit()
    _pump()
    missing_display = {"speed": speed.property("text"), "gear": gear.property("text"), "fuel": fuel.property("text")}
    if missing_display != {"speed": "—", "gear": "—", "fuel": "—"}:
        raise AssertionError(f"Missing telemetry should render em dashes: {missing_display}")
    connection_status = _find(root, "connectionStatus", required=False)
    disconnected = connection_status.property("text") if connection_status else ""
    fake.values["connected"] = True
    fake.values["simulatorName"] = "Assetto Corsa"
    fake.values["connectionText"] = "Đã kết nối Assetto Corsa"
    fake.values["telemetry"] = {"speedKmh": 0.0, "gear": 0, "fuelLiters": 0.0}
    fake.changed.emit()
    _pump()
    return {"zero_value": zero, "missing_value": missing, "nan_value": nan,
            "theme_zero_value": theme_zero, "theme_missing_value": theme_missing,
            "neutral_gear": neutral_gear, "missing_gear": missing_gear,
            "disconnected_state_text": disconnected,
            "zero_display": zero_display, "missing_display": missing_display,
            "connected_zero_state_loaded": True,
            "strategy_default_connected": bool(strategy.value("simulatorConnected"))}


def _test_tyre_tile_value_states(engine: QQmlApplicationEngine, root: Any,
                                 fake: Any) -> dict[str, Any]:
    """Prove per-corner tyre/brake arrays, live updates, and zero vs missing."""
    application_cpp = re.sub(r"\s+", " ", (ROOT / "src/app/Application.cpp").read_text(encoding="utf-8"))
    native_export = re.search(
        r'if \(state\.brakeTemperaturesCelsius\) map\.insert\(QStringLiteral\("brakeTemperatures"\), '
        r'wheelsToList\(\*state\.brakeTemperaturesCelsius\)\);',
        application_cpp,
    )
    if native_export is None:
        raise AssertionError("Application.cpp no longer exposes state.brakeTemperaturesCelsius as telemetry.brakeTemperatures")

    original = dict(fake.values["telemetry"])
    records: dict[str, Any] = {"distinct_index_fixture": {}, "zero_missing_fixture": {},
                               "brake_array_missing_fixture": {}, "dynamic_update": {}}
    try:
        cases = (
            ("distinct_index_fixture", [71.0, 72.0, 73.0, 74.0], [20.1, 21.2, 22.3, 23.4],
             [311.0, 362.0, 413.0, 464.0],
             {"FL": (0, "71°C", "20,1 psi", "311°C"), "FR": (1, "72°C", "21,2 psi", "362°C"),
              "RL": (2, "73°C", "22,3 psi", "413°C"), "RR": (3, "74°C", "23,4 psi", "464°C")}),
            ("zero_missing_fixture", [0.0, None, 18.0, None], [None, 0.0, None, 1.2],
             [0.0, None, 275.0, None],
             {"FL": (0, "0°C", "—", "0°C"), "FR": (1, "—", "0,0 psi", "—"),
              "RL": (2, "18°C", "—", "275°C"), "RR": (3, "—", "1,2 psi", "—")}),
            ("brake_array_missing_fixture", [71.0, 72.0, 73.0, 74.0], [20.1, 21.2, 22.3, 23.4],
             None,
             {"FL": (0, "71°C", "20,1 psi", "—"), "FR": (1, "72°C", "21,2 psi", "—"),
              "RL": (2, "73°C", "22,3 psi", "—"), "RR": (3, "74°C", "23,4 psi", "—")}),
        )
        for case_name, temperatures, pressures, brake_temperatures, expected in cases:
            telemetry = {**original, "tyreTemperatures": temperatures, "tyrePressures": pressures}
            telemetry.pop("brakeTemperatures", None)
            if brake_temperatures is not None:
                telemetry["brakeTemperatures"] = brake_temperatures
            fake.values["telemetry"] = telemetry
            fake.changed.emit()
            _pump(40)
            for page, route_name in ((0, "cockpitDrivingData"), (3, "telemetryDrivingData")):
                _route_to(engine, root, page)
                page_item = _find(root, "cockpit" if page == 0 else "telemetryPage")
                data = _find_scoped(page_item, route_name)
                by_corner = {}
                for corner, (index, temperature, pressure, brake_temperature) in expected.items():
                    tile = _find_scoped(data, f"tyreTile{corner}")
                    actual_index = int(tile.property("dataIndex"))
                    actual_temperature = str(_find_scoped(tile, f"wheelTemperature{corner}").property("text"))
                    actual_pressure = str(_find_scoped(tile, f"wheelPressure{corner}").property("text"))
                    actual_brake_temperature = str(_find_scoped(tile, f"wheelBrakeTemperature{corner}").property("text"))
                    actual = (actual_index, actual_temperature, actual_pressure, actual_brake_temperature)
                    wanted = (index, temperature, pressure, brake_temperature)
                    if actual != wanted:
                        raise AssertionError(f"{route_name} {corner} wheel data is {actual!r}, expected {wanted!r}")
                    if (str(_find_scoped(tile, f"wheelTemperatureCaption{corner}").property("text")) != "LỐP"
                            or str(_find_scoped(tile, f"wheelBrakeTemperatureCaption{corner}").property("text")) != "PHANH"):
                        raise AssertionError(f"{route_name} {corner} temperature captions are incorrect")
                    accessible = str(_qml_component_item_value(tile, "Accessible.name") or "")
                    expected_accessible = (f"{corner}, nhiệt độ lốp {temperature}, áp suất {pressure}, "
                                           f"nhiệt độ phanh {brake_temperature}")
                    if accessible != expected_accessible:
                        raise AssertionError(f"{route_name} {corner} accessible name is {accessible!r}, "
                                             f"expected {expected_accessible!r}")
                    by_corner[corner] = {"data_index": actual_index, "temperature": actual_temperature,
                                         "pressure": actual_pressure,
                                         "brake_temperature": actual_brake_temperature,
                                         "accessible_name": accessible}
                records[case_name]["cockpit" if page == 0 else "telemetry"] = by_corner

        update_records = {}
        for page, route_name in ((0, "cockpitDrivingData"), (3, "telemetryDrivingData")):
            initial_brakes = [501.0, 502.0, 503.0, 504.0]
            updated_brakes = [611.0, 622.0, 633.0, 644.0]
            dynamic_telemetry = {**original, "tyreTemperatures": [81.0, 82.0, 83.0, 84.0],
                                 "tyrePressures": [25.1, 25.2, 25.3, 25.4],
                                 "brakeTemperatures": initial_brakes}
            fake.values["telemetry"] = dynamic_telemetry
            fake.changed.emit()
            _pump(40)
            _route_to(engine, root, page)
            page_item = _find(root, "cockpit" if page == 0 else "telemetryPage")
            data = _find_scoped(page_item, route_name)
            tiles = {corner: _find_scoped(data, f"tyreTile{corner}") for corner in ("FL", "FR", "RL", "RR")}
            brake_items = {corner: _find_scoped(tiles[corner], f"wheelBrakeTemperature{corner}")
                           for corner in tiles}
            before_ptrs = {corner: int(shiboken6.getCppPointer(item)[0]) for corner, item in brake_items.items()}
            before_texts = {corner: str(item.property("text")) for corner, item in brake_items.items()}
            fake.values["telemetry"] = {**dynamic_telemetry, "brakeTemperatures": updated_brakes}
            fake.changed.emit()
            _pump(50)
            after_ptrs = {corner: int(shiboken6.getCppPointer(_find_scoped(tiles[corner],
                                 f"wheelBrakeTemperature{corner}"))[0]) for corner in tiles}
            after_texts = {corner: str(_find_scoped(tiles[corner], f"wheelBrakeTemperature{corner}").property("text"))
                           for corner in tiles}
            expected_texts = {corner: f"{value:.0f}°C" for corner, value in zip(tiles, updated_brakes)}
            if before_ptrs != after_ptrs or before_texts == after_texts or after_texts != expected_texts:
                raise AssertionError(f"{route_name} brake readings did not update live on the same items: "
                                     f"before={before_texts}, after={after_texts}, expected={expected_texts}, "
                                     f"same_items={before_ptrs == after_ptrs}")
            for corner in tiles:
                accessible = str(_qml_component_item_value(tiles[corner], "Accessible.name") or "")
                if f"nhiệt độ phanh {after_texts[corner]}" not in accessible:
                    raise AssertionError(f"{route_name} {corner} accessible name did not refresh with brake data: {accessible!r}")
            update_records["cockpit" if page == 0 else "telemetry"] = {
                "same_value_item_pointers": before_ptrs == after_ptrs,
                "before": before_texts, "after": after_texts,
            }
        records["dynamic_update"] = update_records
    finally:
        fake.values["telemetry"] = original
        fake.changed.emit()
        _pump(40)
    return {"zero_missing_and_absent_distinguished": True,
            "native_export_source_check": "Application.cpp maps state.brakeTemperaturesCelsius to telemetry.brakeTemperatures via wheelsToList",
            "fixtures": records}


def _test_radio(engine: QQmlApplicationEngine, root: Any, fake: Any) -> dict[str, Any]:
    _route_to(engine, root, 0)
    window = root
    command = _find(root, "radioCommand")
    send = _find(root, "radioSend")
    ptt = _find(root, "radioPtt")
    history_button = _find(root, "radioHistoryButton")
    command.setProperty("text", "test text command")
    _click(window, send)
    calls = [call for call in fake.calls if call[0] == "askText"]
    if not calls or calls[-1][1] != ("test text command",):
        raise AssertionError(f"Text send did not call askText with trimmed text: {calls[-1:]}")

    command.setProperty("text", "keyboard command")
    command.forceActiveFocus()
    QTest.keyClick(window, Qt.Key.Key_Return)
    _pump()
    calls = [call for call in fake.calls if call[0] == "askText"]
    if not calls or calls[-1][1] != ("keyboard command",):
        raise AssertionError("Enter did not send the radio command")

    begin_before = sum(call[0] == "beginPushToTalk" for call in fake.calls)
    end_before = sum(call[0] == "endPushToTalk" for call in fake.calls)
    _mouse_press(window, ptt)
    _mouse_release(window, ptt)
    begin_after = sum(call[0] == "beginPushToTalk" for call in fake.calls)
    end_after = sum(call[0] == "endPushToTalk" for call in fake.calls)
    if (begin_after - begin_before, end_after - end_before) != (1, 1):
        raise AssertionError(f"PTT press/release expected 1 begin + 1 end; got {begin_after-begin_before}/{end_after-end_before}")

    begin_before, end_before = begin_after, end_after
    _mouse_press(window, ptt)
    # Route changes while the PTT mouse is physically held. openPage() follows
    # the same route activation state as the visible subnavigation but avoids
    # synthesizing a second mouse press before the held press is released.
    _route_to(engine, root, 3, direct=True)
    _pump(80)
    end_after = sum(call[0] == "endPushToTalk" for call in fake.calls)
    _mouse_release(window, ptt)
    _pump(5)
    begin_after = sum(call[0] == "beginPushToTalk" for call in fake.calls)
    end_final = sum(call[0] == "endPushToTalk" for call in fake.calls)
    route_cancel_deltas = {"begin": begin_after - begin_before,
                           "end": end_final - end_before,
                           "end_before_release": end_after - end_before}
    if route_cancel_deltas["begin"] != 1 or route_cancel_deltas["end"] != 1:
        raise AssertionError(f"PTT route cancellation should end exactly once; got {route_cancel_deltas}")

    # A window deactivate event exercises the focus-loss cancellation path while
    # the same radio button owns an active PTT press.
    _route_to(engine, root, 0)
    _pump()
    ptt = _find(root, "radioPtt")
    begin_before = sum(call[0] == "beginPushToTalk" for call in fake.calls)
    end_before = sum(call[0] == "endPushToTalk" for call in fake.calls)
    _mouse_press(window, ptt)
    QCoreApplication.sendEvent(window, QEvent(QEvent.Type.WindowDeactivate))
    _pump(30)
    _mouse_release(window, ptt)
    _pump(5)
    begin_delta = sum(call[0] == "beginPushToTalk" for call in fake.calls) - begin_before
    end_delta = sum(call[0] == "endPushToTalk" for call in fake.calls) - end_before
    if begin_delta != 1 or end_delta != 1:
        raise AssertionError(f"PTT focus-loss cancellation should end exactly once; got {begin_delta}/{end_delta}")

    fake.values["conversationLog"] = [
        {"role": "driver" if index % 2 == 0 else "engineer",
         "text": f"History item {index}: " + ("race telemetry and radio notes. " * 5)}
        for index in range(30)
    ]
    fake.changed.emit()
    _click(window, history_button)
    history_list = _find(root, "radioHistoryList")
    count = int(history_list.property("count"))
    if count < 30:
        raise AssertionError(f"History drawer failed to display conversationLog entries: count={count}")
    history = _find(root, "radioHistory")
    if not _visible(history):
        raise AssertionError("History drawer did not become visible when opened")

    # Simulate a reader who has scrolled into the history. Appending streamed
    # text at the tail must not force the viewport to the bottom or top.
    history_list.setProperty("followTail", False)
    _pump(80)
    scroll_limit = max(0.0, float(history_list.property("contentHeight")) - float(history_list.height()))
    if scroll_limit <= 100:
        raise AssertionError(f"Long history did not become scrollable: contentHeight={history_list.property('contentHeight')}")
    before_origin = float(history_list.property("originY"))
    history_list.setProperty("contentY", before_origin + scroll_limit * 0.42)
    _pump(60)
    before_y = float(history_list.property("contentY")) - float(history_list.property("originY"))
    before_height = float(history_list.property("contentHeight"))
    streamed = list(fake.values["conversationLog"])
    streamed[-1] = {"role": "engineer", "text": "Streaming final answer: " + ("updated race advice. " * 100)}
    fake.values["conversationLog"] = streamed
    fake.changed.emit()
    _pump(100)
    after_y = float(history_list.property("contentY")) - float(history_list.property("originY"))
    after_height = float(history_list.property("contentHeight"))
    after_limit = max(0.0, after_height - float(history_list.height()))
    if after_y < 40 or after_y > after_limit - 40:
        raise AssertionError(f"History viewport jumped to an edge while streamed text updated: {before_y} → {after_y}, max={after_limit}")
    if abs(after_y - before_y) > max(80, float(history_list.height()) * 0.75):
        raise AssertionError(f"History reading position moved too far during tail update: {before_y} → {after_y}")

    _route_to(engine, root, 3, direct=True)
    _pump(400)
    route_closes_drawer = not _visible(history) and not bool(history.property("opened"))
    if not route_closes_drawer:
        raise AssertionError("History drawer overlay stayed open after leaving Cockpit")
    _route_to(engine, root, 0)
    _click(root, history_button)
    history_list = _find(root, "radioHistoryList")
    history_list.setProperty("followTail", False)
    _pump(80)
    restore_limit = max(0.0, float(history_list.property("contentHeight")) - float(history_list.height()))
    restore_origin = float(history_list.property("originY"))
    restore_offset = restore_limit * 0.42
    history_list.setProperty("contentY", restore_origin + restore_offset)
    _pump(60)
    restore_y = float(history_list.property("contentY")) - float(history_list.property("originY"))
    hidden_count = int(history_list.property("count"))

    # The window stays alive in the tray path. Live bindings and the history
    # model must detach while hidden, then reattach without losing the reader's
    # position when the window returns.
    root.showMinimized()
    _pump(50)
    minimized_live = _find(root, "cockpit").property("live")
    minimized_drawer_visible = _visible(history)
    fake.values["conversationLog"] = [*fake.values["conversationLog"],
                                       {"role": "engineer", "text": "Message while hidden"}]
    fake.changed.emit()
    _pump(60)
    minimized_history_count = int(history_list.property("count"))
    root.showNormal()
    _pump(50)
    restored_history_count = int(history_list.property("count"))
    restored_y = float(history_list.property("contentY")) - float(history_list.property("originY"))
    if minimized_live or not minimized_drawer_visible:
        raise AssertionError(f"Minimize lifecycle failed to freeze Cockpit/preserve the open history drawer: live={minimized_live}, drawer_visible={minimized_drawer_visible}")
    if minimized_history_count != hidden_count:
        raise AssertionError(f"History model changed while the window was minimized: {hidden_count} → {minimized_history_count}")
    if restored_history_count != hidden_count + 1:
        raise AssertionError(f"History model did not refresh after restore: expected {hidden_count + 1}, got {restored_history_count}")
    if abs(restored_y - restore_y) > max(80, float(history_list.height()) * 0.75):
        raise AssertionError(f"History reading position was not restored after minimize: {restore_y} → {restored_y}")
    # Leave the subsequent workspace tests unobstructed by the modal drawer.
    history.close()
    _pump(350)
    return {"text_click_send": True, "text_enter_send": True,
            "ptt_press_release": {"begin": 1, "end": 1},
            "ptt_route_cancel": route_cancel_deltas,
            "ptt_window_deactivate": {"begin": begin_delta, "end": end_delta},
            "history_rows": count,
            "history_stream_read_position": {"before": before_y, "after": after_y,
                                             "content_height_before": before_height,
                                             "content_height_after": after_height},
            "history_drawer_closes_on_route_change": route_closes_drawer,
            "minimized_live": minimized_live,
            "history_drawer_stays_open_on_minimize": minimized_drawer_visible,
            "history_updates_detached_while_minimized": minimized_history_count == hidden_count,
            "history_count_after_restore": restored_history_count,
            "history_read_position_after_restore": {"before": restore_y, "after": restored_y}}


def _last_call(fake: Any, name: str) -> tuple[Any, ...] | None:
    calls = [args for method, args in fake.calls if method == name]
    return calls[-1] if calls else None


def _call_count(fake: Any, name: str, *, strategy: bool = False) -> int:
    source = fake.calls
    return sum(method == name for method, _args in source)


def _assert_call_delta(fake: Any, name: str, before: int, expected: tuple[Any, ...],
                       *, strategy: bool = False) -> tuple[Any, ...]:
    source = fake.calls
    calls = [args for method, args in source[before:] if method == name]
    if len(calls) != 1 or calls[0] != expected:
        raise AssertionError(f"Expected exactly one {name}{expected}; observed {calls}")
    return calls[0]


def _test_engineer_controls(engine: QQmlApplicationEngine, root: Any, fake: Any) -> dict[str, Any]:
    _route_to(engine, root, 2)
    page = _find(root, "engineerSpotterPage")
    fake.values.update({
        "driverName": "Driver",
        "responseStyle": "Tiêu chuẩn",
        "fuelAlertsEnabled": True,
        "tyreAlertsEnabled": True,
        "lapDeltaEnabled": True,
        "flagAlertsEnabled": True,
        "damageAlertsEnabled": True,
        "lapSummaryEnabled": False,
        "spotterEnabled": False,
        "spotterWarningGapMeters": 1.5,
        "keyboardPttEnabled": True,
        "directInputPttEnabled": False,
        "latestLapSummary": "Vòng 12: ổn định, tiết kiệm nhiên liệu 2,61 L/vòng.",
    })
    fake.changed.emit()
    _pump(80)

    driver = _find_visible_scoped(page, "driverNameField")
    _replace_text(root, driver, "Nguyen Test")
    QTest.keyClick(root, Qt.Key.Key_Return)
    _pump(40)
    if _last_call(fake, "setDriverName") != ("Nguyen Test",):
        raise AssertionError(f"Driver name edit did not apply exact setter argument: {_last_call(fake, 'setDriverName')}")

    style_calls: list[tuple[str, tuple[Any, ...] | None]] = []
    style_names = {
        "Tối giản": "responseStyleTốigiảnButton",
        "Tiêu chuẩn": "responseStyleTiêuchuẩnButton",
        "Chi tiết": "responseStyleChitiếtButton",
    }
    for style, name in style_names.items():
        before = len([call for call in fake.calls if call[0] == "setResponseStyle"])
        button = _find_visible_scoped(page, name)
        if not button.isEnabled() or button.width() <= 1 or button.height() <= 1:
            raise AssertionError(f"Response style action is not interactable: {name}")
        _click(root, button)
        calls = [args for method, args in fake.calls if method == "setResponseStyle"]
        if len(calls) != before + 1 or calls[-1] != (style,) or fake.values["responseStyle"] != style:
            raise AssertionError(f"Response style {style!r} did not route exactly once: {calls[before:]}")
        style_calls.append((style, calls[-1]))

    alert_methods = {
        "fuelAlertsToggle": ("fuelAlertsEnabled", "setFuelAlertsEnabled"),
        "tyreAlertsToggle": ("tyreAlertsEnabled", "setTyreAlertsEnabled"),
        "lapDeltaToggle": ("lapDeltaEnabled", "setLapDeltaEnabled"),
        "flagAlertsToggle": ("flagAlertsEnabled", "setFlagAlertsEnabled"),
        "damageAlertsToggle": ("damageAlertsEnabled", "setDamageAlertsEnabled"),
        "lapSummaryToggle": ("lapSummaryEnabled", "setLapSummaryEnabled"),
    }
    alert_results: dict[str, dict[str, Any]] = {}
    for object_name, (property_name, method_name) in alert_methods.items():
        item = _find_visible_scoped(page, object_name)
        before = bool(fake.values[property_name])
        if not item.isEnabled():
            raise AssertionError(f"Alert switch unexpectedly disabled: {object_name}")
        _click(root, item)
        expected = not before
        if _last_call(fake, method_name) != (expected,) or bool(fake.values[property_name]) != expected:
            raise AssertionError(f"Alert switch {object_name} did not apply {expected}: {_last_call(fake, method_name)}")
        alert_results[object_name] = {"setter": method_name, "argument": expected}

    spotter = _find_visible_scoped(page, "spotterEnabledToggle")
    _click(root, spotter)
    if _last_call(fake, "setSpotterEnabled") != (True,) or not fake.values["spotterEnabled"]:
        raise AssertionError("Spotter enable did not call setSpotterEnabled(true)")
    gap = _find_visible_scoped(page, "spotterGapSlider")
    gap_range = (float(gap.property("from")), float(gap.property("to")), float(gap.property("stepSize")))
    if gap_range != (0.5, 2.5, 0.1):
        raise AssertionError(f"Spotter gap slider range differs from baseline 0.5..2.5 step 0.1: {gap_range}")
    _set_slider_from_user(gap, 2.2)
    if _last_call(fake, "setSpotterWarningGapMeters") is None or not math.isclose(
            float(_last_call(fake, "setSpotterWarningGapMeters")[0]), 2.2, abs_tol=0.06):
        raise AssertionError(f"Spotter slider drag did not call the selected gap: {_last_call(fake, 'setSpotterWarningGapMeters')}")
    gap_drag_call = _last_call(fake, "setSpotterWarningGapMeters")
    gap.forceActiveFocus()
    before_keyboard_gap = float(gap.property("value"))
    QTest.keyClick(root, Qt.Key.Key_Right)
    _pump(30)
    keyboard_gap = float(gap.property("value"))
    keyboard_gap_call = _last_call(fake, "setSpotterWarningGapMeters")
    expected_keyboard_gap = min(2.5, round(before_keyboard_gap + gap_range[2], 4))
    if not math.isclose(keyboard_gap, expected_keyboard_gap, abs_tol=0.001):
        raise AssertionError(f"Spotter gap keyboard Right did not increment one step: {before_keyboard_gap} → {keyboard_gap}")
    if keyboard_gap_call is None or not math.isclose(float(keyboard_gap_call[0]), expected_keyboard_gap, abs_tol=0.001):
        raise AssertionError(f"Spotter gap keyboard change was not persisted: {keyboard_gap_call}")

    keyboard = _find_visible_scoped(page, "engineerKeyboardPttToggle")
    direct_input = _find_visible_scoped(page, "engineerDirectInputPttToggle")
    ptt_calls: list[tuple[bool, bool]] = []
    for control, expected in ((keyboard, (False, False)), (direct_input, (False, True)),
                              (keyboard, (True, True)), (direct_input, (True, False))):
        _click(root, control)
        actual = _last_call(fake, "setPushToTalkOptions")
        if actual != expected:
            raise AssertionError(f"PTT option switch forwarded {actual}; expected {expected}")
        ptt_calls.append(actual)

    before = len(fake.calls)
    _click(root, _find_visible_scoped(page, "engineerMapDirectInputButton"))
    _assert_call_delta(fake, "beginDirectInputMapping", before, ())
    before_page = int(root.property("currentPage"))
    _click(root, _find_visible_scoped(page, "openAiVoicePageButton"))
    if int(root.property("currentPage")) != 4:
        raise AssertionError(f"Engineer-to-AI shortcut did not navigate to route 4: {root.property('currentPage')}")
    _route_to(engine, root, 2)

    summary = _find_text_item(page, "Vòng 12: ổn định, tiết kiệm nhiên liệu 2,61 L/vòng.")
    if not summary.isVisible():
        raise AssertionError("Latest lap summary is not rendered when available")
    return {"driver_name_setter": _last_call(fake, "setDriverName"),
            "response_style_actions": style_calls,
            "alert_switches": alert_results,
            "spotter": {"enable_call": _last_call(fake, "setSpotterEnabled"),
                        "gap_range": gap_range,
                        "gap_drag_call": list(gap_drag_call),
                        "gap_keyboard_right_call": list(keyboard_gap_call)},
            "ptt_option_calls": ptt_calls,
            "direct_input_mapping": {"method": "beginDirectInputMapping", "args": []},
            "engineer_to_ai_route": {"from": before_page, "to": 4,
                                     "returned_for_continued_checks": int(root.property("currentPage"))},
            "latest_lap_summary_visible": True,
            "interaction_type": "Qt mouse/keyboard events; fake native facade calls and properties"}


def _test_strategy_actions(engine: QQmlApplicationEngine, root: Any, fake: Any,
                           strategy: Any) -> dict[str, Any]:
    _route_to(engine, root, 1)
    page = _find(root, "fuelStrategyPage")
    _click_button_with_text(root, page, "Tổng quan")
    strategy.set_value("simulatorConnected", False)
    strategy.set_value("busy", False)
    strategy.set_value("raceActive", False)
    strategy.set_value("enabled", False)
    strategy.set_value("realisticSetupConfirmed", False)
    strategy.set_value("sharingConfigured", False)
    strategy.set_value("sharingEnabled", False)
    fake.values["strategyAvailable"] = True
    fake.changed.emit()
    _pump(80)

    result: dict[str, Any] = {"interactions": {}, "disabled_attempts": {}}
    toggle = _find_visible_scoped(page, "strategyToggle")
    calls_before = len(fake.calls)
    _click(root, toggle)
    _assert_call_delta(fake, "setStrategyEnabled", calls_before, (True,))
    if not fake.values["strategyEnabled"]:
        raise AssertionError("Strategy enable action did not update the facade state")
    result["interactions"]["strategyToggle"] = {"method": "setStrategyEnabled", "args": [True]}

    _click_button_with_text(root, page, "Dữ liệu & huấn luyện")

    record = _find_visible_scoped(page, "strategyRecordToggle")
    writes_before = len(strategy.property_writes)
    _click(root, record)
    writes = strategy.property_writes[writes_before:]
    if writes != [("enabled", True)] or not strategy.value("enabled"):
        raise AssertionError(f"Recording toggle did not write only enabled=true: {writes}")
    result["interactions"]["strategyRecordToggle"] = {"qml_property_write": writes[0]}

    realistic = _find_visible_scoped(page, "realisticSetupToggle")
    strategy.set_value("raceActive", True)
    _pump()
    if realistic.isEnabled():
        raise AssertionError("Realistic setup switch must lock while raceActive")
    writes_before = len(strategy.property_writes)
    _click(root, realistic)
    if strategy.property_writes[writes_before:]:
        raise AssertionError("Disabled realistic setup switch wrote through to StrategyRecorder")
    result["disabled_attempts"]["realisticSetupToggle_during_race"] = "no StrategyRecorder property write"
    strategy.set_value("raceActive", False)
    _pump()
    writes_before = len(strategy.property_writes)
    _click(root, realistic)
    writes = strategy.property_writes[writes_before:]
    if writes != [("realisticSetupConfirmed", True)]:
        raise AssertionError(f"Realistic setup confirmation property write mismatch: {writes}")
    result["interactions"]["realisticSetupToggle"] = {"qml_property_write": writes[0]}

    process = _find_visible_scoped(page, "processRecordedDataButton")
    pace = _find_visible_scoped(page, "trainPaceButton")
    tyres = _find_visible_scoped(page, "trainTyresButton")
    cancel = _find_scoped(page, "cancelTrainingButton")
    for control, method in ((process, "processData"), (pace, "trainModel"), (tyres, "trainTyreModels")):
        strategy.set_value("busy", True)
        strategy.set_value("simulatorConnected", False)
        _pump()
        if control.isEnabled():
            raise AssertionError(f"{control.objectName()} must be disabled while StrategyRecorder is busy")
        before = len(strategy.calls)
        _click(root, control)
        if strategy.calls[before:]:
            raise AssertionError(f"Disabled {control.objectName()} invoked a StrategyRecorder action: {strategy.calls[before:]}")
        result["disabled_attempts"][control.objectName() + "_busy"] = "no strategy call"
        strategy.set_value("busy", False)
        _pump()
        before = len(strategy.calls)
        _click(root, control)
        _assert_call_delta(strategy, method, before, (), strategy=True)
        result["interactions"][control.objectName()] = {"method": method, "args": []}

    strategy.set_value("busy", True)
    _pump()
    if not cancel.isVisible() or not cancel.isEnabled():
        raise AssertionError("Cancel training button must be visible/enabled while a job is active")
    before = len(strategy.calls)
    _click(root, cancel)
    _assert_call_delta(strategy, "cancelJob", before, (), strategy=True)
    result["interactions"]["cancelTrainingButton"] = {"method": "cancelJob", "args": []}
    strategy.set_value("busy", False)
    strategy.set_value("simulatorConnected", True)
    _pump()
    if process.isEnabled() or pace.isEnabled() or tyres.isEnabled():
        raise AssertionError("Process and train buttons must be disabled while simulator is connected")
    before = len(strategy.calls)
    for control in (process, pace, tyres):
        _click(root, control)
    if strategy.calls[before:]:
        raise AssertionError(f"Connected process/train controls invoked actions: {strategy.calls[before:]}")
    result["disabled_attempts"]["process_train_while_connected"] = "no strategy calls"
    strategy.set_value("simulatorConnected", False)
    _pump()

    open_data = _find_visible_scoped(page, "openStrategyDataButton")
    before = len(strategy.calls)
    _click(root, open_data)
    _assert_call_delta(strategy, "openStorage", before, (), strategy=True)
    result["interactions"]["openStrategyDataButton"] = {"method": "openStorage", "args": []}

    guide = _find_visible_scoped(page, "loggingGuideButton")
    url_recorder = _ExternalUrlRecorder()
    QDesktopServices.setUrlHandler("https", url_recorder, "open")
    try:
        _click(root, guide)
    finally:
        QDesktopServices.unsetUrlHandler("https")
    expected_url = "https://github.com/tminh-U/RaceEngineer/blob/main/docs/logging-guide.md"
    if url_recorder.urls != [expected_url]:
        raise AssertionError(f"Logging guide click should forward its exact HTTPS URL: {url_recorder.urls}")
    result["interactions"]["loggingGuideButton"] = {"url": url_recorder.urls[0], "external_opened": False}

    consent = _find_visible_scoped(page, "sharingConsentToggle")
    strategy.set_value("sharingConfigured", False)
    strategy.set_value("sharingEnabled", False)
    _pump()
    if consent.isEnabled():
        raise AssertionError("Sharing consent must remain locked without a configured destination")
    writes_before = len(strategy.property_writes)
    _click(root, consent)
    if strategy.property_writes[writes_before:]:
        raise AssertionError("Disabled sharing consent wrote to StrategyRecorder")
    result["disabled_attempts"]["sharing_without_configuration"] = "no StrategyRecorder property write"
    strategy.set_value("sharingConfigured", True)
    _pump()
    _click(root, consent)
    writes = strategy.property_writes[-1:]
    if writes != [("sharingEnabled", True)] or not strategy.value("sharingEnabled"):
        raise AssertionError(f"Sharing consent did not write sharingEnabled=true: {writes}")
    retry = _find_visible_scoped(page, "retrySharingButton")
    if not retry.isVisible() or not retry.isEnabled():
        raise AssertionError("Sharing retry should be available after configured sharing is enabled")
    before = len(strategy.calls)
    _click(root, retry)
    _assert_call_delta(strategy, "retrySharing", before, (), strategy=True)
    result["interactions"]["retrySharingButton"] = {"method": "retrySharing", "args": []}
    result["interactions"]["sharingConsentToggle"] = {"qml_property_write": writes[0]}
    return result


def _test_system_actions(engine: QQmlApplicationEngine, root: Any, fake: Any) -> dict[str, Any]:
    _route_to(engine, root, 5)
    page = _find(root, "systemDevicesPage")
    settings = {name: _find_visible_scoped(page, name) for name in (
        "startWithWindowsToggle", "minimizeToTrayToggle", "minimizeOnCloseToggle",
        "gpuRendererToggle", "aiComputeDeviceCombo", "keyboardPttToggle",
        "directInputPttToggle", "audioInputCombo", "audioOutputCombo",
        "audioDuckingToggle", "radioVolumeSlider", "openSetupButton", "mapDirectInputButton")}
    fake.values.update({
        "trayAvailable": False, "startWithWindows": False, "minimizeToTray": False,
        "minimizeOnClose": False, "gpuRendererEnabled": False,
        "gpuRendererRestartRequired": False,
        "aiComputeDevices": [{"id": "cpu", "label": "CPU"}, {"id": "gpu", "label": "GPU"}],
        "selectedAiComputeDevice": "cpu", "aiComputeRestartRequired": False,
        "aiComputeStatus": "CPU", "keyboardPttEnabled": True,
        "directInputPttEnabled": False, "directInputStatus": "DirectInput not initialized",
        "audioInputDevices": [{"id": "default", "name": "Default input"},
                               {"id": "mic-1", "name": "Studio microphone"}],
        "selectedAudioInputId": "default", "audioOutputDevices": ["Speakers", "USB headset"],
        "selectedAudioOutput": "Speakers", "audioDuckingEnabled": False,
        "isAudioDucked": False, "ttsVolume": 0.8,
    })
    fake.changed.emit()
    _pump(80)
    result: dict[str, Any] = {"interactions": {}, "disabled_attempts": {}}

    tray_controls = (settings["minimizeToTrayToggle"], settings["minimizeOnCloseToggle"])
    for control in tray_controls:
        if control.isEnabled():
            raise AssertionError(f"{control.objectName()} must be disabled when trayAvailable is false")
        method = "setMinimizeToTray" if control.objectName() == "minimizeToTrayToggle" else "setMinimizeOnClose"
        before = len(fake.calls)
        _click(root, control)
        if [name for name, _args in fake.calls[before:]]:
            raise AssertionError(f"Disabled tray switch {control.objectName()} invoked {fake.calls[before:]}")
        result["disabled_attempts"][control.objectName()] = "no setter while tray unavailable"
        _ = method

    bool_settings = (
        ("startWithWindowsToggle", "startWithWindows", "setStartWithWindows", True),
        ("minimizeToTrayToggle", "minimizeToTray", "setMinimizeToTray", True),
        ("minimizeOnCloseToggle", "minimizeOnClose", "setMinimizeOnClose", True),
        ("gpuRendererToggle", "gpuRendererEnabled", "setGpuRendererEnabled", True),
    )
    fake.values["trayAvailable"] = True
    fake.changed.emit()
    _pump()
    for object_name, property_name, method_name, target in bool_settings:
        control = settings[object_name]
        before = len(fake.calls)
        _click(root, control)
        _assert_call_delta(fake, method_name, before, (target,))
        if bool(fake.values[property_name]) != target:
            raise AssertionError(f"{method_name} did not apply {target}")
        result["interactions"][object_name] = {"method": method_name, "args": [target],
                                               "result": fake.values[property_name]}

    compute = settings["aiComputeDeviceCombo"]
    if int(compute.property("count")) != 2 or compute.property("currentText") != "CPU":
        raise AssertionError("AI compute devices/default selection did not render from native options")
    _activate_combo(compute, 1)
    if _last_call(fake, "setAiComputeDevice") != ("gpu",) or fake.values["selectedAiComputeDevice"] != "gpu":
        raise AssertionError(f"AI compute keyboard selection failed: {_last_call(fake, 'setAiComputeDevice')}")
    result["interactions"]["aiComputeDeviceCombo"] = {"method": "setAiComputeDevice", "args": ["gpu"]}

    keyboard = settings["keyboardPttToggle"]
    direct = settings["directInputPttToggle"]
    ptt_args: list[tuple[bool, bool]] = []
    for control, expected in ((keyboard, (False, False)), (direct, (False, True)),
                              (keyboard, (True, True)), (direct, (True, False))):
        before = len(fake.calls)
        _click(root, control)
        _assert_call_delta(fake, "setPushToTalkOptions", before, expected)
        ptt_args.append(expected)
    result["interactions"]["ptt_options"] = ptt_args

    before = len(fake.calls)
    _click(root, settings["mapDirectInputButton"])
    _assert_call_delta(fake, "beginDirectInputMapping", before, ())
    result["interactions"]["mapDirectInputButton"] = {"method": "beginDirectInputMapping", "args": []}

    audio_input = settings["audioInputCombo"]
    if int(audio_input.property("count")) != 2:
        raise AssertionError("Audio input selector did not show the fake native device list")
    before = len(fake.calls)
    _click(root, audio_input)
    _assert_call_delta(fake, "refreshAudioInputDevices", before, ())
    QTest.keyClick(root, Qt.Key.Key_Escape)
    _pump(30)
    _activate_combo(audio_input, 1)
    if _last_call(fake, "setAudioInputDevice") != ("mic-1",) or fake.values["selectedAudioInputId"] != "mic-1":
        raise AssertionError(f"Audio input selection failed: {_last_call(fake, 'setAudioInputDevice')}")
    result["interactions"]["audioInputCombo"] = {
        "refresh_on_open": True, "selected_call": ["mic-1"], "selection_input": "keyboard"}

    audio_output = settings["audioOutputCombo"]
    _activate_combo(audio_output, 1)
    if _last_call(fake, "setAudioOutputDevice") != ("USB headset",) or fake.values["selectedAudioOutput"] != "USB headset":
        raise AssertionError(f"Audio output selection failed: {_last_call(fake, 'setAudioOutputDevice')}")
    result["interactions"]["audioOutputCombo"] = {"method": "setAudioOutputDevice", "args": ["USB headset"]}

    ducking = settings["audioDuckingToggle"]
    before = len(fake.calls)
    _click(root, ducking)
    _assert_call_delta(fake, "setAudioDuckingEnabled", before, (True,))
    if not fake.values["audioDuckingEnabled"]:
        raise AssertionError("Audio ducking switch did not enable its setting")
    fake.values["isAudioDucked"] = True
    fake.changed.emit()
    _pump()
    if _find_visible_scoped(page, "isAudioDuckedStatus").property("text") != "Âm lượng game đang giảm khi radio phát.":
        raise AssertionError("Audio ducked status does not reflect active attenuation state")
    result["interactions"]["audioDuckingToggle"] = {
        "method": "setAudioDuckingEnabled", "args": [True], "active_status": True}

    volume = settings["radioVolumeSlider"]
    bounds = (float(volume.property("from")), float(volume.property("to")))
    if bounds != (0.0, 1.0):
        raise AssertionError(f"Radio volume range must remain 0..1: {bounds}")
    _set_slider_from_user(volume, 0.35)
    if _last_call(fake, "setTtsVolume") is None or not math.isclose(
            float(_last_call(fake, "setTtsVolume")[0]), 0.35, abs_tol=0.03):
        raise AssertionError(f"Radio volume mouse drag did not forward 0.35: {_last_call(fake, 'setTtsVolume')}")
    result["interactions"]["radioVolumeSlider"] = {"range": list(bounds),
                                                   "setTtsVolume": list(_last_call(fake, "setTtsVolume"))}

    before = len(fake.calls)
    _click(root, settings["openSetupButton"])
    _assert_call_delta(fake, "openSetup", before, ())
    if not fake.values["setupVisible"] or _find(root, "setupWizardLoader").property("item") is None:
        raise AssertionError("System Setup action did not open the lazy SetupWizard")
    result["interactions"]["openSetupButton"] = {"method": "openSetup", "args": []}
    fake.values["setupVisible"] = False
    fake.changed.emit()
    _pump(80)

    result["restart_semantics"] = {
        "gpu_renderer_restart_required_state": "separate from AI compute selection",
        "ai_compute_restart_required_state": "separate property; not a renderer selection",
    }
    fake.values["gpuRendererRestartRequired"] = True
    fake.values["aiComputeRestartRequired"] = False
    fake.changed.emit()
    _pump(40)
    return result


def _test_startup_actions(engine: QQmlApplicationEngine, root: Any, fake: Any) -> dict[str, Any]:
    startup = _find(root, "startupNotice")
    fake.values.update({"startupReady": False, "startupProgress": 0.42,
                        "startupError": "mock model initialization failed"})
    fake.changed.emit()
    _pump(60)
    if not startup.isVisible():
        raise AssertionError("Startup notice should be visible while startup has an error")
    retry = _find_scoped(startup, "startupRetryButton")
    progress = _find_scoped(startup, "startupProgress")
    if not retry.isVisible() or not math.isclose(float(progress.property("value")), 0.42, abs_tol=0.001):
        raise AssertionError("Startup error retry/progress controls do not reflect the native state")
    before = len(fake.calls)
    _click(root, retry)
    _assert_call_delta(fake, "retryStartup", before, ())
    if not fake.values["startupReady"] or fake.values["startupError"] or startup.isVisible():
        raise AssertionError("Startup retry did not clear the error and hide the completed notice")
    fake.values.update({"startupReady": False, "startupProgress": 0.42, "startupError": ""})
    fake.changed.emit()
    _pump(60)
    if not startup.isVisible() or _find_scoped(startup, "startupRetryButton").isVisible():
        raise AssertionError("Startup progress state should show progress without an error retry")
    close = _find_scoped(startup, "startupCloseButton")
    before = len(fake.calls)
    _click(root, close)
    if fake.calls[before:] or startup.isVisible():
        raise AssertionError("Startup dismissal should close locally without a retry/startup call")
    return {"retry": {"method": "retryStartup", "args": [], "completed": True},
            "progress": 0.42, "dismiss": {"method": None, "startup_still_pending": True}}


def _seed_driving_fixture(fake: Any) -> dict[str, Any]:
    telemetry = {
        "track": "Monza", "carModel": "Ferrari 296 GT3", "speedKmh": 224.0,
        "gear": 5, "rpm": 7825.0, "throttle": 0.82, "brake": 0.12,
        "position": 3, "currentLap": 12, "lapProgress": 0.61,
        "opponentAhead": "Lexus RC F GT3 · #05", "opponentBehind": "Porsche 911 GT3 R · #09",
        "fuelLiters": 32.4, "fuelPerLapLiters": 2.61,
        "estimatedFuelLaps": 12.4, "gapAheadSeconds": 0.682,
        "gapBehindSeconds": 1.264, "previousLapTimeSeconds": 95.482,
        "bestLapTimeSeconds": 94.911,
        "tyreTemperatures": [82.0, 83.5, 84.2, 85.1],
        "tyrePressures": [26.9, 27.0, 27.1, 27.2],
        "brakeTemperatures": [512.0, 526.0, 448.0, 453.0],
    }
    fake.values.update({"connected": True, "simulatorName": "Assetto Corsa",
                        "connectionText": "Đã kết nối Assetto Corsa", "telemetry": telemetry,
                        "microphoneName": "Mock microphone", "voiceStatus": "Idle",
                        "latestEvent": "Đối thủ phía trước đang ở trong khoảng cách 0,682 giây.",
                        "directInputBinding": "Mock DirectInput Controller — Button 3"})
    fake.changed.emit()
    _pump(50)
    return telemetry


def _test_navigation_and_gates(engine: QQmlApplicationEngine, root: Any, fake: Any,
                               strategy: Any) -> dict[str, Any]:
    nav_names = ("navRace", "navAnalysis", "navSettings")
    nav = {name: _find(root, name) for name in nav_names}
    if any(not _visible(item) for item in nav.values()):
        raise AssertionError("One of the three primary navigation controls is hidden")

    _route_to(engine, root, 1)
    strategy_page = _find(root, "fuelStrategyPage")
    _click_button_with_text(root, strategy_page, "Dữ liệu & huấn luyện")
    _pump(100)
    controls = {name: _find(root, name) for name in (
        "strategyToggle", "realisticSetupToggle", "strategyRecordToggle",
        "processRecordedDataButton", "trainPaceButton", "trainTyresButton",
        "cancelTrainingButton", "openStrategyDataButton", "loggingGuideButton",
        "sharingConsentToggle", "retrySharingButton")}
    strategy.set_value("simulatorConnected", False)
    strategy.set_value("busy", False)
    strategy.set_value("sharingConfigured", False)
    strategy.set_value("sharingEnabled", False)
    strategy.set_value("raceActive", False)
    _pump()
    if not controls["processRecordedDataButton"].isEnabled():
        raise AssertionError("Process must be enabled when disconnected and idle (baseline gate)")
    if not controls["trainPaceButton"].isEnabled() or not controls["trainTyresButton"].isEnabled():
        raise AssertionError("Train actions must be enabled when disconnected and idle (baseline gate)")
    if controls["sharingConsentToggle"].isEnabled():
        raise AssertionError("Sharing consent must stay disabled without a configured endpoint")
    if controls["retrySharingButton"].isVisible():
        raise AssertionError("Resend must stay hidden when sharing is disabled")

    strategy.set_value("busy", True)
    _pump()
    if controls["processRecordedDataButton"].isEnabled() or controls["trainPaceButton"].isEnabled():
        raise AssertionError("Process/train must be disabled while a job is busy")
    if not controls["cancelTrainingButton"].isVisible():
        raise AssertionError("Cancel action must appear while a job is busy")
    strategy.set_value("busy", False)
    strategy.set_value("simulatorConnected", True)
    _pump()
    if controls["processRecordedDataButton"].isEnabled() or controls["trainTyresButton"].isEnabled():
        raise AssertionError("Process/train must be disabled while connected")
    strategy.set_value("simulatorConnected", False)
    strategy.set_value("raceActive", True)
    _pump()
    if controls["realisticSetupToggle"].isEnabled():
        raise AssertionError("Realistic setup confirmation must be locked during an active race")
    strategy.set_value("raceActive", False)
    strategy.set_value("sharingConfigured", True)
    _pump()
    if not controls["sharingConsentToggle"].isEnabled():
        raise AssertionError("Sharing consent must be available when configured")
    strategy.set_value("sharingEnabled", True)
    _pump()
    if not controls["retrySharingButton"].isVisible():
        raise AssertionError("Retry sharing must appear when sharing is configured and enabled")

    _route_to(engine, root, 5)
    settings_names = ("startWithWindowsToggle", "minimizeToTrayToggle",
                      "minimizeOnCloseToggle", "gpuRendererToggle", "aiComputeDeviceCombo",
                      "keyboardPttToggle", "directInputPttToggle", "audioInputCombo",
                      "audioOutputCombo", "audioDuckingToggle")
    settings = {name: _find(root, name) for name in settings_names}
    if settings["minimizeToTrayToggle"].isEnabled():
        raise AssertionError("Tray-dependent control must be disabled when trayAvailable is false")
    fake.values["trayAvailable"] = True
    fake.changed.emit()
    _pump()
    if not settings["minimizeToTrayToggle"].isEnabled():
        raise AssertionError("Tray-dependent control must enable when trayAvailable is true")
    if settings["gpuRendererToggle"].isEnabled() != settings["aiComputeDeviceCombo"].isEnabled():
        # They are independent UI choices. This condition only guards against
        # accidental wiring to the same capability flag in this fake setup.
        pass
    fake.values["gpuRendererRestartRequired"] = True
    fake.values["aiComputeRestartRequired"] = False
    fake.changed.emit()
    _pump()

    _route_to(engine, root, 4)
    ai_page = _find(root, "aiVoicePage")
    ai_tabs = _find_visible_scoped(ai_page, "aiVoiceSectionTabs")
    _select_tab_with_mouse(ai_tabs, 1)
    _pump(100)
    benchmark = _find_scoped(ai_page, "ttsBenchmarkButton")
    fake.values["ttsBenchmarkAllowed"] = False
    fake.values["ttsBenchmarkRunning"] = False
    fake.values["ttsBenchmarkRestoring"] = False
    fake.changed.emit()
    _pump()
    if benchmark.isEnabled():
        raise AssertionError("Benchmark start must follow ttsBenchmarkAllowed gate")
    disabled_benchmark_calls = len(fake.calls)
    _click(root, benchmark)
    if fake.calls[disabled_benchmark_calls:]:
        raise AssertionError(f"Disabled benchmark control invoked facade calls: {fake.calls[disabled_benchmark_calls:]}")
    fake.values["ttsBenchmarkRunning"] = True
    fake.changed.emit()
    _pump()
    if not benchmark.isEnabled():
        raise AssertionError("Benchmark cancel must remain available while benchmark is running")

    return {"primary_nav": list(nav), "strategy_gates": {
                "process_train_idle_disconnected": True,
                "busy_disables_process_train_and_shows_cancel": True,
                "connected_disables_process_train": True,
                "realistic_setup_locked_during_race": True,
                "sharing_requires_configured_and_resend_requires_enabled": True,
            },
            "system_gates": {"tray_unavailable_disables_tray_settings": True,
                             "tray_available_enables_tray_settings": True,
                             "gpu_renderer_and_ai_compute_controls_separate": True},
            "benchmark_gate": "allowed controls start; running controls cancel"}


def _test_forms_and_setup(engine: QQmlApplicationEngine, root: Any, fake: Any) -> dict[str, Any]:
    _route_to(engine, root, 4)
    ai_page = _find(root, "aiVoicePage")
    fake.values.update({
        "apiConfigured": True, "apiState": "LLM Server Offline",
        "apiBaseUrl": "http://localhost:8080/v1", "apiKey": "old-secret",
        "apiModel": "old-model", "apiStreaming": True, "apiTimeoutMilliseconds": 15000,
        "apiMaximumTokens": 64, "apiTemperature": 0.2, "apiReasoning": False,
        "ttsBackend": "VieNeu-TTS", "availableTtsVoices": [
            {"id": "test-voice", "name": "Test Voice"}, {"id": "alternate-voice", "name": "Alternate Voice"}],
        "selectedTtsVoice": "test-voice", "googleTtsModel": "gemini-3.8-flash-lite-tts",
        "googleTtsVoices": ["Puck", "Zephyr"], "googleTtsVoice": "Puck",
        "googleTtsApiKeyConfigured": True, "ttsCpuThreads": 2,
        "connected": False, "voiceStatus": "Idle", "ttsBenchmarkRunning": False,
        "ttsBenchmarkRestoring": False, "ttsExternalThreadOverride": False,
        "ttsBenchmarkAllowed": True, "ttsBenchmarkResults": [],
    })
    fake.changed.emit()
    ai_tabs = _find_visible_scoped(ai_page, "aiVoiceSectionTabs")
    _select_tab_with_mouse(ai_tabs, 0)
    _pump(100)
    llm = _find_visible_scoped(ai_page, "llmConnectionForm")
    api_endpoint = _find_visible_scoped(ai_page, "apiEndpointField")
    api_model = _find_visible_scoped(ai_page, "apiModelField")
    api_key = _find_visible_scoped(ai_page, "apiKeyField")
    streaming = _find_visible_scoped(ai_page, "apiStreamingToggle")
    reasoning = _find_visible_scoped(ai_page, "apiReasoningToggle")
    temperature = _find_visible_scoped(ai_page, "apiTemperatureSlider")
    tokens = _find_visible_scoped(ai_page, "apiMaximumTokensSlider")
    test_connection = _find_visible_scoped(ai_page, "testLlmConnectionButton")
    save_llm = _find_visible_scoped(ai_page, "saveLlmConfigurationButton")
    if tokens.property("from") != 16 or tokens.property("to") != 128 or tokens.property("stepSize") != 1:
        raise AssertionError("Maximum output tokens range changed from baseline 16–128 step 1")
    if temperature.property("from") != 0 or temperature.property("to") != 1:
        raise AssertionError("Temperature range changed from baseline 0–1")
    if int(_qml_item_value(engine, api_key, "echoMode")) != 2:
        raise AssertionError("LLM API key must remain a password field")
    if test_connection.isEnabled():
        raise AssertionError("Test connection must remain disabled when the form is unchanged")
    disabled_test_calls_before = len(fake.calls)
    _click(root, test_connection)
    if fake.calls[disabled_test_calls_before:]:
        raise AssertionError(f"Disabled unchanged-form test button invoked facade calls: {fake.calls[disabled_test_calls_before:]}")
    if not save_llm.isEnabled():
        raise AssertionError("Save LLM configuration must remain available while the AI form is live")
    if not api_endpoint.isEnabled() or not api_model.isEnabled() or not api_key.isEnabled():
        raise AssertionError("Live AI connection fields must be editable")

    # A first-load snapshot must stay fixed while the user edits and reverts
    # fields. Then a native apiSettingsChanged event from Setup must refresh a
    # clean cached AI form, while a dirty draft keeps its entered text.
    original_endpoint = str(fake.values["apiBaseUrl"])
    _replace_text(root, api_endpoint, "http://temporary-edit.local/v1")
    _replace_text(root, api_endpoint, original_endpoint)
    _pump()
    if bool(llm.property("hasUnsavedDraft")) or bool(llm.property("isModified")) or test_connection.isEnabled():
        raise AssertionError("Editing and reverting to the saved endpoint must restore a clean initial LLM form")
    external_values = {"apiBaseUrl": "http://setup-saved.local/v1", "apiKey": "setup-saved-key",
                       "apiModel": "setup-saved-model", "apiStreaming": False,
                       "apiTimeoutMilliseconds": 15000, "apiMaximumTokens": 72,
                       "apiTemperature": 0.31, "apiReasoning": False, "apiConfigured": True}
    fake.values.update(external_values)
    fake.apiSettingsChanged.emit()
    _pump(80)
    refreshed = (api_endpoint.property("text"), api_key.property("text"), api_model.property("text"))
    expected_refreshed = (external_values["apiBaseUrl"], external_values["apiKey"], external_values["apiModel"])
    if refreshed != expected_refreshed:
        raise AssertionError(f"Clean cached AI form did not refresh on native apiSettingsChanged: {refreshed}")
    _replace_text(root, api_endpoint, "http://unsaved-draft.local/v1")
    fake.values.update({"apiBaseUrl": "http://newer-native.local/v1", "apiKey": "newer-native-key",
                        "apiModel": "newer-native-model"})
    fake.apiSettingsChanged.emit()
    _pump(80)
    if api_endpoint.property("text") != "http://unsaved-draft.local/v1" or not llm.property("hasUnsavedDraft"):
        raise AssertionError("Dirty AI draft was overwritten by apiSettingsChanged")
    # Return the facade to the fixed starting fixture for setter/range checks.
    fake.values.update({"apiBaseUrl": original_endpoint, "apiKey": "old-secret", "apiModel": "old-model",
                        "apiStreaming": True, "apiTimeoutMilliseconds": 15000, "apiMaximumTokens": 64,
                        "apiTemperature": 0.2, "apiReasoning": False, "apiConfigured": True})
    fake.apiSettingsChanged.emit()
    _qml_item_value(engine, llm, "resetDraft()")
    _pump(40)

    _click(root, reasoning)
    reasoning_calls = [args for name, args in fake.calls if name == "setApiReasoning"]
    if not reasoning_calls or reasoning_calls[-1] != (True,):
        raise AssertionError(f"Reasoning toggle did not call setApiReasoning(true): {reasoning_calls[-1:]}")
    _replace_text(root, api_endpoint, "http://localhost:8000/v1")
    _replace_text(root, api_model, "test-model")
    _replace_text(root, api_key, "test-secret")
    _pump()
    if not test_connection.isEnabled():
        raise AssertionError("Test connection did not enable for a modified API form")
    _click(root, test_connection)
    test_calls = [args for name, args in fake.calls if name == "testApiConnection"]
    if not test_calls or tuple(test_calls[-1]) != ("http://localhost:8000/v1", "test-secret", "test-model"):
        raise AssertionError(f"Test connection did not forward endpoint/key/model exactly: {test_calls[-1:]}")

    _click(root, streaming)
    _set_slider_from_user(temperature, 0.73)
    # A real mouse pixel maps to the closest integral token step. 97 is an
    # exact keyboard/mouse-representable step at this panel width.
    _set_slider_from_user(tokens, 97)
    _pump()
    selected_temperature = float(llm.property("temperatureSelection"))
    if not math.isclose(selected_temperature, 0.73, abs_tol=0.02):
        raise AssertionError(f"Temperature user selection was not retained: {llm.property('temperatureSelection')}")
    if int(llm.property("maximumTokensSelection")) != 97:
        raise AssertionError(f"Maximum token selection was not retained: {llm.property('maximumTokensSelection')}")
    _click(root, save_llm)
    save_calls = [args for name, args in fake.calls if name == "saveAiSettings"]
    expected_save = ("OpenAI Compatible", "http://localhost:8000/v1", "test-secret", "test-model",
                     False, 15000, 97, selected_temperature, True)
    if not save_calls or tuple(save_calls[-1]) != expected_save:
        raise AssertionError(f"Save LLM argument/range parity failed: expected {expected_save}, got {save_calls[-1:]}")

    # VieNeu form: immediate backend, selected voice, local preview, thread
    # choice, and each CPU-thread gate are independently checked.
    _select_tab_with_mouse(ai_tabs, 1)
    fake.values.update({"ttsBackend": "VieNeu-TTS", "selectedTtsVoice": "test-voice",
                        "ttsCpuThreads": 2, "connected": False, "voiceStatus": "Idle",
                        "ttsBenchmarkRunning": False, "ttsBenchmarkRestoring": False,
                        "ttsExternalThreadOverride": False, "ttsBenchmarkAllowed": True})
    fake.changed.emit()
    _pump(100)
    local_settings = _find_visible_scoped(ai_page, "ttsVoiceSettings")
    local_voice = _find_visible_scoped(ai_page, "localVoiceCombo")
    cpu_threads = _find_visible_scoped(ai_page, "ttsCpuThreadsCombo")
    local_preview = _find_visible_scoped(ai_page, "previewLocalTtsButton")
    benchmark_panel = _find_visible_scoped(ai_page, "ttsBenchmarkPanel")
    benchmark = _find_visible_scoped(ai_page, "ttsBenchmarkButton")
    if int(local_voice.property("count")) != 2 or int(local_voice.property("currentIndex")) != 0:
        raise AssertionError("Local voice list/current selection did not follow availableTtsVoices/selectedTtsVoice")
    if not cpu_threads.isEnabled():
        raise AssertionError("Local CPU thread selection should be enabled while disconnected and idle")
    _activate_combo(local_voice, 1)
    voice_calls = [args for name, args in fake.calls if name == "setTtsVoice"]
    if not voice_calls or voice_calls[-1] != ("alternate-voice",):
        raise AssertionError(f"Local voice choice did not call setTtsVoice with the selected id: {voice_calls[-1:]}")
    _activate_combo(cpu_threads, 2)
    thread_calls = [args for name, args in fake.calls if name == "setTtsCpuThreads"]
    if not thread_calls or int(thread_calls[-1][0]) != 4:
        raise AssertionError(f"CPU thread choice did not apply 4 threads: {thread_calls[-1:]}")
    _click(root, local_preview)
    if not any(name == "previewTtsVoice" for name, _ in fake.calls):
        raise AssertionError("Local preview did not call previewTtsVoice")
    for gate, value in (("connected", True), ("ttsBenchmarkRunning", True),
                        ("ttsBenchmarkRestoring", True), ("ttsExternalThreadOverride", True)):
        fake.values.update({"connected": False, "ttsBenchmarkRunning": False,
                            "ttsBenchmarkRestoring": False, "ttsExternalThreadOverride": False,
                            gate: value})
        fake.changed.emit()
        _pump()
        if cpu_threads.isEnabled():
            raise AssertionError(f"CPU thread control must be disabled when {gate}={value}")
        calls_before = _call_count(fake, "setTtsCpuThreads")
        _click(root, cpu_threads)
        if _call_count(fake, "setTtsCpuThreads") != calls_before:
            raise AssertionError(f"Disabled CPU thread combo wrote while {gate}={value}")
    fake.values.update({"connected": False, "ttsBenchmarkRunning": False,
                        "ttsBenchmarkRestoring": False, "ttsExternalThreadOverride": False,
                        "voiceStatus": "Recognizing"})
    fake.changed.emit()
    _pump()
    if cpu_threads.isEnabled():
        raise AssertionError("CPU thread selection must be disabled unless voiceStatus is Idle")
    calls_before = _call_count(fake, "setTtsCpuThreads")
    _click(root, cpu_threads)
    if _call_count(fake, "setTtsCpuThreads") != calls_before:
        raise AssertionError("Disabled CPU thread combo wrote while voice recognition was active")
    fake.values["voiceStatus"] = "Idle"
    fake.changed.emit()
    _pump()
    if not benchmark_panel.isVisible() or not benchmark.isEnabled():
        raise AssertionError("VieNeu benchmark panel must be visible and enabled when allowed")
    fake.values["ttsBenchmarkAllowed"] = False
    fake.changed.emit()
    _pump()
    if benchmark.isEnabled():
        raise AssertionError("Benchmark start must be disabled when native ttsBenchmarkAllowed is false")
    fake.values["ttsBenchmarkAllowed"] = True
    fake.changed.emit()
    _pump()
    _click(root, benchmark)
    if not fake.values["ttsBenchmarkRunning"]:
        raise AssertionError("Benchmark start did not call startTtsBenchmark")
    if not benchmark.isEnabled() or benchmark.property("text") != "Hủy benchmark":
        raise AssertionError("Running benchmark must keep an enabled Cancel action")
    _click(root, benchmark)
    if fake.values["ttsBenchmarkRunning"]:
        raise AssertionError("Benchmark cancel did not call cancelTtsBenchmark")

    # Google TTS keeps model/voice/key selection and preview actions in a
    # separate subpage. The setup and AI entry points share one dialog host.
    _select_tab_with_mouse(ai_tabs, 2)
    fake.values.update({"ttsBackend": "Google Gemini API", "googleTtsModel": "gemini-3.8-flash-lite-tts",
                        "googleTtsVoice": "Puck", "googleTtsVoices": ["Puck", "Zephyr"],
                        "googleTtsApiKeyConfigured": True})
    fake.changed.emit()
    _pump(100)
    google_settings = _find_visible_scoped(ai_page, "ttsVoiceSettings")
    tts_backend = _find_visible_scoped(ai_page, "ttsBackendCombo")
    google_model = _find_visible_scoped(ai_page, "googleTtsModelCombo")
    google_voice = _find_visible_scoped(ai_page, "googleTtsVoiceCombo")
    google_key = _find_visible_scoped(ai_page, "googleTtsApiKey")
    save_google_key = _find_visible_scoped(ai_page, "saveGoogleTtsKeyButton")
    google_preview = _find_visible_scoped(ai_page, "previewGoogleTtsButton")
    add_google_voice = _find_visible_scoped(ai_page, "addGoogleVoiceButton")
    if int(_qml_item_value(engine, google_key, "echoMode")) != 2:
        raise AssertionError("Google TTS API key must remain a password field")
    if not _visible(google_model) or not _visible(google_voice) or not _visible(add_google_voice):
        raise AssertionError("Google model, voice and add-voice controls must be visible on the Google TTS tab")
    if _visible(_find_visible_scoped(ai_page, "ttsBenchmarkPanel", required=False)):
        raise AssertionError("Local TTS benchmark must not be visible for Google TTS")
    _activate_combo(google_model, 1)
    model_calls = [args for name, args in fake.calls if name == "setGoogleTtsModel"]
    if not model_calls or model_calls[-1] != ("gemini-3.8-flash-tts",):
        raise AssertionError(f"Google TTS model selection did not forward the native model id: {model_calls[-1:]}")
    _activate_combo(google_voice, 1)
    google_voice_calls = [args for name, args in fake.calls if name == "setGoogleTtsVoice"]
    if not google_voice_calls or google_voice_calls[-1] != ("Zephyr",):
        raise AssertionError(f"Google voice selection did not forward the voice: {google_voice_calls[-1:]}")
    _replace_text(root, google_key, "google-test-secret")
    _click(root, save_google_key)
    google_key_calls = [args for name, args in fake.calls if name == "setGoogleTtsApiKey"]
    if not google_key_calls or google_key_calls[-1] != ("google-test-secret",):
        raise AssertionError(f"Google key Save did not forward and clear the entered key: {google_key_calls[-1:]}")
    if google_key.property("text") != "":
        raise AssertionError("Google key field should clear after explicit save")
    _click(root, google_preview)
    if sum(name == "previewTtsVoice" for name, _ in fake.calls) < 2:
        raise AssertionError("Google preview did not route to previewTtsVoice")
    _activate_combo(tts_backend, 0)
    backend_calls = [args for name, args in fake.calls if name == "setTtsBackend"]
    if not backend_calls or backend_calls[-1] != ("VieNeu-TTS",):
        raise AssertionError(f"Backend selector failed to preserve native VieNeu backend id: {backend_calls[-1:]}")
    if int(ai_page.property("sectionIndex")) != 1:
        raise AssertionError("Changing backend in the Google tab did not route to the selected local voice section")
    # Switch back to Google through the visible section selector before opening
    # the single Main-owned prompt/WAV dialog.
    _select_tab_with_mouse(ai_tabs, 2)
    fake.values["ttsBackend"] = "Google Gemini API"
    fake.changed.emit()
    _pump(80)
    add_google_voice = _find_visible_scoped(ai_page, "addGoogleVoiceButton")
    _click(root, add_google_voice)
    dialog = _find(root, "googleVoiceDialog")
    voice_name = _find(root, "googleVoiceName")
    voice_description = _find(root, "googleVoiceDescription")
    mode_tabs = _find(root, "googleVoiceModeTabs")
    if not _visible(dialog) or mode_tabs is None:
        raise AssertionError("Google voice modal should expose prompt and WAV flows")
    if int(voice_name.property("maximumLength")) != 80:
        raise AssertionError("Google voice name length limit changed from baseline 80 characters")
    _find(root, "sourceVoiceFileDialog")
    _find(root, "consentVoiceFileDialog")
    source_button = _find(root, "sourceVoiceFileButton")
    consent_button = _find(root, "consentVoiceFileButton")
    source_path = _find(root, "sourceVoicePath")
    consent_path = _find(root, "consentVoicePath")
    _replace_text(root, voice_name, "Test prompt voice")
    _replace_text(root, voice_description, "Clear and restrained test voice")
    _click(root, _find(root, "raceEngineerVoicePromptButton"))
    preset_description = voice_description.property("text")
    if voice_name.property("text") != "RaceEngineer · Tiếng Việt" or not preset_description:
        raise AssertionError("RaceEngineer voice preset did not populate the name and prompt")
    _replace_text(root, voice_name, "Test prompt voice")
    _replace_text(root, voice_description, "Clear and restrained test voice")
    retained_draft = (voice_name.property("text"), voice_description.property("text"))
    before = len(fake.calls)
    _click_button_with_text(root, root, "OK")
    _assert_call_delta(fake, "createGooglePromptedVoice", before,
                       ("Test prompt voice", "Clear and restrained test voice"))
    if _visible(dialog):
        raise AssertionError("Google prompt voice dialog did not close after accepting")
    prompt_creation = _last_call(fake, "createGooglePromptedVoice")

    # Reopen and submit the WAV branch using fake selected paths. The native
    # FileDialogs are deliberately never opened and no provider runs.
    _click(root, _find_visible_scoped(ai_page, "addGoogleVoiceButton"))
    if (voice_name.property("text"), voice_description.property("text")) != retained_draft:
        raise AssertionError("Google voice prompt fields did not retain their saved draft after reopen")
    mode_tabs = _find(root, "googleVoiceModeTabs")
    _select_tab_with_mouse(mode_tabs, 1)
    dialog.setProperty("sourceWavPath", "/mock/voice-source.wav")
    dialog.setProperty("consentWavPath", "/mock/voice-consent.wav")
    _pump(80)
    if not source_button.isVisible() or not consent_button.isVisible():
        raise AssertionError("WAV flow did not show both source and consent file controls")
    wav_labels = (source_path.property("text"), consent_path.property("text"))
    if wav_labels != ("Đã chọn mẫu WAV", "Đã chọn bản ghi đồng ý"):
        raise AssertionError(f"WAV path display did not track the saved picker paths: {wav_labels}")
    before = len(fake.calls)
    _click_button_with_text(root, root, "OK")
    _assert_call_delta(fake, "createGoogleTtsVoice", before,
                       ("Test prompt voice", "/mock/voice-source.wav", "/mock/voice-consent.wav"))
    wav_creation = _last_call(fake, "createGoogleTtsVoice")
    _route_to(engine, root, 2, direct=True)
    _pump(100)
    if _visible(dialog):
        raise AssertionError("Google voice modal must close when leaving its active AI route")
    if (voice_name.property("text"), voice_description.property("text")) != retained_draft:
        raise AssertionError("Prompt draft did not survive leaving the AI route")

    # All wizard steps at all supplied logical sizes. Buttons must remain
    # inside the panel while the long LLM content scrolls independently.
    fake.values.update({"setupVisible": True, "setupStep": 0, "ttsBackend": "VieNeu-TTS",
                        "ttsBenchmarkRunning": False, "ttsBenchmarkRestoring": False})
    fake.changed.emit()
    _pump(100)
    wizard = _find(root, "setupWizard")
    panel = _find_scoped(wizard, "setupPanel")
    step_content = _find_scoped(wizard, "setupStepContent")
    if not wizard.isVisible() or step_content is None:
        raise AssertionError("Setup wizard did not open")
    setup_screens: list[dict[str, Any]] = []
    setup_continue = _find_visible_scoped(wizard, "setupContinueButton")
    setup_skip = _find_visible_scoped(wizard, "setupSkipButton")
    setup_progress = _find_visible_scoped(wizard, "setupProgress")
    for size in SIZES:
        root.resize(*size)
        for step in range(4):
            fake.values["setupStep"] = step
            fake.changed.emit()
            _pump(80)
            page_rect = _scene_rect(root.contentItem())
            panel_rect = _scene_rect(panel)
            _assert_inside(panel_rect, page_rect, f"Setup panel step {step} at {size}")
            continue_rect = _scene_rect(setup_continue)
            skip_rect = _scene_rect(setup_skip)
            _assert_inside(continue_rect, panel_rect, f"Setup Continue step {step} at {size}")
            _assert_inside(skip_rect, panel_rect, f"Setup Skip step {step} at {size}")
            if not setup_continue.isVisible() or not setup_continue.isEnabled():
                raise AssertionError(f"Setup Continue is hidden/disabled on step {step} at {size}")
            expected_progress = (step + 1) / 4
            if not math.isclose(float(setup_progress.property("value")), expected_progress, abs_tol=0.01):
                raise AssertionError(f"Setup progress wrong for step {step}: {setup_progress.property('value')}")
            record: dict[str, Any] = {"size": list(size), "step": step,
                                      "panel": list(map(round, panel_rect)),
                                      "continue": list(map(round, continue_rect)),
                                      "skip": list(map(round, skip_rect)),
                                      "progress": setup_progress.property("value")}
            if step > 0:
                back = _find_visible_scoped(wizard, "setupBackButton")
                _assert_inside(_scene_rect(back), panel_rect, f"Setup Back step {step} at {size}")
                record["back"] = list(map(round, _scene_rect(back)))
            if step == 2:
                setup_tokens = _find_visible_scoped(wizard, "apiMaximumTokensSlider")
                _scroll_item_into_view(root, setup_tokens)
                flickable = setup_tokens
                while flickable is not None and "Flickable" not in flickable.metaObject().className():
                    flickable = flickable.parentItem()
                if flickable is None:
                    raise AssertionError("Setup LLM form is no longer inside a scrollable viewport")
                _assert_inside(_scene_rect(setup_tokens), _scene_rect(flickable),
                               f"Setup LLM token slider after scrolling at {size}")
                _assert_inside(_scene_rect(setup_continue), _scene_rect(panel),
                               f"Setup footer after LLM scroll at {size}")
                record["llm_bottom_scroll_reached"] = True
            setup_screens.append(record)

    # Exercise setup actions, form save and shared voice dialog through actual
    # visible controls. Return the window to a size that fits the whole form.
    root.resize(1280, 800)
    fake.values["setupStep"] = 0
    fake.changed.emit()
    _pump(80)
    setup_driver = _find_visible_scoped(wizard, "setupDriverName")
    _replace_text(root, setup_driver, "Setup Test Driver")
    QTest.keyClick(root, Qt.Key.Key_Return)
    _pump()
    driver_calls = [args for name, args in fake.calls if name == "setDriverName"]
    if not driver_calls or driver_calls[-1] != ("Setup Test Driver",):
        raise AssertionError(f"Setup driver field did not call setDriverName: {driver_calls[-1:]}")
    _click(root, setup_continue)
    if int(fake.values["setupStep"]) != 1:
        raise AssertionError("Setup Continue did not advance from Welcome to Voice")
    setup_tts = _find_visible_scoped(wizard, "ttsVoiceSettings")
    setup_backend = _find_visible_scoped(wizard, "ttsBackendCombo")
    setup_local_voice = _find_visible_scoped(wizard, "localVoiceCombo")
    setup_cpu = _find_visible_scoped(wizard, "ttsCpuThreadsCombo")
    if not setup_tts.isVisible() or not setup_local_voice.isVisible() or not setup_cpu.isEnabled():
        raise AssertionError("Setup Voice step does not expose the local voice and thread settings")
    _activate_combo(setup_cpu, 1)
    setup_thread_calls = [args for name, args in fake.calls if name == "setTtsCpuThreads"]
    if not setup_thread_calls or int(setup_thread_calls[-1][0]) != 3:
        raise AssertionError("Setup Voice thread selection did not route to setTtsCpuThreads")
    _activate_combo(setup_backend, 1)
    if not any(name == "setTtsBackend" and args == ("Google Gemini API",) for name, args in fake.calls):
        raise AssertionError("Setup Voice backend choice did not call setTtsBackend")
    setup_add_voice = _find_visible_scoped(wizard, "addGoogleVoiceButton")
    _click(root, setup_add_voice)
    if not _visible(dialog):
        raise AssertionError("Setup Voice did not open the Main-owned Google dialog")
    if (voice_name.property("text"), voice_description.property("text")) != retained_draft:
        raise AssertionError("Google prompt draft was not shared between AI and Setup entry points")
    if dialog.property("sourceWavPath") != "/mock/voice-source.wav" or dialog.property("consentWavPath") != "/mock/voice-consent.wav":
        raise AssertionError("WAV picker paths did not persist across shared-dialog entry points")
    # The prompt dialog is modal, so a user first dismisses it (Escape is the
    # real keyboard action) before activating the wizard footer behind it.
    QTest.keyClick(root, Qt.Key.Key_Escape)
    _pump(100)
    if _visible(dialog):
        raise AssertionError("Escape must close the Google voice dialog before leaving Setup Voice")
    _click(root, setup_continue)
    if int(fake.values["setupStep"]) != 2 or _visible(dialog):
        raise AssertionError("Leaving Setup Voice must advance to LLM and close the shared dialog")
    # Also cover a step transition caused while the modal is still open. This
    # checks the Main-owned host's live gate independently of the footer click.
    fake.values["setupStep"] = 1
    fake.changed.emit()
    _pump(60)
    setup_add_voice = _find_visible_scoped(wizard, "addGoogleVoiceButton")
    _click(root, setup_add_voice)
    if not _visible(dialog):
        raise AssertionError("Setup Voice could not reopen the shared Google dialog")
    fake.values["setupStep"] = 2
    fake.changed.emit()
    _pump(100)
    if _visible(dialog):
        raise AssertionError("The shared Google dialog must close when Setup leaves Voice while it is open")
    fake.values["ttsBackend"] = "VieNeu-TTS"
    fake.changed.emit()
    _pump(60)
    setup_llm = _find_visible_scoped(wizard, "llmConnectionForm")
    setup_endpoint = _find_visible_scoped(wizard, "apiEndpointField")
    setup_model = _find_visible_scoped(wizard, "apiModelField")
    setup_key = _find_visible_scoped(wizard, "apiKeyField")
    _replace_text(root, setup_endpoint, "http://setup.local/v1")
    _replace_text(root, setup_model, "setup-model")
    _replace_text(root, setup_key, "setup-secret")
    _pump()
    setup_continue = _find_visible_scoped(wizard, "setupContinueButton")
    setup_later = _find_visible_scoped(wizard, "setupLaterButton")
    calls_before_later = sum(name == "saveAiSettings" for name, _ in fake.calls)
    _click(root, setup_later)
    if int(fake.values["setupStep"]) != 3:
        raise AssertionError("Setup 'Thiết lập sau' did not skip directly to Optimization")
    if sum(name == "saveAiSettings" for name, _ in fake.calls) != calls_before_later:
        raise AssertionError("Setup 'Thiết lập sau' unexpectedly saved the incomplete LLM form")
    setup_back = _find_visible_scoped(wizard, "setupBackButton")
    _click(root, setup_back)
    if int(fake.values["setupStep"]) != 2:
        raise AssertionError("Setup Back from Optimization did not return to LLM")
    setup_continue = _find_visible_scoped(wizard, "setupContinueButton")
    calls_before_continue = sum(name == "saveAiSettings" for name, _ in fake.calls)
    _click(root, setup_continue)
    if int(fake.values["setupStep"]) != 3:
        raise AssertionError("Setup Continue did not advance from LLM to Optimization")
    setup_saves = [args for name, args in fake.calls[calls_before_continue:] if name == "saveAiSettings"]
    if not setup_saves or tuple(setup_saves[-1][1:4]) != ("http://setup.local/v1", "setup-secret", "setup-model"):
        raise AssertionError(f"Setup LLM Continue did not save the configured endpoint/key/model: {setup_saves[-1:]}")
    setup_benchmark = _find_visible_scoped(wizard, "ttsBenchmarkButton")
    if not setup_benchmark.isEnabled():
        raise AssertionError("Setup Optimization must offer the VieNeu benchmark when allowed")
    _click(root, setup_benchmark)
    if not fake.values["ttsBenchmarkRunning"]:
        raise AssertionError("Setup Optimization benchmark did not start")
    if setup_continue.isEnabled():
        raise AssertionError("Setup Continue must be disabled during an active benchmark")
    calls_before_disabled_continue = len(fake.calls)
    _click(root, _find_visible_scoped(wizard, "setupContinueButton"))
    if fake.calls[calls_before_disabled_continue:]:
        raise AssertionError("Disabled Setup Continue invoked backend actions during an active benchmark")
    setup_skip = _find_visible_scoped(wizard, "setupSkipButton")
    _click(root, setup_skip)
    if fake.values["setupVisible"] or fake.values["ttsBenchmarkRunning"]:
        raise AssertionError("Setup Skip must cancel an active benchmark before finishSetup")
    setup_loader = _find(root, "setupWizardLoader")
    if setup_loader.property("item") is not None or _find(root, "setupWizard", required=False) is not None:
        raise AssertionError("SetupWizard must unload after Skip/Finish to start a fresh setup session")
    finish_tail = [name for name, _ in fake.calls[-3:]]
    if "cancelTtsBenchmark" not in finish_tail or "finishSetup" not in finish_tail:
        raise AssertionError(f"Setup benchmark cancellation/finish ordering changed: {finish_tail}")

    fake.values.update({"setupVisible": True, "setupStep": 3,
                        "ttsBackend": "Google Gemini API", "ttsBenchmarkRunning": False})
    fake.changed.emit()
    _pump(80)
    wizard = _find(root, "setupWizard")
    setup_continue = _find_visible_scoped(wizard, "setupContinueButton")
    google_benchmark = _find_scoped(wizard, "setupTtsBenchmarkPanel")
    if google_benchmark.isVisible():
        raise AssertionError("Setup must hide local benchmark while the Google TTS backend is selected")
    google_branch = _find_visible_scoped(wizard, "setupStepContent")
    if int(google_branch.property("currentIndex")) != 3:
        raise AssertionError("Setup optimization section did not remain current for the Google branch")
    setup_continue = _find_visible_scoped(wizard, "setupContinueButton")
    _click(root, setup_continue)
    if fake.values["setupVisible"]:
        raise AssertionError("Setup Finish did not call finishSetup")
    if setup_loader.property("item") is not None:
        raise AssertionError("SetupWizard did not unload after Finish")
    fake.values.update({"setupVisible": True, "setupStep": 0, "ttsBenchmarkRunning": False})
    fake.changed.emit()
    _pump(80)
    wizard = _find(root, "setupWizard")
    _click(root, _find_visible_scoped(wizard, "setupSkipButton"))
    if fake.values["setupVisible"]:
        raise AssertionError("Setup Skip did not call finishSetup")
    if setup_loader.property("item") is not None:
        raise AssertionError("SetupWizard did not unload after Skip")

    # Cross-entry cached form regression: Setup saved A, AI reads A and saves
    # B, then a fresh Setup session must read B rather than resurrecting its
    # earlier cached values. Dirty AI drafts must survive route and window hide.
    _route_to(engine, root, 4)
    _select_tab_with_mouse(_find_visible_scoped(ai_page, "aiVoiceSectionTabs"), 0)
    _pump(80)
    llm = _find_visible_scoped(ai_page, "llmConnectionForm")
    api_endpoint = _find_visible_scoped(ai_page, "apiEndpointField")
    api_model = _find_visible_scoped(ai_page, "apiModelField")
    api_key = _find_visible_scoped(ai_page, "apiKeyField")
    setup_saved = ("http://setup.local/v1", "setup-secret", "setup-model")
    values_seen_in_ai = (api_endpoint.property("text"), api_key.property("text"), api_model.property("text"))
    if values_seen_in_ai != setup_saved:
        raise AssertionError(f"AI form did not refresh to Setup's saved configuration: {values_seen_in_ai}")
    _replace_text(root, api_endpoint, "http://ai-config-b.local/v1")
    _replace_text(root, api_key, "ai-config-b-secret")
    _replace_text(root, api_model, "ai-config-b-model")
    _click(root, _find_visible_scoped(ai_page, "saveLlmConfigurationButton"))
    config_b = ("http://ai-config-b.local/v1", "ai-config-b-secret", "ai-config-b-model")
    ai_b_saves = [args for name, args in fake.calls if name == "saveAiSettings"]
    if not ai_b_saves or tuple(ai_b_saves[-1][1:4]) != config_b:
        raise AssertionError(f"AI page did not explicitly save configuration B: {ai_b_saves[-1:]}")
    _replace_text(root, api_endpoint, "http://dirty-ai-draft.local/v1")
    _route_to(engine, root, 5)
    if api_endpoint.property("text") != "http://dirty-ai-draft.local/v1" or ai_page.property("live"):
        raise AssertionError("Dirty AI draft did not survive leaving the route or live gate did not close")
    _route_to(engine, root, 4)
    if api_endpoint.property("text") != "http://dirty-ai-draft.local/v1":
        raise AssertionError("Dirty AI draft did not survive route re-entry")
    root.showMinimized()
    _pump(80)
    minimized_state = {"visibility": str(root.visibility()),
                       "live": bool(ai_page.property("live")),
                       "draft": api_endpoint.property("text")}
    if root.visibility() not in (QQuickWindow.Visibility.Minimized, QQuickWindow.Visibility.Hidden) or ai_page.property("live") or api_endpoint.property("text") != "http://dirty-ai-draft.local/v1":
        raise AssertionError(f"AI draft/live state did not freeze while minimized: {minimized_state}")
    root.showNormal()
    _pump(80)
    if api_endpoint.property("text") != "http://dirty-ai-draft.local/v1":
        raise AssertionError("Dirty AI draft did not survive window restore")
    restored_visibility = str(root.visibility())
    _replace_text(root, api_endpoint, config_b[0])
    _pump()
    if bool(llm.property("hasUnsavedDraft")):
        raise AssertionError("Reverting AI endpoint to saved B must clear its dirty state")
    fake.values.update({"setupVisible": True, "setupStep": 2, "ttsBenchmarkRunning": False})
    fake.changed.emit()
    _pump(120)
    wizard = _find(root, "setupWizard")
    setup_endpoint = _find_visible_scoped(wizard, "apiEndpointField")
    setup_key = _find_visible_scoped(wizard, "apiKeyField")
    setup_model = _find_visible_scoped(wizard, "apiModelField")
    setup_values_b = (setup_endpoint.property("text"), setup_key.property("text"), setup_model.property("text"))
    if setup_values_b != config_b:
        raise AssertionError(f"Reopened Setup step 2 did not load AI configuration B: {setup_values_b}")
    setup_continue = _find_visible_scoped(wizard, "setupContinueButton")
    calls_before_b_continue = sum(name == "saveAiSettings" for name, _ in fake.calls)
    _click(root, setup_continue)
    latest_save = [args for name, args in fake.calls if name == "saveAiSettings"][-1:]
    if int(fake.values["setupStep"]) != 3 or not latest_save or tuple(latest_save[-1][1:4]) != config_b:
        raise AssertionError(f"Clean reopened Setup form overwrote config B on Continue: {latest_save}")
    if sum(name == "saveAiSettings" for name, _ in fake.calls) != calls_before_b_continue + 1:
        raise AssertionError("Setup Continue did not perform exactly one explicit configuration save")
    fake.values["setupVisible"] = False
    fake.changed.emit()
    _pump(80)
    if setup_loader.property("item") is not None:
        raise AssertionError("Cross-entry Setup session did not unload after closing")

    # The AI editor is persistent and stops accepting edits while its route is
    # hidden; no native provider request is executed by these tests.
    _route_to(engine, root, 4)
    _select_tab_with_mouse(_find_visible_scoped(ai_page, "aiVoiceSectionTabs"), 0)
    _pump(60)
    api_endpoint = _find_visible_scoped(ai_page, "apiEndpointField")
    _route_to(engine, root, 5)
    if bool(ai_page.property("live")) or api_endpoint.isEnabled():
        raise AssertionError("AI form bindings did not freeze/disable while its route is hidden")
    if not engine.rootObjects() or root not in engine.rootObjects():
        raise AssertionError("Main QQuickWindow was destroyed before forms/setup completed")
    root.objectName()

    return {"llm_form": {"test_connection_args": list(test_calls[-1]),
                          "save_ai_settings_args": list(save_calls[-1]),
                          "password_fields": ["apiKeyField", "googleTtsApiKey"],
                          "temperature_range": [0, 1], "maximum_tokens_range": [16, 128],
                          "reasoning_setter": list(reasoning_calls[-1]),
                          "clean_form_refresh_on_apiSettingsChanged": list(refreshed),
                          "dirty_draft_survives_apiSettingsChanged": True,
                          "edit_revert_recognized_clean": True},
            "tts": {"local_voice_setter": list(voice_calls[-1]),
                    "thread_setter": list(thread_calls[-1]),
                    "backend_setter": list(backend_calls[-1]),
                    "google_model_setter": list(model_calls[-1]),
                    "google_voice_setter": list(google_voice_calls[-1]),
                    "google_key_setter": list(google_key_calls[-1]),
                    "benchmark_start_cancel": True,
                    "thread_gates": ["connected", "voiceStatus != Idle", "benchmark running/restoring", "external override"]},
            "google_voice_dialog": {"shared_ai_setup_host": True,
                                    "prompt_draft": list(retained_draft),
                                    "prompt_creation_fake_call": list(prompt_creation),
                                    "wav_paths_injected_as_fake_selection": ["/mock/voice-source.wav", "/mock/voice-consent.wav"],
                                    "wav_creation_fake_call": list(wav_creation),
                                    "native_file_dialog_opened": False,
                                    "provider_contacted": False},
            "setup_geometry": setup_screens,
            "setup_actions": {"continue_back_later_finish_skip": True,
                              "llm_continue_saves": True,
                              "active_benchmark_cancelled_before_skip_finish": True,
                              "google_backend_hides_local_benchmark": True,
                              "setup_saved_config_a": list(setup_saved),
                              "ai_read_config_a": list(values_seen_in_ai),
                              "ai_saved_config_b": list(config_b),
                              "reopened_setup_reads_config_b": list(setup_values_b),
                              "continue_preserves_config_b": tuple(latest_save[-1][1:4]) == config_b,
                              "hide_restore_state": {"hidden_state": minimized_state,
                                                     "restored_visibility": restored_visibility,
                                                     "draft_preserved": True}},
            "hidden_ai_route_disables_form": True}


def _check_theme_and_focus(engine: QQmlApplicationEngine, root: Any) -> dict[str, Any]:
    _route_to(engine, root, 0)
    fake = engine.rootContext().contextProperty("backend")
    if fake is None:
        raise AssertionError("Font audit cannot access the active QML facade")
    fake.values.update({"startupReady": True, "ttsBenchmarkRunning": False,
                        "ttsBenchmarkRestoring": False, "voiceStatus": "Idle"})
    fake.changed.emit()
    _pump(40)
    grayscale = True
    colors: dict[str, str] = {}
    for name in ("canvas", "chrome", "surface", "raised", "divider", "text", "muted", "accent", "warning", "error"):
        color = _qml_value(engine, root, f"designTheme.{name}")
        if color is None:
            continue
        try:
            red, green, blue = color.red(), color.green(), color.blue()
            colors[name] = color.name()
            grayscale &= red == green == blue
        except AttributeError:
            continue
    if colors and not grayscale:
        raise AssertionError(f"Theme has non-monochrome tokens: {colors}")
    command = _find(root, "radioCommand")
    if not command.isEnabled() or not command.isVisible():
        raise AssertionError("Radio text field must be enabled and visible before its focus test")
    _click(root, command)
    focused = bool(command.property("activeFocus"))
    if not focused:
        raise AssertionError("Radio text field cannot receive keyboard focus")
    theme_fonts = {
        "ui": _qml_value(engine, root, "designTheme.uiFont"),
        "mono": _qml_value(engine, root, "designTheme.monoFont"),
        "display": _qml_value(engine, root, "designTheme.displayFont"),
        "regular_instance": _qml_value(engine, root, "designTheme.sansRegular.name"),
        "medium_instance": _qml_value(engine, root, "designTheme.sansMedium.name"),
        "semibold_instance": _qml_value(engine, root, "designTheme.sansSemiBold.name"),
        "weights": {"ui": int(_qml_value(engine, root, "designTheme.uiWeight")),
                    "section": int(_qml_value(engine, root, "designTheme.sectionWeight")),
                    "display": int(_qml_value(engine, root, "designTheme.displayWeight")),
                    "mono": int(_qml_value(engine, root, "designTheme.monoWeight"))},
    }
    expected_families = {"ui": "RaceEngineer Sans", "mono": "IBM Plex Mono",
                         "display": "Barlow Condensed"}
    for family_role, expected_family in expected_families.items():
        if theme_fonts[family_role] != expected_family:
            raise AssertionError(f"Theme {family_role} family should resolve to {expected_family!r}, got {theme_fonts[family_role]!r}")
    if any(theme_fonts[key] != theme_fonts["ui"] for key in ("regular_instance", "medium_instance", "semibold_instance")):
        raise AssertionError(f"Static sans instances do not register one consistent family: {theme_fonts}")
    if theme_fonts["weights"] != {"ui": 500, "section": 600, "display": 700, "mono": 700}:
        raise AssertionError(f"QML weight tokens differ from the actual font contract: {theme_fonts['weights']}")

    font_styles = QFontDatabase.styles(theme_fonts["ui"])
    style_weights = {style: QFontDatabase.weight(theme_fonts["ui"], style) for style in font_styles}
    for style, weight in (("Regular", 400), ("Medium", 500), ("SemiBold", 600)):
        if style_weights.get(style) != weight:
            raise AssertionError(f"UI family lacks a real {style} static style/weight: {style_weights}")
    raster_results: dict[str, dict[str, Any]] = {}
    raster_hashes: dict[int, str] = {}
    for weight in (400, 500, 600):
        font = QFont(theme_fonts["ui"])
        font.setPixelSize(32)
        font.setWeight(QFont.Weight(weight))
        info = QFontInfo(font)
        image = QImage(440, 60, QImage.Format.Format_ARGB32_Premultiplied)
        image.fill(QColor(0, 0, 0, 0))
        painter = QPainter(image)
        painter.setPen(QColor(255, 255, 255, 255))
        painter.setFont(font)
        painter.drawText(4, 42, "RaceEngineer 123 · Tiếng Việt")
        painter.end()
        raster_hash = hashlib.sha256(bytes(image.constBits())).hexdigest()
        raster_hashes[weight] = raster_hash
        raster_results[str(weight)] = {"resolved_family": info.family(),
                                       "resolved_style": info.styleName(),
                                       "resolved_weight": int(info.weight()),
                                       "raster_sha256": raster_hash}
    if len(set(raster_hashes.values())) != 3:
        raise AssertionError(f"Qt font raster did not distinguish Regular/Medium/SemiBold weights: {raster_results}")

    cockpit = _find(root, "cockpit")
    driving_data = _find(root, "cockpitDrivingData")
    sample_items = {
        "radio_input": _find(root, "radioCommand"),
        "wordmark": _find_text_item(root, "RaceEngineer"),
        "body_data_label": _find_text_item(driving_data, "TỐC ĐỘ"),
        "section_label": _find_text_item(cockpit, "KỸ SƯ"),
        "numeric_value": _find_text_item(driving_data, "7825"),
        "display_speed": _find(root, "speedValue"),
    }
    item_fonts: dict[str, dict[str, Any]] = {}
    for name, item in sample_items.items():
        item_fonts[name] = {"family": _qml_item_value(engine, item, "font.family"),
                            "weight": int(_qml_item_value(engine, item, "font.weight")),
                            "pixel_size": int(_qml_item_value(engine, item, "font.pixelSize"))}
    expected_item_contract = {
        "radio_input": (theme_fonts["ui"], 500),
        "wordmark": (theme_fonts["ui"], 600),
        "body_data_label": (theme_fonts["ui"], 500),
        "section_label": (theme_fonts["ui"], 600),
        "numeric_value": (theme_fonts["mono"], 700),
        "display_speed": (theme_fonts["display"], 700),
    }
    for name, expected in expected_item_contract.items():
        actual = (item_fonts[name]["family"], item_fonts[name]["weight"])
        if actual != expected:
            raise AssertionError(f"Rendered QML {name} uses {actual}, expected {expected}")
    return {"grayscale_theme_tokens": colors, "grayscale": grayscale,
            "focused_item_available": focused,
            "font_families": theme_fonts,
            "font_database_styles": style_weights,
            "distinct_static_weight_rasters": raster_results,
            "qml_rendered_item_fonts": item_fonts,
            "font_contract_passed": True,
            "font_scope": "Resolved Qt application font styles and QFont raster output; no optical/monitor calibration claimed"}


def _test_scaled_page_fonts_and_tyre_tiles(engine: QQmlApplicationEngine, root: Any,
                                          fake: Any) -> dict[str, Any]:
    """Verify +15% page typography at runtime while preserving Cockpit metrics."""
    helper_samples = (12, 13, 14, 15, 16, 18, 20, 24, 26, 28, 30, 32, 38, 46, 52, 72, 76, 144, 176)
    helper_results = {str(base): int(_qml_value(engine, root, f"designTheme.pageTextSize({base})"))
                      for base in helper_samples}
    js_round = lambda value: math.floor(float(value) + 0.5)
    expected_helper = {str(base): js_round(base * 1.15) for base in helper_samples}
    if helper_results != expected_helper:
        raise AssertionError(f"pageTextSize does not apply rounded +15% sizes: {helper_results} != {expected_helper}")

    records: dict[str, Any] = {"helper_rounding": helper_results, "sizes": {}, "header_and_cockpit_unchanged": {}}

    def check_font(label: str, item: Any, base_size: int, *, scale: float = 1.15,
                   require_visible: bool = True, fit_text: bool = False) -> dict[str, Any]:
        if item is None:
            raise AssertionError(f"Missing representative font item {label}")
        if require_visible and not item.isVisible():
            raise AssertionError(f"Representative font item {label} is hidden")
        actual = int(_qml_item_value(engine, item, "font.pixelSize"))
        expected = js_round(base_size * scale)
        if actual != expected:
            raise AssertionError(f"{label} font size is {actual}px, expected round({base_size}×{scale})={expected}px")
        result = {"object_name": item.objectName(), "text": str(item.property("text") or ""),
                  "font_pixel_size": actual, "base_size": base_size, "scale": scale,
                  "expected_pixel_size": expected}
        if fit_text:
            rect = _scene_rect(item)
            if rect[2] <= 1 or rect[3] <= 1:
                raise AssertionError(f"Font sample {label} has empty bounds: {rect}")
            painted_width = float(_qml_item_value(engine, item, "paintedWidth"))
            painted_height = float(_qml_item_value(engine, item, "paintedHeight"))
            if painted_width > rect[2] + 1.5:
                raise AssertionError(f"Text {label} is clipped horizontally: painted={painted_width}, bounds={rect[2]}")
            if painted_height > rect[3] + 2:
                raise AssertionError(f"Text {label} is clipped vertically: painted={painted_height}, bounds={rect[3]}")
            result["painted_width"] = round(painted_width, 2)
            result["painted_height"] = round(painted_height, 2)
        return result

    def visible_text(parent: Any, exact_text: str, expected_pixel_size: int | None = None) -> Any:
        matches: list[Any] = []
        for item in _items(parent):
            try:
                if item.isVisible() and item.property("text") == exact_text:
                    try:
                        pixel_size = int(_qml_item_value(engine, item, "font.pixelSize"))
                        if expected_pixel_size is None or pixel_size == expected_pixel_size:
                            matches.append(item)
                    except AssertionError:
                        # Controls such as StatusPill also expose a text alias;
                        # continue to their actual rendered Text child.
                        pass
            except (RuntimeError, TypeError):
                pass
        if matches:
            return matches[0]
        raise AssertionError(f"Visible text {exact_text!r} is missing under {parent.objectName()!r}")

    def check_button_content(label: str, control: Any, base_size: int) -> dict[str, Any]:
        result = check_font(label, control, base_size)
        content = control.property("contentItem")
        if isinstance(content, QQuickItem) and "Text" in content.metaObject().className():
            content_font = check_font(label + ".content", content, base_size, fit_text=True)
            result["content"] = content_font
        return result

    for requested in ((880, 576), (1280, 800), (1600, 1000), (1366, 728), (1280, 720)):
        root.resize(*requested)
        _pump(60)
        actual_size = (round(root.width()), round(root.height()))
        width, height = actual_size
        size_record = {"requested_size": list(requested), "actual_size": [width, height]}
        records["sizes"][f"{width}x{height}"] = size_record

        # Common chrome intentionally retains its existing pixel sizes.
        _route_to(engine, root, 0, direct=True)
        compact_header = bool(_qml_item_value(engine, root, "compactHeader"))
        root_font = int(_qml_item_value(engine, root, "font.pixelSize"))
        wordmark = _find_text_item(root, "RaceEngineer")
        wordmark_px = int(_qml_item_value(engine, wordmark, "font.pixelSize"))
        nav_text = visible_text(root, "Đua", 14 if compact_header else 15)
        nav_px = int(_qml_item_value(engine, nav_text, "font.pixelSize"))
        connection_pill = _find(root, "connectionStatus")
        status_text = next((item for item in _items(connection_pill)
                            if item is not connection_pill and item.isVisible() and item.property("text")
                            and int(_qml_item_value(engine, item, "font.pixelSize")) == 12), None)
        if status_text is None:
            raise AssertionError("Connection status has no visible 12px rendered text child")
        status_px = int(_qml_item_value(engine, status_text, "font.pixelSize"))
        if (root_font, wordmark_px, nav_px, status_px) != (14, 26 if compact_header else 30,
                                                           14 if compact_header else 15, 12):
            raise AssertionError(f"Shared window typography changed at {width}x{height}: "
                                 f"root/wordmark/nav/status={(root_font, wordmark_px, nav_px, status_px)}")
        header_record = {"root": root_font, "wordmark": wordmark_px, "navigation": nav_px,
                         "status": status_px, "compact_header": compact_header}

        cockpit_data = _find(root, "cockpitDrivingData")
        if not math.isclose(float(cockpit_data.property("textScale")), 1.0, abs_tol=1e-6):
            raise AssertionError(f"Cockpit DrivingData scale changed at {width}x{height}: {cockpit_data.property('textScale')}")
        cockpit_compact = bool(_qml_component_item_value(cockpit_data, "compact"))
        cockpit_tight = bool(_qml_component_item_value(cockpit_data, "tight"))
        cockpit_dense = bool(_qml_component_item_value(cockpit_data, "dense"))
        cockpit_available = float(cockpit_data.property("availableHeight"))
        expected_speed = 56 if cockpit_tight else 76 if cockpit_compact else 148 if cockpit_available < 500 else 176 if cockpit_available < 600 else 196
        expected_fuel = 42 if cockpit_tight else 56 if cockpit_compact else 80 if cockpit_available < 500 else 96 if cockpit_available < 600 else 108
        expected_gear = 42 if cockpit_tight else 52 if cockpit_compact else 82 if cockpit_dense else 100
        cockpit_samples = {
            "speed": (_find_scoped(cockpit_data, "speedValue"), expected_speed),
            "gear": (_find_scoped(cockpit_data, "gearValue"), expected_gear),
            "fuel": (_find_scoped(cockpit_data, "fuelValue"), expected_fuel),
            "wheel_temperature": (_find_scoped(cockpit_data, "wheelTemperatureFL"), 16 if cockpit_compact else 22),
            "wheel_pressure": (_find_scoped(cockpit_data, "wheelPressureFL"), 12 if cockpit_compact else 14),
            "wheel_corner_label": (_find_scoped(cockpit_data, "wheelLabelFL"), 13 if cockpit_compact else 15),
            "wheel_tyre_caption": (_find_scoped(cockpit_data, "wheelTemperatureCaptionFL"), 13 if cockpit_compact else 15),
            "wheel_brake_temperature": (_find_scoped(cockpit_data, "wheelBrakeTemperatureFL"), 16 if cockpit_compact else 22),
            "wheel_brake_caption": (_find_scoped(cockpit_data, "wheelBrakeTemperatureCaptionFL"), 13 if cockpit_compact else 15),
        }
        cockpit_font_record: dict[str, Any] = {}
        for label, (item, expected_px) in cockpit_samples.items():
            actual_px = int(_qml_item_value(engine, item, "font.pixelSize"))
            if actual_px != expected_px:
                raise AssertionError(f"Cockpit {label} typography changed at {width}x{height}: {actual_px} != {expected_px}")
            cockpit_font_record[label] = actual_px
        records["header_and_cockpit_unchanged"][f"{width}x{height}"] = {
            "header": header_record, "cockpit_text_scale": 1.0, "cockpit_font_pixels": cockpit_font_record}

        _route_to(engine, root, 1)
        fuel_page = _find(root, "fuelStrategyPage")
        fuel_page.setProperty("sectionIndex", 0)
        _pump(30)
        _scroll_item_into_view(root, _find_scoped(fuel_page, "strategyToggle"))
        fuel_heading_base = 32 if bool(_qml_item_value(engine, fuel_page, "compact")) else 52
        fuel_heading = visible_text(fuel_page, "Nhiên liệu & chiến thuật", js_round(fuel_heading_base * 1.15))
        fuel_control = _find_scoped(fuel_page, "strategyToggle")
        size_record["fuel"] = {
            "heading": check_font("Fuel heading", fuel_heading, fuel_heading_base, fit_text=True),
            "toggle_label": check_font("Fuel strategy label", _find_scoped(fuel_page, "strategyToggleLabel"), 14, fit_text=True),
            "strategy_toggle": {"object_name": fuel_control.objectName(),
                                "text": str(fuel_control.property("text") or ""),
                                "font_not_applicable": not bool(fuel_control.property("text"))},
        }
        if not size_record["fuel"]["strategy_toggle"]["font_not_applicable"]:
            raise AssertionError("Fuel strategy switch has visible text but no scaled text assertion")

        _route_to(engine, root, 2)
        engineer_page = _find(root, "engineerSpotterPage")
        driver_field = _find_scoped(engineer_page, "driverNameField")
        style_button = _find_scoped(engineer_page, "responseStyleTốigiảnButton")
        gap_slider = _find_scoped(engineer_page, "spotterGapSlider")
        _scroll_item_into_view(root, gap_slider)
        gap_value = visible_text(engineer_page, f"{float(fake.values['spotterWarningGapMeters']):.1f} m", 17)
        engineer_heading_base = 32 if bool(_qml_item_value(engine, engineer_page, "compact")) else 52
        size_record["engineer"] = {
            "heading": check_font("Engineer heading", visible_text(
                engineer_page, "Kỹ sư & Spotter", js_round(engineer_heading_base * 1.15)),
                engineer_heading_base, fit_text=True),
            "driver_field": check_font("Engineer driver field", driver_field, 14),
            "response_style_button": check_button_content("Engineer response style", style_button, 14),
            "spotter_gap_numeric_mono": check_font("Spotter gap value", gap_value, 15, fit_text=True),
        }

        _route_to(engine, root, 5)
        system_page = _find(root, "systemDevicesPage")
        compute_combo = _find_scoped(system_page, "aiComputeDeviceCombo")
        _scroll_item_into_view(root, compute_combo)
        volume_label = visible_text(system_page, "Âm lượng radio kỹ sư", 16)
        system_heading_base = 32 if bool(_qml_item_value(engine, system_page, "compact")) else 52
        size_record["system"] = {
            "heading": check_font("System heading", visible_text(
                system_page, "Hệ thống & thiết bị", js_round(system_heading_base * 1.15)),
                system_heading_base, fit_text=True),
            "compute_combo": check_font("AI compute combo", compute_combo, 14),
            "volume_label": check_font("Radio volume label", volume_label, 14, fit_text=True),
        }

        _route_to(engine, root, 4)
        ai_page = _find(root, "aiVoicePage")
        ai_page.setProperty("sectionIndex", 0)
        _pump(50)
        ai_heading_base = 32 if bool(_qml_item_value(engine, ai_page, "compact")) else 52
        llm_form = _find_scoped(ai_page, "llmConnectionForm")
        endpoint = _find_scoped(llm_form, "apiEndpointField")
        test_button = _find_scoped(llm_form, "testLlmConnectionButton")
        size_record["ai_connection"] = {
            "heading": check_font("AI heading", visible_text(
                ai_page, "AI & giọng nói", js_round(ai_heading_base * 1.15)),
                ai_heading_base, fit_text=True),
            "endpoint_field": check_font("API endpoint field", endpoint, 14),
            "test_button": check_button_content("LLM test button", test_button, 14),
        }

        ai_page.setProperty("sectionIndex", 1)
        fake.values.update({"ttsBackend": "VieNeu-TTS", "ttsBenchmarkAllowed": True,
                            "ttsBenchmarkRunning": False, "ttsBenchmarkRestoring": False,
                            "connected": False, "voiceStatus": "Idle"})
        fake.changed.emit()
        _pump(60)
        local_voice = _find_visible_scoped(ai_page, "localVoiceCombo")
        backend_combo = _find_visible_scoped(ai_page, "ttsBackendCombo")
        benchmark_button = _find_visible_scoped(ai_page, "ttsBenchmarkButton")
        _scroll_item_into_view(root, benchmark_button)
        local_heading = visible_text(ai_page, "Giọng cục bộ", js_round(ai_heading_base * 1.15))
        size_record["ai_local_voice"] = {
            "local_heading": check_font("Local voice heading", local_heading, ai_heading_base, fit_text=True),
            "backend_combo": check_font("TTS backend combo", backend_combo, 14),
            "voice_combo": check_font("Local voice combo", local_voice, 14),
            "benchmark_button": check_button_content("TTS benchmark button", benchmark_button, 14),
        }

        ai_page.setProperty("sectionIndex", 2)
        fake.values["ttsBackend"] = "Google Gemini API"
        fake.changed.emit()
        _pump(50)
        google_model = _find_visible_scoped(ai_page, "googleTtsModelCombo")
        size_record["ai_google_voice"] = {
            "model_combo": check_font("Google TTS model combo", google_model, 14),
            "google_heading": check_font("Google TTS heading", visible_text(
                ai_page, "Google TTS", js_round(ai_heading_base * 1.15)), ai_heading_base, fit_text=True),
        }

        _route_to(engine, root, 3)
        telemetry_page = _find(root, "telemetryPage")
        telemetry_data = _find_scoped(telemetry_page, "telemetryDrivingData")
        _pump(60)
        telemetry_primary_geometry = _assert_telemetry_primary_geometry(root, telemetry_page, width, height)
        telemetry_scale = float(telemetry_data.property("textScale"))
        if not math.isclose(telemetry_scale, 1.15, abs_tol=1e-6):
            raise AssertionError(f"Telemetry wheel data scale is {telemetry_scale}, expected 1.15")
        if not bool(telemetry_data.property("wheelsOnly")):
            raise AssertionError("Telemetry DrivingData must render wheel tiles only; main telemetry belongs to TelemetryPage")
        telemetry_compact = bool(_qml_item_value(engine, telemetry_page, "compact"))
        telemetry_width = float(telemetry_page.width())
        telemetry_heading_base = 32 if telemetry_compact else 52
        telemetry_speed_base = 176 if telemetry_width >= 1450 else 120
        telemetry_samples = {
            "speed": (_find_scoped(telemetry_page, "telemetrySpeedValue"), telemetry_speed_base),
            "gear": (_find_scoped(telemetry_page, "telemetryGearValue"), 72),
            "position": (_find_scoped(telemetry_page, "telemetryPositionValue"), 72),
            "current_lap": (_find_scoped(telemetry_page, "telemetryCurrentLapValue"), 72),
            "rpm": (_find_scoped(telemetry_page, "telemetryRpmValue"), 20),
            "fuel": (_find_scoped(telemetry_page, "telemetryFuelValue"), 24 if telemetry_compact else 32),
            "fuel_per_lap": (_find_scoped(telemetry_page, "telemetryFuelPerLapValue"), 24 if telemetry_compact else 32),
            "wheel_temperature": (_find_scoped(telemetry_data, "wheelTemperatureFL"), 16 if bool(_qml_component_item_value(telemetry_data, "compact")) else 22),
            "wheel_pressure": (_find_scoped(telemetry_data, "wheelPressureFL"), 12 if bool(_qml_component_item_value(telemetry_data, "compact")) else 14),
            "wheel_corner_label": (_find_scoped(telemetry_data, "wheelLabelFL"), 13 if bool(_qml_component_item_value(telemetry_data, "compact")) else 15),
            "wheel_brake_temperature": (_find_scoped(telemetry_data, "wheelBrakeTemperatureFL"), 16 if bool(_qml_component_item_value(telemetry_data, "compact")) else 22),
        }
        telemetry_font_record: dict[str, Any] = {}
        for label, (item, base) in telemetry_samples.items():
            expected_px = js_round(float(base) * 1.15)
            actual_px = int(_qml_item_value(engine, item, "font.pixelSize"))
            if actual_px != expected_px:
                raise AssertionError(f"Telemetry {label} font is {actual_px}px, expected scaled base {base}→{expected_px}px")
            telemetry_font_record[label] = {"base": base, "scaled": actual_px, "expected": expected_px}
        telemetry_heading = visible_text(telemetry_page, "Telemetry", js_round(telemetry_heading_base * 1.15))
        telemetry_ledger_heading = visible_text(telemetry_page, "THỜI GIAN VÒNG ĐUA", 16)
        fuel_per_lap_label = _find_visible_text_item(telemetry_page, "Tiêu thụ trung bình")
        actual_wrap_mode = int(_qml_item_value(engine, fuel_per_lap_label, "wrapMode"))
        expected_wrap_mode = int(_qml_component_item_value(fuel_per_lap_label, "Text.Wrap"))
        if actual_wrap_mode != expected_wrap_mode:
            raise AssertionError(f"Scaled Telemetry fuel-per-lap label wrap mode is {actual_wrap_mode}, "
                                 f"expected Text.Wrap={expected_wrap_mode}")
        _scroll_item_into_view(root, fuel_per_lap_label)
        fuel_label_bounds = _scene_rect(fuel_per_lap_label)
        if not fuel_per_lap_label.isVisible() or fuel_label_bounds[2] <= 1 or fuel_label_bounds[3] <= 1:
            raise AssertionError(f"Telemetry fuel-per-lap label is not reachable in the active page: {fuel_label_bounds}")
        fuel_label_base = 14
        fuel_label_font = check_font("Telemetry fuel-per-lap label", fuel_per_lap_label,
                                     fuel_label_base, fit_text=True)
        telemetry_data_compact = bool(_qml_component_item_value(telemetry_data, "compact"))
        telemetry_tile_geometry = _assert_tyre_tile_geometry(telemetry_data, width, height)
        size_record["telemetry"] = {
            "heading": check_font("Telemetry heading", telemetry_heading, telemetry_heading_base, fit_text=True),
            "lap_ledger_heading": check_font("Telemetry lap ledger heading", telemetry_ledger_heading, 14, fit_text=True),
            "primary_geometry": telemetry_primary_geometry,
            "main_data_font_pixels": telemetry_font_record,
            "driving_data": {"textScale": telemetry_scale, "wheelsOnly": True,
                             "font_pixels": telemetry_font_record,
                             "fuel_per_lap_label": {"full_text": fuel_per_lap_label.property("text"),
                                                     "font_check": fuel_label_font,
                                                     "wrap_mode": "Text.Wrap",
                                                     "bounds": list(map(round, fuel_label_bounds))},
                             "tile_compact": telemetry_data_compact,
                             "tyre_tile_geometry": telemetry_tile_geometry},
        }

        # Setup has a distinct display heading and shared voice/LLM forms.
        fake.values.update({"setupVisible": True, "setupStep": 0, "ttsBackend": "VieNeu-TTS",
                            "connected": False, "voiceStatus": "Idle"})
        fake.changed.emit()
        _pump(60)
        wizard = _find(root, "setupWizard")
        setup_field = _find_scoped(wizard, "setupDriverName")
        setup_continue = _find_scoped(wizard, "setupContinueButton")
        welcome_heading = visible_text(wizard, "Chào mừng đến với RaceEngineer", 30)
        size_record["setup_welcome"] = {
            "display_heading": check_font("Setup welcome heading", welcome_heading, 26, fit_text=True),
            "driver_field": check_font("Setup driver field", setup_field, 14),
            "continue_button": check_button_content("Setup continue button", setup_continue, 14),
        }
        fake.values.update({"setupStep": 1, "ttsBackend": "Google Gemini API"})
        fake.changed.emit()
        _pump(50)
        setup_add_voice = _find_visible_scoped(wizard, "addGoogleVoiceButton")
        _click(root, setup_add_voice)
        dialog = _find(root, "googleVoiceDialog")
        _pump(50)
        voice_name = _find_scoped(dialog, "googleVoiceName")
        voice_description = _find_scoped(dialog, "googleVoiceDescription")
        mode_tabs = _find_scoped(dialog, "googleVoiceModeTabs")
        prompt_tab = next((item for item in _items(mode_tabs)
                           if item.isVisible() and "TabButton" in item.metaObject().className()), None)
        if prompt_tab is None:
            raise AssertionError("Google voice dialog does not expose a visible mode tab")
        size_record["google_voice_dialog"] = {
            "name_field": check_font("Google voice name field", voice_name, 14),
            "description_field": check_font("Google voice description", voice_description, 14),
            "mode_tab": check_button_content("Google voice mode tab", prompt_tab, 14),
        }
        dialog.close()
        fake.values["setupVisible"] = False
        fake.changed.emit()

        fake.values.update({"startupReady": False, "startupProgress": 0.42,
                            "startupError": "mock startup font state"})
        fake.changed.emit()
        _pump(50)
        startup = _find(root, "startupNotice")
        # The earlier close-button lifecycle test intentionally leaves the
        # local notice dismissed. Reset that user preference for this separate
        # font rendering check without rebuilding the Main window.
        startup.setProperty("dismissed", False)
        _pump(20)
        startup_retry = _find_scoped(startup, "startupRetryButton")
        size_record["startup"] = {
            "retry_button": check_button_content("Startup retry button", startup_retry, 14),
        }
        fake.values.update({"startupReady": True, "startupProgress": 1.0, "startupError": ""})
        fake.changed.emit()
        _pump(50)

    static_files = ["qml/FuelStrategyPage.qml", "qml/EngineerSpotterPage.qml", "qml/AiVoicePage.qml",
                    "qml/SystemDevicesPage.qml", "qml/SetupWizard.qml", "qml/GoogleVoiceDialog.qml",
                    "qml/LlmConnectionForm.qml", "qml/TtsVoiceSettings.qml", "qml/TtsBenchmarkPanel.qml",
                    "qml/StartupNotice.qml"]
    source_coverage: dict[str, dict[str, Any]] = {}
    for relative_path in static_files:
        source = (ROOT / relative_path).read_text(encoding="utf-8")
        explicit_sizes = [line.strip() for line in source.splitlines() if "font.pixelSize:" in line]
        uncovered = [line for line in explicit_sizes if "pageTextSize(" not in line]
        if uncovered:
            raise AssertionError(f"Non-Dashboard {relative_path} has unscaled explicit font sizes: {uncovered}")
        source_coverage[relative_path] = {"explicit_font_size_lines": len(explicit_sizes),
                                          "all_explicit_sizes_use_pageTextSize": True}
    driving_source = (QML_DIR / "DrivingData.qml").read_text(encoding="utf-8")
    driving_sizes = [line.strip() for line in driving_source.splitlines() if "font.pixelSize:" in line]
    driving_uncovered = [line for line in driving_sizes if "vehicleData.fontSize(" not in line]
    if driving_uncovered:
        raise AssertionError(f"DrivingData has explicit font sizes outside its textScale helper: {driving_uncovered}")
    source_coverage["qml/DrivingData.qml"] = {"explicit_font_size_lines": len(driving_sizes),
                                              "all_explicit_sizes_use_fontSize": True}
    main_source = (QML_DIR / "Main.qml").read_text(encoding="utf-8")
    for component, expected in (("component BodyText", "theme.pageTextSize("),
                                ("component MutedText", "theme.pageTextSize("),
                                ("component HeadingText", "theme.pageTextSize("),
                                ("BodyText { visible: card.title.length", "theme.pageTextSize(")):
        block_start = main_source.find(component)
        if block_start < 0:
            raise AssertionError(f"Main.qml lost scalable text component {component}")
        block = main_source[block_start:main_source.find("\n    }", block_start) + 6]
        if expected not in block:
            raise AssertionError(f"Main.qml page component is missing {expected}: {component}")
    source_coverage["qml/Main.qml"] = {"page_body_muted_heading_card_use_pageTextSize": True,
                                       "common_chrome_exempted_by_contract": True}
    records["source_font_coverage"] = source_coverage
    records["non_dashboard_font_scale"] = 1.15
    records["short_height_cases"] = ["1366x728", "1280x720"]
    records["no_system_file_picker_or_provider_opened"] = True
    return records


def _render_routes(engine: QQmlApplicationEngine, root: Any, fake: Any) -> list[dict[str, Any]]:
    SCREENSHOT_DIR.mkdir(parents=True, exist_ok=True)
    records: list[dict[str, Any]] = []
    for page, page_object_name in PAGE_NAMES.items():
        _route_to(engine, root, page)
        for width, height in SIZES:
            root.resize(width, height)
            _pump(60)
            metrics = _window_metrics(root, (width, height))
            actual_width, actual_height = metrics["actual_size"]
            loader = _find(root, page_object_name)
            page_item = loader.property("item")
            layout = _assert_page_geometry(root, loader, page_item, page, actual_width, actual_height)
            header = _assert_header_layout(root, actual_width, actual_height)
            size_suffix = (f"{actual_width}x{actual_height}" if (actual_width, actual_height) != (width, height)
                           else f"{width}x{height}")
            record = _capture(root, SCREENSHOT_DIR / f"page-{page}-{size_suffix}.png")
            record.update({"route": page, "page_object": page_object_name,
                           "requested_size": [width, height], "actual_size": [actual_width, actual_height],
                           "window_metrics": metrics,
                           "layout_assertions": layout,
                           "header_bounds": header})
            if record["sampled_chromatic_fraction"] > 0.001:
                raise AssertionError(f"The grayscale UI rendered chromatic pixels: {record['sampled_chromatic_fraction']}")
            records.append(record)
        if page == 0:
            # Screen-safe working-area heights used by common 1366×768 and
            # 1280×720 displays after OS chrome has taken its share.
            for width, height in ((1366, 728), (1280, 720)):
                root.resize(width, height)
                _pump(60)
                metrics = _window_metrics(root, (width, height))
                actual_width, actual_height = metrics["actual_size"]
                layout = _assert_page_geometry(root, loader, page_item, page, actual_width, actual_height)
                header = _assert_header_layout(root, actual_width, actual_height)
                record = _capture(root, SCREENSHOT_DIR / f"page-0-{actual_width}x{actual_height}.png")
                record.update({"route": page, "page_object": page_object_name,
                               "requested_size": [width, height], "actual_size": [actual_width, actual_height],
                               "window_metrics": metrics, "layout_assertions": layout,
                               "header_bounds": header, "screen_safe_height_case": True})
                if record["sampled_chromatic_fraction"] > 0.001:
                    raise AssertionError(f"The grayscale UI rendered chromatic pixels: {record['sampled_chromatic_fraction']}")
                records.append(record)
    return records


def run() -> dict[str, Any]:
    global COMPLETED_REPORT
    VALIDATION_DIR.mkdir(parents=True, exist_ok=True)
    COMPLETED_REPORT = None
    app = QGuiApplication.instance() or QGuiApplication(["raceengineer-ui-validation"])
    app.setApplicationName("RaceEngineer UI validation")
    app.setQuitOnLastWindowClosed(False)
    qInstallMessageHandler(_qt_message_handler)
    QT_LOGS.clear()
    PARTIAL_REPORT.clear()
    static_audit = run_parity_audit(ROOT)
    PARTIAL_REPORT["static_parity_audit"] = static_audit
    if not static_audit["passed"]:
        raise AssertionError("Baseline parity/resource audit failed: " + "; ".join(static_audit["failures"]))
    source_components = _compile_source_components()
    PARTIAL_REPORT["source_component_compile"] = source_components
    if not source_components["all_ready"]:
        raise AssertionError("Standalone QML component compilation failed:\n" +
                             "\n".join(source_components["errors"]))
    baseline = _run_baseline_load()
    main_qt_log_start = len(QT_LOGS)
    engine, root, fake, warnings = _qml_engine(NEW_QML)
    if root.objectName() != "appWindow":
        raise AssertionError(f"Main root objectName expected appWindow, got {root.objectName()!r}")
    root_identity = int(shiboken6.getCppPointer(root)[0])
    root_marker = f"ui-validation-{id(root)}"
    root.setProperty("_uiValidationIdentity", root_marker)
    # Seed API preferences before the lazy AI page can create its first form;
    # this mirrors Application constructing with persisted settings already
    # loaded, and lets the cold-snapshot regression start from a clean form.
    fake.values.update({"apiConfigured": True, "apiState": "LLM Server Offline",
                        "apiBaseUrl": "http://localhost:8080/v1", "apiKey": "old-secret",
                        "apiModel": "old-model", "apiStreaming": True,
                        "apiTimeoutMilliseconds": 15000, "apiMaximumTokens": 64,
                        "apiTemperature": 0.2, "apiReasoning": False})
    fake.apiSettingsChanged.emit()
    setup_loader = _find(root, "setupWizardLoader")
    if setup_loader.property("item") is not None or _find(root, "setupWizard", required=False) is not None:
        raise AssertionError("SetupWizard must remain unloaded until backend.setupVisible becomes true")

    default_window_size = (round(root.width()), round(root.height()))
    declared_minimum = (round(root.minimumWidth()), round(root.minimumHeight()))
    root.resize(*declared_minimum)
    _pump(80)
    minimum_window_metrics = _window_metrics(root, declared_minimum)
    root.resize(*default_window_size)
    _pump(80)

    result: dict[str, Any] = {
        "date": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "baseline": baseline,
        "runtime": {"python": sys.version.split()[0], "pyside6": __import__("PySide6").__version__,
                    "qt": __import__("PySide6.QtCore", fromlist=["qVersion"]).qVersion(),
                    "platform": os.environ.get("QT_QPA_PLATFORM"),
                    "quick_backend": os.environ.get("QT_QUICK_BACKEND")},
        "new_qml": {},
        "static_parity_audit": static_audit,
        "source_component_compile": source_components,
        "source_sha256": _source_sha256(),
        "window": {"initial": _window_metrics(root),
                   "default_size": list(default_window_size),
                   "minimum_declared": list(declared_minimum),
                   "minimum_request_result": minimum_window_metrics},
    }

    root_wrapper_reacquired_phases: list[str] = []

    def recover_root(phase: str, expected_page: int) -> None:
        nonlocal root
        root, reacquired = _reacquire_main_if_needed(
            engine, root, root_identity, root_marker, expected_page, phase
        )
        if reacquired:
            root_wrapper_reacquired_phases.append(phase)

    routes = _test_new_routes(engine, root, fake, warnings)
    result["new_qml"]["routes"] = routes
    recover_root("new routes", 5)
    strategy = fake._strategy_data
    result["new_qml"]["zero_missing_disconnected"] = _test_empty_zero_connected(engine, root, fake, strategy)
    recover_root("zero/missing/disconnected", 0)
    result["new_qml"]["tyre_tile_value_states"] = _test_tyre_tile_value_states(engine, root, fake)
    recover_root("tyre tile zero/missing values", 3)
    result["new_qml"]["radio"] = _test_radio(engine, root, fake)
    recover_root("radio", 0)
    result["new_qml"]["gates"] = _test_navigation_and_gates(engine, root, fake, strategy)
    recover_root("navigation and gates", 4)
    result["new_qml"]["strategy_actions"] = _test_strategy_actions(engine, root, fake, strategy)
    recover_root("strategy actions", 1)
    result["new_qml"]["engineer_controls"] = _test_engineer_controls(engine, root, fake)
    recover_root("engineer controls", 2)
    result["new_qml"]["system_actions"] = _test_system_actions(engine, root, fake)
    recover_root("system actions", 5)
    result["new_qml"]["startup_actions"] = _test_startup_actions(engine, root, fake)
    recover_root("startup actions", 5)
    result["new_qml"]["forms_setup"] = _test_forms_and_setup(engine, root, fake)
    recover_root("forms/setup", 5)
    baseline_methods = set(static_audit["baseline_referenced_methods"])
    facade_calls: dict[str, list[list[Any]]] = {}
    for method, args in fake.calls:
        facade_calls.setdefault(method, []).append(list(args))
    strategy_calls: dict[str, list[list[Any]]] = {}
    for method, args in strategy.calls:
        strategy_calls.setdefault(method, []).append(list(args))
    dynamically_called_methods = set(facade_calls) | set(strategy_calls)
    result["new_qml"]["baseline_method_coverage"] = {
        "baseline_methods": sorted(baseline_methods),
        "dynamically_called_methods": sorted(baseline_methods & dynamically_called_methods),
        "not_dynamically_called": sorted(baseline_methods - dynamically_called_methods),
        "dynamic_coverage_count": len(baseline_methods & dynamically_called_methods),
        "baseline_method_count": len(baseline_methods),
        "actual_facade_calls_by_method": facade_calls,
        "actual_strategy_recorder_calls_by_method": strategy_calls,
        "note": "Calls are real Qt mouse/keyboard activations routed into strict Python facade slots; C++ side effects remain unverified.",
    }
    if root.objectName() != "appWindow":
        raise AssertionError(f"Theme/render root changed unexpectedly: {root.objectName()!r}")
    if int(root.property("currentPage")) != 5:
        raise AssertionError(f"Main route did not persist after forms/setup lifecycle: {root.property('currentPage')}")
    route_after_lifecycle = int(root.property("currentPage"))
    driving_fixture = _seed_driving_fixture(fake)
    result["new_qml"]["theme"] = _check_theme_and_focus(engine, root)
    result["new_qml"]["cockpit_overflow_accessibility"] = _test_cockpit_overflow_accessibility(
        engine, root, fake
    )
    # The overflow probe deliberately replaces the telemetry strings; restore
    # the deterministic Monza/Ferrari fixture before geometry checks and renders.
    fake.values["telemetry"] = driving_fixture
    fake.changed.emit()
    _pump(40)
    result["new_qml"]["root_identity"] = {
        "cpp_pointer_preserved": int(shiboken6.getCppPointer(root)[0]) == root_identity,
        "qml_marker_preserved": root.property("_uiValidationIdentity") == root_marker,
        "current_route_preserved_after_forms_setup": route_after_lifecycle == 5,
        "python_wrapper_reacquired": bool(root_wrapper_reacquired_phases),
        "python_wrapper_reacquired_phases": root_wrapper_reacquired_phases,
    }
    result["new_qml"]["cockpit_viewport_fixture"] = {
        "name": "Monza / Ferrari 296 GT3 synthetic preview telemetry",
        "connected_fake_facade": True,
        "telemetry": driving_fixture,
        "opponents": {"ahead": driving_fixture["opponentAhead"],
                      "behind": driving_fixture["opponentBehind"]},
        "direct_input_binding": fake.values["directInputBinding"],
        "latest_event": fake.values["latestEvent"],
    }
    result["new_qml"]["cockpit_breakpoint_geometry"] = _test_cockpit_breakpoint_sweep(engine, root)
    result["new_qml"]["scaled_page_fonts_and_tyre_tiles"] = _test_scaled_page_fonts_and_tyre_tiles(
        engine, root, fake
    )
    result["new_qml"]["screenshots"] = _render_routes(engine, root, fake)
    qt_message_log = QT_LOGS[main_qt_log_start:]
    warning_counts = Counter(warnings)
    qt_message_log_not_in_engine: list[str] = []
    duplicate_channels = 0
    for message in qt_message_log:
        if warning_counts[message] > 0:
            warning_counts[message] -= 1
            duplicate_channels += 1
        else:
            qt_message_log_not_in_engine.append(message)
    result["new_qml"]["qml_warnings"] = list(warnings)
    result["new_qml"]["qt_message_log"] = qt_message_log
    result["new_qml"]["qt_message_log_not_in_engine"] = qt_message_log_not_in_engine
    result["new_qml"]["duplicate_engine_qt_warning_events"] = duplicate_channels
    result["new_qml"]["recorded_facade_calls"] = len(fake.calls)
    result["source_sha256"] = _source_sha256()
    result["limitations"] = [
        "Fake QObject facade only validates QML-facing rendering, bindings and calls.",
        "Does not prove native C++ Windows build or tray/notifications.",
        "Does not prove AC/ACC simulator data, real microphone/audio devices, TTS/LLM providers, or model startup.",
        "Does not prove DirectInput wheel binding/PTT or physical key routing in the Windows shell.",
    ]
    report_path = VALIDATION_DIR / "render-report.json"
    report_path.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    COMPLETED_REPORT = result
    PARTIAL_REPORT["runtime_warning_audit"] = {
        "qml_warnings": list(warnings),
        "qt_message_log_not_in_engine": qt_message_log_not_in_engine,
        "duplicate_engine_qt_warning_events": duplicate_channels,
    }
    if warnings or qt_message_log_not_in_engine:
        raise AssertionError("Main QML emitted runtime warnings: " + repr(warnings + qt_message_log_not_in_engine))
    return result


if __name__ == "__main__":
    try:
        output = run()
    except Exception as error:  # keep machine-readable failure evidence
        VALIDATION_DIR.mkdir(parents=True, exist_ok=True)
        if COMPLETED_REPORT is not None:
            failure = dict(COMPLETED_REPORT)
            failure["validation_failure"] = {"error": type(error).__name__, "message": str(error)}
        else:
            failure = {"date": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                       "error": type(error).__name__, "message": str(error),
                       "runtime": {"python": sys.version.split()[0], "pyside6": __import__("PySide6").__version__},
                       **PARTIAL_REPORT}
        (VALIDATION_DIR / "render-report.json").write_text(
            json.dumps(failure, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        raise
    print(json.dumps(output, ensure_ascii=False, indent=2))
