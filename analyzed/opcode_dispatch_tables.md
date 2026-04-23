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
0x35 query_dialogue_stream_complete  # orig FUN_0025d748 — calls FUN_00237c70 to check if dialogue finished (iGpffffbcf4==8 && pcGpffffaec0=='\0'); see analyzed/ops/0x35_query_dialogue_stream_complete.c
0x36 script_read_flag_or_work_memory  # orig FUN_0025d768 — reads work array u32 (0x36) or flag bucket byte (0x38); see analyzed/script_read_flag_or_work_memory.c
0x37 variable_or_flag_alu  # orig FUN_0025d818 — ALU op on var array (0x37) or flag bucket (0x39)
0x38 script_read_flag_or_work_memory  # alias of 0x36; same handler branches by DAT_00355cd8
0x39 variable_or_flag_alu  # alias of 0x37; same handler chooses mode by sGpffffbd68
0x3A read_backup_map_coordinates_bcd  # orig FUN_0025dab8 — convert uGpffffae10/ae14 to BCD via FUN_0025da48, return combined 16-bit value (backup map coords); see analyzed/ops/0x3A_0x3B_0x3C_map_coordinate_opcodes.c
0x3B read_current_map_coordinates_bcd  # orig FUN_0025da78 — convert uGpffffb284/b280 to BCD via FUN_0025da48, return combined 16-bit value (current map coords); see analyzed/ops/0x3A_0x3B_0x3C_map_coordinate_opcodes.c
0x3C set_descriptor_counter  # orig FUN_0025daf8 — eval expr, store in uGpffffb298 (entity descriptor counter); see analyzed/ops/0x3A_0x3B_0x3C_map_coordinate_opcodes.c
0x3D FUN_0025e560  # name: modify_flag_state (query/set/clear/toggle cluster via prev opcode)
0x3E FUN_0025e560  # name: modify_flag_state
0x3F FUN_0025e560  # name: modify_flag_state
0x40 FUN_0025e560  # name: modify_flag_state
0x41 FUN_0025db20  # guess: FUN_0025db20
0x42 advance_timed_interpolation  # orig FUN_0025dd60 — shared with 0x44; see analyzed/ops/0x44_advance_timed_interpolation.c
0x43 build_and_submit_3way_vertex_streams  # orig FUN_0025de08 — reads 3 streams (xyz/uv/xyz), normalizes, submits
0x44 advance_timed_interpolation  # orig FUN_0025dd60 — shared with 0x42; see analyzed/ops/0x44_advance_timed_interpolation.c
0x45 submit_single_3d_point  # orig FUN_0025dfc8 — eval expr, call FUN_00217e18 to init graphics pipeline; see analyzed/ops/0x45_submit_single_3d_point.c
0x46 submit_dual_3d_coordinates  # orig FUN_0025dff0 — eval 6 exprs (2 xyz triplets), normalize by fGpffff8c04, call FUN_00217d70; see analyzed/ops/0x46_submit_dual_3d_coordinates.c
0x47 submit_single_3d_coordinate  # orig FUN_0025e0e8 — eval 3 exprs (xyz triplet), normalize by DAT_00352b78, call FUN_00217d40 (conditional on cGpffffb6e1==0x23); see analyzed/ops/0x47_submit_single_3d_coordinate.c
0x48 submit_triple_3d_coordinate  # orig FUN_0025e170 — eval 3 exprs (xyz triplet), normalize by DAT_00352b7c, call FUN_00217d10 (conditional on cGpffffb6e1==0x23, paired with 0x47 for camera position+target); see analyzed/ops/0x48_submit_triple_3d_coordinate.c
0x49 submit_entity_position_to_projection  # orig FUN_0025e1f8 — eval entity index, if <0x100 select from pool, read position, call FUN_00217d10; see analyzed/ops/0x49_0x4A_0x4B_0x4C_projection_system.c
0x4A init_projection_parameters  # orig FUN_0025e250 — read mode byte, eval 6 params, normalize by DAT_00352b80, store projection plane (DAT_005721b8/bc/c0) and distances (DAT_00355d0c/10/14), init graphics if mode!=0; see analyzed/ops/0x49_0x4A_0x4B_0x4C_projection_system.c
0x4B query_projection_intersection  # orig FUN_0025e420 — compute delta to listener, angle via atan2, ray-cast via FUN_002189b0, return true if distance < DAT_00355d10 (targeting/visibility test); see analyzed/ops/0x49_0x4A_0x4B_0x4C_projection_system.c
0x4C set_projection_distance  # orig FUN_0025e520 — eval expr, normalize by DAT_00352b8c, store in DAT_0035564c (global distance parameter); see analyzed/ops/0x49_0x4A_0x4B_0x4C_projection_system.c
0x4D process_resource_id_list  # orig FUN_0025e628 — read N 16-bit IDs from stream into temp arena, 0-terminate, call FUN_002661f8; see analyzed/ops/0x4D_process_resource_id_list.c
0x4E push_to_lookup_table  # orig FUN_0025e730 — eval 2 exprs + read 1 byte, store 12-byte entry in DAT_00571d00 table (max 16 entries, counter DAT_0035504c), error on overflow; used by 0x51 for ID lookups; see analyzed/ops/0x4E_push_to_lookup_table.c
0x4F process_pending_spawn_requests  # orig FUN_0025e7c0 — iterate pending list, load/resolve descriptors, init entities/effects
0x50 initialize_selected_entity_with_type  # orig FUN_0025eaf0 — save uGpffffb0d4, eval selector, read type byte, select entity via FUN_0025d6c0, init via FUN_00229c40; like 0x52 but operates on explicit selection; see analyzed/ops/0x50_initialize_selected_entity_with_type.c
0x51 set_pw_all_dispatch  # orig FUN_0025eb48 — "tbox" set_pw_all group dispatcher; reads mode from pbGpffffbd60 and spawns/configures entries; see analyzed/ops/0x51_set_pw_all_dispatch.c
0x52 spawn_entity_by_type  # orig FUN_0025edc8 — eval expr for type ID, if type!=0x55 call FUN_00265dc0(10,0xF6) to find free slot, init via FUN_00229c40; see analyzed/ops/0x52_spawn_entity_by_type.c
0x53 submit_entity_position_component  # orig FUN_0025ee08 — eval 2 exprs (idx,axis), select entity, read pos component (+0x20/24/28), scale by fGpffff8c34/38/3c, submit; see analyzed/ops/0x53_submit_entity_position_component.c
0x54 set_entity_position  # orig FUN_0025eeb0 — eval 4 exprs (idx,x,y,z), normalize by fGpffff8c40, call FUN_002662e0 to set entity pos (+0x20/24/28/4C); see analyzed/ops/0x54_0x55_set_entity_position.c
0x55 set_entity_position  # orig FUN_0025eeb0 — same as 0x54 but also calls FUN_00227070(x,y,entity) to calc terrain height, stores at entity+0x26; see analyzed/ops/0x54_0x55_set_entity_position.c
0x56 set_entity_scale  # orig FUN_0025efa8 — eval selector + scale, select entity, normalize scale by fGpffff8c44, set entity scale via FUN_00229ef0 (+0x14C/150); see analyzed/ops/0x56_set_entity_scale.c
0x57 configure_entity_animation_params  # orig FUN_0025f010 — eval selector + mode + value, select entity; if mode==0 set scale=1.0, else calc animation params at +0x140/144/148 using entity+0x54/58 and fGpffff8c48/4c/50; see analyzed/ops/0x57_configure_entity_animation_params.c
0x58 select_pw_slot_by_index  # orig FUN_0025f0d8 — sets DAT_00355044 = &DAT_0058beb0 + (expr*0xEC) if expr<0x100; see analyzed/ops/0x58_select_pw_slot_by_index.c
0x59 get_pw_slot_index  # orig FUN_0025f120 — returns selected pool slot index (or 0x100 if none); see analyzed/ops/0x59_get_pw_slot_index.c
0x5A select_pw_by_index  # orig FUN_0025f150 — selects the first active pool object with slot[+0x4C]==arg; sets DAT_00355044; returns 1/0; see analyzed/ops/0x5A_select_pw_by_index.c
0x5B FUN_0025f1c8  # guess: FUN_0025f1c8
0x5C destroy_entity_by_index  # orig FUN_0025f238 — eval index; if <0x100 select entity from pool, else use DAT_00355044; call FUN_00265ec0 to destroy; see analyzed/ops/0x5C_destroy_entity_by_index.c
0x5D add_velocity_to_entity_position  # orig FUN_0025f290 — eval selector + speed + angle, select entity, convert polar→cartesian (speed*cos/sin), add to entity[+0x30/34] (XZ position); see analyzed/ops/0x5D_add_velocity_to_entity_position.c
0x5E calculate_polar_component  # orig FUN_0025f380 — eval magnitude + angle, calc magnitude*cos(angle)*scale, submit result; see analyzed/ops/0x5E_0x5F_calculate_polar_component.c
0x5F calculate_polar_component  # orig FUN_0025f380 — eval magnitude + angle, calc magnitude*sin(angle)*scale, submit result; same handler as 0x5E, branches on opcode; see analyzed/ops/0x5E_0x5F_calculate_polar_component.c
0x60 calculate_3d_magnitude  # orig FUN_0025f428 — eval 3 exprs (x,y,z), normalize, calc sqrt(x²+y²+z²), scale, submit result; see analyzed/ops/0x60_calculate_3d_magnitude.c
0x61 test_controller_button_state  # orig FUN_0025f4b8 — eval expr (unused), read button flags byte (bits 0-6=mask, bit 7=port), test controller state, return bool; see analyzed/ops/0x61_test_controller_button_state.c
0x62 find_entity_by_id_in_secondary_pool  # orig FUN_0025f548 — eval target_id, search secondary pool (DAT_0058d120, 245 entities), match entity[+0x95], return slot index (80-324) or 0; see analyzed/ops/0x62_find_entity_by_id_in_secondary_pool.c
0x63 set_entity_tracking_and_position  # orig FUN_0025f5d8 — eval 2 selectors + mode + 3 pos coords, link tracker→target entities, set tracking mode (+0xA0/192/194), set position; see analyzed/ops/0x63_set_entity_tracking_and_position.c
0x64 FUN_0025f700   # name: update_object_transform_from_bone (tentative)
0x65 FUN_0025f7d8   # name: multi_call_feeder
0x66 FUN_0025f950  # convert_entity_to_party_member - Type 0x37→0x38 conversion, flag 0x4000, slot assignment at +0x130 → [analyzed/ops/0x66_convert_entity_to_party_member.c](../analyzed/ops/0x66_convert_entity_to_party_member.c)
0x67 FUN_0025fa40  # set_entity_target_position_with_rotation - Read 6 params (selector + mode + 4 coords), normalize 2 position values by fGpffff8c70, update entity tracking angle via FUN_00257c78 → [analyzed/ops/0x67_set_entity_target_position_with_rotation.c](../analyzed/ops/0x67_set_entity_target_position_with_rotation.c)
0x68 FUN_0025fae0  # check_entity_tracking_state - Read 3 params (selector unused), check if entity has active tracking on channels 1 or 2 via FUN_00257f18 → [analyzed/ops/0x68_check_entity_tracking_state.c](../analyzed/ops/0x68_check_entity_tracking_state.c)
0x69 FUN_0025fb30  # clear_entity_tracking_state - Read 3 params (selector unused), clear tracking channels 1 and 2 via FUN_00257f78, returns 0 → [analyzed/ops/0x69_clear_entity_tracking_state.c](../analyzed/ops/0x69_clear_entity_tracking_state.c)
0x6A FUN_0025fb80  # set_global_normalized_parameters - Read 3 int params, normalize by fGpffff8c74, apply sin(2*x)/sin(2.0) transform to param0, store to globals (uGpffffbd7c, fGpffffbd80, uGpffffb6e8=1.0, fGpffffb6ec) → [analyzed/ops/0x6A_set_global_normalized_parameters.c](../analyzed/ops/0x6A_set_global_normalized_parameters.c)
0x6B LAB_0025fc28  # step_global_parameter_toward_target - Frame-rate independent interpolation: step fGpffffb6e8 toward fGpffffbd7c by rate fGpffffbd80 * frame_time, return 1 when reached → [analyzed/ops/0x6B_step_global_parameter_toward_target.c](../analyzed/ops/0x6B_step_global_parameter_toward_target.c)
0x6C FUN_0025fca0  # set_global_parameters_simple - Read 2 int params, normalize by fGpffff8c78, apply sin(2*x)/sin(2.0) to param0, store to uGpffffb6e8 and fGpffffb6ec → [analyzed/ops/0x6C_set_global_parameters_simple.c](../analyzed/ops/0x6C_set_global_parameters_simple.c)
0x6D FUN_0025fd10  # control_character_ai_mode - Control main character AI/combat state, manages entity flags, timing params, character type checks → [analyzed/ops/0x6D_control_character_ai_mode.c](../analyzed/ops/0x6D_control_character_ai_mode.c)
0x6E FUN_0025fe98  # calculate_angle_between_points - Read 4 coords (x1,y1,x2,y2), normalize by fGpffff8c7c, compute atan2(dy,dx), transform and submit → [analyzed/ops/0x6E_calculate_angle_between_points.c](../analyzed/ops/0x6E_calculate_angle_between_points.c)
0x6F FUN_0025ff58  # calculate_distance_between_points - Read 4 coords (x1,y1,x2,y2), normalize by fGpffff8c80, compute sqrt(dx²+dy²), scale and submit → [analyzed/ops/0x6F_calculate_distance_between_points.c](../analyzed/ops/0x6F_calculate_distance_between_points.c)
0x70 submit_angle_to_target  # orig FUN_00260038 — selects object (index or direct), computes angle via atan2f wrapper FUN_0023a480, scales by fGpffff8c84, submits; see analyzed/ops/0x70_submit_angle_to_target.c
0x71 submit_distance_to_target  # orig FUN_00260080 — select object (idx or keep current), compute distance via FUN_0023a418(DAT_00355044), scale by DAT_00352bf8, submit; see analyzed/ops/0x71_submit_distance_to_target.c
0x72 FUN_002600c8  # name: lerp_wrap_and_submit — see analyzed/ops/0x72_lerp_wrap_and_submit.c
0x73 submit_wrapped_delta  # orig FUN_00260188 — delta = wrap((b/scale)-(a/scale)) * scale; submit; see analyzed/ops/0x73_submit_wrapped_delta.c
0x74 calculate_entity_distance  # orig FUN_002601f8 — read 2 entity selectors, compute horizontal distance sqrt(dx²+dz²) via FUN_0023a4e8, scale by DAT_00352c04, submit; shared handler with 0x75; see analyzed/ops/0x75_calculate_entity_distance_or_angle.c
0x75 calculate_entity_angle  # orig FUN_002601f8 — read 2 entity selectors, compute horizontal angle atan2(dz,dx) via FUN_0023a4b8, scale by DAT_00352c04, submit; shared handler with 0x74; see analyzed/ops/0x75_calculate_entity_distance_or_angle.c
0x76 FUN_00260318  # name: select_object_and_read_register
0x77 FUN_00260360  # name: modify_register_rmw (AND/OR/XOR/ADD/SUB family via read+op+write)
0x78 FUN_00260360  # name: modify_register_rmw
0x79 FUN_00260360  # name: modify_register_rmw
0x7A FUN_00260360  # name: modify_register_rmw
0x7B FUN_00260360  # name: modify_register_rmw
0x7C FUN_00260360  # name: modify_register_rmw
0x7D update_entity_timed_parameter  # orig FUN_00260738 — read entity index + param slot + value, scale by DAT_00352c08, store at entity[+0x3C + slot*4] as packed via FUN_00216690, set status flag 0x02; shared handler with 0x7E; see analyzed/update_entity_timed_parameter.c
0x7E update_entity_timed_parameter  # orig FUN_00260738 — read entity index + param slot + value, scale by DAT_00352c0c, store at entity[+0x48 + slot*4] as float, set status flag 0x01; shared handler with 0x7D; see analyzed/update_entity_timed_parameter.c
0x7F submit_param_from_model_axis  # orig FUN_00260880 — read modelIndex (expr) + axis (imm), pick 3-component field, scale, submit via FUN_0030bd20
0x80 submit_param_from_model_axis  # orig FUN_00260880 — same handler as 0x7F (alias)
0x81 update_entity_angle_offset_parameter  # orig FUN_00260958 — read entity index + slot + value + rate, compute cos(angle[+0x68]) + value/scale, store via FUN_00216690 to [+0x3C], update angle by rate*frame_delta, flag 0x02; shared with 0x82; see analyzed/ops/0x81_0x82_update_entity_angle_offset_parameter.c
0x82 update_entity_angle_offset_parameter  # orig FUN_00260958 — read entity index + slot + value + rate, compute cos(angle[+0x5C]) + value/scale, store via FUN_00216690 to [+0x48], update angle by rate*frame_delta, flag 0x01; shared with 0x81; see analyzed/ops/0x81_0x82_update_entity_angle_offset_parameter.c
0x83 set_entity_flags  # orig FUN_00260b60 — read entity index + flags (u16), write to entity[+0x02] (0x74 stride pool at iGpffffb770); see analyzed/ops/0x83_set_entity_flags.c
0x84 get_entity_flags  # orig FUN_00260bc8 — read entity index, return flags (u16) from entity[+0x02] (0x74 stride pool); see analyzed/ops/0x84_get_entity_flags.c
0x85 dispatch_rgb_event  # orig FUN_00260c20 — read event_id + rgb_flags (bits 1/2/4=R/G/B), pack RGB (0x00/0xFF per bit), dispatch via FUN_0025d1c0 with buffer=1; shared with 0x87; see analyzed/ops/0x85_0x87_dispatch_rgb_event.c
0x86 advance_fullscreen_fade  # orig FUN_00260ca0 — calls FUN_0025d238 to step and submit fullscreen fade; see analyzed/ops/0x86_advance_fullscreen_fade.c
0x87 dispatch_rgb_event  # orig FUN_00260c20 — read event_id + rgb_flags (bits 1/2/4=R/G/B), pack RGB (0x00/0xFF per bit), dispatch via FUN_0025d1c0 with buffer=0; shared with 0x85; see analyzed/ops/0x85_0x87_dispatch_rgb_event.c
0x88 trigger_fade_step  # orig FUN_00260cc0 — calls FUN_0025d2f8() with no params; wrapper for fade/transition stepper; see analyzed/ops/0x88_trigger_fade_step.c
0x89 dispatch_tagged_event  # orig FUN_00260ce0 — read 2 values + 1 byte, combine as value1|(value2<<24), call FUN_0025d0e0(tagged, byte) for tagged event dispatch; see analyzed/ops/0x89_dispatch_tagged_event.c
0x8A trigger_audio_with_mask_and_params  # orig FUN_00260d30 — read 5 params + mask, check DAT_0058bf1c & mask, gate on DAT_0058bebc, call FUN_0022b298; flags 0x10000/0x200000/0x02/0x10; returns 1 if triggered, 0 if blocked; see analyzed/ops/0x8A_trigger_audio_with_mask_and_params.c
0x8B trigger_positional_audio_with_coords  # orig FUN_00260e30 — read 7 params (5 audio + XYZ), check mask gating like 0x8A, normalize coords by fGpffff8cb4, call FUN_0022b2c0; returns 1 if triggered, 0 if blocked; see analyzed/ops/0x8B_trigger_positional_audio_with_coords.c
0x8C trigger_positional_audio_simple  # orig FUN_00260f78 — read 6 params (3 audio + XYZ), no mask/enable checks, normalize coords by fGpffff8cb8, call FUN_0022b2c0; flags 0x200000/0x02; returns 0 (void-like); see analyzed/ops/0x8C_trigger_positional_audio_simple.c
0x8D set_audio_mode_and_copy_defaults  # orig FUN_00261068 — set DAT_003551ec=0x40001, copy 12 bytes from 0x31e668→0x325340 via FUN_00267da0; audio system initialization; see analyzed/ops/0x8D_set_audio_mode_and_copy_defaults.c
0x8E set_audio_mode_with_param  # orig FUN_002610a8 — read 1 param, copy 12 bytes from 0x58bed0→0x31e668, store param to DAT_003551f8, set DAT_003551ec=0x20001; audio mode with listener config; see analyzed/ops/0x8E_set_audio_mode_with_param.c
0x8F get_frame_delta  # orig LAB_002610f8 — returns uGpffffb64c (frame time delta for frame-rate independent calculations); see analyzed/ops/0x8F_get_frame_delta.c
0x90 audio_init_param_ramp  # orig FUN_00261100 — eval 4 exprs (idx,target,current,step), normalize by DAT_00352c2c, write to DAT_00571de0[idx*0x0C], negate step if target<current; see analyzed/ops/0x90_audio_init_param_ramp.c
0x91 param_ramp_current_toward_target  # orig FUN_002611b8 — step DAT_00571de0[idx].current toward target by step * (DAT_003555bc/32)
0x92 audio_submit_current_param  # orig FUN_00261258 — submit DAT_00571de0[idx].current * DAT_00352c30 via FUN_0030bd20
0x93 scale_param_by_frame_delta_and_shift  # orig FUN_002612a0 — read param, multiply by DAT_003555bc, divide by 32 (>>5) with rounding; frame-rate independent scaling; see analyzed/ops/0x93_scale_param_by_frame_delta_and_shift.c
0x94 set_audio_position_normalized  # orig FUN_002612e0 — read 2 params, normalize first by DAT_00352c34, call FUN_0022dcf0(normalized, param1); audio positioning; see analyzed/ops/0x94_set_audio_position_normalized.c
0x95 get_absolute_value  # orig FUN_00261890 — call FUN_00216868(), return absolute value; see analyzed/ops/0x95_get_absolute_value.c
0x96 set_global_rgb_color  # orig FUN_002618c0 — read three values via evaluator, pack low bytes into uGpffffb6fc (0xRRGGBB); see analyzed/ops/0x96_set_global_rgb_color.c
0x97 set_3d_vector_with_rgb  # orig FUN_00261910 — read 6 params (XYZ + RGB), normalize XYZ by fGpffff8cd0, pack RGB to uGpffffb700, store vector at 0x3439c8, call FUN_00216510; see analyzed/ops/0x97_set_3d_vector_with_rgb.c
0x98 set_3d_vector_polar_with_rgb  # orig FUN_002619e0 — read 6 params (angle/magnitude/z + RGB), convert polar→cartesian, pack RGB, store at 0x3439c8, call FUN_00216510; see analyzed/ops/0x98_set_3d_vector_polar_with_rgb.c
0x99 set_3d_vector_simple  # orig FUN_00261af0 — read 3 params (XYZ), normalize by fGpffff8cd8, store at 0x343a08 (no RGB, no function call); see analyzed/ops/0x99_set_3d_vector_simple.c
0x9A dispatch_indexed_event_with_dual_rgb  # orig FUN_00261b80 — read 8 params (index + 2 RGB triplets + param), validate index<16, pack dual RGB, call FUN_0025d408; see analyzed/ops/0x9A_dispatch_indexed_event_with_dual_rgb.c
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
0xB8 set_camera_distance  # orig FUN_00263cb8 — eval expr, normalize by fGpffff8d44, set DAT_0032538c/fGpffffb6b8 (distance) and fGpffffb6bc (near plane = distance-5.0); see analyzed/ops/0xB8_set_camera_distance.c
0xB9 FUN_00263d10  # guess: FUN_00263d10
0xBA FUN_00263d60  # guess: FUN_00263d60
0xBB FUN_00263db0  # guess: FUN_00263db0
0xBC FUN_00263e30  # guess: FUN_00263e30
0xBD object_method_dispatch  # orig FUN_00263e80 — select obj (FUN_0025d6c0) then call FUN_00242a18(obj, method, arg0, arg1); see analyzed/ops/0xBD_object_method_dispatch.c
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
0xDE set_audio_channel_parameter  # orig FUN_00264f50 — eval 2 exprs (value,channel), normalize value by fGpffff8d5c, call FUN_0023bbd8 to set audio channel state; see analyzed/ops/0xDE_set_audio_channel_parameter.c
0xDF initialize_battle_logo  # orig FUN_00264fa0 — calls FUN_0025d5b8 to init battle logo entity at 0x58C7E8 (type 0x49); see analyzed/ops/0xDF_initialize_camera_entity.c
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
0x101 set_2d_params_normalized  # orig FUN_00262118 — read 3 params, normalize 2 by scale, call FUN_0021abc8; shared with 0x105/0x107, branches by opcode ID; see analyzed/ops/0x101_0x105_0x107_set_2d_params_normalized.c
0x102 set_quad_params_normalized_with_entity  # orig FUN_00262250 — read 8 params (4 coords + 3 values + entity index), normalize 4 coords, select entity from pool, call FUN_0021ac00; shared with 0x106/0x108; see analyzed/ops/0x102_0x106_0x108_set_quad_params_normalized_with_entity.c
0x103 set_single_normalized_param  # orig FUN_00262488 — read 2 params, normalize second by DAT_00352c64, call FUN_0021b480; see analyzed/ops/0x103_set_single_normalized_param.c
0x104 set_triple_normalized_params_with_entity  # orig FUN_002624d8 — read 7 params (3 coords + 3 values + entity index), normalize coords by DAT_00352c68, select entity, call FUN_0021b4b8; see analyzed/ops/0x104_set_triple_normalized_params_with_entity.c
0x105 set_2d_params_normalized  # orig FUN_00262118 — shared with 0x101, normalizes by DAT_00352c50, calls FUN_0021d410; see analyzed/ops/0x101_0x105_0x107_set_2d_params_normalized.c
0x106 set_quad_params_normalized_with_entity  # orig FUN_00262250 — shared with 0x102, normalizes by DAT_00352c5c, calls FUN_0021d448; see analyzed/ops/0x102_0x106_0x108_set_quad_params_normalized_with_entity.c
0x107 set_2d_params_normalized  # orig FUN_00262118 — shared with 0x101, normalizes by DAT_00352c54, calls FUN_0021c710; see analyzed/ops/0x101_0x105_0x107_set_2d_params_normalized.c
0x108 set_quad_params_normalized_with_entity  # orig FUN_00262250 — shared with 0x102, normalizes by DAT_00352c60, calls FUN_0021c748; see analyzed/ops/0x102_0x106_0x108_set_quad_params_normalized_with_entity.c
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
