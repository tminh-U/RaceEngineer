"""Read-only QML-facing API schema extracted from the checked-in C++ headers.

The recording facade uses this schema instead of learning its API from the UI
under test. This catches misspelled/nonexistent properties and calls that have
the wrong number of arguments before a mock can accidentally accept them.
"""

from __future__ import annotations

import re
from pathlib import Path
from typing import Any


def _arguments(source: str, opening: int) -> tuple[list[str], int]:
    depth = 1
    quote: str | None = None
    escaped = False
    commas: list[int] = []
    index = opening + 1
    while index < len(source):
        char = source[index]
        if quote:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = None
        elif char in "\"'":
            quote = char
        elif char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                break
        elif char == "," and depth == 1:
            commas.append(index)
        index += 1

    if depth != 0:
        raise ValueError("Unclosed C++ method argument list")
    boundaries = [opening + 1, *[index + 1 for index in commas], index]
    ends = [*commas, index]
    values = [source[start:end].strip() for start, end in zip(boundaries, ends)]
    values = [value for value in values if value]
    return values, index + 1


def _header_api(path: Path) -> dict[str, Any]:
    text = path.read_text(encoding="utf-8")
    properties = set(re.findall(r"Q_PROPERTY\s*\([^)]*?\b([A-Za-z_]\w*)\s+READ\s+[A-Za-z_]\w*", text, re.S))
    methods: dict[str, dict[str, Any]] = {}
    declaration = re.compile(r"Q_INVOKABLE\s+[A-Za-z_][\w:<>*&\s]*?\s+([A-Za-z_]\w*)\s*\(")
    for match in declaration.finditer(text):
        name = match.group(1)
        opening = match.end() - 1
        args, end = _arguments(text, opening)
        required = sum("=" not in argument for argument in args)
        methods[name] = {"minimum_arity": required, "maximum_arity": len(args),
                         "declaration": re.sub(r"\s+", " ", text[match.start():end]).strip()}
    signal_block = re.search(
        r"^\s*signals\s*:\s*(.*?)(?=^\s*(?:(?:private|protected|public)(?:\s+slots)?\s*:)|^\s*};)",
        text,
        re.S | re.M,
    )
    signals = sorted(set(re.findall(r"\bvoid\s+([A-Za-z_]\w*)\s*\(", signal_block.group(1)))) if signal_block else []
    return {"properties": sorted(properties), "methods": methods, "signals": signals}


def load_schema(root: Path) -> dict[str, Any]:
    return {
        "application": _header_api(root / "src/app/Application.h"),
        "strategy_data": _header_api(root / "src/strategy/StrategyRecorder.h"),
    }


def qml_call_arities_from_text(text: str) -> dict[str, set[int]]:
    result: dict[str, set[int]] = {}
    call = re.compile(r"(?<!Qt\.)\b(?:backend|controller|application|strategyData)\.([A-Za-z_]\w*)\s*\(")
    for match in call.finditer(text):
        opening = match.end() - 1
        # Reuse the same nesting rules as C++ declarations. QML calls do not
        # contain C++ templates, only ordinary JS expressions.
        args, _ = _arguments(text, opening)
        result.setdefault(match.group(1), set()).add(len(args))
    return result


def _qml_call_arities(sources: list[Path]) -> dict[str, set[int]]:
    result: dict[str, set[int]] = {}
    for source in sources:
        for name, arities in qml_call_arities_from_text(source.read_text(encoding="utf-8")).items():
            result.setdefault(name, set()).update(arities)
    return result


def _qml_api_refs(sources: list[Path]) -> dict[str, set[str]]:
    refs: dict[str, set[str]] = {"application_properties": set(), "application_methods": set(),
                                 "strategy_properties": set(), "strategy_methods": set()}
    direct = re.compile(r"(?<!Qt\.)\b(?:backend|controller|application)\.([A-Za-z_]\w*)")
    nested = re.compile(r"(?<!Qt\.)\b(?:backend|controller|application)\.strategyData\.([A-Za-z_]\w*)")
    local_strategy = re.compile(r"\bstrategyData\.([A-Za-z_]\w*)")
    for source in sources:
        text = source.read_text(encoding="utf-8")
        for pattern, property_key, method_key in (
            (direct, "application_properties", "application_methods"),
            (nested, "strategy_properties", "strategy_methods"),
            (local_strategy, "strategy_properties", "strategy_methods"),
        ):
            for match in pattern.finditer(text):
                name = match.group(1)
                if pattern is direct and name == "strategyData":
                    refs[property_key].add(name)
                    continue
                if pattern is local_strategy and name == "enabled" and "strategyData.enabled" in text:
                    # Keep this reference: strategyData is a proxy to the C++
                    # recorder during live use, and the property exists there.
                    pass
                if re.match(r"\s*\(", text[match.end():]):
                    refs[method_key].add(name)
                else:
                    refs[property_key].add(name)
    return refs


def validate_sources(root: Path, sources: list[Path]) -> dict[str, Any]:
    schema = load_schema(root)
    refs = _qml_api_refs(sources)
    app_properties = set(schema["application"]["properties"])
    app_properties.add("strategyData")
    strategy_properties = set(schema["strategy_data"]["properties"])
    app_methods = set(schema["application"]["methods"])
    strategy_methods = set(schema["strategy_data"]["methods"])
    failures: list[str] = []

    missing_app_properties = sorted(refs["application_properties"] - app_properties)
    missing_app_methods = sorted(refs["application_methods"] - app_methods)
    missing_strategy_properties = sorted(refs["strategy_properties"] - strategy_properties)
    missing_strategy_methods = sorted(refs["strategy_methods"] - strategy_methods)
    for category, names in (
        ("application properties absent from Application.h", missing_app_properties),
        ("Application invokables absent from Application.h", missing_app_methods),
        ("StrategyRecorder properties absent from StrategyRecorder.h", missing_strategy_properties),
        ("StrategyRecorder invokables absent from StrategyRecorder.h", missing_strategy_methods),
    ):
        if names:
            failures.append(f"{category}: {names}")

    qml_arities = _qml_call_arities(sources)
    method_owner: dict[str, tuple[str, dict[str, Any]]] = {
        **{name: ("application", spec) for name, spec in schema["application"]["methods"].items()},
        **{name: ("strategy_data", spec) for name, spec in schema["strategy_data"]["methods"].items()},
    }
    invalid_arities: list[dict[str, Any]] = []
    for name in sorted(refs["application_methods"] | refs["strategy_methods"]):
        if name not in method_owner:
            continue
        owner, spec = method_owner[name]
        found = sorted(qml_arities.get(name, set()))
        allowed = list(range(spec["minimum_arity"], spec["maximum_arity"] + 1))
        invalid = [arity for arity in found if arity not in allowed]
        if invalid:
            invalid_arities.append({"owner": owner, "method": name, "calls": found,
                                    "allowed": allowed, "declaration": spec["declaration"]})
    if invalid_arities:
        failures.append(f"QML call arities do not match native signatures: {invalid_arities}")

    return {
        "header_sources": ["src/app/Application.h", "src/strategy/StrategyRecorder.h"],
        "application_native_properties": sorted(app_properties),
        "application_native_methods": schema["application"]["methods"],
        "application_native_signals": schema["application"]["signals"],
        "strategy_native_properties": sorted(strategy_properties),
        "strategy_native_methods": schema["strategy_data"]["methods"],
        "strategy_native_signals": schema["strategy_data"]["signals"],
        "qml_references": {key: sorted(value) for key, value in refs.items()},
        "qml_call_arities": {key: sorted(value) for key, value in qml_arities.items()},
        "missing_application_properties": missing_app_properties,
        "missing_application_methods": missing_app_methods,
        "missing_strategy_properties": missing_strategy_properties,
        "missing_strategy_methods": missing_strategy_methods,
        "invalid_method_arities": invalid_arities,
        "failures": failures,
        "passed": not failures,
    }
