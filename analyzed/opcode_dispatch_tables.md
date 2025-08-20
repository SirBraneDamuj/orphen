# Opcode Dispatch Tables (Bytecode Interpreter)

Original tables: `PTR_LAB_0031e228` (standard opcodes 0x32–0xF5) and `PTR_LAB_0031e538` (extended opcodes 0x100+ via 0xFF prefix). Interpreter: `bytecode_interpreter.c` (original `FUN_0025c258`).

The VM logic:

- If next byte > 0x31 and != 0xFF: opcode = byte (0x32–0xFE). Handler = `PTR_LAB_0031e228[opcode - 0x32]`.
- If byte == 0xFF: extended opcode value = 0x100 + following byte. Handler = `PTR_LAB_0031e538[following byte]`. IP advances by 2.
- Bytes 0x00–0x31 handled inline / by helper `FUN_0025bf70` and the switch at end of interpreter (see comments there for arithmetic/comparison ops and 0x0B return).

## Standard Dispatch Table (PTR_LAB_0031e228)

Opcode -> Target function (still using original FUN*/LAB* names until analyzed). Duplicates indicate alias opcodes mapping to same handler.

Naming note: Until we confirm behavior, our "best-guess" name defaults to the current FUN\_\* symbol. When we've got a better semantic hypothesis, we annotate it inline after the symbol.

```
0x32 return_zero  # orig LAB_0025d6f0 — jr ra; move v0, zero
0x33 FUN_0025d6f8  # name: advance_ip_and_sync_frame
0x34 FUN_0025d728  # name: probe_system_busy
0x35 FUN_0025d748  # guess: FUN_0025d748
0x36 script_read_flag_or_work_memory  # orig FUN_0025d768 — reads work array u32 (0x36) or flag bucket byte (0x38); see analyzed/script_read_flag_or_work_memory.c
0x37 variable_or_flag_alu  # orig FUN_0025d818 — ALU op on var array (0x37) or flag bucket (0x39)
0x38 script_read_flag_or_work_memory  # alias of 0x36; same handler branches by DAT_00355cd8
0x39 variable_or_flag_alu  # alias of 0x37; same handler chooses mode by sGpffffbd68
0x3A FUN_0025dab8  # guess: FUN_0025dab8
0x3B FUN_0025da78  # guess: FUN_0025da78
0x3C FUN_0025daf8  # guess: FUN_0025daf8
0x3D FUN_0025e560  # name: modify_flag_state (query/set/clear/toggle cluster via prev opcode)
0x3E FUN_0025e560  # name: modify_flag_state
0x3F FUN_0025e560  # name: modify_flag_state
0x40 FUN_0025e560  # name: modify_flag_state
0x41 FUN_0025db20  # guess: FUN_0025db20
0x42 FUN_0025dd60  # guess: FUN_0025dd60
0x43 build_and_submit_3way_vertex_streams  # orig FUN_0025de08 — reads 3 streams (xyz/uv/xyz), normalizes, submits
0x44 FUN_0025dd60  # guess: FUN_0025dd60
0x45 FUN_0025dfc8  # guess: FUN_0025dfc8  # TODO: analyze (seen in recent trace)
0x46 FUN_0025dff0  # guess: FUN_0025dff0
0x47 FUN_0025e0e8  # guess: FUN_0025e0e8
0x48 FUN_0025e170  # guess: FUN_0025e170
0x49 FUN_0025e1f8  # guess: FUN_0025e1f8
0x4A FUN_0025e250  # guess: FUN_0025e250
0x4B FUN_0025e420  # guess: FUN_0025e420
0x4C FUN_0025e520  # guess: FUN_0025e520
0x4D process_resource_id_list  # orig FUN_0025e628 — read N 16-bit IDs from stream into temp arena, 0-terminate, call FUN_002661f8; see analyzed/ops/0x4D_process_resource_id_list.c
0x4E FUN_0025e730  # guess: FUN_0025e730
0x4F process_pending_spawn_requests  # orig FUN_0025e7c0 — iterate pending list, load/resolve descriptors, init entities/effects
0x50 FUN_0025eaf0  # guess: FUN_0025eaf0
0x51 set_pw_all_dispatch  # orig FUN_0025eb48 — "tbox" set_pw_all group dispatcher; reads mode from pbGpffffbd60 and spawns/configures entries; see analyzed/ops/0x51_set_pw_all_dispatch.c
0x52 FUN_0025edc8  # guess: FUN_0025edc8
0x53 FUN_0025ee08  # guess: FUN_0025ee08
0x54 FUN_0025eeb0  # guess: FUN_0025eeb0
0x55 FUN_0025eeb0  # guess: FUN_0025eeb0
0x56 FUN_0025efa8  # guess: FUN_0025efa8
0x57 FUN_0025f010  # guess: FUN_0025f010
0x58 select_pw_slot_by_index  # orig FUN_0025f0d8 — sets DAT_00355044 = &DAT_0058beb0 + (expr*0xEC) if expr<0x100; see analyzed/ops/0x58_select_pw_slot_by_index.c
0x59 get_pw_slot_index  # orig FUN_0025f120 — returns selected pool slot index (or 0x100 if none); see analyzed/ops/0x59_get_pw_slot_index.c
0x5A select_pw_by_index  # orig FUN_0025f150 — selects the first active pool object with slot[+0x4C]==arg; sets DAT_00355044; returns 1/0; see analyzed/ops/0x5A_select_pw_by_index.c
0x5B FUN_0025f1c8  # guess: FUN_0025f1c8
0x5C FUN_0025f238  # guess: FUN_0025f238
0x5D FUN_0025f290  # guess: FUN_0025f290
0x5E FUN_0025f380  # guess: FUN_0025f380
0x5F FUN_0025f380  # guess: FUN_0025f380
0x60 FUN_0025f428  # guess: FUN_0025f428
0x61 FUN_0025f4b8  # guess: FUN_0025f4b8
0x62 FUN_0025f548  # guess: FUN_0025f548
0x63 FUN_0025f5d8  # guess: FUN_0025f5d8
0x64 FUN_0025f700   # name: update_object_transform_from_bone (tentative)
0x65 FUN_0025f7d8   # name: multi_call_feeder
0x66 FUN_0025f950  # guess: FUN_0025f950
0x67 FUN_0025fa40  # guess: FUN_0025fa40
0x68 FUN_0025fae0  # guess: FUN_0025fae0
0x69 FUN_0025fb30  # guess: FUN_0025fb30
0x6A FUN_0025fb80  # guess: FUN_0025fb80
0x6B LAB_0025fc28
0x6C FUN_0025fca0  # guess: FUN_0025fca0
0x6D FUN_0025fd10  # guess: FUN_0025fd10
0x6E FUN_0025fe98  # guess: FUN_0025fe98
0x6F FUN_0025ff58  # guess: FUN_0025ff58
0x70 submit_angle_to_target  # orig FUN_00260038 — selects object (index or direct), computes angle via atan2f wrapper FUN_0023a480, scales by fGpffff8c84, submits; see analyzed/ops/0x70_submit_angle_to_target.c
0x71 submit_distance_to_target  # orig FUN_00260080 — select object (idx or keep current), compute distance via FUN_0023a418(DAT_00355044), scale by DAT_00352bf8, submit; see analyzed/ops/0x71_submit_distance_to_target.c
0x72 FUN_002600c8  # name: lerp_wrap_and_submit — see analyzed/ops/0x72_lerp_wrap_and_submit.c
0x73 submit_wrapped_delta  # orig FUN_00260188 — delta = wrap((b/scale)-(a/scale)) * scale; submit; see analyzed/ops/0x73_submit_wrapped_delta.c
0x74 FUN_002601f8  # guess: FUN_002601f8
0x75 FUN_002601f8  # guess: FUN_002601f8
0x76 FUN_00260318  # name: select_object_and_read_register
0x77 FUN_00260360  # name: modify_register_rmw (AND/OR/XOR/ADD/SUB family via read+op+write)
0x78 FUN_00260360  # name: modify_register_rmw
0x79 FUN_00260360  # name: modify_register_rmw
0x7A FUN_00260360  # name: modify_register_rmw
0x7B FUN_00260360  # name: modify_register_rmw
0x7C FUN_00260360  # name: modify_register_rmw
0x7D FUN_00260738  # guess: FUN_00260738
0x7E FUN_00260738  # guess: FUN_00260738
0x7F submit_param_from_model_axis  # orig FUN_00260880 — read modelIndex (expr) + axis (imm), pick 3-component field, scale, submit via FUN_0030bd20
0x80 submit_param_from_model_axis  # orig FUN_00260880 — same handler as 0x7F (alias)
0x81 FUN_00260958  # guess: FUN_00260958
0x82 FUN_00260958  # guess: FUN_00260958
0x83 FUN_00260b60  # guess: FUN_00260b60
0x84 FUN_00260bc8  # guess: FUN_00260bc8
0x85 FUN_00260c20  # guess: FUN_00260c20
0x86 advance_fullscreen_fade  # orig FUN_00260ca0 — calls FUN_0025d238 to step and submit fullscreen fade; see analyzed/ops/0x86_advance_fullscreen_fade.c
0x87 FUN_00260c20  # guess: FUN_00260c20
0x88 FUN_00260cc0  # guess: FUN_00260cc0
0x89 FUN_00260ce0  # guess: FUN_00260ce0
0x8A FUN_00260d30  # guess: FUN_00260d30
0x8B FUN_00260e30  # guess: FUN_00260e30
0x8C FUN_00260f78  # guess: FUN_00260f78
0x8D FUN_00261068  # guess: FUN_00261068
0x8E FUN_002610a8  # guess: FUN_002610a8
0x8F LAB_002610f8
0x90 FUN_00261100  # guess: FUN_00261100
0x91 param_ramp_current_toward_target  # orig FUN_002611b8 — step DAT_00571de0[idx].current toward target by step * (DAT_003555bc/32)
0x92 audio_submit_current_param  # orig FUN_00261258 — submit DAT_00571de0[idx].current * DAT_00352c30 via FUN_0030bd20
0x93 FUN_002612a0  # guess: FUN_002612a0
0x94 FUN_002612e0  # guess: FUN_002612e0
0x95 FUN_00261890  # guess: FUN_00261890
0x96 set_global_rgb_color  # orig FUN_002618c0 — read three values via evaluator, pack low bytes into uGpffffb6fc (0xRRGGBB); see analyzed/ops/0x96_set_global_rgb_color.c
0x97 FUN_00261910  # guess: FUN_00261910
0x98 FUN_002619e0  # guess: FUN_002619e0
0x99 FUN_00261af0  # guess: FUN_00261af0
0x9A FUN_00261b80  # guess: FUN_00261b80
0x9B FUN_00261c38  # name: advance_fade_track
0x9C FUN_00261c60  # guess: FUN_00261c60
0x9D set_slot_pointer_from_stream_offset  # orig FUN_00261cb8 — slot[idx] = iGpffffb0e8 + next_u32(); idx from expr; bounds 0x40
0x9E finish_process_slot  # orig FUN_00261d18 — clears slot[sel]; sel<0 => current slot (uGpffffbd88)
0x9F FUN_00261d88  # guess: FUN_00261d88
0xA0 FUN_00261de0  # guess: FUN_00261de0
0xA1 FUN_00261e30  # guess: FUN_00261e30
0xA2 FUN_00261ea8  # guess: FUN_00261ea8
0xA3 FUN_00261f08  # guess: FUN_00261f08
0xA4 FUN_00261f60  # guess: FUN_00261f60
0xA5 FUN_00262058  # guess: FUN_00262058
0xA6 FUN_00261f60  # guess: FUN_00261f60
0xA7 FUN_00261fd8  # guess: FUN_00261fd8
0xA8 FUN_00262f38  # guess: FUN_00262f38
0xA9 FUN_00262f80  # guess: FUN_00262f80
0xAA FUN_00263118  # guess: FUN_00263118
0xAB FUN_00263148  # guess: FUN_00263148
0xAC FUN_002631f0  # guess: FUN_002631f0
0xAD FUN_00263498  # guess: FUN_00263498
0xAE FUN_00263518  # guess: FUN_00263518
0xAF FUN_002635c0  # guess: FUN_002635c0
0xB0 FUN_00263630  # guess: FUN_00263630
0xB1 FUN_00263878  # guess: FUN_00263878
0xB2 FUN_002638d8  # guess: FUN_002638d8
0xB3 FUN_00263978  # guess: FUN_00263978
0xB4 FUN_00263a58  # guess: FUN_00263a58
0xB5 FUN_00263b18  # guess: FUN_00263b18
0xB6 FUN_00263c10  # guess: FUN_00263c10
0xB7 FUN_00263c58  # guess: FUN_00263c58
0xB8 FUN_00263cb8  # guess: FUN_00263cb8
0xB9 FUN_00263d10  # guess: FUN_00263d10
0xBA FUN_00263d60  # guess: FUN_00263d60
0xBB FUN_00263db0  # guess: FUN_00263db0
0xBC FUN_00263e30  # guess: FUN_00263e30
0xBD FUN_00263e80  # guess: FUN_00263e80
0xBE call_function_table_entry  # orig FUN_00263ee0 — dispatch PTR_FUN_0031e730[index](arg)
0xBF FUN_00263f28  # guess: FUN_00263f28
0xC0 FUN_00263f28  # guess: FUN_00263f28
0xC1 FUN_00263fe8  # guess: FUN_00263fe8
0xC2 FUN_00264148  # guess: FUN_00264148
0xC3 FUN_00264190  # guess: FUN_00264190
0xC4 FUN_00264218  # guess: FUN_00264218
0xC5 FUN_00264298  # guess: FUN_00264298
0xC6 FUN_00264360  # guess: FUN_00264360
0xC7 FUN_002643f0  # guess: FUN_002643f0
0xC8 FUN_00264448  # guess: FUN_00264448
0xC9 FUN_00264470  # guess: FUN_00264470
0xCA FUN_00264500  # guess: FUN_00264500
0xCB FUN_00264528  # guess: FUN_00264528
0xCC FUN_002645b8  # guess: FUN_002645b8
0xCD FUN_00264628  # guess: FUN_00264628
0xCE FUN_00264700  # guess: FUN_00264700
0xCF FUN_00264740  # guess: FUN_00264740
0xD0 FUN_00264790  # guess: FUN_00264790
0xD1 FUN_002647d0  # guess: FUN_002647d0
0xD2 FUN_00264858  # guess: FUN_00264858
0xD3 FUN_002648f8  # guess: FUN_002648f8
0xD4 query_status_bit_0x40  # orig LAB_00264998 — returns (DAT_003555fa & 0x40)
0xD5 FUN_00264d40  # guess: FUN_00264d40
0xD6 FUN_00264d68  # guess: FUN_00264d68
0xD7 FUN_00264d90  # guess: FUN_00264d90
0xD8 FUN_00264de8  # guess: FUN_00264de8
0xD9 FUN_00264e30  # guess: FUN_00264e30
0xDA FUN_00264ea0  # guess: FUN_00264ea0
0xDB FUN_00264ec8  # guess: FUN_00264ec8
0xDC FUN_00264ee8  # guess: FUN_00264ee8
0xDD FUN_00264f18  # guess: FUN_00264f18
0xDE FUN_00264f50  # guess: FUN_00264f50
0xDF FUN_00264fa0  # guess: FUN_00264fa0
0xE0 FUN_00264fc0  # guess: FUN_00264fc0
0xE1 FUN_00265000  # guess: FUN_00265000
0xE2 FUN_002650e0  # guess: FUN_002650e0
0xE3 FUN_00265120  # guess: FUN_00265120
0xE4 stage_swizzled_word_stream_upload  # orig FUN_00265148 — copy words into swizzled bank buffer and schedule DMA (FUN_00210b60)
0xE5 FUN_002651a0  # guess: FUN_002651a0
0xE6 FUN_00265200  # guess: FUN_00265200
0xE7 FUN_00265290  # guess: FUN_00265290
0xE8 FUN_00265290  # guess: FUN_00265290
0xE9 LAB_00265d88
0xEA LAB_00265818
0xEB FUN_00265840  # guess: FUN_00265840
0xEC FUN_00265880  # guess: FUN_00265880
0xED LAB_002658b0
0xEE FUN_002658c0  # guess: FUN_002658c0
0xEF FUN_002658c0  # guess: FUN_002658c0
0xF0 FUN_002658c0  # guess: FUN_002658c0
0xF1 FUN_002658c0  # guess: FUN_002658c0
0xF2 FUN_00265b90  # guess: FUN_00265b90
0xF3 FUN_00265c30  # guess: FUN_00265c30
0xF4 FUN_00265cb0  # guess: FUN_00265cb0
0xF5 LAB_00265d98
```

## Extended Dispatch Table (PTR_LAB_0031e538)

Extended opcode = 0x100 + index byte following 0xFF.

```
0x100 LAB_002620a8
0x101 FUN_00262118  # guess: FUN_00262118
0x102 FUN_00262250  # guess: FUN_00262250
0x103 FUN_00262488  # guess: FUN_00262488
0x104 FUN_002624d8  # guess: FUN_002624d8
0x105 FUN_00262118  # guess: FUN_00262118
0x106 FUN_00262250  # guess: FUN_00262250
0x107 FUN_00262118  # guess: FUN_00262118
0x108 FUN_00262250  # guess: FUN_00262250
0x109 FUN_002625b8  # guess: FUN_002625b8
0x10A FUN_00262690  # guess: FUN_00262690
0x10B FUN_00262780  # guess: FUN_00262780
0x10C FUN_00262898  # guess: FUN_00262898
0x10D submit_scaled_param_block  # orig FUN_002629c0 — parses 9 args, scales first four by fGpffff8d0c, calls FUN_0021e088 (see analyzed/ops/0x10D_submit_scaled_param_block.c)
0x10E FUN_00262a98  # guess: FUN_00262a98
0x10F FUN_00262b90  # guess: FUN_00262b90
0x110 FUN_00262cf0  # guess: FUN_00262cf0
0x111 FUN_00262cf0  # guess: FUN_00262cf0
0x112 FUN_00262d88  # guess: FUN_00262d88
0x113 FUN_00262db0  # guess: FUN_00262db0
0x114 FUN_00262dd8  # guess: FUN_00262dd8
0x115 FUN_00262f10  # guess: FUN_00262f10
0x116 FUN_002649a8  # guess: FUN_002649a8
0x117 FUN_002649a8  # guess: FUN_002649a8
0x118 FUN_002649a8  # guess: FUN_002649a8
0x119 FUN_00264a60  # guess: FUN_00264a60
0x11A FUN_00264a60  # guess: FUN_00264a60
0x11B FUN_00264a60  # guess: FUN_00264a60
0x11C FUN_00264b18  # guess: FUN_00264b18
0x11D FUN_00264b18  # guess: FUN_00264b18
0x11E FUN_00264b18  # guess: FUN_00264b18
0x11F FUN_00264be8  # guess: FUN_00264be8
0x120 FUN_00264be8  # guess: FUN_00264be8
0x121 FUN_00264be8  # guess: FUN_00264be8
0x122 FUN_00264c88  # guess: FUN_00264c88
0x123 FUN_00264c88  # guess: FUN_00264c88
0x124 FUN_00264c88  # guess: FUN_00264c88
0x125 FUN_00261330  # guess: FUN_00261330
0x126 FUN_00261330  # guess: FUN_00261330
0x127 FUN_002613d8  # guess: FUN_002613d8
0x128 FUN_002613d8  # guess: FUN_002613d8
0x129 FUN_00261500  # guess: FUN_00261500
0x12A FUN_00261530  # guess: FUN_00261530
0x12B FUN_00261570  # guess: FUN_00261570
0x12C FUN_002615b0  # guess: FUN_002615b0
0x12D FUN_002615d8  # guess: FUN_002615d8
0x12E FUN_00261600  # guess: FUN_00261600
0x12F FUN_00261638  # guess: FUN_00261638
0x130 FUN_00261670  # guess: FUN_00261670
0x131 FUN_002616b8  # guess: FUN_002616b8
0x132 FUN_00261700  # guess: FUN_00261700
0x133 FUN_00261760  # guess: FUN_00261760
0x134 FUN_002617c0  # guess: FUN_002617c0
0x135 FUN_002617e0  # guess: FUN_002617e0
0x136 FUN_00261808  # guess: FUN_00261808
0x137 FUN_00261868  # guess: FUN_00261868
0x138 FUN_002652d0  # guess: FUN_002652d0
0x139 FUN_00265350  # guess: FUN_00265350
0x13A FUN_00265378  # guess: FUN_00265378
0x13B FUN_002653a0  # guess: FUN_002653a0
0x13C FUN_002653e8  # guess: FUN_002653e8
0x13D FUN_00265410  # guess: FUN_00265410
0x13E LAB_00265468
0x13F FUN_002604a8  # guess: FUN_002604a8
0x140 FUN_00260578  # guess: FUN_00260578
0x141 FUN_00260578  # guess: FUN_00260578
0x142 FUN_002606d0  # guess: FUN_002606d0
0x143 FUN_00265478  # guess: FUN_00265478
0x144 FUN_002654f8  # guess: FUN_002654f8
0x145 FUN_00265620  # guess: FUN_00265620
0x146 FUN_00265738  # guess: FUN_00265738
0x147 FUN_00265760  # guess: FUN_00265760
0x148 LAB_00265788
0x149 FUN_00265790  # guess: FUN_00265790
0x14A FUN_002657b8  # guess: FUN_002657b8
```
