#!/usr/bin/env python3
"""Overlay SCR-scripted map markers into an existing glTF map.

The first pass focuses on entity placement opcodes used by cutscene scripts:

- 0x54: set_entity_position
- 0x55: set_entity_position with terrain-height lookup

Those handlers evaluate entity, x, y, z VM expressions, then divide coordinates
by fGpffff8c40 before writing entity+0x20/+0x24/+0x28. Debug coordinate output
prints those entity floats multiplied by 1000, and VM token 0x0F multiplies its
signed literal by 100 before the handler sees it. That makes 100000.0 the
calibrated VM-to-world divisor for script/debug coordinates.
The scale remains exposed as a CLI option so additional scenes can be checked.

Coordinates encoded with VM token 0x0F are multiplied by 100 by the bytecode
interpreter before opcode 0x54/0x55 sees them. Marker reports keep both forms:
raw_xyz is the script/debug-screen coordinate, while vm_xyz is the interpreter
value that gets divided by coord_scale for placement.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import struct
import sys
from pathlib import Path
from typing import Iterable


TOOL_DIR = Path(__file__).resolve().parent
if str(TOOL_DIR) not in sys.path:
    sys.path.insert(0, str(TOOL_DIR))

import scr_decompile as scr  # noqa: E402


POSITION_OPS = {
    0x54: "set_entity_position",
    0x55: "set_entity_position_with_terrain",
}

POSITION_SIGNATURE = scr.OpSignature(("entity", "x", "y", "z"))
NUMBER_RE = re.compile(r"^-?(?:0x[0-9a-fA-F]+|\d+)$")
WORK_ENTITY_RE = re.compile(
    r"^ctx\.read_script_work\(index=(?P<index>\d+), addr=0x[0-9a-fA-F]+\)$"
)


def parse_number_literal(text: str) -> int | None:
    if not NUMBER_RE.match(text):
        return None
    value = int(text, 0)
    if value >= 0x80000000:
        value -= 0x100000000
    return value


def signed_u32(value: int) -> int:
    if value >= 0x80000000:
        value -= 0x100000000
    return value


def single_literal_values(data: bytes, start_pc: int, next_pc: int) -> tuple[int, int] | None:
    """Return (raw_script_value, vm_value) for a simple literal expression."""

    if start_pc >= len(data):
        return None

    op = data[start_pc]
    token_end: int
    raw_value: int
    vm_value: int

    if op == 0x0C and start_pc + 2 < len(data):
        token_end = start_pc + 2
        raw_value = data[start_pc + 1]
        vm_value = raw_value
    elif op == 0x0D and start_pc + 3 < len(data):
        token_end = start_pc + 3
        raw_value = struct.unpack_from("<H", data, start_pc + 1)[0]
        vm_value = raw_value
    elif op == 0x0E and start_pc + 5 < len(data):
        token_end = start_pc + 5
        raw_value = signed_u32(struct.unpack_from("<I", data, start_pc + 1)[0])
        vm_value = raw_value
    elif op == 0x0F and start_pc + 5 < len(data):
        token_end = start_pc + 5
        raw_value = signed_u32(struct.unpack_from("<I", data, start_pc + 1)[0])
        vm_value = raw_value * 100
    else:
        return None

    if token_end >= len(data) or data[token_end] != 0x0B or next_pc != token_end + 1:
        return None
    return raw_value, vm_value


def parse_position_args(
    data: bytes,
    pc: int,
    end: int,
    opcode_names: dict[int, str],
) -> tuple[list[tuple[str, scr.Expr]], dict[str, tuple[int, int]], int]:
    args: list[tuple[str, scr.Expr]] = []
    literals: dict[str, tuple[int, int]] = {}
    for arg_name in POSITION_SIGNATURE.arg_names:
        expr_start = pc
        expr, pc = scr.parse_vm_expr(data, pc, end, opcode_names)
        args.append((arg_name, expr))
        literal = single_literal_values(data, expr_start, pc)
        if literal is not None:
            literals[arg_name] = literal
    return args, literals, pc


def entity_expr_is_static_enough(text: str) -> bool:
    return parse_number_literal(text) is not None or WORK_ENTITY_RE.match(text) is not None


def parse_vec3(text: str) -> tuple[float, float, float]:
    parts = [part.strip() for part in text.split(",")]
    if len(parts) != 3:
        raise argparse.ArgumentTypeError("expected three comma-separated floats")
    try:
        return (float(parts[0]), float(parts[1]), float(parts[2]))
    except ValueError as exc:
        raise argparse.ArgumentTypeError("expected three comma-separated floats") from exc


def ps2_to_gltf(point: tuple[float, float, float]) -> tuple[float, float, float]:
    x, y, z = point
    return (x, z, -y)


def blender_to_gltf(point: tuple[float, float, float]) -> tuple[float, float, float]:
    x, y, z = point
    return (x, z, -y)


def apply_manual_translation(
    markers: list[dict[str, object]],
    manual_gltf_xyz: tuple[float, float, float] | None,
    manual_blender_xyz: tuple[float, float, float] | None,
) -> None:
    if manual_gltf_xyz is None and manual_blender_xyz is None:
        return
    if manual_gltf_xyz is not None and manual_blender_xyz is not None:
        raise ValueError("use only one of --manual-gltf-xyz or --manual-blender-xyz")
    if len(markers) != 1:
        raise ValueError("manual marker placement requires exactly one emitted marker")

    marker = markers[0]
    marker["uncalibrated_gltf_xyz"] = marker.get("display_gltf_xyz", marker["gltf_xyz"])
    if manual_blender_xyz is not None:
        gltf_xyz = blender_to_gltf(manual_blender_xyz)
        marker["manual_blender_xyz"] = [round(value, 6) for value in manual_blender_xyz]
        marker["manual_placement_source"] = "blender"
    else:
        gltf_xyz = manual_gltf_xyz
        marker["manual_placement_source"] = "gltf"
    marker["display_gltf_xyz"] = [round(value, 6) for value in gltf_xyz]


def marker_matches_filters(
    marker: dict[str, object],
    include_addrs: set[int] | None,
    entity_filters: set[str] | None,
) -> bool:
    if include_addrs is not None and int(marker["addr"]) not in include_addrs:
        return False

    if entity_filters is not None:
        entity_text = str(marker["entity"])
        matched = False
        for entity_filter in entity_filters:
            if entity_filter.startswith("work:"):
                wanted = entity_filter.split(":", 1)[1]
                match = WORK_ENTITY_RE.match(entity_text)
                if match and match.group("index") == wanted:
                    matched = True
                    break
            elif entity_text == entity_filter:
                matched = True
                break
        if not matched:
            return False

    return True


def apply_marker_filters(
    markers: list[dict[str, object]],
    include_addrs: set[int] | None,
    entity_filters: set[str] | None,
) -> list[dict[str, object]]:
    return [
        marker
        for marker in markers
        if marker_matches_filters(marker, include_addrs, entity_filters)
    ]


def apply_display_offset(
    markers: list[dict[str, object]],
    display_offset: tuple[float, float, float],
) -> None:
    for marker in markers:
        base = marker["gltf_xyz"]
        marker["display_gltf_xyz"] = [
            round(float(base[index]) + display_offset[index], 6)
            for index in range(3)
        ]
        marker["display_offset"] = [round(value, 6) for value in display_offset]


def lift_markers_by_radius(markers: list[dict[str, object]], marker_radius: float) -> None:
    for marker in markers:
        base = marker.get("display_gltf_xyz", marker["gltf_xyz"])
        marker["display_gltf_xyz"] = [
            round(float(base[0]), 6),
            round(float(base[1]) + marker_radius, 6),
            round(float(base[2]), 6),
        ]
        marker["marker_lift_gltf_y"] = round(marker_radius, 6)


def collect_position_markers(
    scr_path: Path,
    coord_scale: float,
    scan_start: int | None = None,
    scan_end: int | None = None,
) -> tuple[list[dict[str, object]], dict[str, int]]:
    data = scr.read_data(scr_path)
    header = scr.parse_header(data)
    opcode_names = scr.load_opcode_names(Path("analyzed/opcode_dispatch_tables.md"))

    start = header.words[0] if scan_start is None else scan_start
    end = header.footer_start if scan_end is None else scan_end
    start = max(0, min(start, len(data)))
    end = max(start, min(end, len(data)))

    markers: list[dict[str, object]] = []
    stats = {
        "opcode_candidates": 0,
        "parsed_candidates": 0,
        "static_markers": 0,
        "skipped_dynamic_or_implausible": 0,
    }

    for pc in range(start, end):
        opcode = data[pc]
        if opcode not in POSITION_OPS:
            continue

        stats["opcode_candidates"] += 1
        try:
            args, literal_values, next_pc = parse_position_args(
                data,
                pc + 1,
                min(len(data), pc + 96),
                opcode_names,
            )
        except Exception:
            stats["skipped_dynamic_or_implausible"] += 1
            continue

        stats["parsed_candidates"] += 1
        arg_text = {name: expr.text for name, expr in args}
        vm_xyz = tuple(parse_number_literal(arg_text[axis]) for axis in ("x", "y", "z"))

        if (
            any(value is None for value in vm_xyz)
            or not entity_expr_is_static_enough(arg_text["entity"])
        ):
            stats["skipped_dynamic_or_implausible"] += 1
            continue

        vm_x, vm_y, vm_z = (int(value) for value in vm_xyz if value is not None)
        raw_xyz = tuple(
            literal_values.get(axis, (vm_value, vm_value))[0]
            for axis, vm_value in zip(("x", "y", "z"), (vm_x, vm_y, vm_z))
        )
        raw_x, raw_y, raw_z = raw_xyz
        ps2_xyz = (vm_x / coord_scale, vm_y / coord_scale, vm_z / coord_scale)
        gltf_xyz = ps2_to_gltf(ps2_xyz)

        marker = {
            "source_scr": scr_path.as_posix(),
            "addr": pc,
            "addr_hex": f"0x{pc:05x}",
            "opcode": f"0x{opcode:02x}",
            "kind": POSITION_OPS[opcode],
            "next_pc": next_pc,
            "entity": arg_text["entity"],
            "raw_xyz": [raw_x, raw_y, raw_z],
            "vm_xyz": [vm_x, vm_y, vm_z],
            "ps2_xyz": [round(value, 6) for value in ps2_xyz],
            "gltf_xyz": [round(value, 6) for value in gltf_xyz],
            "coord_scale": coord_scale,
            "raw_coord_scale": coord_scale / 100.0,
        }
        markers.append(marker)
        stats["static_markers"] += 1

    return markers, stats


def pack_f32_vec3(values: Iterable[tuple[float, float, float]]) -> bytes:
    blob = bytearray()
    for x, y, z in values:
        blob.extend(struct.pack("<fff", x, y, z))
    return bytes(blob)


def pack_u16(values: Iterable[int]) -> bytes:
    return b"".join(struct.pack("<H", value) for value in values)


def pad4(buffer: bytearray) -> None:
    while len(buffer) % 4:
        buffer.append(0)


def unit_uv_sphere(segments: int = 16, rings: int = 8) -> tuple[list[tuple[float, float, float]], list[int]]:
    positions: list[tuple[float, float, float]] = []
    indices: list[int] = []

    for ring in range(rings + 1):
        phi = math.pi * ring / rings
        y = math.cos(phi)
        radius = math.sin(phi)
        for segment in range(segments):
            theta = 2.0 * math.pi * segment / segments
            x = radius * math.cos(theta)
            z = radius * math.sin(theta)
            positions.append((x, y, z))

    for ring in range(rings):
        for segment in range(segments):
            a = ring * segments + segment
            b = ring * segments + ((segment + 1) % segments)
            c = (ring + 1) * segments + segment
            d = (ring + 1) * segments + ((segment + 1) % segments)
            if ring != 0:
                indices.extend((a, c, b))
            if ring != rings - 1:
                indices.extend((b, c, d))

    return positions, indices


def read_external_buffer(gltf_path: Path, gltf: dict[str, object]) -> tuple[bytearray, str]:
    buffers = gltf.get("buffers")
    if not isinstance(buffers, list) or not buffers:
        raise ValueError("glTF has no external buffers to extend")

    buffer0 = buffers[0]
    if not isinstance(buffer0, dict):
        raise ValueError("glTF buffer[0] is not an object")

    uri = buffer0.get("uri")
    if not isinstance(uri, str) or uri.startswith("data:"):
        raise ValueError("only external non-data URI glTF buffers are supported")

    bin_path = gltf_path.parent / uri
    return bytearray(bin_path.read_bytes()), uri


def append_marker_mesh(
    gltf_path: Path,
    output_path: Path,
    markers: list[dict[str, object]],
    marker_radius: float,
) -> None:
    gltf = json.loads(gltf_path.read_text(encoding="utf-8"))
    bin_blob, _original_uri = read_external_buffer(gltf_path, gltf)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_bin_path = output_path.with_suffix(".bin")
    output_bin_uri = output_bin_path.name

    positions, indices = unit_uv_sphere()
    normals = positions

    pad4(bin_blob)
    pos_offset = len(bin_blob)
    bin_blob.extend(pack_f32_vec3(positions))

    pad4(bin_blob)
    normal_offset = len(bin_blob)
    bin_blob.extend(pack_f32_vec3(normals))

    pad4(bin_blob)
    index_offset = len(bin_blob)
    bin_blob.extend(pack_u16(indices))

    buffer_views = gltf.setdefault("bufferViews", [])
    accessors = gltf.setdefault("accessors", [])
    meshes = gltf.setdefault("meshes", [])
    materials = gltf.setdefault("materials", [])
    nodes = gltf.setdefault("nodes", [])

    if not all(isinstance(value, list) for value in (buffer_views, accessors, meshes, materials, nodes)):
        raise ValueError("glTF bufferViews/accessors/meshes/materials/nodes must be arrays")

    pos_view = len(buffer_views)
    buffer_views.append(
        {
            "buffer": 0,
            "byteOffset": pos_offset,
            "byteLength": len(positions) * 12,
            "target": 34962,
        }
    )
    normal_view = len(buffer_views)
    buffer_views.append(
        {
            "buffer": 0,
            "byteOffset": normal_offset,
            "byteLength": len(normals) * 12,
            "target": 34962,
        }
    )
    index_view = len(buffer_views)
    buffer_views.append(
        {
            "buffer": 0,
            "byteOffset": index_offset,
            "byteLength": len(indices) * 2,
            "target": 34963,
        }
    )

    pos_accessor = len(accessors)
    accessors.append(
        {
            "bufferView": pos_view,
            "componentType": 5126,
            "count": len(positions),
            "type": "VEC3",
            "min": [-1.0, -1.0, -1.0],
            "max": [1.0, 1.0, 1.0],
        }
    )
    normal_accessor = len(accessors)
    accessors.append(
        {
            "bufferView": normal_view,
            "componentType": 5126,
            "count": len(normals),
            "type": "VEC3",
        }
    )
    index_accessor = len(accessors)
    accessors.append(
        {
            "bufferView": index_view,
            "componentType": 5123,
            "count": len(indices),
            "type": "SCALAR",
        }
    )

    material_index = len(materials)
    materials.append(
        {
            "name": "scr_marker_hot_pink",
            "pbrMetallicRoughness": {
                "baseColorFactor": [1.0, 0.0, 0.72, 1.0],
                "metallicFactor": 0.0,
                "roughnessFactor": 0.35,
            },
            "emissiveFactor": [1.0, 0.0, 0.45],
            "doubleSided": True,
        }
    )

    mesh_index = len(meshes)
    meshes.append(
        {
            "name": "scr_marker_sphere",
            "primitives": [
                {
                    "attributes": {"POSITION": pos_accessor, "NORMAL": normal_accessor},
                    "indices": index_accessor,
                    "material": material_index,
                    "mode": 4,
                }
            ],
        }
    )

    scene_index = int(gltf.get("scene", 0))
    scenes = gltf.setdefault("scenes", [{"nodes": []}])
    if not isinstance(scenes, list) or not scenes:
        raise ValueError("glTF scenes must be a non-empty array")
    while scene_index >= len(scenes):
        scenes.append({"nodes": []})
    scene = scenes[scene_index]
    if not isinstance(scene, dict):
        raise ValueError("active glTF scene is not an object")
    scene_nodes = scene.setdefault("nodes", [])
    if not isinstance(scene_nodes, list):
        raise ValueError("active glTF scene nodes must be an array")

    for marker in markers:
        node_index = len(nodes)
        translation = marker.get("display_gltf_xyz", marker["gltf_xyz"])
        nodes.append(
            {
                "name": f"{Path(str(marker['source_scr'])).stem}_{marker['addr_hex']}_{marker['opcode']}",
                "mesh": mesh_index,
                "translation": translation,
                "scale": [marker_radius, marker_radius, marker_radius],
                "extras": {"orphen_scr_marker": marker},
            }
        )
        scene_nodes.append(node_index)

    buffers = gltf["buffers"]
    buffers[0]["uri"] = output_bin_uri
    buffers[0]["byteLength"] = len(bin_blob)

    asset = gltf.setdefault("asset", {"version": "2.0"})
    if isinstance(asset, dict):
        previous_generator = asset.get("generator", "")
        suffix = "scr_gltf_markers.py"
        asset["generator"] = f"{previous_generator} + {suffix}" if previous_generator else suffix

    output_bin_path.write_bytes(bin_blob)
    output_path.write_text(json.dumps(gltf, indent=2), encoding="utf-8")


def write_marker_report(path: Path, markers: list[dict[str, object]], stats: dict[str, int]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    report = {
        "stats": stats,
        "markers": markers,
    }
    path.write_text(json.dumps(report, indent=2), encoding="utf-8")


def default_output_path(gltf_path: Path, scr_path: Path) -> Path:
    return gltf_path.with_name(f"{gltf_path.stem}_{scr_path.stem}_markers.gltf")


def parse_int_arg(value: str) -> int:
    return int(value, 0)


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Inject SCR entity-position markers into a generated map glTF."
    )
    parser.add_argument("scr", type=Path, help="Decoded SCR .out file, e.g. scr/scr2.out")
    parser.add_argument("gltf", type=Path, help="Generated map glTF to annotate")
    parser.add_argument("--output", "-o", type=Path, help="Annotated glTF path")
    parser.add_argument("--json-output", type=Path, help="Optional marker report JSON path")
    parser.add_argument(
        "--coord-scale",
        type=float,
        default=100000.0,
        help="VM/world divisor for 0x54/0x55 coordinates; debug-screen units calibrate to 100000.0",
    )
    parser.add_argument(
        "--marker-radius",
        type=float,
        default=0.35,
        help="Radius of each pink marker sphere in glTF/world units",
    )
    parser.add_argument("--scan-start", type=parse_int_arg, help="Override SCR scan start offset")
    parser.add_argument("--scan-end", type=parse_int_arg, help="Override SCR scan end offset")
    parser.add_argument(
        "--include-addr",
        type=parse_int_arg,
        action="append",
        help="Only emit marker(s) from this SCR opcode address; may be repeated",
    )
    parser.add_argument(
        "--entity",
        action="append",
        help="Only emit markers for an entity expression, e.g. '0', '256', or 'work:4'; may be repeated",
    )
    parser.add_argument(
        "--display-offset",
        type=parse_vec3,
        default=(0.0, 0.0, 0.0),
        help="Optional glTF-space display offset applied to marker nodes, e.g. 0,2,0",
    )
    parser.add_argument(
        "--lift-by-radius",
        action="store_true",
        help="Lift marker centers by marker radius along glTF Y so spheres sit on the entity coordinate",
    )
    parser.add_argument(
        "--manual-gltf-xyz",
        type=parse_vec3,
        help="Override a single marker's glTF-space display position, e.g. --manual-gltf-xyz=-6,1,-1",
    )
    parser.add_argument(
        "--manual-blender-xyz",
        type=parse_vec3,
        help="Override a single marker using Blender coordinates; converted to glTF as (x,z,-y)",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_arg_parser().parse_args(argv)
    scr_path = args.scr
    gltf_path = args.gltf
    output_path = args.output or default_output_path(gltf_path, scr_path)

    markers, stats = collect_position_markers(
        scr_path,
        coord_scale=args.coord_scale,
        scan_start=args.scan_start,
        scan_end=args.scan_end,
    )
    include_addrs = set(args.include_addr) if args.include_addr else None
    entity_filters = set(args.entity) if args.entity else None
    markers = apply_marker_filters(markers, include_addrs, entity_filters)
    stats["emitted_markers"] = len(markers)
    apply_display_offset(markers, args.display_offset)
    if args.lift_by_radius:
        lift_markers_by_radius(markers, args.marker_radius)
    apply_manual_translation(markers, args.manual_gltf_xyz, args.manual_blender_xyz)
    append_marker_mesh(gltf_path, output_path, markers, marker_radius=args.marker_radius)

    if args.json_output:
        write_marker_report(args.json_output, markers, stats)

    print(f"SCR markers: {stats['emitted_markers']} written")
    print(f"  opcode candidates: {stats['opcode_candidates']}")
    print(f"  static markers before filters: {stats['static_markers']}")
    print(f"  skipped dynamic/implausible: {stats['skipped_dynamic_or_implausible']}")
    print(f"  glTF: {output_path}")
    print(f"  bin:  {output_path.with_suffix('.bin')}")
    if args.json_output:
        print(f"  json: {args.json_output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())