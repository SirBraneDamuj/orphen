#!/usr/bin/env python3
"""Overlay tagged PSM2 terrain/collision surfaces into an existing glTF map.

This is meant for SCR trigger archaeology. The terrain sampler copies the
runtime PSM2 0x78-record flag word at +0x04 into the lead entity surface-state
fields; SCR opcode 0x61 can then test those bits. Loader FUN_0022b5a8 derives
that flag word from Section D u16[10] | (u16[11] << 16).

For the first SCR2 cutscene gate, the interesting surface predicate is:

    include 0x10, exclude 0x20 | 0x40 | 0x80

The overlay uses the same coordinate transform as the PSM2 glTF exporter:
PSM2/PS2 (x, y, z) -> glTF (x, z, -y).
"""

from __future__ import annotations

import argparse
import json
import math
import struct
import sys
from pathlib import Path
from typing import Iterable


ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.resource_extract.v2.psm2 import parse_psm2  # noqa: E402


def parse_int_arg(value: str) -> int:
    return int(value, 0)


def parse_vec3_arg(value: str) -> tuple[float, float, float]:
    parts = [part.strip() for part in value.split(",")]
    if len(parts) != 3:
        raise argparse.ArgumentTypeError("expected three comma-separated floats")
    try:
        return (float(parts[0]), float(parts[1]), float(parts[2]))
    except ValueError as exc:
        raise argparse.ArgumentTypeError("expected three comma-separated floats") from exc


def parse_color_arg(value: str) -> tuple[float, float, float, float]:
    parts = [part.strip() for part in value.split(",")]
    if len(parts) not in (3, 4):
        raise argparse.ArgumentTypeError("expected r,g,b or r,g,b,a floats")
    try:
        values = [float(part) for part in parts]
    except ValueError as exc:
        raise argparse.ArgumentTypeError("expected r,g,b or r,g,b,a floats") from exc
    if len(values) == 3:
        values.append(0.55)
    if any(value < 0.0 or value > 1.0 for value in values):
        raise argparse.ArgumentTypeError("color components must be in [0, 1]")
    return (values[0], values[1], values[2], values[3])


def ps2_to_gltf(point: tuple[float, float, float]) -> tuple[float, float, float]:
    x, y, z = point
    return (x, z, -y)


def pack_f32_vec3(values: Iterable[tuple[float, float, float]]) -> bytes:
    blob = bytearray()
    for x, y, z in values:
        blob.extend(struct.pack("<fff", x, y, z))
    return bytes(blob)


def pack_u32(values: Iterable[int]) -> bytes:
    return b"".join(struct.pack("<I", value) for value in values)


def pad4(buffer: bytearray) -> None:
    while len(buffer) % 4:
        buffer.append(0)


def bbox(points: list[tuple[float, float, float]]) -> tuple[list[float], list[float]]:
    if not points:
        return ([0.0, 0.0, 0.0], [0.0, 0.0, 0.0])
    min_values = list(points[0])
    max_values = list(points[0])
    for point in points[1:]:
        for index, value in enumerate(point):
            min_values[index] = min(min_values[index], value)
            max_values[index] = max(max_values[index], value)
    return min_values, max_values


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
    return bytearray((gltf_path.parent / uri).read_bytes()), uri


def resolve_near_point(
    near_gltf_xyz: tuple[float, float, float] | None,
    near_ps2_xyz: tuple[float, float, float] | None,
    marker_json: Path | None,
) -> tuple[float, float, float] | None:
    sources = [near_gltf_xyz is not None, near_ps2_xyz is not None, marker_json is not None]
    if sum(sources) > 1:
        raise ValueError("use only one near-point source")
    if near_gltf_xyz is not None:
        return near_gltf_xyz
    if near_ps2_xyz is not None:
        return ps2_to_gltf(near_ps2_xyz)
    if marker_json is None:
        return None

    report = json.loads(marker_json.read_text(encoding="utf-8"))
    markers = report.get("markers")
    if not isinstance(markers, list) or not markers:
        raise ValueError("marker JSON does not contain any markers")
    marker = markers[0]
    if not isinstance(marker, dict):
        raise ValueError("marker JSON first marker is not an object")
    point = marker.get("display_gltf_xyz", marker.get("gltf_xyz"))
    if not isinstance(point, list) or len(point) != 3:
        raise ValueError("marker JSON first marker has no glTF point")
    return (float(point[0]), float(point[1]), float(point[2]))


def distance_to_near(
    center: tuple[float, float, float],
    near: tuple[float, float, float] | None,
    mode: str,
) -> float | None:
    if near is None:
        return None
    dx = center[0] - near[0]
    dy = center[1] - near[1]
    dz = center[2] - near[2]
    if mode == "xz":
        return math.hypot(dx, dz)
    return math.sqrt(dx * dx + dy * dy + dz * dz)


def center_of(points: list[tuple[float, float, float]]) -> tuple[float, float, float]:
    count = float(len(points))
    return (
        sum(point[0] for point in points) / count,
        sum(point[1] for point in points) / count,
        sum(point[2] for point in points) / count,
    )


def collect_trigger_surface_triangles(
    psm2_path: Path,
    include_mask: int,
    include_any_mask: int | None,
    exclude_mask: int,
    near: tuple[float, float, float] | None,
    max_distance: float | None,
    distance_mode: str,
    lift: float,
    min_xz_area: float | None,
    max_y_span: float | None,
) -> tuple[list[tuple[float, float, float]], list[int], list[dict[str, object]], dict[str, object]]:
    mesh = parse_psm2(psm2_path.read_bytes())
    positions: list[tuple[float, float, float]] = []
    indices: list[int] = []
    surfaces: list[dict[str, object]] = []
    stats: dict[str, object] = {
        "source_psm2": psm2_path.as_posix(),
        "primitive_count": len(mesh.primitives),
        "include_mask": include_mask,
        "include_any_mask": include_any_mask,
        "exclude_mask": exclude_mask,
        "matching_mask_count": 0,
        "emitted_surface_count": 0,
        "emitted_triangle_count": 0,
        "skipped_invalid_index": 0,
        "skipped_distance": 0,
        "skipped_geometry": 0,
        "near_gltf_xyz": [round(value, 6) for value in near] if near else None,
        "max_distance": max_distance,
        "distance_mode": distance_mode,
        "min_xz_area": min_xz_area,
        "max_y_span": max_y_span,
    }

    def emit_vertex(point: tuple[float, float, float]) -> int:
        positions.append((point[0], point[1] + lift, point[2]))
        return len(positions) - 1

    vertex_count = len(mesh.positions)
    for prim_index, prim in enumerate(mesh.primitives):
        flags = mesh.primitive_terrain_flags[prim_index] if prim_index < len(mesh.primitive_terrain_flags) else 0
        if include_mask and (flags & include_mask) != include_mask:
            continue
        if include_any_mask is not None and (flags & include_any_mask) == 0:
            continue
        if (flags & exclude_mask) != 0:
            continue
        stats["matching_mask_count"] = int(stats["matching_mask_count"]) + 1

        s0, s1, s2, s3 = prim
        if not all(0 <= index < vertex_count for index in (s0, s1, s2, s3)):
            stats["skipped_invalid_index"] = int(stats["skipped_invalid_index"]) + 1
            continue

        gltf_points = [ps2_to_gltf(mesh.positions[index]) for index in (s0, s1, s2, s3)]
        surface_points = gltf_points[:3] if s2 == s3 else gltf_points
        surface_min, surface_max = bbox(surface_points)
        xz_area = abs((surface_max[0] - surface_min[0]) * (surface_max[2] - surface_min[2]))
        y_span = abs(surface_max[1] - surface_min[1])
        if min_xz_area is not None and xz_area < min_xz_area:
            stats["skipped_geometry"] = int(stats["skipped_geometry"]) + 1
            continue
        if max_y_span is not None and y_span > max_y_span:
            stats["skipped_geometry"] = int(stats["skipped_geometry"]) + 1
            continue
        center = center_of(surface_points)
        distance = distance_to_near(center, near, distance_mode)
        if max_distance is not None and distance is not None and distance > max_distance:
            stats["skipped_distance"] = int(stats["skipped_distance"]) + 1
            continue

        before_triangle_count = len(indices) // 3
        if s2 == s3:
            a = emit_vertex(gltf_points[0])
            b = emit_vertex(gltf_points[1])
            c = emit_vertex(gltf_points[2])
            indices.extend((a, b, c))
        else:
            a = emit_vertex(gltf_points[0])
            b = emit_vertex(gltf_points[1])
            c = emit_vertex(gltf_points[2])
            d = emit_vertex(gltf_points[3])
            indices.extend((a, b, c, a, c, d))

        added_triangles = len(indices) // 3 - before_triangle_count
        surfaces.append(
            {
                "primitive_index": prim_index,
                "flags": flags,
                "flags_hex": f"0x{flags:08x}",
                "vertex_indices": [s0, s1, s2, s3],
                "corners_gltf_xyz": [
                    [round(value, 6) for value in point]
                    for point in surface_points
                ],
                "bbox_gltf_min": [round(value, 6) for value in surface_min],
                "bbox_gltf_max": [round(value, 6) for value in surface_max],
                "xz_area": round(xz_area, 6),
                "y_span": round(y_span, 6),
                "center_gltf_xyz": [round(value, 6) for value in center],
                "distance": round(distance, 6) if distance is not None else None,
                "triangle_count": added_triangles,
            }
        )

    stats["emitted_surface_count"] = len(surfaces)
    stats["emitted_triangle_count"] = len(indices) // 3
    stats["emitted_vertex_count"] = len(positions)
    return positions, indices, surfaces, stats


def append_surface_overlay(
    base_gltf_path: Path,
    output_path: Path,
    positions: list[tuple[float, float, float]],
    indices: list[int],
    surfaces: list[dict[str, object]],
    color: tuple[float, float, float, float],
    stats: dict[str, object],
    label_nodes: bool,
    label_lift: float,
) -> None:
    gltf = json.loads(base_gltf_path.read_text(encoding="utf-8"))
    bin_blob, _original_uri = read_external_buffer(base_gltf_path, gltf)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_bin_path = output_path.with_suffix(".bin")
    output_bin_uri = output_bin_path.name

    buffer_views = gltf.setdefault("bufferViews", [])
    accessors = gltf.setdefault("accessors", [])
    meshes = gltf.setdefault("meshes", [])
    materials = gltf.setdefault("materials", [])
    nodes = gltf.setdefault("nodes", [])
    if not all(isinstance(value, list) for value in (buffer_views, accessors, meshes, materials, nodes)):
        raise ValueError("glTF bufferViews/accessors/meshes/materials/nodes must be arrays")

    pad4(bin_blob)
    position_offset = len(bin_blob)
    bin_blob.extend(pack_f32_vec3(positions))

    pad4(bin_blob)
    index_offset = len(bin_blob)
    bin_blob.extend(pack_u32(indices))

    position_view = len(buffer_views)
    buffer_views.append(
        {
            "buffer": 0,
            "byteOffset": position_offset,
            "byteLength": len(positions) * 12,
            "target": 34962,
        }
    )
    index_view = len(buffer_views)
    buffer_views.append(
        {
            "buffer": 0,
            "byteOffset": index_offset,
            "byteLength": len(indices) * 4,
            "target": 34963,
        }
    )

    min_values, max_values = bbox(positions)
    position_accessor = len(accessors)
    accessors.append(
        {
            "bufferView": position_view,
            "componentType": 5126,
            "count": len(positions),
            "type": "VEC3",
            "min": min_values,
            "max": max_values,
        }
    )
    index_accessor = len(accessors)
    accessors.append(
        {
            "bufferView": index_view,
            "componentType": 5125,
            "count": len(indices),
            "type": "SCALAR",
        }
    )

    material_index = len(materials)
    materials.append(
        {
            "name": "scr_trigger_surface_hot_pink",
            "pbrMetallicRoughness": {
                "baseColorFactor": list(color),
                "metallicFactor": 0.0,
                "roughnessFactor": 0.6,
            },
            "emissiveFactor": [color[0] * 0.7, color[1] * 0.7, color[2] * 0.7],
            "alphaMode": "BLEND",
            "doubleSided": True,
        }
    )

    mesh_index = len(meshes)
    meshes.append(
        {
            "name": "scr_trigger_surface_overlay",
            "primitives": [
                {
                    "attributes": {"POSITION": position_accessor},
                    "indices": index_accessor,
                    "material": material_index,
                    "mode": 4,
                }
            ],
            "extras": {"orphen_trigger_surface_stats": stats},
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

    node_index = len(nodes)
    nodes.append(
        {
            "name": "scr_trigger_surface_overlay",
            "mesh": mesh_index,
            "extras": {"orphen_trigger_surface_stats": stats},
        }
    )
    scene_nodes.append(node_index)

    if label_nodes:
        for surface in surfaces:
            center = surface.get("center_gltf_xyz")
            if not isinstance(center, list) or len(center) != 3:
                continue
            flags_hex = surface.get("flags_hex", "0x00000000")
            primitive_index = int(surface.get("primitive_index", -1))
            label_node_index = len(nodes)
            nodes.append(
                {
                    "name": f"trigger_quad_{primitive_index:04d}_{flags_hex}",
                    "translation": [
                        float(center[0]),
                        float(center[1]) + label_lift,
                        float(center[2]),
                    ],
                    "extras": {"orphen_trigger_surface": surface},
                }
            )
            scene_nodes.append(label_node_index)
        stats["label_node_count"] = len(surfaces)

    buffers = gltf["buffers"]
    buffers[0]["uri"] = output_bin_uri
    buffers[0]["byteLength"] = len(bin_blob)

    asset = gltf.setdefault("asset", {"version": "2.0"})
    if isinstance(asset, dict):
        previous_generator = asset.get("generator", "")
        suffix = "psm2_trigger_surface_overlay.py"
        asset["generator"] = f"{previous_generator} + {suffix}" if previous_generator else suffix

    output_bin_path.write_bytes(bin_blob)
    output_path.write_text(json.dumps(gltf, indent=2), encoding="utf-8")


def write_report(path: Path, stats: dict[str, object], surfaces: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps({"stats": stats, "surfaces": surfaces}, indent=2), encoding="utf-8")


def default_output_path(base_gltf_path: Path) -> Path:
    return base_gltf_path.with_name(f"{base_gltf_path.stem}_trigger_surface.gltf")


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Append a translucent overlay for PSM2 terrain surfaces matching a trigger mask."
    )
    parser.add_argument("psm2", type=Path, help="Source PSM2 payload, e.g. out/target_all/.../map_0002.psm2")
    parser.add_argument("gltf", type=Path, help="Existing generated map glTF to annotate")
    parser.add_argument("--output", "-o", type=Path, help="Annotated output glTF path")
    parser.add_argument("--json-output", type=Path, help="Optional JSON report path")
    parser.add_argument("--include-mask", type=parse_int_arg, default=0x10)
    parser.add_argument(
        "--include-any-mask",
        type=parse_int_arg,
        help="Keep primitives with any bit in this mask set; useful for broad trigger candidate overlays",
    )
    parser.add_argument("--exclude-mask", type=parse_int_arg, default=0xE0)
    parser.add_argument("--near-gltf-xyz", type=parse_vec3_arg, help="Optional glTF-space near point, x,y,z")
    parser.add_argument("--near-ps2-xyz", type=parse_vec3_arg, help="Optional PS2/PSM2 near point, x,y,z")
    parser.add_argument("--near-marker-json", type=Path, help="Use the first marker from a scr_gltf_markers JSON report")
    parser.add_argument("--max-distance", type=float, help="Only emit matching surfaces whose centroid is within this distance")
    parser.add_argument(
        "--distance-mode",
        choices=("xz", "3d"),
        default="xz",
        help="Distance test mode for near filtering; xz ignores glTF vertical Y",
    )
    parser.add_argument("--lift", type=float, default=0.035, help="Lift overlay along glTF Y to reduce z-fighting")
    parser.add_argument("--min-xz-area", type=float, help="Skip surfaces whose glTF X/Z bbox area is below this value")
    parser.add_argument("--max-y-span", type=float, help="Skip surfaces whose glTF vertical Y bbox span is above this value")
    parser.add_argument("--color", type=parse_color_arg, default=(1.0, 0.0, 0.72, 0.55), help="RGBA overlay color")
    parser.add_argument(
        "--label-nodes",
        action="store_true",
        help="Add one empty glTF node per surface, named by primitive index and flags for Blender/Outliner inspection",
    )
    parser.add_argument("--label-lift", type=float, default=0.25, help="Vertical glTF Y offset for label nodes")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_arg_parser().parse_args(argv)
    output_path = args.output or default_output_path(args.gltf)
    near = resolve_near_point(args.near_gltf_xyz, args.near_ps2_xyz, args.near_marker_json)

    positions, indices, surfaces, stats = collect_trigger_surface_triangles(
        args.psm2,
        include_mask=args.include_mask,
        include_any_mask=args.include_any_mask,
        exclude_mask=args.exclude_mask,
        near=near,
        max_distance=args.max_distance,
        distance_mode=args.distance_mode,
        lift=args.lift,
        min_xz_area=args.min_xz_area,
        max_y_span=args.max_y_span,
    )
    if not positions or not indices:
        raise SystemExit("no trigger-surface triangles matched the requested filters")

    append_surface_overlay(
        args.gltf,
        output_path,
        positions,
        indices,
        surfaces,
        args.color,
        stats,
        label_nodes=args.label_nodes,
        label_lift=args.label_lift,
    )
    if args.json_output:
        write_report(args.json_output, stats, surfaces)

    print(f"Trigger surfaces: {stats['emitted_surface_count']} primitive(s), {stats['emitted_triangle_count']} triangle(s)")
    print(f"  mask matches before distance filter: {stats['matching_mask_count']}")
    print(f"  skipped by geometry: {stats['skipped_geometry']}")
    print(f"  skipped by distance: {stats['skipped_distance']}")
    print(f"  glTF: {output_path}")
    print(f"  bin:  {output_path.with_suffix('.bin')}")
    if args.json_output:
        print(f"  json: {args.json_output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
