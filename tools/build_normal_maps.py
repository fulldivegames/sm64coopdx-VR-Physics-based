#!/usr/bin/env python3
"""Build the optional normal-map resource and its builtin texture manifest.

The exported files are keyed by the US ROM address and segment offset in
their names (``<address>_<offset>-N.bmp``).  This tool keeps that association
data-only: the renderer never guesses from color or texture dimensions.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import struct
from dataclasses import dataclass


MAGIC = 0x50414D4E  # "NMAP" in little-endian files
VERSION = 1
HEADER_SIZE = 16
RECORD_SIZE = 16
NAME_RE = re.compile(r"^(\d+)_([0-9A-Fa-f]{8})-N\.bmp$")
BUILTIN_RE = re.compile(
    r"\b(define_builtin_tex_?)\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*,\s*\"([^\"]+)\""
)


@dataclass(frozen=True)
class MapRecord:
    filename: str
    width: int
    height: int
    rgb: bytes


def read_bmp(path: pathlib.Path) -> MapRecord:
    data = path.read_bytes()
    if len(data) < 54 or data[:2] != b"BM":
        raise ValueError(f"{path.name}: not a BMP")
    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    dib_size = struct.unpack_from("<I", data, 14)[0]
    if dib_size < 40:
        raise ValueError(f"{path.name}: unsupported DIB header")
    width = struct.unpack_from("<i", data, 18)[0]
    signed_height = struct.unpack_from("<i", data, 22)[0]
    planes = struct.unpack_from("<H", data, 26)[0]
    bits = struct.unpack_from("<H", data, 28)[0]
    compression = struct.unpack_from("<I", data, 30)[0]
    if width <= 0 or signed_height == 0 or planes != 1 or bits != 24 or compression != 0:
        raise ValueError(
            f"{path.name}: expected uncompressed 24-bit BMP, got "
            f"{width}x{signed_height}, planes={planes}, bpp={bits}, compression={compression}"
        )
    height = abs(signed_height)
    stride = (width * 3 + 3) & ~3
    end = pixel_offset + stride * height
    if pixel_offset < 54 or end > len(data):
        raise ValueError(f"{path.name}: truncated pixel data")

    # Store top-to-bottom RGB. BMP is BGR and positive heights are bottom-up.
    rows = []
    source_rows = range(height - 1, -1, -1) if signed_height > 0 else range(height)
    for row in source_rows:
        source = data[pixel_offset + row * stride : pixel_offset + (row + 1) * stride]
        converted = bytearray(width * 3)
        for x in range(width):
            b, g, r = source[x * 3 : x * 3 + 3]
            converted[x * 3 : x * 3 + 3] = bytes((r, g, b))
        rows.append(bytes(converted))
    return MapRecord(path.name, width, height, b"".join(rows))


def load_assets_keys(path: pathlib.Path) -> dict[tuple[int, int], list[str]]:
    assets = json.loads(path.read_text(encoding="utf-8"))
    result: dict[tuple[int, int], list[str]] = {}
    for asset_path, value in assets.items():
        if not isinstance(value, list) or not value or not isinstance(value[-1], dict):
            continue
        us = value[-1].get("us")
        if not isinstance(us, list) or len(us) < 2:
            continue
        if not all(isinstance(item, (int, float)) for item in us[:2]):
            continue
        base, offset = int(us[0]), int(us[1])
        result.setdefault((base + offset, offset), []).append(asset_path)
    return result


def load_builtin_entries(path: pathlib.Path) -> list[tuple[str, str]]:
    text = path.read_text(encoding="utf-8")
    entries: list[tuple[str, str]] = []
    for macro, symbol, asset_path in BUILTIN_RE.findall(text):
        name = symbol + ("_" if macro.endswith("_") else "")
        entries.append((name, asset_path))
    return entries


def c_string(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def build(args: argparse.Namespace) -> None:
    normal_dir = args.normal_dir
    files = sorted(normal_dir.glob("*.bmp"), key=lambda item: item.name.lower())
    if not files:
        raise SystemExit(f"No BMP normal maps found in {normal_dir}")

    records: list[MapRecord] = []
    keys: dict[tuple[int, int], int] = {}
    for index, path in enumerate(files):
        match = NAME_RE.fullmatch(path.name)
        if match is None:
            raise ValueError(f"Unexpected normal-map filename: {path.name}")
        key = (int(match.group(1)), int(match.group(2), 16))
        if key in keys:
            raise ValueError(f"Duplicate normal-map address key: {path.name}")
        keys[key] = index
        records.append(read_bmp(path))

    assets_keys = load_assets_keys(args.assets_json)
    builtin_entries = load_builtin_entries(args.builtin_table)
    manifest: list[tuple[str, int]] = []
    for name, asset_path in builtin_entries:
        candidates = []
        for key, asset_paths in assets_keys.items():
            if asset_path in asset_paths and key in keys:
                candidates.append(keys[key])
        if candidates:
            manifest.append((name, min(candidates)))

    args.bundle.parent.mkdir(parents=True, exist_ok=True)
    record_table_size = HEADER_SIZE + RECORD_SIZE * len(records)
    table = bytearray()
    payload = bytearray()
    for record in records:
        offset = record_table_size + len(payload)
        payload.extend(record.rgb)
        table.extend(struct.pack("<IIII", offset, len(record.rgb), record.width, record.height))
    with args.bundle.open("wb") as output:
        output.write(struct.pack("<IIII", MAGIC, VERSION, len(records), HEADER_SIZE))
        output.write(table)
        output.write(payload)

    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    with args.manifest.open("w", encoding="utf-8", newline="\n") as output:
        output.write("#ifndef GFX_NORMAL_MAPS_MANIFEST_H\n#define GFX_NORMAL_MAPS_MANIFEST_H\n\n")
        output.write("#include <stdint.h>\n\n")
        output.write("struct GfxNormalMapNameEntry { const char *name; uint32_t index; };\n\n")
        output.write(f"#define GFX_NORMAL_MAP_RECORD_COUNT {len(records)}u\n")
        output.write(f"#define GFX_NORMAL_MAP_NAME_COUNT {len(manifest)}u\n\n")
        output.write("static const struct GfxNormalMapNameEntry gGfxNormalMapNames[] = {\n")
        for name, index in sorted(manifest, key=lambda item: item[0]):
            output.write(f"    {{ {c_string(name)}, {index}u }},\n")
        output.write("};\n\n#endif\n")

    print(
        f"wrote {args.bundle} ({args.bundle.stat().st_size} bytes), "
        f"{len(records)} records and {len(manifest)} builtin associations"
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--normal-dir", type=pathlib.Path, required=True)
    parser.add_argument("--assets-json", type=pathlib.Path, required=True)
    parser.add_argument("--builtin-table", type=pathlib.Path, required=True)
    parser.add_argument("--bundle", type=pathlib.Path, required=True)
    parser.add_argument("--manifest", type=pathlib.Path, required=True)
    build(parser.parse_args())


if __name__ == "__main__":
    main()
