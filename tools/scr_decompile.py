#!/usr/bin/env python3
"""Small SCR .out explorer for Orphen script blobs.

This intentionally targets unpacked/exported SCR .out files first. The compressed
or packaged .bin files have a different outer format and should be decoded before
feeding them here.

Grounding from analyzed code:
- FUN_00228e28 relocates an 11-word SCR header in memory.
- FUN_0025b9e8 uses header[5] as the pointer-table start.
- In scr2.out, header[0] is the end of that pointer table, including a zero
  sentinel at the last word.
- FUN_0025bc68 handles structural opcode 0x32 as:
    push(pc + 5); pc = pc + 1; FUN_0025c220();
  and FUN_0025c220 performs pc += *(s32 *)pc.
"""

from __future__ import annotations

import argparse
import pprint
import re
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


HEADER_WORD_COUNT = 11
HEADER_SIZE = HEADER_WORD_COUNT * 4

HEADER_LABELS = [
    "pointer_table_end",
    "block1_start",
    "block2_start",
    "block3_start",
    "block4_start",
    "pointer_table_start",
    "descriptor_block_start",
    "footer_start",
    "array_a_start",
    "array_b_start",
    "array_c_start",
]

LOW_NAMES = {
    0x00: "LOW_NOOP",
    0x01: "LOW_IF_FALSE_ADVANCE_ELSE_SKIP4",
    0x02: "LOW_SWITCH_ADVANCE",
    0x03: "LOW_VM_ADVANCE",
    0x04: "BLOCK_END",
    0x05: "LOW_NOOP_ALIAS",
    0x06: "LOW_NOOP_ALIAS",
    0x07: "LOW_SKIP4",
    0x08: "LOW_VM_ADVANCE_ALIAS",
    0x09: "LOW_SKIP4_ALIAS",
    0x0A: "LOW_VM_ADVANCE_ALIAS",
}

VM_TOKEN_NAMES = {
    0x0B: "VM_RETURN",
    0x0C: "VM_IMM_U8",
    0x0D: "VM_IMM_U16",
    0x0E: "VM_IMM_U32",
    0x0F: "VM_IMM_U32_SCALE100",
    0x10: "VM_IMM_S16_MS",
    0x11: "VM_IMM_S16_ANGLE",
    0x12: "VM_EQ",
    0x13: "VM_NE",
    0x14: "VM_LT",
    0x15: "VM_GT_SWAP",
    0x16: "VM_GE",
    0x17: "VM_LE_SWAP",
    0x18: "VM_NOT",
    0x19: "VM_BIT_NOT",
    0x1A: "VM_LOGICAL_AND",
    0x1B: "VM_OR",
    0x1C: "VM_ADD",
    0x1D: "VM_SUB",
    0x1E: "VM_NEG",
    0x1F: "VM_XOR",
    0x20: "VM_AND",
    0x21: "VM_OR_ALIAS",
    0x22: "VM_DIV",
    0x23: "VM_MUL",
    0x24: "VM_MOD",
    0x30: "VM_PACK_RGB3",
    0x31: "VM_PACK_RGBA4",
}


@dataclass(frozen=True)
class ScrHeader:
    words: tuple[int, ...]

    @property
    def pointer_table_start(self) -> int:
        return self.words[5]

    @property
    def pointer_table_end(self) -> int:
        return self.words[0]

    @property
    def descriptor_block_start(self) -> int:
        return self.words[6]

    @property
    def footer_start(self) -> int:
        return self.words[7]


@dataclass(frozen=True)
class Expr:
    text: str
    precedence: int = 100


@dataclass(frozen=True)
class OpSignature:
    arg_names: tuple[str, ...]
    method: str | None = None


OP_SIGNATURES = {
    0x36: OpSignature(("index",), "read_script_work"),
    0x3D: OpSignature(("flag",), "query_global_event_flag"),
    0x3E: OpSignature(("flag",), "set_global_event_flag"),
    0x3F: OpSignature(("flag",), "clear_global_event_flag"),
    0x40: OpSignature(("flag",), "toggle_global_event_flag"),
    0x45: OpSignature(("param",)),
    0x52: OpSignature(("type_id",)),
    0x58: OpSignature(("index",), "select_pw_slot_by_index"),
    0x59: OpSignature((), "get_pw_slot_index"),
    0x5A: OpSignature(("target",), "select_pw_by_index"),
    0x5C: OpSignature(("index",), "destroy_entity_by_index"),
    0x76: OpSignature(("selector", "register"), "select_object_and_read_register"),
    0x77: OpSignature(("selector", "register", "value"), "set_register"),
    0x78: OpSignature(("selector", "register", "value"), "and_register"),
    0x79: OpSignature(("selector", "register", "value"), "or_register"),
    0x7A: OpSignature(("selector", "register", "value"), "xor_register"),
    0x7B: OpSignature(("selector", "register", "value"), "add_register"),
    0x7C: OpSignature(("selector", "register", "value"), "sub_register"),
    0x86: OpSignature((), "advance_fullscreen_fade"),
    0x8E: OpSignature(("param",), "set_audio_mode_with_param"),
    0x89: OpSignature(("value", "tag"), "dispatch_tagged_event"),
    0x90: OpSignature(("index", "target", "current", "step")),
    0x94: OpSignature(("position", "param"), "set_audio_position_normalized"),
    0x96: OpSignature(("r", "g", "b")),
    0x97: OpSignature(("x", "y", "z", "r", "g", "b")),
    0x98: OpSignature(("angle", "magnitude", "z", "r", "g", "b")),
    0x99: OpSignature(("x", "y", "z")),
    0x9A: OpSignature(("index", "r1", "g1", "b1", "r2", "g2", "b2", "param")),
    0x9E: OpSignature(("slot",), "finish_process_slot"),
    0xB7: OpSignature(("selector", "value_u16", "value_u32"), "set_entity_short_and_word"),
    0xB3: OpSignature(("selector", "anim_slot", "rx", "ry", "rz"), "set_entity_anim_xyz_rate"),
    0xB8: OpSignature(("distance",)),
    0xB9: OpSignature(("r", "g", "b")),
    0xBA: OpSignature(("r", "g", "b")),
    0xBB: OpSignature(("inner_radius", "outer_radius")),
    0xBE: OpSignature(("index", "arg"), "call_function_table_entry"),
    0xC3: OpSignature(("slot", "r", "g", "b"), "set_light_rgb"),
    0xC4: OpSignature(("slot", "intensity"), "set_light_intensity"),
    0xC5: OpSignature(("slot", "x", "y", "z"), "set_light_position_xyz"),
    0xC7: OpSignature(("slot",), "clear_light_intensity"),
    0xD5: OpSignature(("value",), "set_renderer_byte_a"),
    0xE3: OpSignature(("value",), "set_global_byte_355641"),
    0xE6: OpSignature(("slot", "flag", "value"), "set_minimap_marker_slot"),
    0x102: OpSignature(("param0", "x1", "y1", "param3", "param4", "x2", "y2", "entity_index"), "set_quad_params_normalized_with_entity"),
    0x110: OpSignature(("word0", "word1", "coord"), "submit_single_scaled_coord_2words"),
    0x111: OpSignature(("word0", "word1", "coord"), "submit_single_scaled_coord_2words"),
    0x149: OpSignature(("value",)),
}

VAR_ALU_SELECTORS = {
    0x25: "assign",
    0x26: "mul",
    0x27: "div",
    0x28: "mod",
    0x29: "add",
    0x2A: "sub",
    0x2B: "and",
    0x2C: "xor",
    0x2D: "or",
    0x2E: "inc",
    0x2F: "dec",
}

DIALOGUE_CONTROL_NAMES = {
    0x00: "terminator",
    0x01: "soft_terminator_or_continue",
    0x02: "terminate_stream",
    0x07: "advance_glyph_timers",
    0x0C: "delay_or_timing",
    0x13: "speaker_name",
    0x15: "multi_string_block",
    0x16: "voice_or_audio_play",
    0x17: "wait_for_audio_load",
    0x18: "set_control_byte_conditional",
    0x19: "set_control_byte_conditional_alias",
    0x1A: "wait_on_audio_system_flag",
    0x1B: "set_flag_from_id_if_prefixed",
    0x1C: "set_flag_from_id_alias",
}

DIALOGUE_FIXED_LENGTHS = {
    0x00: 1,
    0x01: 1,
    0x02: 1,
    0x07: 1,
    0x0C: 2,
    0x16: 7,
    0x17: 1,
    0x18: 2,
    0x19: 2,
    0x1A: 2,
    0x1B: 3,
    0x1C: 3,
}


def read_data(path: Path) -> bytes:
    data = path.read_bytes()
    if len(data) < HEADER_SIZE:
        raise SystemExit(f"{path} is too small for an SCR .out header")
    return data


def parse_header(data: bytes) -> ScrHeader:
    words = struct.unpack_from("<11I", data, 0)
    return ScrHeader(tuple(words))


def is_plausible_offset(value: int, data_size: int) -> bool:
    return HEADER_SIZE <= value <= data_size


def parse_int(value: str) -> int:
    return int(value, 0)


def hex_bytes(data: bytes, start: int, size: int) -> str:
    end = min(len(data), start + size)
    return " ".join(f"{byte:02x}" for byte in data[start:end])


def bytes_to_hex(data: bytes) -> str:
    return " ".join(f"{byte:02x}" for byte in data)


def ascii_preview(data: bytes) -> str:
    return "".join(chr(byte) if 0x20 <= byte <= 0x7E else "." for byte in data)


def decode_text_bytes(data: bytes) -> str:
    return data.decode("cp1252", errors="replace")


def pformat_ascii(value: object, width: int = 120) -> str:
    formatted = pprint.pformat(value, width=width, sort_dicts=False)
    return formatted.encode("ascii", errors="backslashreplace").decode("ascii")


def emit_literal_assignment(name: str, value: object, width: int = 120) -> None:
    print(f"{name} = {pformat_ascii(value, width=width)}\n")


def read_zero_terminated_ascii(data: bytes, start: int, end: int) -> tuple[str, int]:
    cursor = start
    while cursor < end and data[cursor] != 0:
        cursor += 1
    text = decode_text_bytes(data[start:cursor])
    if cursor < end:
        cursor += 1
    return text, cursor


def parse_dialogue_control(data: bytes, record_start: int, cursor: int, record_end: int) -> tuple[dict[str, object], int]:
    opcode = data[cursor]
    name = DIALOGUE_CONTROL_NAMES.get(opcode, f"control_0x{opcode:02x}")

    if opcode == 0x13:
        speaker, next_cursor = read_zero_terminated_ascii(data, cursor + 1, record_end)
        raw = data[cursor:next_cursor]
        return (
            {
                "offset": cursor,
                "relative_offset": cursor - record_start,
                "opcode": opcode,
                "name": name,
                "speaker": speaker,
                "raw": bytes_to_hex(raw),
            },
            next_cursor,
        )

    if opcode == 0x15 and cursor + 4 <= record_end:
        count = data[cursor + 3]
        next_cursor = cursor + 4
        choices: list[str] = []
        for _ in range(count):
            if next_cursor >= record_end:
                break
            choice, next_cursor = read_zero_terminated_ascii(data, next_cursor, record_end)
            choices.append(choice)
        raw = data[cursor:next_cursor]
        return (
            {
                "offset": cursor,
                "relative_offset": cursor - record_start,
                "opcode": opcode,
                "name": name,
                "header": bytes_to_hex(data[cursor : min(cursor + 4, record_end)]),
                "choice_count": count,
                "choices": choices,
                "raw": bytes_to_hex(raw),
            },
            next_cursor,
        )

    length = DIALOGUE_FIXED_LENGTHS.get(opcode, 1)
    next_cursor = min(record_end, cursor + length)
    raw = data[cursor:next_cursor]
    control: dict[str, object] = {
        "offset": cursor,
        "relative_offset": cursor - record_start,
        "opcode": opcode,
        "name": name,
        "raw": bytes_to_hex(raw),
    }

    if opcode == 0x0C and len(raw) >= 2:
        control["ticks_or_value"] = raw[1]
    elif opcode == 0x16 and len(raw) >= 7:
        control["channel"] = raw[1]
        control["wait_flag"] = raw[2]
        control["voice_or_audio_id"] = struct.unpack_from("<I", raw, 3)[0]
    elif opcode in (0x18, 0x19, 0x1A) and len(raw) >= 2:
        control["value"] = raw[1]
    elif opcode in (0x1B, 0x1C) and len(raw) >= 3:
        control["flag_id"] = struct.unpack_from("<H", raw, 1)[0]

    return control, next_cursor


def parse_dialogue_record(data: bytes, index: int, start: int, end: int) -> dict[str, object]:
    cursor = start
    tokens: list[dict[str, object]] = []
    controls: list[dict[str, object]] = []
    text_parts: list[str] = []
    speaker: str | None = None

    while cursor < end:
        byte = data[cursor]
        if byte > 0x1E:
            text_start = cursor
            while cursor < end and data[cursor] > 0x1E:
                cursor += 1
            text = decode_text_bytes(data[text_start:cursor])
            text_parts.append(text)
            tokens.append(
                {
                    "type": "text",
                    "offset": text_start,
                    "relative_offset": text_start - start,
                    "text": text,
                    "raw": bytes_to_hex(data[text_start:cursor]),
                }
            )
            continue

        control, cursor = parse_dialogue_control(data, start, cursor, end)
        if control["opcode"] == 0x13:
            speaker = str(control.get("speaker", ""))
        controls.append(control)
        tokens.append({"type": "control", **control})
        if control["opcode"] == 0:
            break

    return {
        "index": index,
        "offset": start,
        "end": end,
        "size": end - start,
        "speaker": speaker,
        "text": "".join(text_parts),
        "controls": controls,
        "tokens": tokens,
        "raw": bytes_to_hex(data[start:end]),
    }


def dialogue_records(data: bytes, header: ScrHeader) -> list[dict[str, object]]:
    entries = pointer_entries(data, header)
    zero_index = first_zero_index(entries)
    if zero_index is not None:
        entries = entries[:zero_index]
    records: list[dict[str, object]] = []
    for index, start in enumerate(entries):
        next_offsets = [value for value in entries[index + 1 :] if value > start]
        end = next_offsets[0] if next_offsets else header.pointer_table_start
        if not is_plausible_offset(start, len(data)) or end > len(data) or start >= end:
            continue
        records.append(parse_dialogue_record(data, index, start, end))
    return records


def pointer_entries(data: bytes, header: ScrHeader) -> list[int]:
    start = header.pointer_table_start
    end = header.pointer_table_end
    if start > end or start < HEADER_SIZE or end > len(data) or (end - start) % 4:
        return []
    return [struct.unpack_from("<I", data, off)[0] for off in range(start, end, 4)]


def first_zero_index(values: list[int]) -> int | None:
    for index, value in enumerate(values):
        if value == 0:
            return index
    return None


def build_dialogue_index(records: list[dict[str, object]]) -> dict[int, dict[str, object]]:
    return {int(record["offset"]): record for record in records}


def normalized_visible_text(value: str) -> str:
    return re.sub(r"\s+", " ", value).strip()


def ascii_safe(value: str) -> str:
    return value.encode("ascii", errors="backslashreplace").decode("ascii")


def scan_dialogue_visible_parts(raw: bytes) -> list[str]:
    parts: list[str] = []
    segment_parts: list[str] = []
    segment_has_label = False
    segment_text_len = 0
    segment_index = 0
    cursor = 0
    record_end = len(raw)

    def finish_segment() -> None:
        nonlocal segment_parts, segment_has_label, segment_text_len, segment_index
        if segment_parts and (segment_index == 0 or segment_has_label or segment_text_len >= 3):
            parts.extend(segment_parts)
        segment_parts = []
        segment_has_label = False
        segment_text_len = 0
        segment_index += 1

    while cursor < record_end:
        byte = raw[cursor]
        if byte > 0x1E:
            text_start = cursor
            while cursor < record_end and raw[cursor] > 0x1E:
                cursor += 1
            text = normalized_visible_text(decode_text_bytes(raw[text_start:cursor]))
            if text:
                segment_parts.append(text)
                segment_text_len += len(text)
            continue

        control, next_cursor = parse_dialogue_control(raw, 0, cursor, record_end)
        if control["opcode"] == 0:
            finish_segment()
            cursor = max(next_cursor, cursor + 1)
            continue
        if control["opcode"] == 0x13:
            speaker = normalized_visible_text(str(control.get("speaker") or ""))
            if speaker:
                segment_parts.append(speaker)
                segment_has_label = True
        elif control["opcode"] == 0x15:
            choices = [normalized_visible_text(str(choice)) for choice in control.get("choices", [])]
            choices = [choice for choice in choices if choice]
            if choices:
                segment_parts.append("choices: " + " / ".join(choices))
                segment_has_label = True
        cursor = max(next_cursor, cursor + 1)

    finish_segment()
    return parts


def dialogue_record_visible_parts(record: dict[str, object]) -> list[str]:
    raw_hex = str(record.get("raw") or "")
    if not raw_hex:
        return []
    parts = scan_dialogue_visible_parts(bytes.fromhex(raw_hex))
    while len(parts) > 1 and len(parts[-1]) == 1:
        parts.pop()
    return parts


def dialogue_record_line(record: dict[str, object]) -> str | None:
    parts = dialogue_record_visible_parts(record)
    if not parts:
        return None
    return (
        f"entry[{int(record['index']):03d}] "
        f"0x{int(record['offset']):04x}-0x{int(record['end']):04x}: "
        + " | ".join(parts)
    )


def scr_path_sort_key(path: Path) -> tuple[int, int | str]:
    match = re.fullmatch(r"scr(\d+)\.out", path.name, re.IGNORECASE)
    if match:
        return (0, int(match.group(1)))
    return (1, path.name.lower())


def collect_scr_paths(inputs: Iterable[str]) -> list[Path]:
    paths: list[Path] = []
    for input_path in inputs:
        path = Path(input_path)
        if path.is_dir():
            paths.extend(path.glob("scr*.out"))
        elif any(char in input_path for char in "*?["):
            paths.extend(Path().glob(input_path))
        else:
            paths.append(path)

    seen: set[Path] = set()
    unique_paths: list[Path] = []
    for path in sorted(paths, key=scr_path_sort_key):
        normalized = path.resolve()
        if normalized in seen:
            continue
        seen.add(normalized)
        unique_paths.append(path)
    return unique_paths


def render_dialogue_corpus(paths: list[Path]) -> str:
    lines: list[str] = [
        "# Orphen SCR Dialogue Corpus",
        "",
        "Generated from decoded `scr/*.out` files with `tools/scr_decompile.py dialogue-corpus`.",
        "Each text-bearing dialogue pointer-table record is listed under its source SCR file.",
        "Speaker labels and menu choices are included when the dialogue control codes expose them.",
        "",
    ]

    total_records = 0
    total_visible_records = 0

    for path in paths:
        try:
            data = read_data(path)
        except SystemExit:
            lines.append(f"## {path.as_posix()}")
            lines.append("")
            size = path.stat().st_size if path.exists() else 0
            lines.append(f"Skipped: not a normal SCR .out header ({size} bytes)")
            lines.append("")
            continue
        header = parse_header(data)
        records = dialogue_records(data, header)
        visible_lines = [line for line in (dialogue_record_line(record) for record in records) if line]
        total_records += len(records)
        total_visible_records += len(visible_lines)

        lines.append(f"## {path.as_posix()}")
        lines.append("")
        lines.append(
            f"Header: pointer_table=0x{header.pointer_table_start:04x}-0x{header.pointer_table_end:04x}, "
            f"footer=0x{header.footer_start:04x}, records={len(records)}, text_records={len(visible_lines)}"
        )
        lines.append("")
        if visible_lines:
            lines.extend(f"- {line}" for line in visible_lines)
        else:
            lines.append("- No visible dialogue text found.")
        lines.append("")

    lines.insert(6, f"Files: {len(paths)}; records: {total_records}; text_records: {total_visible_records}.")
    lines.insert(7, "")
    return ascii_safe("\n".join(lines).rstrip() + "\n")


def humanize_record_numbers(value: object) -> object:
    if isinstance(value, list):
        return [humanize_record_numbers(item) for item in value]
    if isinstance(value, tuple):
        return tuple(humanize_record_numbers(item) for item in value)
    if not isinstance(value, dict):
        return value

    hex8_keys = {
        "offset",
        "end",
        "start",
        "context_start",
        "context_end",
        "addr",
        "stream_offset",
        "stream_start",
        "stream_end",
        "target_offset",
        "table_offset",
        "offset_cell",
        "opcode_offset",
        "sentinel_offset",
        "marker_offset",
        "body_offset",
        "boundary_end",
    }
    hex4_keys = {"subproc_id", "flag_id", "flags"}
    hex2_keys = {"opcode", "channel", "wait_flag", "ticks_or_value", "value"}

    result: dict[str, object] = {}
    for key, item in value.items():
        if isinstance(item, int):
            if key in hex8_keys:
                result[key] = f"0x{item:08x}"
            elif key == "relative_offset":
                result[key] = f"0x{item:04x}"
            elif key in hex4_keys:
                result[key] = f"0x{item:04x}"
            elif key == "voice_or_audio_id":
                result[key] = f"0x{item:08x}"
            elif key in hex2_keys:
                result[key] = f"0x{item:02x}"
            else:
                result[key] = item
        else:
            result[key] = humanize_record_numbers(item)
    return result


def load_opcode_names(op_table_path: Path | None) -> dict[int, str]:
    if op_table_path is None or not op_table_path.exists():
        return {}

    names: dict[int, str] = {}
    pattern = re.compile(r"^0x([0-9A-Fa-f]{2,3})\s+([A-Za-z_][A-Za-z0-9_]*)")
    for line in op_table_path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = pattern.match(line.strip())
        if not match:
            continue
        opcode = int(match.group(1), 16)
        names[opcode] = match.group(2)
    return names


def format_opcode(opcode: int, opcode_names: dict[int, str]) -> str:
    if opcode in LOW_NAMES:
        return LOW_NAMES[opcode]
    if opcode in VM_TOKEN_NAMES:
        return VM_TOKEN_NAMES[opcode]
    if opcode in opcode_names:
        return opcode_names[opcode]
    if opcode >= 0x100:
        return f"EXT_{opcode:03x}"
    if opcode >= 0x32:
        return f"HIGH_{opcode:02x}"
    return f"VM_TOKEN_{opcode:02x}"


def python_ident(value: str) -> str:
    ident = re.sub(r"\W+", "_", value.strip().lower()).strip("_")
    if not ident:
        return "subproc"
    if ident[0].isdigit():
        return f"subproc_{ident}"
    return ident


def emit_ctx_call(indent: str, method: str, *args: str, **kwargs: str) -> None:
    parts = list(args)
    parts.extend(f"{key}={value}" for key, value in kwargs.items())
    print(f"{indent}ctx.{method}({', '.join(parts)})")


def format_number(value: int) -> str:
    if -9999 <= value <= 9999:
        return str(value)
    return f"0x{value & 0xffffffff:x}"


def format_signed_byte(value: int) -> str:
    signed = value - 0x100 if value & 0x80 else value
    return format_number(signed)


def wrap_expr(expr: Expr, parent_precedence: int, *, right_side: bool = False) -> str:
    if expr.precedence < parent_precedence or (right_side and expr.precedence == parent_precedence):
        return f"({expr.text})"
    return expr.text


def unary_expr(operator: str, operand: Expr, precedence: int = 70) -> Expr:
    return Expr(f"{operator}{wrap_expr(operand, precedence)}", precedence)


def binary_expr(left: Expr, operator: str, right: Expr, precedence: int) -> Expr:
    left_text = wrap_expr(left, precedence)
    right_text = wrap_expr(right, precedence, right_side=True)
    return Expr(f"{left_text} {operator} {right_text}", precedence)


def pop_expr(stack: list[Expr], fallback: str) -> Expr:
    if stack:
        return stack.pop()
    return Expr(fallback)


def push_unknown_expr(stack: list[Expr], opcode: int, pc: int, opcode_names: dict[int, str]) -> None:
    name = format_opcode(opcode, opcode_names)
    stack.append(Expr(f"ctx.eval_op({name!r}, opcode=0x{opcode:x}, addr=0x{pc:08x})"))


def parse_vm_expr(data: bytes, pc: int, end: int, opcode_names: dict[int, str]) -> tuple[Expr, int]:
    stack: list[Expr] = []

    while 0 <= pc < end:
        op = data[pc]

        if op == 0x0B:
            pc += 1
            if not stack:
                return Expr("None"), pc
            return stack[-1], pc

        if op == 0x0C and pc + 1 < end:
            stack.append(Expr(format_number(data[pc + 1])))
            pc += 2
            continue

        if op == 0x0D and pc + 2 < end:
            value = struct.unpack_from("<H", data, pc + 1)[0]
            stack.append(Expr(format_number(value)))
            pc += 3
            continue

        if op == 0x0E and pc + 4 < end:
            value = struct.unpack_from("<I", data, pc + 1)[0]
            stack.append(Expr(format_number(value)))
            pc += 5
            continue

        if op == 0x0F and pc + 4 < end:
            value = struct.unpack_from("<I", data, pc + 1)[0]
            stack.append(Expr(format_number(value * 100)))
            pc += 5
            continue

        if op == 0x10 and pc + 2 < end:
            value = struct.unpack_from("<h", data, pc + 1)[0]
            stack.append(Expr(format_number(value * 1000)))
            pc += 3
            continue

        if op == 0x11 and pc + 2 < end:
            value = struct.unpack_from("<h", data, pc + 1)[0]
            stack.append(Expr(f"ctx.angle_units({format_number(value)})"))
            pc += 3
            continue

        if op in (0x30, 0x31):
            component_count = 4 if op == 0x31 else 3
            pc += 1
            components: list[Expr] = []
            for _ in range(component_count):
                component, pc = parse_vm_expr(data, pc, end, opcode_names)
                components.append(component)
            method = "pack_rgba" if op == 0x31 else "pack_rgb"
            stack.append(Expr(f"ctx.{method}({', '.join(component.text for component in components)})"))
            continue

        if op == 0x12:
            right = pop_expr(stack, "rhs")
            left = pop_expr(stack, "lhs")
            stack.append(binary_expr(left, "==", right, 20))
            pc += 1
            continue

        if op == 0x13:
            right = pop_expr(stack, "rhs")
            left = pop_expr(stack, "lhs")
            stack.append(binary_expr(left, "!=", right, 20))
            pc += 1
            continue

        if op == 0x14:
            right = pop_expr(stack, "rhs")
            left = pop_expr(stack, "lhs")
            stack.append(binary_expr(left, "<", right, 20))
            pc += 1
            continue

        if op == 0x15:
            right = pop_expr(stack, "rhs")
            left = pop_expr(stack, "lhs")
            stack.append(binary_expr(right, "<", left, 20))
            pc += 1
            continue

        if op == 0x16:
            right = pop_expr(stack, "rhs")
            left = pop_expr(stack, "lhs")
            stack.append(binary_expr(left, ">=", right, 20))
            pc += 1
            continue

        if op == 0x17:
            right = pop_expr(stack, "rhs")
            left = pop_expr(stack, "lhs")
            stack.append(binary_expr(right, ">=", left, 20))
            pc += 1
            continue

        if op == 0x18:
            operand = pop_expr(stack, "value")
            stack.append(Expr(f"not {wrap_expr(operand, 70)}", 70))
            pc += 1
            continue

        if op == 0x19:
            operand = pop_expr(stack, "value")
            stack.append(unary_expr("~", operand))
            pc += 1
            continue

        if op == 0x1A:
            right = pop_expr(stack, "rhs")
            left = pop_expr(stack, "lhs")
            stack.append(binary_expr(left, "and", right, 10))
            pc += 1
            continue

        if op in (0x1B, 0x21):
            right = pop_expr(stack, "rhs")
            left = pop_expr(stack, "lhs")
            stack.append(binary_expr(left, "|", right, 30))
            pc += 1
            continue

        if op == 0x1C:
            right = pop_expr(stack, "rhs")
            left = pop_expr(stack, "lhs")
            stack.append(binary_expr(left, "+", right, 50))
            pc += 1
            continue

        if op == 0x1D:
            right = pop_expr(stack, "rhs")
            left = pop_expr(stack, "lhs")
            stack.append(binary_expr(left, "-", right, 50))
            pc += 1
            continue

        if op == 0x1E:
            operand = pop_expr(stack, "value")
            stack.append(unary_expr("-", operand))
            pc += 1
            continue

        if op == 0x1F:
            right = pop_expr(stack, "rhs")
            left = pop_expr(stack, "lhs")
            stack.append(binary_expr(left, "^", right, 35))
            pc += 1
            continue

        if op == 0x20:
            right = pop_expr(stack, "rhs")
            left = pop_expr(stack, "lhs")
            stack.append(binary_expr(left, "&", right, 40))
            pc += 1
            continue

        if op == 0x22:
            right = pop_expr(stack, "rhs")
            left = pop_expr(stack, "lhs")
            stack.append(binary_expr(left, "//", right, 60))
            pc += 1
            continue

        if op == 0x23:
            right = pop_expr(stack, "rhs")
            left = pop_expr(stack, "lhs")
            stack.append(binary_expr(left, "*", right, 60))
            pc += 1
            continue

        if op == 0x24:
            right = pop_expr(stack, "rhs")
            left = pop_expr(stack, "lhs")
            stack.append(binary_expr(left, "%", right, 60))
            pc += 1
            continue

        if op == 0xFF and pc + 1 < end:
            ext = data[pc + 1]
            full_opcode = 0x100 + ext
            signature = OP_SIGNATURES.get(full_opcode)
            if signature is not None:
                args_for_call, pc_after_call = parse_signature_args(data, pc + 2, end, opcode_names, signature)
                method = python_ident(signature.method or format_opcode(full_opcode, opcode_names))
                stack.append(expression_call(method, args_for_call, pc))
                pc = pc_after_call
            else:
                push_unknown_expr(stack, full_opcode, pc, opcode_names)
                pc += 2
            continue

        if op >= 0x32:
            if op == 0x61:
                mask_expr, cursor = parse_vm_expr(data, pc + 1, end, opcode_names)
                if cursor < end:
                    selector_expr = Expr(format_number(data[cursor]))
                    cursor += 1
                else:
                    selector_expr = Expr("None")
                method = python_ident(format_opcode(op, opcode_names))
                stack.append(
                    expression_call(
                        method,
                        [("mask", mask_expr), ("selector", selector_expr)],
                        pc,
                    )
                )
                pc = cursor
                continue

            signature = OP_SIGNATURES.get(op)
            if signature is not None:
                args_for_call, pc_after_call = parse_signature_args(data, pc + 1, end, opcode_names, signature)
                method = python_ident(signature.method or format_opcode(op, opcode_names))
                stack.append(expression_call(method, args_for_call, pc))
                pc = pc_after_call
            else:
                push_unknown_expr(stack, op, pc, opcode_names)
                pc += 1
            continue

        stack.append(Expr(f"ctx.vm_token_expr({format_opcode(op, opcode_names)!r}, opcode=0x{op:02x}, addr=0x{pc:08x})"))
        pc += 1

    if not stack:
        return Expr("None"), pc
    return stack[-1], pc


def parse_signature_args(
    data: bytes,
    pc: int,
    end: int,
    opcode_names: dict[int, str],
    signature: OpSignature,
) -> tuple[list[tuple[str, Expr]], int]:
    args: list[tuple[str, Expr]] = []
    for arg_name in signature.arg_names:
        expr, pc = parse_vm_expr(data, pc, end, opcode_names)
        args.append((arg_name, expr))
    return args, pc


def emit_decompiled_op_call(indent: str, method: str, args: list[tuple[str, Expr]], addr: int) -> None:
    parts = format_call_parts(args, addr)
    print(f"{indent}ctx.{method}({', '.join(parts)})")


def format_call_parts(args: list[tuple[str, Expr]], addr: int) -> list[str]:
    parts = [f"{arg_name}={expr.text}" for arg_name, expr in args]
    parts.append(f"addr=0x{addr:08x}")
    return parts


def expression_call(method: str, args: list[tuple[str, Expr]], addr: int) -> Expr:
    return Expr(f"ctx.{method}({', '.join(format_call_parts(args, addr))})")


def parse_inline_u32_list(data: bytes, pc: int) -> tuple[int, list[int], int, bool]:
    if pc + 2 > len(data):
        return 0, [], min(pc + 1, len(data)), True
    count = data[pc + 1]
    cursor = pc + 2
    values: list[int] = []
    truncated = False
    for _ in range(count):
        if cursor + 4 > len(data):
            truncated = True
            break
        values.append(struct.unpack_from("<I", data, cursor)[0])
        cursor += 4
    return count, values, cursor, truncated


def align4(value: int) -> int:
    return (value + 3) & ~3


def parse_low_switch_table(
    data: bytes,
    pc: int,
    end: int,
    opcode_names: dict[int, str],
) -> tuple[Expr, list[tuple[int, int]], int | None, int]:
    selector_expr, cursor = parse_vm_expr(data, pc + 1, end, opcode_names)
    if cursor >= end:
        return selector_expr, [], None, cursor
    count = data[cursor]
    table_cursor = align4(cursor + 1)
    cases: list[tuple[int, int]] = []
    if count != 0xFF:
        for _ in range(count):
            if table_cursor + 8 > end:
                return selector_expr, cases, None, table_cursor
            key = struct.unpack_from("<i", data, table_cursor)[0]
            target_cell = table_cursor + 4
            rel32 = struct.unpack_from("<i", data, target_cell)[0]
            cases.append((key, target_cell + rel32))
            table_cursor += 8
    default_target = None
    if table_cursor + 4 <= end:
        rel32 = struct.unpack_from("<i", data, table_cursor)[0]
        default_target = table_cursor + rel32
        table_cursor += 4
    return selector_expr, cases, default_target, table_cursor


def emit_variable_or_flag_alu(
    data: bytes,
    pc: int,
    end: int,
    opcode_names: dict[int, str],
) -> int:
    index_expr, cursor = parse_vm_expr(data, pc + 1, end, opcode_names)
    value_expr, cursor = parse_vm_expr(data, cursor, end, opcode_names)
    if cursor < end:
        selector = data[cursor]
        cursor += 1
    else:
        selector = -1
    selector_name = VAR_ALU_SELECTORS.get(selector, f"unknown_0x{selector & 0xff:02x}")
    method = "script_work_alu" if data[pc] == 0x37 else "scenario_flag_bucket_alu"
    mode = "work" if data[pc] == 0x37 else "flag_bucket"
    emit_decompiled_op_call(
        "    ",
        method,
        [
            ("mode", Expr(repr(mode))),
            ("index", index_expr),
            ("op", Expr(repr(selector_name))),
            ("value", value_expr),
        ],
        pc,
    )
    return cursor


def cmd_summary(args: argparse.Namespace) -> None:
    path = Path(args.file)
    data = read_data(path)
    header = parse_header(data)

    print(f"file: {path}")
    print(f"size: {len(data)} bytes (0x{len(data):x})")
    print("header:")
    for index, (label, value) in enumerate(zip(HEADER_LABELS, header.words)):
        plaus = "" if is_plausible_offset(value, len(data)) else "  ; outside file or before body"
        print(f"  [{index:02d}] {label:24s} 0x{value:08x} ({value}){plaus}")

    entries = pointer_entries(data, header)
    if not entries:
        print("pointer_table: unavailable or implausible")
        return

    zero_index = first_zero_index(entries)
    nonzero = entries[: zero_index if zero_index is not None else len(entries)]
    sorted_nonzero = all(a <= b for a, b in zip(nonzero, nonzero[1:]))
    in_file = all(0 <= value < len(data) for value in nonzero)
    print("pointer_table:")
    print(f"  range: 0x{header.pointer_table_start:x}..0x{header.pointer_table_end:x}")
    print(f"  words: {len(entries)}")
    print(f"  entries_before_zero: {len(nonzero)}")
    print(f"  first_zero_index: {zero_index if zero_index is not None else 'none'}")
    print(f"  sorted_nonzero: {sorted_nonzero}")
    print(f"  nonzero_offsets_within_file: {in_file}")

    for index, value in enumerate(entries[: args.entries]):
        marker = "sentinel" if value == 0 else ""
        preview = hex_bytes(data, value, args.preview) if 0 <= value < len(data) and value != 0 else ""
        print(f"  entry[{index:03d}] = 0x{value:08x} {marker:8s} {preview}")


def cmd_entries(args: argparse.Namespace) -> None:
    path = Path(args.file)
    data = read_data(path)
    header = parse_header(data)
    entries = pointer_entries(data, header)
    if not entries:
        raise SystemExit("pointer table is unavailable or implausible")

    start = args.start
    stop = min(len(entries), start + args.count)
    for index in range(start, stop):
        value = entries[index]
        if value == 0:
            print(f"entry[{index:03d}] 0x00000000 sentinel")
            continue
        preview = hex_bytes(data, value, args.preview) if value < len(data) else "<outside file>"
        print(f"entry[{index:03d}] -> 0x{value:08x}: {preview}")


def iter_subproc_markers(data: bytes, start: int, end: int) -> Iterable[tuple[int, int, str]]:
    cursor = start
    while cursor + 3 < end:
        hit = data.find(b"\x0b\x04", cursor, end)
        if hit < 0 or hit + 3 >= end:
            break
        subproc_id = data[hit + 2] | (data[hit + 3] << 8)
        kind = "plain"
        if hit >= 4 and data[hit - 4 : hit + 1] == b"\x9e\x0c\x01\x1e\x0b":
            kind = "finish_slot_minus1"
        yield hit, subproc_id, kind
        cursor = hit + 1


def subproc_marker_records(data: bytes, start: int, end: int, context_size: int) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []
    for offset, subproc_id, kind in iter_subproc_markers(data, start, end):
        if subproc_id == 0:
            continue
        context_start = max(start, offset - context_size)
        context_end = min(end, offset + 4 + context_size)
        records.append(
            {
                "offset": offset,
                "subproc_id": subproc_id,
                "kind": kind,
                "raw": bytes_to_hex(data[offset : offset + 4]),
                "context_start": context_start,
                "context_end": context_end,
                "context_raw": bytes_to_hex(data[context_start:context_end]),
                "context_ascii": ascii_preview(data[context_start:context_end]),
            }
        )
    return records


def region_metadata(name: str, start: int, end: int, marker_records: list[dict[str, object]]) -> dict[str, object]:
    markers = [record for record in marker_records if start <= int(record["offset"]) < end]
    return {
        "name": name,
        "start": start,
        "end": end,
        "size": max(0, end - start),
        "subproc_marker_count": len(markers),
    }


def region_chunks(
    data: bytes,
    start: int,
    end: int,
    chunk_size: int,
    marker_records: list[dict[str, object]],
) -> list[dict[str, object]]:
    chunks: list[dict[str, object]] = []
    cursor = max(0, start)
    end = min(end, len(data))
    chunk_size = max(1, chunk_size)

    while cursor < end:
        chunk_end = min(end, cursor + chunk_size)
        markers = [
            {
                "offset": int(record["offset"]),
                "subproc_id": int(record["subproc_id"]),
                "kind": str(record["kind"]),
            }
            for record in marker_records
            if cursor <= int(record["offset"]) < chunk_end
        ]
        chunk_data = data[cursor:chunk_end]
        chunks.append(
            {
                "offset": cursor,
                "end": chunk_end,
                "size": chunk_end - cursor,
                "subproc_markers": markers,
                "raw": bytes_to_hex(chunk_data),
                "ascii": ascii_preview(chunk_data),
            }
        )
        cursor = chunk_end
    return chunks


def footer_stream_table(data: bytes, header: ScrHeader) -> tuple[list[dict[str, object]], bytes]:
    records: list[dict[str, object]] = []
    cursor = min(max(header.footer_start, 0), len(data))
    index = 0
    while cursor + 4 <= len(data):
        stream_offset = struct.unpack_from("<I", data, cursor)[0]
        records.append(
            {
                "index": index,
                "table_offset": cursor,
                "stream_offset": stream_offset,
                "valid_stream_offset": header.descriptor_block_start <= stream_offset < header.footer_start,
            }
        )
        cursor += 4
        index += 1
    return records, data[cursor:]


def classify_cutscene_target(
    data: bytes,
    target_offset: int,
    dialogue_by_offset: dict[int, dict[str, object]],
) -> dict[str, object]:
    if target_offset == 0:
        return {"target_kind": "sentinel"}

    if target_offset in dialogue_by_offset:
        dialogue = dialogue_by_offset[target_offset]
        return {
            "target_kind": "dialogue",
            "dialogue_index": dialogue.get("index"),
            "speaker": dialogue.get("speaker"),
            "text_preview": str(dialogue.get("text", ""))[:80],
        }

    if not (0 <= target_offset < len(data)):
        return {"target_kind": "outside_file"}

    if target_offset < HEADER_SIZE:
        return {"target_kind": "small_offset_or_header", "target_preview": hex_bytes(data, target_offset, 12)}

    if target_offset >= 6 and data[target_offset - 6 : target_offset - 4] == b"\x0b\x04":
        subproc_id = data[target_offset - 4] | (data[target_offset - 3] << 8)
        result: dict[str, object] = {
            "target_kind": "subproc_body",
            "subproc_id": subproc_id,
            "marker_offset": target_offset - 6,
            "body_offset": target_offset,
            "target_preview": hex_bytes(data, target_offset, 16),
        }
        if data[target_offset : target_offset + 5] == b"\x9e\x0c\x01\x1e\x0b":
            result["body_class"] = "self_remove_noop"
        return result

    if target_offset >= 4 and data[target_offset - 4 : target_offset - 2] == b"\x0b\x04":
        subproc_id = data[target_offset - 2] | (data[target_offset - 1] << 8)
        return {
            "target_kind": "subproc_body_no_padding",
            "subproc_id": subproc_id,
            "marker_offset": target_offset - 4,
            "body_offset": target_offset,
            "target_preview": hex_bytes(data, target_offset, 16),
        }

    if target_offset + 4 <= len(data) and data[target_offset : target_offset + 2] == b"\x0b\x04":
        subproc_id = data[target_offset + 2] | (data[target_offset + 3] << 8)
        return {
            "target_kind": "subproc_marker",
            "subproc_id": subproc_id,
            "marker_offset": target_offset,
            "body_offset": target_offset + 6,
            "target_preview": hex_bytes(data, target_offset, 16),
        }

    return {"target_kind": "script_or_data", "target_preview": hex_bytes(data, target_offset, 16)}


def parse_cutscene_event_stream(
    data: bytes,
    header: ScrHeader,
    stream_offset: int,
    boundary_end: int,
    dialogue_by_offset: dict[int, dict[str, object]],
    source_refs: list[str],
) -> dict[str, object]:
    records: list[dict[str, object]] = []
    cursor = stream_offset
    boundary_end = min(boundary_end, header.footer_start, len(data))
    sentinel_offset: int | None = None

    while cursor + 8 <= boundary_end:
        delay_frames, flags, target_offset = struct.unpack_from("<HHI", data, cursor)
        record: dict[str, object] = {
            "index": len(records),
            "offset": cursor,
            "delay_frames": delay_frames,
            "flags": flags,
            "target_offset": target_offset,
            "raw": bytes_to_hex(data[cursor : cursor + 8]),
        }
        record.update(classify_cutscene_target(data, target_offset, dialogue_by_offset))
        records.append(record)
        cursor += 8
        if target_offset == 0:
            sentinel_offset = cursor - 8
            break

    entry_count = len([record for record in records if record.get("target_kind") != "sentinel"])
    return {
        "stream_offset": stream_offset,
        "boundary_end": boundary_end,
        "sentinel_offset": sentinel_offset,
        "entry_count": entry_count,
        "source_refs": source_refs,
        "records": records,
    }


def parse_coroutine_stream_refs(
    data: bytes,
    header: ScrHeader,
    first_stream_offset: int,
) -> list[dict[str, object]]:
    refs: list[dict[str, object]] = []
    scan_start = header.pointer_table_end
    scan_end = min(first_stream_offset, header.footer_start, len(data))
    opcode_names: dict[int, str] = {}

    for opcode_offset in range(scan_start, scan_end):
        if data[opcode_offset] != 0xA1:
            continue
        parsed = parse_vm_expr(data, opcode_offset + 1, min(scan_end, opcode_offset + 32), opcode_names)
        if parsed is None:
            continue
        slot_expr, offset_cell = parsed
        if offset_cell + 4 > len(data):
            continue
        stream_offset = struct.unpack_from("<I", data, offset_cell)[0]
        if stream_offset % 8 != 0:
            continue
        if not (first_stream_offset <= stream_offset < header.footer_start):
            continue
        refs.append(
            {
                "opcode_offset": opcode_offset,
                "slot_expr": slot_expr.text,
                "offset_cell": offset_cell,
                "stream_offset": stream_offset,
            }
        )
    return refs


def cutscene_event_streams(
    data: bytes,
    header: ScrHeader,
    dialogue_by_offset: dict[int, dict[str, object]],
) -> tuple[list[dict[str, object]], list[dict[str, object]], bytes, list[dict[str, object]]]:
    table_records, trailing_bytes = footer_stream_table(data, header)
    table_offsets = [
        int(record["stream_offset"])
        for record in table_records
        if bool(record["valid_stream_offset"]) and int(record["stream_offset"]) % 8 == 0
    ]
    first_stream_offset = min(table_offsets) if table_offsets else header.descriptor_block_start
    coroutine_refs = parse_coroutine_stream_refs(data, header, first_stream_offset)

    source_map: dict[int, list[str]] = {}
    for record in table_records:
        if not bool(record["valid_stream_offset"]):
            continue
        stream_offset = int(record["stream_offset"])
        source_map.setdefault(stream_offset, []).append(f"footer_table[{record['index']}]")
    for ref in coroutine_refs:
        stream_offset = int(ref["stream_offset"])
        source_map.setdefault(stream_offset, []).append(f"0xA1@0x{int(ref['opcode_offset']):05x}")

    stream_offsets = sorted(offset for offset in source_map if offset % 8 == 0 and offset < header.footer_start)
    streams: list[dict[str, object]] = []
    for index, stream_offset in enumerate(stream_offsets):
        boundary_end = stream_offsets[index + 1] if index + 1 < len(stream_offsets) else header.footer_start
        streams.append(
            parse_cutscene_event_stream(
                data,
                header,
                stream_offset,
                boundary_end,
                dialogue_by_offset,
                source_map.get(stream_offset, []),
            )
        )

    return streams, table_records, trailing_bytes, coroutine_refs


def cmd_scan_subprocs(args: argparse.Namespace) -> None:
    path = Path(args.file)
    data = read_data(path)
    header = parse_header(data)
    start = header.pointer_table_end if args.start is None else args.start
    end = len(data) if args.end is None else min(args.end, len(data))
    count = 0
    for offset, subproc_id, kind in iter_subproc_markers(data, start, end):
        if subproc_id == 0 and not args.include_zero:
            continue
        context_start = max(0, offset - args.context)
        context_size = args.context + 4 + args.context
        context = hex_bytes(data, context_start, context_size)
        print(
            f"0x{offset:08x}: subproc_id={subproc_id:5d} "
            f"(0x{subproc_id:04x}) kind={kind} context[{context_start:#x}]={context}"
        )
        count += 1
        if args.limit is not None and count >= args.limit:
            break
    print(f"markers: {count}")


def decode_vm_token(data: bytes, pc: int) -> tuple[int, str] | None:
    op = data[pc]
    remaining = len(data) - pc
    if op == 0x0C and remaining >= 2:
        return 2, f"value={data[pc + 1]}"
    if op == 0x0D and remaining >= 3:
        value = struct.unpack_from("<H", data, pc + 1)[0]
        return 3, f"value={value}"
    if op in (0x0E, 0x0F) and remaining >= 5:
        value = struct.unpack_from("<I", data, pc + 1)[0]
        if op == 0x0F:
            return 5, f"raw={value} scaled={value * 100}"
        return 5, f"value={value}"
    if op == 0x10 and remaining >= 3:
        value = struct.unpack_from("<h", data, pc + 1)[0]
        return 3, f"raw={value} ms={value * 1000}"
    if op == 0x11 and remaining >= 3:
        value = struct.unpack_from("<h", data, pc + 1)[0]
        scaled = (value * 0xF570) // 0x168
        return 3, f"raw={value} angle_scaled={scaled}"
    return None


def cmd_disasm(args: argparse.Namespace) -> None:
    path = Path(args.file)
    data = read_data(path)
    opcode_names = load_opcode_names(Path(args.op_table) if args.op_table else None)
    pc = args.start
    end = len(data) if args.end is None else min(args.end, len(data))
    stack: list[int] = []
    seen: set[tuple[int, tuple[int, ...]]] = set()
    lines = 0

    while 0 <= pc < end and lines < args.max_lines:
        state_key = (pc, tuple(stack[-8:]))
        if state_key in seen:
            print(f"0x{pc:08x}: ; loop detected, stopping")
            break
        seen.add(state_key)

        op = data[pc]
        lines += 1

        if op < 0x0B:
            name = format_opcode(op, opcode_names)
            if op == 0x04:
                print(f"0x{pc:08x}: 04              {name}")
                pc += 1
                if not stack:
                    print(f"0x{pc:08x}: ; top-level block end")
                    break
                pc = stack.pop()
                continue
            if op in (0x07, 0x09):
                print(f"0x{pc:08x}: {op:02x}              {name} -> pc += 5")
                pc += 5
                continue
            if op == 0x02:
                selector_expr, cases, default_target, table_end = parse_low_switch_table(data, pc, end, opcode_names)
                cases_text = ", ".join(f"{key}:0x{target:08x}" for key, target in cases)
                default_text = "None" if default_target is None else f"0x{default_target:08x}"
                print(
                    f"0x{pc:08x}: 02              {name} selector={selector_expr.text} "
                    f"cases={{{cases_text}}} default={default_text} table_end=0x{table_end:08x}"
                )
                break
            if op in (0x03, 0x08, 0x0A):
                if pc + 5 > end:
                    print(f"0x{pc:08x}: {op:02x}              {name} <truncated rel32>")
                    break
                rel32 = struct.unpack_from("<i", data, pc + 1)[0]
                target = pc + 1 + rel32
                print(f"0x{pc:08x}: {op:02x} {hex_bytes(data, pc + 1, 4):11s} {name} target=0x{target:08x}")
                pc = target
                continue
            if op == 0x01:
                print(f"0x{pc:08x}: 01              {name} ; branch depends on VM eval")
                pc += 1
                continue
            print(f"0x{pc:08x}: {op:02x}              {name}")
            pc += 1
            continue

        if op == 0x32:
            if pc + 5 > len(data):
                print(f"0x{pc:08x}: 32              BLOCK_OPEN <truncated rel32>")
                break
            delta = struct.unpack_from("<i", data, pc + 1)[0]
            target = pc + 1 + delta
            cont = pc + 5
            payload = hex_bytes(data, pc + 1, 4)
            print(
                f"0x{pc:08x}: 32 {payload:11s} BLOCK_OPEN "
                f"push=0x{cont:08x} delta={delta:+d} target=0x{target:08x}"
            )
            stack.append(cont)
            if target < 0 or target >= len(data):
                print(f"0x{pc:08x}: ; target outside file, stopping")
                break
            pc = target
            continue

        if op == 0xFF:
            if pc + 1 >= len(data):
                print(f"0x{pc:08x}: ff              EXT <truncated>")
                break
            ext = data[pc + 1]
            full_opcode = 0x100 + ext
            name = format_opcode(full_opcode, opcode_names)
            print(f"0x{pc:08x}: ff {ext:02x}           {name}")
            pc += 2
            continue

        if op == 0x4D:
            count, values, cursor, truncated = parse_inline_u32_list(data, pc)
            suffix = " <truncated>" if truncated else ""
            ids = ", ".join(f"0x{value:08x}" for value in values)
            print(f"0x{pc:08x}: 4d              {format_opcode(op, opcode_names)} count={count} ids=[{ids}]{suffix}")
            pc = cursor
            continue

        if op == 0x6D and pc + 1 < end:
            mode_byte = data[pc + 1]
            print(
                f"0x{pc:08x}: 6d {mode_byte:02x}           control_character_ai_mode "
                f"mode={format_signed_byte(mode_byte)}"
            )
            pc += 2
            continue

        if op < 0x32:
            decoded = decode_vm_token(data, pc)
            name = format_opcode(op, opcode_names)
            if decoded is None:
                print(f"0x{pc:08x}: {op:02x}              {name} ; VM token in expression stream")
                pc += 1
            else:
                size, detail = decoded
                print(f"0x{pc:08x}: {hex_bytes(data, pc, size):14s} {name} {detail}")
                pc += size
            continue

        name = format_opcode(op, opcode_names)
        print(f"0x{pc:08x}: {op:02x}              {name}")
        pc += 1

    if lines >= args.max_lines:
        print(f"0x{pc:08x}: ; max line count reached")


def emit_python_function(
    path: Path,
    data: bytes,
    opcode_names: dict[int, str],
    start: int,
    end: int | None,
    max_ops: int,
    name: str | None,
) -> None:
    pc = start
    end = len(data) if end is None else min(end, len(data))
    stack: list[int] = []
    seen: set[tuple[int, tuple[int, ...]]] = set()
    ops = 0
    function_name = python_ident(name or f"subproc_{start:04x}")

    print(f"def {function_name}(ctx):")
    print(f"    # source={path.as_posix()} start=0x{start:08x}")
    print("    # Generated pseudocode. Known opcodes consume decompiled VM expressions;")
    print("    # unknown opcodes fall back to ctx.op/ctx.vm_token calls.")

    while 0 <= pc < end and ops < max_ops:
        state_key = (pc, tuple(stack[-8:]))
        if state_key in seen:
            print(f"    # loop detected at 0x{pc:08x}; stopping")
            break
        seen.add(state_key)
        ops += 1

        op = data[pc]

        if op < 0x0B:
            name = format_opcode(op, opcode_names)
            if op == 0x04:
                emit_ctx_call("    ", "block_end", pc=f"0x{pc:08x}")
                pc += 1
                if not stack:
                    print("    return")
                    break
                pc = stack.pop()
                continue
            if op in (0x07, 0x09):
                emit_ctx_call("    ", "low_op", repr(name), opcode=f"0x{op:02x}", pc=f"0x{pc:08x}", skip_bytes="4")
                pc += 5
                continue
            if op == 0x02:
                selector_expr, cases, default_target, table_end = parse_low_switch_table(data, pc, end, opcode_names)
                cases_text = "{" + ", ".join(f"{key}: 0x{target:08x}" for key, target in cases) + "}"
                default_text = "None" if default_target is None else f"0x{default_target:08x}"
                emit_ctx_call(
                    "    ",
                    "low_switch",
                    selector=f"{selector_expr.text}",
                    cases=cases_text,
                    default=f"{default_text}",
                    table_end=f"0x{table_end:08x}",
                    pc=f"0x{pc:08x}",
                    note=repr("target depends on VM eval"),
                )
                break
            if op in (0x03, 0x08, 0x0A):
                if pc + 5 > end:
                    print(f"    # truncated {name} rel32 at 0x{pc:08x}")
                    break
                rel32 = struct.unpack_from("<i", data, pc + 1)[0]
                target = pc + 1 + rel32
                emit_ctx_call(
                    "    ",
                    "low_jump",
                    repr(name),
                    rel32=str(rel32),
                    target=f"0x{target:08x}",
                    addr=f"0x{pc:08x}",
                )
                pc = target
                continue
            if op == 0x01:
                condition_expr, cursor = parse_vm_expr(data, pc + 1, end, opcode_names)
                if cursor + 4 <= end:
                    rel32 = struct.unpack_from("<i", data, cursor)[0]
                    target = cursor + rel32
                    fallthrough = cursor + 4
                    emit_ctx_call(
                        "    ",
                        "branch_if_false",
                        condition=f"{condition_expr.text}",
                        rel32=str(rel32),
                        target=f"0x{target:08x}",
                        fallthrough=f"0x{fallthrough:08x}",
                        addr=f"0x{pc:08x}",
                    )
                    pc = fallthrough
                else:
                    emit_ctx_call(
                        "    ",
                        "branch_if_false",
                        condition=f"{condition_expr.text}",
                        addr=f"0x{pc:08x}",
                        note=repr("missing rel32 branch cell"),
                    )
                    pc = cursor
                continue
            emit_ctx_call("    ", "low_op", repr(name), opcode=f"0x{op:02x}", pc=f"0x{pc:08x}")
            pc += 1
            continue

        if op == 0x32:
            if pc + 5 > len(data):
                print(f"    # truncated BLOCK_OPEN rel32 at 0x{pc:08x}")
                break
            delta = struct.unpack_from("<i", data, pc + 1)[0]
            target = pc + 1 + delta
            cont = pc + 5
            emit_ctx_call(
                "    ",
                "block_open",
                pc=f"0x{pc:08x}",
                rel32=str(delta),
                target=f"0x{target:08x}",
                continuation=f"0x{cont:08x}",
            )
            stack.append(cont)
            if target < 0 or target >= len(data):
                print(f"    # block target outside file; stopping")
                break
            pc = target
            continue

        if op == 0xFF:
            if pc + 1 >= len(data):
                print(f"    # truncated EXT opcode at 0x{pc:08x}")
                break
            ext = data[pc + 1]
            full_opcode = 0x100 + ext
            name = format_opcode(full_opcode, opcode_names)
            if full_opcode in (0x125, 0x126) and pc + 4 <= end:
                tag = struct.unpack_from("<h", data, pc + 2)[0]
                cursor = pc + 4
                entity_index, cursor = parse_vm_expr(data, cursor, end, opcode_names)
                args_for_call = [("tag", Expr(format_number(tag))), ("entity_index", entity_index)]
                if full_opcode == 0x126:
                    flags, cursor = parse_vm_expr(data, cursor, end, opcode_names)
                    args_for_call.append(("flags", flags))
                emit_decompiled_op_call("    ", "audio_play_for_entity", args_for_call, pc)
                pc = cursor
                continue
            if full_opcode in (0x127, 0x128) and pc + 4 <= end:
                tag = struct.unpack_from("<h", data, pc + 2)[0]
                cursor = pc + 4
                args_for_call = [("tag", Expr(format_number(tag)))]
                for arg_name in ("x", "y", "z"):
                    expr, cursor = parse_vm_expr(data, cursor, end, opcode_names)
                    args_for_call.append((arg_name, expr))
                if full_opcode == 0x128:
                    volume, cursor = parse_vm_expr(data, cursor, end, opcode_names)
                    args_for_call.append(("volume", volume))
                else:
                    args_for_call.append(("volume", Expr("100")))
                emit_decompiled_op_call("    ", "audio_play_positional", args_for_call, pc)
                pc = cursor
                continue
            signature = OP_SIGNATURES.get(full_opcode)
            if signature is not None:
                args_for_call, next_pc = parse_signature_args(data, pc + 2, end, opcode_names, signature)
                emit_decompiled_op_call("    ", python_ident(signature.method or name), args_for_call, pc)
                pc = next_pc
                continue
            emit_ctx_call("    ", "op", repr(name), opcode=f"0x{full_opcode:03x}", pc=f"0x{pc:08x}")
            pc += 2
            continue

        if op < 0x32:
            decoded = decode_vm_token(data, pc)
            name = format_opcode(op, opcode_names)
            if decoded is None:
                emit_ctx_call("    ", "vm_token", repr(name), opcode=f"0x{op:02x}", pc=f"0x{pc:08x}")
                pc += 1
            else:
                size, detail = decoded
                emit_ctx_call(
                    "    ",
                    "vm_token",
                    repr(name),
                    opcode=f"0x{op:02x}",
                    pc=f"0x{pc:08x}",
                    raw=repr(hex_bytes(data, pc, size)),
                    detail=repr(detail),
                )
                pc += size
            continue

        name = format_opcode(op, opcode_names)
        if op == 0x4A and pc + 2 <= end:
            mode = data[pc + 1]
            cursor = pc + 2
            args_for_call = [("mode", Expr(format_number(mode)))]
            for arg_name in ("plane_x", "plane_y", "plane_z", "near", "far", "extent"):
                expr, cursor = parse_vm_expr(data, cursor, end, opcode_names)
                args_for_call.append((arg_name, expr))
            emit_decompiled_op_call("    ", "init_projection_parameters", args_for_call, pc)
            pc = cursor
            continue
        if op == 0x4D:
            count, values, cursor, truncated = parse_inline_u32_list(data, pc)
            args_for_call = [
                ("count", Expr(str(count))),
                ("ids", Expr(repr(values))),
            ]
            if truncated:
                args_for_call.append(("truncated", Expr("True")))
            emit_decompiled_op_call("    ", python_ident(name), args_for_call, pc)
            pc = cursor
            continue
        if op == 0x6D and pc + 1 < end:
            emit_decompiled_op_call(
                "    ",
                "control_character_ai_mode",
                [("mode", Expr(format_signed_byte(data[pc + 1])))],
                pc,
            )
            pc += 2
            continue
        if op in (0xA4, 0xA6):
            param_expr, cursor = parse_vm_expr(data, pc + 1, end, opcode_names)
            if cursor < end:
                inline_byte = Expr(format_number(data[cursor]))
                cursor += 1
            else:
                inline_byte = Expr("None")
            emit_decompiled_op_call(
                "    ",
                "audio_submit",
                [("param", param_expr), ("inline_byte", inline_byte)],
                pc,
            )
            pc = cursor
            continue
        if op == 0xA1:
            slot_expr, cursor = parse_vm_expr(data, pc + 1, end, opcode_names)
            if cursor + 4 <= end:
                stream_offset = struct.unpack_from("<I", data, cursor)[0]
                emit_ctx_call(
                    "    ",
                    "set_coroutine_code_ptr",
                    slot=f"{slot_expr.text}",
                    stream_offset=f"0x{stream_offset:08x}",
                    addr=f"0x{pc:08x}",
                )
                pc = cursor + 4
            else:
                emit_ctx_call(
                    "    ",
                    "set_coroutine_code_ptr",
                    slot=f"{slot_expr.text}",
                    addr=f"0x{pc:08x}",
                    note=repr("missing stream offset cell"),
                )
                pc = cursor
            continue
        if op == 0x61:
            mask_expr, cursor = parse_vm_expr(data, pc + 1, end, opcode_names)
            if cursor < end:
                selector_expr = Expr(format_number(data[cursor]))
                cursor += 1
            else:
                selector_expr = Expr("None")
            emit_decompiled_op_call(
                "    ",
                python_ident(name),
                [("mask", mask_expr), ("selector", selector_expr)],
                pc,
            )
            pc = cursor
            continue
        if op in (0x37, 0x39):
            pc = emit_variable_or_flag_alu(data, pc, end, opcode_names)
            continue
        signature = OP_SIGNATURES.get(op)
        if signature is not None:
            args_for_call, next_pc = parse_signature_args(data, pc + 1, end, opcode_names, signature)
            emit_decompiled_op_call("    ", python_ident(signature.method or name), args_for_call, pc)
            pc = next_pc
            continue
        emit_ctx_call("    ", "op", repr(name), opcode=f"0x{op:02x}", pc=f"0x{pc:08x}")
        pc += 1

    else:
        if ops >= max_ops:
            print(f"    # max op count reached at 0x{pc:08x}")

    if ops == 0:
        print("    pass")


def cmd_emit_python(args: argparse.Namespace) -> None:
    path = Path(args.file)
    data = read_data(path)
    opcode_names = load_opcode_names(Path(args.op_table) if args.op_table else None)
    emit_python_function(path, data, opcode_names, args.start, args.end, args.max_ops, args.name)


def cmd_dialogue_corpus(args: argparse.Namespace) -> None:
    paths = collect_scr_paths(args.paths)
    if not paths:
        raise SystemExit("no SCR .out files matched")

    corpus = render_dialogue_corpus(paths)
    if args.output:
        output_path = Path(args.output)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(corpus, encoding="ascii")
    else:
        print(corpus, end="")


HEADER_ENTRYPOINTS = (
    (0, "scene_init", "FUN_0025b6d0 calls FUN_0025bc68(base + header[0])"),
    (1, "scene_start", "FUN_0025b728 calls FUN_0025bc68(base + header[1])"),
    (2, "scene_tick", "FUN_0025b778 calls FUN_0025bc68(base + header[2]) before scheduler/subproc slots"),
    (3, "actor_state_primary", "FUN_0025b978 calls FUN_0025bc68(base + header[3]) from actor context"),
    (4, "actor_state_secondary", "FUN_0025b9a8 calls FUN_0025bc68(base + header[4]) from actor context"),
)


def discover_header_entrypoints(data: bytes, header: ScrHeader) -> list[tuple[str, int, str]]:
    entries: list[tuple[str, int, str]] = []
    seen: set[int] = set()
    for index, label, note in HEADER_ENTRYPOINTS:
        offset = header.words[index]
        if offset in seen or offset == 0 or offset >= len(data):
            continue
        seen.add(offset)
        entries.append((f"{label}_{offset:04x}", offset, note))
    return entries


def emit_pointer_table(values: list[int], zero_index: int | None) -> None:
    print("DIALOGUE_POINTER_TABLE = [")
    for index, value in enumerate(values):
        suffix = ""
        if zero_index is not None and index == zero_index:
            suffix = "  # sentinel"
        print(f"    0x{value:08x},{suffix}")
    print("]\n")


def emit_subproc_marker_stubs(marker_records: list[dict[str, object]]) -> None:
    for record in marker_records:
        offset = int(record["offset"])
        subproc_id = int(record["subproc_id"])
        kind = str(record["kind"])
        raw = str(record["raw"])
        context_raw = str(record["context_raw"])
        function_name = f"subproc_marker_{subproc_id:04x}_at_{offset:08x}"
        print(f"def {function_name}(ctx):")
        print(f"    # subproc_id=0x{subproc_id:04x} kind={kind!r}")
        emit_ctx_call(
            "    ",
            "subproc_marker",
            subproc_id=f"0x{subproc_id:04x}",
            addr=f"0x{offset:08x}",
            kind=repr(kind),
            raw=repr(raw),
            context=repr(context_raw),
        )
        print()


def cmd_emit_file(args: argparse.Namespace) -> None:
    path = Path(args.file)
    data = read_data(path)
    header = parse_header(data)
    opcode_names = load_opcode_names(Path(args.op_table) if args.op_table else None)
    entries = discover_header_entrypoints(data, header)
    pointer_values = pointer_entries(data, header)
    zero_index = first_zero_index(pointer_values) if pointer_values else None
    records = dialogue_records(data, header) if pointer_values else []
    dialogue_by_offset = build_dialogue_index(records)
    marker_records = subproc_marker_records(data, header.pointer_table_end, len(data), args.subproc_context)
    descriptor_start = min(max(header.descriptor_block_start, 0), len(data))
    footer_start = min(max(header.footer_start, descriptor_start), len(data))
    cutscene_streams, cutscene_stream_table, cutscene_table_trailing, coroutine_refs = cutscene_event_streams(
        data,
        header,
        dialogue_by_offset,
    )
    cutscene_chunks = region_chunks(
        data,
        descriptor_start,
        footer_start,
        args.region_chunk_size,
        marker_records,
    )
    footer_chunks = region_chunks(data, footer_start, len(data), args.region_chunk_size, marker_records)

    print(f"# Generated from {path.as_posix()}")
    print("# Whole-file mode emits the currently understood SCR2 structure:")
    print("# - dialogue records from the pointer table, preserving text and control-code tokens")
    print("# - known VM entrypoints loaded from SCR header words 0..4")
    print("# - non-zero subproc marker candidates as metadata and one-line stubs")
    print("# - cutscene scheduler streams as 8-byte [delay, flags, target_offset] records")
    print("# - opaque descriptor/cutscene and footer regions as raw chunks")
    print("\nSCR_HEADER = {")
    for index, (label, value) in enumerate(zip(HEADER_LABELS, header.words)):
        print(f"    {index}: (0x{value:08x}, {label!r}),")
    print("}\n")

    print("SCR_ENTRYPOINTS = {")
    for name, offset, note in entries:
        print(f"    {name!r}: 0x{offset:08x},  # {note}")
    print("}\n")

    if pointer_values:
        nonzero_count = zero_index if zero_index is not None else len(pointer_values)
        print(
            f"# dialogue_pointer_table: 0x{header.pointer_table_start:08x}..0x{header.pointer_table_end:08x}, "
            f"{nonzero_count} non-zero entries"
        )
        emit_pointer_table(pointer_values, zero_index)
        emit_literal_assignment("DIALOGUE_RECORDS", humanize_record_numbers(records), width=140)

    emit_literal_assignment("SUBPROC_MARKERS", humanize_record_numbers(marker_records), width=140)
    emit_literal_assignment("CUTSCENE_STREAM_TABLE", humanize_record_numbers(cutscene_stream_table), width=140)
    emit_literal_assignment("CUTSCENE_STREAM_TABLE_TRAILING_BYTES", bytes_to_hex(cutscene_table_trailing))
    emit_literal_assignment("COROUTINE_STREAM_REFS", humanize_record_numbers(coroutine_refs), width=140)
    emit_literal_assignment("CUTSCENE_EVENT_STREAMS", humanize_record_numbers(cutscene_streams), width=160)
    emit_literal_assignment(
        "CUTSCENE_REGION",
        humanize_record_numbers(region_metadata("descriptor_or_cutscene", descriptor_start, footer_start, marker_records)),
    )
    emit_literal_assignment("CUTSCENE_CHUNKS", humanize_record_numbers(cutscene_chunks), width=160)
    emit_literal_assignment(
        "FOOTER_REGION",
        humanize_record_numbers(region_metadata("footer", footer_start, len(data), marker_records)),
    )
    emit_literal_assignment("FOOTER_CHUNKS", humanize_record_numbers(footer_chunks), width=160)

    if marker_records:
        print("# Subproc marker stubs. These are structural anchors, not yet proven callable script bodies.\n")
        emit_subproc_marker_stubs(marker_records)

    for name, offset, note in entries:
        print(f"# {note}")
        emit_python_function(path, data, opcode_names, offset, None, args.max_ops_per_function, name)
        print()


def cmd_scan_rel32(args: argparse.Namespace) -> None:
    path = Path(args.file)
    data = read_data(path)
    start = args.start
    end = len(data) if args.end is None else min(args.end, len(data))
    count = 0
    for pc in range(start, max(start, end - 4)):
        if data[pc] != 0x32:
            continue
        delta = struct.unpack_from("<i", data, pc + 1)[0]
        target = pc + 1 + delta
        if args.min_delta <= delta <= args.max_delta and 0 <= target < len(data):
            print(
                f"0x{pc:08x}: delta={delta:+7d} target=0x{target:08x} "
                f"bytes={hex_bytes(data, pc, 8)}"
            )
            count += 1
            if args.limit is not None and count >= args.limit:
                break
    print(f"hits: {count}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Explore unpacked Orphen SCR .out files")
    subparsers = parser.add_subparsers(dest="command", required=True)

    summary = subparsers.add_parser("summary", help="print header and pointer-table summary")
    summary.add_argument("file")
    summary.add_argument("--entries", type=int, default=16, help="pointer entries to preview")
    summary.add_argument("--preview", type=int, default=12, help="bytes to preview at each pointer entry")
    summary.set_defaults(func=cmd_summary)

    entries = subparsers.add_parser("entries", help="list pointer-table entries")
    entries.add_argument("file")
    entries.add_argument("--start", type=int, default=0)
    entries.add_argument("--count", type=int, default=32)
    entries.add_argument("--preview", type=int, default=16)
    entries.set_defaults(func=cmd_entries)

    scan_subprocs = subparsers.add_parser("scan-subprocs", help="find 0b 04 <id16> markers")
    scan_subprocs.add_argument("file")
    scan_subprocs.add_argument("--start", type=parse_int, default=None, help="default: header[0]")
    scan_subprocs.add_argument("--end", type=parse_int, default=None)
    scan_subprocs.add_argument("--context", type=int, default=8)
    scan_subprocs.add_argument("--limit", type=int, default=40)
    scan_subprocs.add_argument("--include-zero", action="store_true", help="include id 0 markers")
    scan_subprocs.set_defaults(func=cmd_scan_subprocs)

    disasm = subparsers.add_parser("disasm", help="skeletal structural/VM-token listing")
    disasm.add_argument("file")
    disasm.add_argument("--start", type=parse_int, required=True)
    disasm.add_argument("--end", type=parse_int, default=None)
    disasm.add_argument("--max-lines", type=int, default=120)
    disasm.add_argument(
        "--op-table",
        default="analyzed/opcode_dispatch_tables.md",
        help="markdown opcode table used for names",
    )
    disasm.set_defaults(func=cmd_disasm)

    emit_python = subparsers.add_parser("emit-python", help="emit expression-aware Python pseudocode for a SCR slice")
    emit_python.add_argument("file")
    emit_python.add_argument("--start", type=parse_int, required=True)
    emit_python.add_argument("--end", type=parse_int, default=None)
    emit_python.add_argument("--max-ops", type=int, default=160)
    emit_python.add_argument("--name", default=None, help="function name to emit")
    emit_python.add_argument(
        "--op-table",
        default="analyzed/opcode_dispatch_tables.md",
        help="markdown opcode table used for names",
    )
    emit_python.set_defaults(func=cmd_emit_python)

    dialogue_corpus = subparsers.add_parser(
        "dialogue-corpus",
        help="emit a Markdown corpus of visible dialogue text from SCR .out files",
    )
    dialogue_corpus.add_argument(
        "paths",
        nargs="+",
        help="SCR .out files or directories containing scr*.out files",
    )
    dialogue_corpus.add_argument("--output", help="write corpus to this file instead of stdout")
    dialogue_corpus.set_defaults(func=cmd_dialogue_corpus)

    emit_file = subparsers.add_parser("emit-file", help="emit all currently known SCR VM entrypoint functions")
    emit_file.add_argument("file")
    emit_file.add_argument("--max-ops-per-function", type=int, default=500)
    emit_file.add_argument("--pointer-preview", type=int, default=12, help=argparse.SUPPRESS)
    emit_file.add_argument("--marker-preview", type=int, default=20, help=argparse.SUPPRESS)
    emit_file.add_argument("--subproc-context", type=int, default=8, help="bytes around each subproc marker to preserve")
    emit_file.add_argument("--region-chunk-size", type=int, default=128, help="raw chunk size for opaque regions")
    emit_file.add_argument(
        "--op-table",
        default="analyzed/opcode_dispatch_tables.md",
        help="markdown opcode table used for names",
    )
    emit_file.set_defaults(func=cmd_emit_file)

    rel32 = subparsers.add_parser("scan-rel32", help="find plausible 0x32 self-relative jumps")
    rel32.add_argument("file")
    rel32.add_argument("--start", type=parse_int, default=HEADER_SIZE)
    rel32.add_argument("--end", type=parse_int, default=None)
    rel32.add_argument("--min-delta", type=int, default=1)
    rel32.add_argument("--max-delta", type=int, default=0x4000)
    rel32.add_argument("--limit", type=int, default=80)
    rel32.set_defaults(func=cmd_scan_rel32)

    return parser


def main() -> None:
    parser = build_parser()
    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
