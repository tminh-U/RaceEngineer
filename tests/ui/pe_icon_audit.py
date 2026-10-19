#!/usr/bin/env python3
"""Verify that every ICO frame is embedded as an RT_ICON resource in PE files.

This uses only the Python standard library and reads PE resource directories
directly, so it can audit a native Windows executable from a non-Windows host.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path
from typing import Any


class AuditError(ValueError):
    """Input file is malformed or truncated."""


def _span(data: bytes, offset: int, size: int, what: str) -> bytes:
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise AuditError(f"{what} extends beyond end of file")
    return data[offset : offset + size]


def _u16(data: bytes, offset: int, what: str) -> int:
    return struct.unpack("<H", _span(data, offset, 2, what))[0]


def _u32(data: bytes, offset: int, what: str) -> int:
    return struct.unpack("<I", _span(data, offset, 4, what))[0]


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _ico_frames(data: bytes) -> list[dict[str, Any]]:
    reserved, image_type, count = struct.unpack("<HHH", _span(data, 0, 6, "ICO header"))
    if reserved != 0 or image_type != 1:
        raise AuditError("input is not a valid ICO file (expected reserved=0 and type=1)")
    if count == 0:
        raise AuditError("ICO contains no frames")
    directory_end = 6 + count * 16
    _span(data, 6, count * 16, "ICO directory")

    frames: list[dict[str, Any]] = []
    for index in range(count):
        offset = 6 + index * 16
        width_byte, height_byte, color_count, entry_reserved, planes, bit_count, size, image_offset = struct.unpack(
            "<BBBBHHII", _span(data, offset, 16, f"ICO frame {index} directory entry")
        )
        if entry_reserved != 0:
            raise AuditError(f"ICO frame {index} has a nonzero reserved byte")
        if size == 0:
            raise AuditError(f"ICO frame {index} has an empty payload")
        if image_offset < directory_end:
            raise AuditError(f"ICO frame {index} payload overlaps its directory")
        payload = _span(data, image_offset, size, f"ICO frame {index} payload")
        frames.append({
            "index": index,
            "size": [width_byte or 256, height_byte or 256],
            "color_count": color_count,
            "planes": planes,
            "bit_count": bit_count,
            "payload_size": size,
            "payload_sha256": _sha256(payload),
        })
    return frames


class _PeImage:
    def __init__(self, data: bytes, path: Path):
        self.data = data
        self.path = path
        self.sections: list[tuple[int, int, int, int]] = []
        self.size_of_headers = 0
        self.resource_rva = 0
        self.resource_size = 0
        self.magic = 0
        self._parse_headers()

    def _parse_headers(self) -> None:
        if _span(self.data, 0, 2, "DOS signature") != b"MZ":
            raise AuditError(f"{self.path}: missing MZ signature")
        pe_offset = _u32(self.data, 0x3C, "DOS e_lfanew")
        if _span(self.data, pe_offset, 4, "PE signature") != b"PE\0\0":
            raise AuditError(f"{self.path}: invalid PE signature")
        coff = pe_offset + 4
        section_count = _u16(self.data, coff + 2, "COFF section count")
        optional_size = _u16(self.data, coff + 16, "COFF optional-header size")
        optional = coff + 20
        self.magic = _u16(self.data, optional, "optional-header magic")
        if self.magic == 0x10B:  # PE32
            directory_offset = 96
            directory_count_offset = 92
        elif self.magic == 0x20B:  # PE32+
            directory_offset = 112
            directory_count_offset = 108
        else:
            raise AuditError(f"{self.path}: unsupported optional-header magic 0x{self.magic:04x}")
        if optional_size < directory_offset + 8 * 3:
            raise AuditError(f"{self.path}: optional header is too short for resource data directory")
        self.size_of_headers = _u32(self.data, optional + 60, "SizeOfHeaders")
        directory_count = _u32(self.data, optional + directory_count_offset, "NumberOfRvaAndSizes")
        if directory_count <= 2:
            raise AuditError(f"{self.path}: PE has no resource data directory")
        resource_entry = optional + directory_offset + 8 * 2
        self.resource_rva = _u32(self.data, resource_entry, "resource directory RVA")
        self.resource_size = _u32(self.data, resource_entry + 4, "resource directory size")
        if self.resource_rva == 0 or self.resource_size == 0:
            raise AuditError(f"{self.path}: PE has an empty resource data directory")

        section_table = optional + optional_size
        _span(self.data, section_table, section_count * 40, "PE section table")
        for index in range(section_count):
            offset = section_table + index * 40
            virtual_size = _u32(self.data, offset + 8, f"section {index} VirtualSize")
            virtual_address = _u32(self.data, offset + 12, f"section {index} VirtualAddress")
            raw_size = _u32(self.data, offset + 16, f"section {index} SizeOfRawData")
            raw_offset = _u32(self.data, offset + 20, f"section {index} PointerToRawData")
            self.sections.append((virtual_address, max(virtual_size, raw_size), raw_size, raw_offset))

        # Validate that the full resource directory itself maps to file bytes.
        self.rva_to_offset(self.resource_rva, min(self.resource_size, 16), "resource directory")

    def rva_to_offset(self, rva: int, size: int, what: str) -> int:
        if rva < self.size_of_headers:
            _span(self.data, rva, size, what)
            return rva
        for virtual_address, virtual_span, raw_size, raw_offset in self.sections:
            delta = rva - virtual_address
            if 0 <= delta < virtual_span:
                if delta > raw_size or size > raw_size - delta:
                    raise AuditError(f"{self.path}: {what} is not fully present in section raw data")
                _span(self.data, raw_offset + delta, size, what)
                return raw_offset + delta
        raise AuditError(f"{self.path}: {what} RVA 0x{rva:x} is not mapped by a PE section")

    def _resource_identifier(self, raw_name: int) -> int | str:
        if raw_name & 0x80000000 == 0:
            return raw_name & 0xFFFF
        relative = raw_name & 0x7FFFFFFF
        string_offset = self.rva_to_offset(self.resource_rva, 0, "resource root") + relative
        length = _u16(self.data, string_offset, "resource name length")
        text = _span(self.data, string_offset + 2, length * 2, "resource name").decode("utf-16le")
        return text

    def icon_resources(self) -> list[dict[str, Any]]:
        root_offset = self.rva_to_offset(self.resource_rva, 16, "resource directory root")
        found: list[dict[str, Any]] = []

        def walk(relative: int, path: tuple[int | str, ...], ancestors: frozenset[int]) -> None:
            if len(path) > 8:
                raise AuditError(f"{self.path}: resource directory nesting is too deep")
            if relative in ancestors:
                raise AuditError(f"{self.path}: cyclic resource directory")
            directory_offset = root_offset + relative
            header = _span(self.data, directory_offset, 16, "resource directory header")
            named = struct.unpack_from("<H", header, 12)[0]
            ids = struct.unpack_from("<H", header, 14)[0]
            entries_count = named + ids
            _span(self.data, directory_offset + 16, entries_count * 8, "resource directory entries")
            next_ancestors = ancestors | {relative}
            for index in range(entries_count):
                entry_offset = directory_offset + 16 + index * 8
                raw_name = _u32(self.data, entry_offset, "resource entry name")
                raw_target = _u32(self.data, entry_offset + 4, "resource entry target")
                identifier = self._resource_identifier(raw_name)
                child_path = path + (identifier,)
                target_relative = raw_target & 0x7FFFFFFF
                if raw_target & 0x80000000:
                    walk(target_relative, child_path, next_ancestors)
                    continue
                if not child_path or child_path[0] != 3:  # IMAGE_RESOURCE_TYPE_ICON
                    continue
                data_entry = root_offset + target_relative
                payload_rva = _u32(self.data, data_entry, "icon resource data RVA")
                payload_size = _u32(self.data, data_entry + 4, "icon resource data size")
                payload_offset = self.rva_to_offset(payload_rva, payload_size, "RT_ICON payload")
                payload = _span(self.data, payload_offset, payload_size, "RT_ICON payload")
                found.append({
                    "resource_path": list(child_path),
                    "payload_size": payload_size,
                    "payload_sha256": _sha256(payload),
                })

        walk(0, (), frozenset())
        if not found:
            raise AuditError(f"{self.path}: PE contains no RT_ICON resources")
        return found


def _display_path(path: Path, root: Path) -> str:
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        return str(path.resolve())


def audit(icon_path: Path, exe_paths: list[Path], output_path: Path, root: Path) -> dict[str, Any]:
    icon_bytes = icon_path.read_bytes()
    frames = _ico_frames(icon_bytes)
    exe_records: list[dict[str, Any]] = []
    failures: list[str] = []
    for exe_path in exe_paths:
        exe_bytes = exe_path.read_bytes()
        resources = _PeImage(exe_bytes, exe_path).icon_resources()
        resource_hashes = {resource["payload_sha256"] for resource in resources}
        missing_frames = [frame for frame in frames
                          if frame["payload_sha256"] not in resource_hashes]
        record = {
            "path": _display_path(exe_path, root),
            "sha256": _sha256(exe_bytes),
            "pe_kind": "PE32+" if struct.unpack_from("<H", exe_bytes, _u32(exe_bytes, 0x3C, "DOS e_lfanew") + 24)[0] == 0x20B else "PE32",
            "rt_icon_resource_count": len(resources),
            "rt_icon_resources": resources,
            "source_frame_count": len(frames),
            "matched_source_frame_count": len(frames) - len(missing_frames),
            "missing_source_frames": [
                {"index": frame["index"], "size": frame["size"],
                 "payload_sha256": frame["payload_sha256"]}
                for frame in missing_frames
            ],
            "all_source_frames_embedded": not missing_frames,
        }
        exe_records.append(record)
        if missing_frames:
            failures.append(f"{exe_path}: {len(missing_frames)} source ICO frame(s) are absent from RT_ICON resources")

    report = {
        "schema_version": 1,
        "audit": "PE RT_ICON payloads contain every source ICO frame payload",
        "icon": {
            "path": _display_path(icon_path, root),
            "sha256": _sha256(icon_bytes),
            "frame_count": len(frames),
            "frames": frames,
        },
        "executables": exe_records,
        "passed": not failures,
        "errors": failures,
    }
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return report


def main(argv: list[str] | None = None) -> int:
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--icon", type=Path, default=Path("assets/final_icon.ico"),
                        help="source ICO (default: assets/final_icon.ico from repository root)")
    parser.add_argument("--exe", type=Path, action="append", required=True,
                        help="Windows PE executable to audit; may be repeated")
    parser.add_argument("--output", type=Path, default=Path(".tooling/native-icon-audit.json"),
                        help="JSON report path (default: .tooling/native-icon-audit.json)")
    args = parser.parse_args(argv)

    icon_path = args.icon if args.icon.is_absolute() else (root / args.icon)
    exe_paths = [path if path.is_absolute() else (root / path) for path in args.exe]
    output_path = args.output if args.output.is_absolute() else (root / args.output)
    missing_paths = [path for path in [icon_path, *exe_paths] if not path.is_file()]
    if missing_paths:
        parser.error("input file not found: " + ", ".join(str(path) for path in missing_paths))

    try:
        report = audit(icon_path, exe_paths, output_path, root)
    except (AuditError, OSError, struct.error, UnicodeError) as exc:
        parser.error(str(exc))
    print(json.dumps({
        "passed": report["passed"],
        "report": _display_path(output_path, root),
        "icon_sha256": report["icon"]["sha256"],
        "icon_frame_count": report["icon"]["frame_count"],
        "executables": [{
            "path": record["path"],
            "sha256": record["sha256"],
            "rt_icon_resource_count": record["rt_icon_resource_count"],
            "matched_source_frame_count": record["matched_source_frame_count"],
            "source_frame_count": record["source_frame_count"],
            "all_source_frames_embedded": record["all_source_frames_embedded"],
        } for record in report["executables"]],
        "errors": report["errors"],
    }, ensure_ascii=False, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
