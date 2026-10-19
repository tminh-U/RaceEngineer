"""Static source and qrc coverage checks for the baseline UI contract."""

from __future__ import annotations

import re
import subprocess
from pathlib import Path
from typing import Any
from native_contract import qml_call_arities_from_text, validate_sources


BASELINE = "f01acc553c0d938af74f4ea6a026dbc6571eb06c"


def _classify_references(source: str) -> dict[str, set[str]]:
    result: dict[str, set[str]] = {"properties": set(), "methods": set()}
    direct = re.compile(r"\b(?:backend|controller|application)\.([A-Za-z_]\w*)")
    nested = re.compile(
        r"\b(?:backend|controller|application)\.strategyData\.([A-Za-z_]\w*)"
    )
    local_strategy = re.compile(r"\bstrategyData\.([A-Za-z_]\w*)")

    def add_matches(pattern: re.Pattern[str], text: str) -> None:
        for match in pattern.finditer(text):
            name = match.group(1)
            if name == "strategyData" and pattern is direct:
                continue
            following = text[match.end():]
            bucket = "methods" if re.match(r"\s*\(", following) else "properties"
            result[bucket].add(name)

    add_matches(direct, source)
    add_matches(nested, source)
    add_matches(local_strategy, source)
    return result


def _cmake_list(cmake: str, variable: str) -> list[str]:
    if variable == "QML_FILES":
        match = re.search(r"\bQML_FILES\b(.*?)\bRESOURCES\b", cmake, re.S)
        block = match.group(1) if match else ""
    else:
        match = re.search(rf"set\(\s*{re.escape(variable)}\b(.*?)\)", cmake, re.S)
        block = match.group(1) if match else ""
    block = re.sub(r"#[^\n]*", "", block)
    return re.findall(r"(?<![\w$])(?:qml|assets)/[A-Za-z0-9_./-]+", block)


def audit(root: Path) -> dict[str, Any]:
    baseline_source = subprocess.check_output(
        ["git", "show", f"{BASELINE}:qml/Main.qml"], cwd=root, text=True
    )
    current_sources = sorted((root / "qml").glob("*.qml"))
    current_source_text = "\n".join(path.read_text(encoding="utf-8") for path in current_sources)
    baseline_refs = _classify_references(baseline_source)
    current_refs = _classify_references(current_source_text)

    cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    qml_resources = _cmake_list(cmake, "QML_FILES")
    font_resources = _cmake_list(cmake, "RACEENGINEER_FONT_RESOURCES")
    disk_qml = sorted(path.relative_to(root).as_posix() for path in current_sources)
    missing_qml_resources = sorted(set(disk_qml) - set(qml_resources))
    absent_qml_resources = sorted(
        path for path in qml_resources if not (root / path).is_file()
    )

    font_loaders: list[dict[str, str | bool]] = []
    for source in current_sources:
        text = source.read_text(encoding="utf-8")
        for match in re.finditer(
            r"FontLoader\s*\{[^}]*?source\s*:\s*[\"']([^\"']+)[\"']",
            text,
            re.S,
        ):
            relative_url = match.group(1)
            resolved = (source.parent / relative_url).resolve()
            try:
                resource_path = resolved.relative_to(root.resolve()).as_posix()
            except ValueError:
                resource_path = ""
            font_loaders.append({
                "source_file": source.relative_to(root).as_posix(),
                "source_url": relative_url,
                "local_source_exists": resolved.is_file(),
                "cmake_resource_listed": resource_path in font_resources,
                "normalized_resource_path": resource_path,
            })

    # Only package files the UI actually loads (plus their corresponding OFL
    # notices). Source/intermediate files may remain beside the selected static
    # faces without being part of the application resource bundle.
    loaded_font_files = sorted({
        str(item["normalized_resource_path"])
        for item in font_loaders
        if item["normalized_resource_path"] and str(item["normalized_resource_path"]).lower().endswith((".ttf", ".otf"))
    })
    license_for_font = {
        "RaceEngineerSans-Regular.ttf": "assets/fonts/raceengineer-sans-OFL.txt",
        "RaceEngineerSans-Medium.ttf": "assets/fonts/raceengineer-sans-OFL.txt",
        "RaceEngineerSans-SemiBold.ttf": "assets/fonts/raceengineer-sans-OFL.txt",
        "IBMPlexMono-Regular.ttf": "assets/fonts/ibmplexmono-OFL.txt",
        "BarlowCondensed-SemiBold.ttf": "assets/fonts/barlowcondensed-OFL.txt",
    }
    required_font_files = sorted(set(loaded_font_files) | {
        license_for_font[Path(font).name]
        for font in loaded_font_files
        if Path(font).name in license_for_font
    })
    missing_font_resources = sorted(set(required_font_files) - set(font_resources))
    unused_font_assets = sorted(
        path.relative_to(root).as_posix()
        for path in (root / "assets/fonts").glob("*")
        if path.suffix.lower() in {".ttf", ".otf"} and path.relative_to(root).as_posix() not in loaded_font_files
    )
    nonexisting_font_resources = sorted(
        path for path in font_resources if path.startswith("assets/fonts/") and not (root / path).is_file()
    )
    missing_properties = sorted(baseline_refs["properties"] - current_refs["properties"])
    missing_methods = sorted(baseline_refs["methods"] - current_refs["methods"])
    native_contract = validate_sources(root, current_sources)
    baseline_arities = qml_call_arities_from_text(baseline_source)
    current_arities = {name: set(values) for name, values in native_contract["qml_call_arities"].items()}
    baseline_arity_loss = {
        name: sorted(arities - current_arities.get(name, set()))
        for name, arities in baseline_arities.items()
        if arities - current_arities.get(name, set())
    }

    report = {
        "baseline_commit": BASELINE,
        "baseline_referenced_properties": sorted(baseline_refs["properties"]),
        "baseline_referenced_methods": sorted(baseline_refs["methods"]),
        "current_referenced_properties": sorted(current_refs["properties"]),
        "current_referenced_methods": sorted(current_refs["methods"]),
        "missing_baseline_properties": missing_properties,
        "missing_baseline_methods": missing_methods,
        "qml_source_files": disk_qml,
        "qml_files_listed_in_cmake": sorted(qml_resources),
        "qml_files_missing_from_cmake": missing_qml_resources,
        "cmake_qml_files_not_on_disk": absent_qml_resources,
        "font_loaders": font_loaders,
        "font_files_missing_from_cmake": missing_font_resources,
        "font_assets_not_loaded_by_qml": unused_font_assets,
        "font_resources_not_on_disk": nonexisting_font_resources,
        "source_local_font_paths_verified": all(
            bool(item["local_source_exists"]) for item in font_loaders
        ),
        "font_resource_paths_listed": all(
            bool(item["cmake_resource_listed"]) for item in font_loaders
        ),
        "native_qrc_packaging_verified": False,
        "native_qobject_contract": native_contract,
        "baseline_qml_call_arities": {name: sorted(arities) for name, arities in baseline_arities.items()},
        "current_qml_call_arities": {name: sorted(arities) for name, arities in current_arities.items()},
        "baseline_method_call_shapes_missing": baseline_arity_loss,
    }
    failures = []
    for key in (
        "missing_baseline_properties",
        "missing_baseline_methods",
        "qml_files_missing_from_cmake",
        "cmake_qml_files_not_on_disk",
        "font_files_missing_from_cmake",
        "font_resources_not_on_disk",
    ):
        if report[key]:
            failures.append(f"{key}: {report[key]}")
    if not report["source_local_font_paths_verified"]:
        failures.append("At least one FontLoader local source path does not exist")
    if not report["font_resource_paths_listed"]:
        failures.append("At least one FontLoader target is absent from RACEENGINEER_FONT_RESOURCES")
    if not native_contract["passed"]:
        failures.extend(native_contract["failures"])
    if baseline_arity_loss:
        failures.append(f"Baseline method call shapes missing from current QML: {baseline_arity_loss}")
    report["failures"] = failures
    report["passed"] = not failures
    return report
