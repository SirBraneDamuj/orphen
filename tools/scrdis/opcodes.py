"""Operand shapes and working names for all 271 SCR opcodes.

Shapes are the part that has to be right: one wrong width desyncs every
instruction after it. Each one comes from, in order of trust:

  port   the native port's interpreter (port/src/ported/script/), which runs
         real scenes to a clean block end and halts on anything it cannot read
  elf    tools/scrdis/elfscan.py -- the handler's own stream reads, read off
         the retail ELF, for straight-line handlers the port does not implement
  ghidra the handler decompiled by hand, for unported handlers that branch

Names are hypotheses (see CLAUDE.md, "Ground Truth Sources"). Where the port
has corrected a name from analyzed/opcode_dispatch_tables.md, the port's
reading is used. Unknowns keep a neutral name built from the global they
touch, never a guess at intent.

Operand kinds:
  e   one VM expression (FUN_0025c258)
  b   one inline byte
  h   one inline little-endian u16
  w   one inline little-endian u32 (FUN_0025c1d0)

A `@ref` suffix says what the operand points at, so the explorer can follow it:
  @code    a script body (blob offset)
  @stream  an event-scheduler stream (blob offset)
  @data    some other blob offset
  @pts3    a point list: u32 count, then count * 3 expressions
  @pts2    a point list: u32 count, then count * 2 expressions
On an `e` operand a ref only resolves when the expression is a constant.
"""
from __future__ import annotations

from dataclasses import dataclass, field


@dataclass(frozen=True)
class Operand:
    kind: str          # e b h w
    name: str
    ref: str | None = None


@dataclass(frozen=True)
class Op:
    code: int
    name: str
    operands: tuple[Operand, ...]
    handler: int       # handler address in the ELF dispatch table
    source: str        # port | elf | ghidra | structural
    note: str = ""


def _parse(spec: str) -> tuple[Operand, ...]:
    out = []
    for token in spec.split():
        ref = None
        if "@" in token:
            token, ref = token.split("@", 1)
        kind, _, name = token.partition(":")
        out.append(Operand(kind, name or kind, ref))
    return tuple(out)


# opcode: (handler, source, name, operand spec[, note])
_TABLE: dict[int, tuple] = {
    # -- structural (FUN_0025bc68 / PTR_LAB_0031e1f8), decoded specially --
    0x00: (0x25BDC8, "port", "nop", ""),
    0x01: (0x25BDD0, "port", "if_false_goto", ""),
    0x02: (0x25BE10, "port", "switch", ""),
    0x03: (0x25BEA0, "port", "goto", ""),
    0x04: (0, "port", "end", ""),
    0x05: (0x25BDC8, "port", "nop", ""),
    0x06: (0x25BEA8, "port", "skip4", ""),
    0x07: (0x25BEB8, "port", "goto", ""),
    0x08: (0x25BEC0, "port", "skip4", ""),
    0x09: (0x25BED0, "port", "goto", ""),
    0x0A: (0x25BED8, "port", "debug_code", "", "prints and bumps the debug counter iGpffffb0cc"),
    0x32: (0x25D6F0, "port", "call", "", "statement form; inside an expression it is PTR_LAB_0031e228[0], which returns 0"),
    0x33: (0x25D6F8, "port", "say", "", "inline dialogue: rel32 skip, then the text"),

    # -- standard table PTR_LAB_0031e228 --
    0x34: (0x25D728, "elf", "system_busy", ""),
    0x35: (0x25D748, "port", "dialogue_done", ""),
    0x36: (0x25D768, "port", "work", "e:index"),
    0x37: (0x25D818, "port", "work_assign", "e:index e:value b:op"),
    0x38: (0x25D768, "port", "flag_byte", "e:bit"),
    0x39: (0x25D818, "port", "flag_byte_assign", "e:bit e:value b:op"),
    0x3A: (0x25DAB8, "port", "came_from_scene_bcd", ""),
    0x3B: (0x25DA78, "port", "current_scene_bcd", ""),
    0x3C: (0x25DAF8, "port", "set_map_prop_bank", "e:bank"),
    0x3D: (0x25E560, "port", "flag", "e:id", "returns the value before any write"),
    0x3E: (0x25E560, "port", "set_flag", "e:id", "returns the value before the write"),
    0x3F: (0x25E560, "port", "clear_flag", "e:id", "returns the value before the write"),
    0x40: (0x25E560, "port", "toggle_flag", "e:id", "returns the value before the write"),
    0x41: (0x25DB20, "port", "camera_path", "e:eye@pts3 e:look_at@pts3 b:flags", "flags bit 0 splices the live pose in first"),
    0x42: (0x25DD60, "port", "step_camera_path", "e:duration"),
    0x43: (0x25DE08, "port", "camera_path_roll_zoom", "e:eye@pts3 e:roll_zoom@pts2 e:look_at@pts3"),
    0x44: (0x25DD60, "port", "step_camera_path_roll_zoom", "e:duration"),
    0x45: (0x25DFC8, "port", "release_camera", "e:restore"),
    0x46: (0x25DFF0, "port", "set_camera", "e:eye_x e:eye_y e:eye_z e:look_x e:look_y e:look_z"),
    0x47: (0x25E0E8, "port", "set_camera_eye", "e:x e:y e:z"),
    0x48: (0x25E170, "port", "set_camera_look_at", "e:x e:y e:z"),
    0x49: (0x25E1F8, "elf", "camera_look_at_entity", "e:entity"),
    0x4A: (0x25E250, "ghidra", "set_projection", "b:reset_camera e:plane_x e:plane_y e:plane_z e:near e:far e:depth"),
    0x4B: (0x25E420, "elf", "projection_hit", ""),
    0x4C: (0x25E520, "port", "set_camera_roll", "e:roll"),
    0x4D: (0x25E628, "port", "preload_models", "", "count byte, then that many u32 type ids"),
    0x4E: (0x25E730, "port", "push_spawn_lookup", "e:value1 w:value3 e:value2"),
    0x4F: (0x25E7C0, "port", "spawn_placements", "", "map placement groups 0, 4 and 5"),
    0x50: (0x25EAF0, "port", "init_entity", "e:entity w:type"),
    0x51: (0x25EB48, "port", "spawn_placement_group", "b:group"),
    0x52: (0x25EDC8, "port", "spawn", "e:type"),
    0x53: (0x25EE08, "port", "entity_position", "e:entity e:axis"),
    0x54: (0x25EEB0, "port", "set_position", "e:entity e:x e:y e:z"),
    0x55: (0x25EEB0, "port", "set_position_on_ground", "e:entity e:x e:y e:z"),
    0x56: (0x25EFA8, "port", "set_size", "e:entity e:size"),
    0x57: (0x25F010, "elf", "set_animation_params", "e:entity e:mode e:value"),
    0x58: (0x25F0D8, "port", "select", "e:slot"),
    0x59: (0x25F120, "port", "selected", ""),
    0x5A: (0x25F150, "port", "select_by_record", "e:record"),
    0x5B: (0x25F1C8, "ghidra", "placement_field", "e:index b:field", "field < 4 is a byte at +0x0C, else a scaled float"),
    0x5C: (0x25F238, "port", "destroy", "e:entity"),
    0x5D: (0x25F290, "port", "push_polar", "e:entity e:speed e:angle"),
    0x5E: (0x25F380, "port", "polar_x", "e:magnitude e:angle"),
    0x5F: (0x25F380, "port", "polar_y", "e:magnitude e:angle"),
    0x60: (0x25F428, "port", "ground_height", "e:x e:y e:z"),
    0x61: (0x25F4B8, "port", "lead_on_terrain", "e:mask b:selector"),
    0x62: (0x25F548, "port", "find_by_tag", "e:tag"),
    0x63: (0x25F5D8, "port", "attach", "e:parent e:bone e:child e:anim e:x e:y e:z"),
    0x64: (0x25F700, "port", "detach_from_bone", "e:entity"),
    0x65: (0x25F7D8, "ghidra", "spawn_npc", "h:type w:body@code e:field_a0 e:x e:y e:z e:facing",
           "allocates a type-0x38 NPC whose real type is parked at +0x1CE"),
    0x66: (0x25F950, "port", "make_npc", "e:entity w:body@code", "body runs every frame with the entity focused"),
    0x67: (0x25FA40, "port", "look_at_point", "e:entity e:x e:y e:unused1 e:unused2 e:frames"),
    0x68: (0x25FAE0, "elf", "is_tracking", "e:entity e:a e:b"),
    0x69: (0x25FB30, "elf", "clear_tracking", "e:entity e:a e:b"),
    0x6A: (0x25FB80, "elf", "zoom_ramp_start", "e:target e:rate e:value"),
    0x6B: (0x25FC28, "elf", "zoom_ramp_step", ""),
    0x6C: (0x25FCA0, "port", "set_zoom", "e:zoom e:value"),
    0x6D: (0x25FD10, "port", "player_lock", "b:mode", "signed byte: -1 locks, 1 releases"),
    0x6E: (0x25FE98, "port", "angle_between", "e:x1 e:y1 e:x2 e:y2"),
    0x6F: (0x25FF58, "elf", "distance_between", "e:x1 e:y1 e:x2 e:y2"),
    0x70: (0x260038, "port", "angle_to_lead", "e:entity"),
    0x71: (0x260080, "elf", "distance_to_lead", "e:entity"),
    0x72: (0x2600C8, "port", "ease_angle", "e:from e:to e:rate"),
    0x73: (0x260188, "port", "angle_delta", "e:a e:b"),
    0x74: (0x2601F8, "port", "entity_distance", "e:a e:b"),
    0x75: (0x2601F8, "port", "entity_angle", "e:a e:b"),
    0x76: (0x260318, "port", "reg", "e:entity e:index"),
    0x77: (0x260360, "port", "reg_assign", "e:entity e:index e:value"),
    0x78: (0x260360, "port", "reg_and", "e:entity e:index e:value"),
    0x79: (0x260360, "port", "reg_or", "e:entity e:index e:value"),
    0x7A: (0x260360, "port", "reg_xor", "e:entity e:index e:value"),
    0x7B: (0x260360, "port", "reg_add", "e:entity e:index e:value"),
    0x7C: (0x260360, "port", "reg_sub", "e:entity e:index e:value"),
    0x7D: (0x260738, "port", "set_group_rotation", "e:group b:axis e:value"),
    0x7E: (0x260738, "port", "set_group_offset", "e:group b:axis e:value"),
    0x7F: (0x260880, "port", "group_rotation", "e:group b:axis"),
    0x80: (0x260880, "port", "group_offset", "e:group b:axis"),
    0x81: (0x260958, "elf", "group_rotation_wave", "e:group b:axis e:value e:rate"),
    0x82: (0x260958, "elf", "group_offset_wave", "e:group b:axis e:value e:rate"),
    0x83: (0x260B60, "elf", "set_group_flags", "e:group e:flags"),
    0x84: (0x260BC8, "elf", "group_flags", "e:group"),
    0x85: (0x260C20, "port", "fade_start", "e:rate e:colour_bits", "fade bank 1"),
    0x86: (0x260CA0, "port", "fade_step", "", "fade bank 1; returns whether it has finished"),
    0x87: (0x260C20, "port", "fade0_start", "e:rate e:colour_bits", "fade bank 0"),
    0x88: (0x260CC0, "port", "fade0_step", "", "fade bank 0"),
    0x89: (0x260CE0, "port", "screen_overlay", "e:colour e:alpha", "paints one frame"),
    0x8A: (0x260D30, "ghidra", "conditional_warp", "e:mask e:a e:b e:flags e:c", "gated on lead terrain & mask, into FUN_0022b298"),
    0x8B: (0x260E30, "elf", "conditional_map_change", "e:mask e:section e:entry e:flags e:x e:y e:z"),
    0x8C: (0x260F78, "port", "map_change", "e:section e:entry e:flags e:x e:y e:z"),
    0x8D: (0x261068, "elf", "scene_request_40001", "", "DAT_003551ec = 0x40001"),
    0x8E: (0x2610A8, "port", "scene_change", "e:destination"),
    0x8F: (0x2610F8, "port", "frame_ticks", ""),
    0x90: (0x261100, "port", "ramp_start", "e:index e:target e:current e:step"),
    0x91: (0x2611B8, "port", "ramp_step", "e:index", "returns 1 once arrived"),
    0x92: (0x261258, "port", "ramp_value", "e:index"),
    0x93: (0x2612A0, "elf", "scale_by_frame", "e:value"),
    0x94: (0x2612E0, "port", "camera_shake", "e:magnitude e:ticks"),
    0x95: (0x261890, "port", "random", ""),
    0x96: (0x2618C0, "port", "set_global_rgb", "e:r e:g e:b"),
    0x97: (0x261910, "port", "set_light_direction", "e:x e:y e:z e:r e:g e:b"),
    0x98: (0x2619E0, "elf", "set_light_direction_polar", "e:angle e:magnitude e:z e:r e:g e:b"),
    0x99: (0x261AF0, "port", "set_lead_offset", "e:x e:y e:z"),
    0x9A: (0x261B80, "port", "colour_ramp_start", "e:track e:r1 e:g1 e:b1 e:r2 e:g2 e:b2 e:duration"),
    0x9B: (0x261C38, "port", "colour_ramp_step", "e:track", "returns whether it finished"),
    0x9C: (0x261C60, "port", "colour_ramp_channel", "e:track b:channel"),
    0x9D: (0x261CB8, "port", "install", "e:slot w:body@code"),
    0x9E: (0x261D18, "port", "retire", "e:slot", "negative means the running slot"),
    0x9F: (0x261D88, "port", "slot_busy", "b:slot"),
    0xA0: (0x261DE0, "port", "free_slot", ""),
    0xA1: (0x261E30, "port", "arm_events", "e:channel w:stream@stream"),
    0xA2: (0x261EA8, "port", "disarm_events", "e:channel"),
    0xA3: (0x261F08, "port", "events_consumed", "e:channel"),
    0xA4: (0x261F60, "port", "map_primitive_flag_20", "e:mask b:on"),
    0xA5: (0x262058, "port", "map_primitive_flag_mask", "e:mask b:on e:bits"),
    0xA6: (0x261F60, "port", "map_primitive_flag_800", "e:mask b:on"),
    0xA7: (0x261FD8, "port", "retag_primitives", "e:mask e:nibble"),
    0xA8: (0x262F38, "port", "install_lead_script", "w:body@code"),
    0xA9: (0x262F80, "port", "lead_walk_to", "e:x e:y b:run"),
    0xAA: (0x263118, "port", "lead_stop", ""),
    0xAB: (0x263148, "port", "teleport_lead", "b:mode e:x e:y e:z"),
    0xAC: (0x2631F0, "port", "party_bind", "e:slot e:entity e:a e:b"),
    0xAD: (0x263498, "port", "party_release", "e:slot"),
    0xAE: (0x263518, "port", "party_release_flag", "e:slot e:flag"),
    0xAF: (0x2635C0, "port", "select_party", "e:slot"),
    0xB0: (0x263630, "port", "swap_lead", "e:entity"),
    0xB1: (0x263878, "port", "clear_bone_override", "e:entity e:bone"),
    0xB2: (0x2638D8, "port", "set_bone_scale", "e:entity e:bone e:scale"),
    0xB3: (0x263978, "port", "set_bone_translation", "e:entity e:bone e:x e:y e:z"),
    0xB4: (0x263A58, "port", "set_bone_field", "e:entity e:bone b:field e:value"),
    0xB5: (0x263B18, "port", "sample_bone", "e:entity e:bone e:work"),
    0xB6: (0x263C10, "elf", "clear_anim_slot", "e:entity e:slot"),
    0xB7: (0x263C58, "port", "set_entity_short_and_word", "e:entity e:a e:b"),
    0xB8: (0x263CB8, "port", "set_draw_distance", "e:distance"),
    0xB9: (0x263D10, "port", "set_fog_colour", "e:r e:g e:b"),
    0xBA: (0x263D60, "port", "set_fog_colour2", "e:r e:g e:b"),
    0xBB: (0x263DB0, "port", "set_fog_band", "e:near e:far"),
    0xBC: (0x263E30, "port", "give_item", "e:item"),
    0xBD: (0x263E80, "port", "method", "e:entity e:method e:a e:b", "FUN_00242a18's method table on the selected entity"),
    0xBE: (0x263EE0, "port", "engine_hook", "e:index e:arg", "PTR_FUN_0031e730[index](arg); 1 sets the camera submode"),
    0xBF: (0x263F28, "port", "light_alloc", "e:r e:g e:b e:radius"),
    0xC0: (0x263F28, "port", "light_alloc0", "e:r e:g e:b e:radius"),
    0xC1: (0x263FE8, "port", "light_bind_bone", "e:slot e:entity e:bone e:a e:b e:c e:d e:e"),
    0xC2: (0x264148, "port", "light_alpha", "e:slot e:alpha"),
    0xC3: (0x264190, "port", "light_rgb", "e:slot e:r e:g e:b"),
    0xC4: (0x264218, "port", "light_intensity", "e:slot e:value"),
    0xC5: (0x264298, "port", "light_position", "e:slot e:x e:y e:z"),
    0xC6: (0x264360, "port", "light_follow_entity", "e:slot e:entity"),
    0xC7: (0x2643F0, "port", "light_off", "e:slot"),
    0xC8: (0x264448, "port", "screen_smear", "e:alpha"),
    0xC9: (0x264470, "port", "screen_smear_transform", "e:alpha e:dx e:dy e:sx e:sy e:rotation"),
    0xCA: (0x264500, "port", "set_dialogue_speaker_byte", "e:value"),
    0xCB: (0x264528, "elf", "find_visible_entity", "e:mask"),
    0xCC: (0x2645B8, "elf", "lead_terrain_flags", "e:which"),
    0xCD: (0x264628, "elf", "sample_motion_keyframe", "e:entity"),
    0xCE: (0x264700, "port", "select_debug", "e:entity"),
    0xCF: (0x264740, "port", "apply_extra_resource", "e:entity w:blob@data"),
    0xD0: (0x264790, "port", "entity_op_d0", "e:entity"),
    0xD1: (0x2647D0, "elf", "bind_attack_once", "e:entity w:record@data e:value"),
    0xD2: (0x264858, "ghidra", "peek", "e:offset@data b:width", "reads `width` bytes of the blob, little endian"),
    0xD3: (0x2648F8, "ghidra", "poke", "e:offset@data b:width e:value", "writes `width` bytes into the blob"),
    0xD4: (0x264998, "elf", "status_bit_40", ""),
    0xD5: (0x264D40, "port", "set_hud_variant", "e:value"),
    0xD6: (0x264D68, "port", "set_renderer_byte_b", "e:value"),
    0xD7: (0x264D90, "port", "set_palette_slot", "e:slot e:value"),
    0xD8: (0x264DE8, "port", "set_hazard_attack", "w:record@data e:value"),
    0xD9: (0x264E30, "port", "set_break_table", "e:entity w:table@data"),
    0xDA: (0x264EA0, "port", "set_byte_b6f0", "e:value"),
    0xDB: (0x264EC8, "elf", "call_266fc0", ""),
    0xDC: (0x264EE8, "elf", "nop_stub1", "e:a", "FUN_0023baf8 is empty in retail"),
    0xDD: (0x264F18, "elf", "nop_stub2", "e:a e:b", "FUN_0023baf8 is empty in retail"),
    0xDE: (0x264F50, "port", "vibrate", "e:strength e:pattern@data"),
    0xDF: (0x264FA0, "elf", "battle_logo_show", ""),
    0xE0: (0x264FC0, "elf", "battle_logo_hide", ""),
    0xE1: (0x265000, "port", "save_prompt", ""),
    0xE2: (0x2650E0, "port", "set_effect_water_line", "e:z"),
    0xE3: (0x265120, "port", "set_byte_355641", "e:value"),
    0xE4: (0x265148, "elf", "upload_words", "e:a e:b e:c e:d"),
    0xE5: (0x2651A0, "port", "load_backdrop", "h:model e:slot"),
    0xE6: (0x265200, "port", "backdrop_shade", "e:slot e:shade e:rotation"),
    0xE7: (0x265290, "port", "gate_mask", ""),
    0xE8: (0x265290, "port", "set_gate_mask", "e:value"),
    0xE9: (0x265D88, "port", "interacted", "", "reads and clears the focus's +0x1CC"),
    0xEA: (0x265818, "port", "focus_index", ""),
    0xEB: (0x265840, "port", "focus", "e:slot"),
    0xEC: (0x265880, "port", "set_step", "e:step"),
    0xED: (0x2658B0, "port", "step", ""),
    0xEE: (0x2658C0, "port", "walk_to_anim", "e:anim e:speed e:x e:y"),
    0xEF: (0x2658C0, "port", "walk_to_speed", "e:speed e:x e:y"),
    0xF0: (0x2658C0, "port", "walk_to", "e:x e:y"),
    0xF1: (0x2658C0, "port", "run_to", "e:x e:y"),
    0xF2: (0x265B90, "port", "hold_anim", "e:anim e:frames"),
    0xF3: (0x265C30, "port", "play_anim", "e:anim"),
    0xF4: (0x265CB0, "port", "turn_to", "e:angle e:rate"),
    0xF5: (0x265D98, "port", "restore_type", ""),

    # -- extended table PTR_LAB_0031e538, behind 0xFF --
    0x100: (0x2620A8, "port", "clear_pool_gate", "b:pool", "1-based: 1 is the rain, 5 the haze"),
    0x101: (0x262118, "elf", "pool_101", "e:a e:b e:c"),
    0x102: (0x262250, "port", "rain", "e:count e:length e:fall e:magnitude e:height e:rotate_x e:rotate_z e:entity"),
    0x103: (0x262488, "elf", "pool_103", "e:a e:b"),
    0x104: (0x2624D8, "elf", "pool_104", "e:x e:y e:z e:a e:b e:c e:entity"),
    0x105: (0x262118, "elf", "pool_105", "e:a e:b e:c"),
    0x106: (0x262250, "port", "pool_106", "e:count e:length e:fall e:magnitude e:height e:rotate_x e:rotate_z e:entity"),
    0x107: (0x262118, "elf", "pool_107", "e:a e:b e:c"),
    0x108: (0x262250, "port", "pool_108", "e:count e:length e:fall e:magnitude e:height e:rotate_x e:rotate_z e:entity"),
    0x109: (0x2625B8, "port", "haze", "e:count e:size e:speed e:magnitude e:angle e:entity"),
    0x10A: (0x262690, "port", "dust_scatter", "e:x e:y e:z e:size e:life e:jitter_x e:jitter_y e:count"),
    0x10B: (0x262780, "port", "dust_ring", "e:x e:y e:z e:size e:life e:jitter_x e:jitter_y e:radius e:outer e:inner"),
    0x10C: (0x262898, "port", "dust_ring_coloured", "e:x e:y e:z e:size e:life e:jitter_x e:jitter_y e:radius e:outer e:inner e:colour"),
    0x10D: (0x2629C0, "port", "spray", "e:count e:rise e:x e:y e:z e:speed e:life e:mode e:colour"),
    0x10E: (0x262A98, "port", "plume", "e:bursts e:rise e:size e:life e:x e:y e:z e:cycles e:mode e:colour"),
    0x10F: (0x262B90, "port", "fountain", "e:count e:rise e:fall e:drift e:speed e:z_jitter e:size e:life e:x e:y e:z e:loop e:camera_relative e:colour"),
    0x110: (0x262CF0, "port", "smoke", "e:count e:colour e:scale"),
    0x111: (0x262CF0, "port", "smoke_reseed", "e:count e:colour e:scale"),
    0x112: (0x262D88, "port", "fountain_gate", "e:on"),
    0x113: (0x262DB0, "port", "emitter_gate", "e:on"),
    0x114: (0x262DD8, "port", "gather", "e:count e:x e:y e:z e:speed e:radius e:yaw e:pitch e:spread e:size e:colour"),
    0x115: (0x262F10, "port", "release_gather", "e:group"),
    0x116: (0x2649A8, "ghidra", "uv_anim_a", "e:entity e:track e:value"),
    0x117: (0x2649A8, "port", "uv_anim_a_b788", "e:track e:value"),
    0x118: (0x2649A8, "ghidra", "uv_anim_a_345a2c", "e:track e:value"),
    0x119: (0x264A60, "ghidra", "uv_anim_b", "e:entity e:from e:to"),
    0x11A: (0x264A60, "port", "uv_anim_b_b788", "e:from e:to"),
    0x11B: (0x264A60, "ghidra", "uv_anim_b_345a2c", "e:from e:to"),
    0x11C: (0x264B18, "ghidra", "uv_anim_c", "e:entity e:a e:b e:c"),
    0x11D: (0x264B18, "ghidra", "uv_anim_c_b788", "e:a e:b e:c"),
    0x11E: (0x264B18, "ghidra", "uv_anim_c_345a2c", "e:a e:b e:c"),
    0x11F: (0x264BE8, "ghidra", "uv_anim_d", "e:entity e:track"),
    0x120: (0x264BE8, "port", "uv_anim_d_b788", "e:track"),
    0x121: (0x264BE8, "ghidra", "uv_anim_d_345a2c", "e:track"),
    0x122: (0x264C88, "ghidra", "uv_anim_e", "e:entity e:a e:b e:c"),
    0x123: (0x264C88, "ghidra", "uv_anim_e_b788", "e:a e:b e:c"),
    0x124: (0x264C88, "ghidra", "uv_anim_e_345a2c", "e:a e:b e:c"),
    0x125: (0x261330, "port", "sound_at_entity", "h:cue e:entity"),
    0x126: (0x261330, "port", "sound_at_entity_volume", "h:cue e:entity e:volume"),
    0x127: (0x2613D8, "port", "sound_at", "h:cue e:x e:y e:z"),
    0x128: (0x2613D8, "port", "sound_at_volume", "h:cue e:x e:y e:z e:volume"),
    0x129: (0x261500, "port", "music_play", "e:slot e:fader"),
    0x12A: (0x261530, "port", "music_ramp_up", "e:slot e:speed e:fader"),
    0x12B: (0x261570, "port", "music_ramp_down", "e:slot e:speed e:fader"),
    0x12C: (0x2615B0, "port", "music_ramping", "e:slot"),
    0x12D: (0x2615D8, "port", "music_12d", "e:slot"),
    0x12E: (0x261600, "port", "music_12e", "e:a e:b"),
    0x12F: (0x261638, "port", "music_12f", "e:a e:b"),
    0x130: (0x261670, "elf", "set_byte_3555d5", "e:value"),
    0x131: (0x2616B8, "elf", "set_byte_3555d6", "e:value"),
    0x132: (0x261700, "port", "voice_cache", "e:channel w:bank"),
    0x133: (0x261760, "port", "voice_cache0", "e:channel w:bank"),
    0x134: (0x2617C0, "port", "voice_loaded", ""),
    0x135: (0x2617E0, "port", "voice_play", "e:channel"),
    0x136: (0x261808, "port", "voice_play_136", "e:channel e:value"),
    0x137: (0x261868, "port", "voice_busy", "e:ignored"),
    0x138: (0x2652D0, "elf", "op_138", "e:a e:b e:c e:d e:e e:f e:g e:h"),
    0x139: (0x265350, "elf", "op_139", "e:value"),
    0x13A: (0x265378, "port", "request_movie", "e:movie"),
    0x13B: (0x2653A0, "port", "set_depth_blur", "e:level"),
    0x13C: (0x2653E8, "elf", "set_byte_355655", "e:value"),
    0x13D: (0x265410, "port", "set_entity_anim", "e:entity e:anim"),
    0x13E: (0x265468, "elf", "set_byte_b655_ff", ""),
    0x13F: (0x2604A8, "port", "publish_rig_children", "e:entity e:body_work e:head_work"),
    0x140: (0x260578, "port", "attach_prop", "e:entity e:hide e:type e:work"),
    0x141: (0x260578, "port", "attach_prop_bone", "e:entity e:bone e:hide e:type e:work"),
    0x142: (0x2606D0, "port", "detach_props", "e:entity"),
    0x143: (0x265478, "elf", "op_143", "e:x e:y e:z"),
    0x144: (0x2654F8, "ghidra", "load_path", "e:points@pts3 e:value"),
    0x145: (0x265620, "elf", "sample_path", "e:a e:b e:c e:d e:e"),
    0x146: (0x265738, "port", "set_bandana_mode", "e:mode"),
    0x147: (0x265760, "elf", "set_byte_3555c4", "e:value"),
    0x148: (0x265788, "port", "byte_b6e4", ""),
    0x149: (0x265790, "port", "set_byte_355656", "e:value"),
    0x14A: (0x2657B8, "elf", "op_14a", "e:a e:b"),
}

# 0xF6..0xFE fall off the end of the standard table into the start of the
# extended one (0x31e228 + 0xC4 * 4 == 0x31e538), so they run 0x100..0x108's
# handlers. Nothing has been seen to use them.
for _alias in range(0xF6, 0xFF):
    _h, _src, _name, _spec, *_ = _TABLE[0x100 + _alias - 0xF6]
    _TABLE[_alias] = (_h, _src, _name + "_alias", _spec, f"standard-table alias of 0x{0x100 + _alias - 0xF6:X}")


def _build() -> dict[int, Op]:
    ops = {}
    for code, row in _TABLE.items():
        handler, source, name, spec, *rest = row
        ops[code] = Op(code, name, _parse(spec), handler, source, rest[0] if rest else "")
    return ops


OPS: dict[int, Op] = _build()

# Operators of the postfix expression evaluator (FUN_0025c258). "second" is
# the operand pushed first, so it is the left-hand side.
BINARY = {
    0x12: "==", 0x13: "!=", 0x14: "<", 0x15: ">", 0x16: "<=", 0x17: ">=",
    0x1A: "&&", 0x1B: "||", 0x1C: "+", 0x1D: "-", 0x1F: "^", 0x20: "&",
    0x21: "|", 0x22: "/", 0x23: "*", 0x24: "%",
}
UNARY = {0x18: "!", 0x19: "~", 0x1E: "-"}

# C precedence, higher binds tighter.
PRECEDENCE = {
    "||": 1, "&&": 2, "|": 3, "^": 4, "&": 5, "==": 6, "!=": 6,
    "<": 7, ">": 7, "<=": 7, ">=": 7, "+": 9, "-": 9, "*": 10, "/": 10, "%": 10,
}

# 0x37/0x39's trailing operation byte (FUN_0025d818).
ASSIGN_OPS = {
    0x25: "=", 0x26: "*=", 0x27: "/=", 0x28: "%=", 0x29: "+=", 0x2A: "-=",
    0x2B: "&=", 0x2C: "^=", 0x2D: "|=", 0x2E: "++", 0x2F: "--",
}

# 0x77..0x7C, rendered as assignments to reg(entity, index).
REG_OPS = {0x77: "=", 0x78: "&=", 0x79: "|=", 0x7A: "^=", 0x7B: "+=", 0x7C: "-="}

# 0xBD's method numbers that the port has identified.
METHODS = {
    1: "battle_stop",
    2: "battle_start",
    3: "battle_build_party",
    0x68: "battle_camera",
    0x70: "path_follow",
    0x72: "path_progress",
    0x76: "scripted_beat",
    0x77: "play_voice_clip",
    0x78: "equip_spell",
}

HEADER_WORDS = [
    ("init", "entry 0, run at load by FUN_0025b6d0"),
    ("start", "entry 1, run at load by FUN_0025b728"),
    ("tick", "entry 2, run every frame by FUN_0025b778"),
    ("interact", "entry 3, the player's interaction probe (FUN_00252828)"),
    ("teardown", "entry 4, entity teardown hook (FUN_00265EC0)"),
    ("dialogue_table", "zero-terminated table of dialogue record offsets"),
    ("scene_defaults", "texture page ids and FUN_0025b600's scene defaults"),
    ("word7", ""),
    ("word8", ""),
    ("word9", ""),
    ("word10", ""),
]
