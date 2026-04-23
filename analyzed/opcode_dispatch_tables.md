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
0x41 submit_vertex_stream_generic  # orig FUN_0025db20 — see [analyzed/ops/0x41_submit_vertex_stream_generic.c](../analyzed/ops/0x41_submit_vertex_stream_generic.c)
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
0x5B read_slot_field_by_index  # orig FUN_0025f1c8 — see [analyzed/ops/0x5B_read_slot_field_by_index.c](../analyzed/ops/0x5B_read_slot_field_by_index.c)
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
0x91 audio_ramp_current_toward_target  # orig FUN_002611b8 — step DAT_00571de0[idx].current toward target by step * (DAT_003555bc/32); see [analyzed/ops/0x91_audio_ramp_current_toward_target.c](../analyzed/ops/0x91_audio_ramp_current_toward_target.c)
0x92 audio_submit_current_param  # orig FUN_00261258 — submit DAT_00571de0[idx].current * DAT_00352c30 via FUN_0030bd20
0x93 scale_param_by_frame_delta_and_shift  # orig FUN_002612a0 — read param, multiply by DAT_003555bc, divide by 32 (>>5) with rounding; frame-rate independent scaling; see analyzed/ops/0x93_scale_param_by_frame_delta_and_shift.c
0x94 set_audio_position_normalized  # orig FUN_002612e0 — read 2 params, normalize first by DAT_00352c34, call FUN_0022dcf0(normalized, param1); audio positioning; see analyzed/ops/0x94_set_audio_position_normalized.c
0x95 get_absolute_value  # orig FUN_00261890 — call FUN_00216868(), return absolute value; see analyzed/ops/0x95_get_absolute_value.c
0x96 set_global_rgb_color  # orig FUN_002618c0 — read three values via evaluator, pack low bytes into uGpffffb6fc (0xRRGGBB); see analyzed/ops/0x96_set_global_rgb_color.c
0x97 set_3d_vector_with_rgb  # orig FUN_00261910 — read 6 params (XYZ + RGB), normalize XYZ by fGpffff8cd0, pack RGB to uGpffffb700, store vector at 0x3439c8, call FUN_00216510; see analyzed/ops/0x97_set_3d_vector_with_rgb.c
0x98 set_3d_vector_polar_with_rgb  # orig FUN_002619e0 — read 6 params (angle/magnitude/z + RGB), convert polar→cartesian, pack RGB, store at 0x3439c8, call FUN_00216510; see analyzed/ops/0x98_set_3d_vector_polar_with_rgb.c
0x99 set_3d_vector_simple  # orig FUN_00261af0 — read 3 params (XYZ), normalize by fGpffff8cd8, store at 0x343a08 (no RGB, no function call); see analyzed/ops/0x99_set_3d_vector_simple.c
0x9A dispatch_indexed_event_with_dual_rgb  # orig FUN_00261b80 — read 8 params (index + 2 RGB triplets + param), validate index<16, pack dual RGB, call FUN_0025d408; see analyzed/ops/0x9A_dispatch_indexed_event_with_dual_rgb.c
0x9B advance_fade_track  # orig FUN_00261c38 — see [analyzed/ops/0x9B_0x9C_fade_track.c](../analyzed/ops/0x9B_0x9C_fade_track.c)
0x9C configure_fade_slot  # orig FUN_00261c60 — see [analyzed/ops/0x9B_0x9C_fade_track.c](../analyzed/ops/0x9B_0x9C_fade_track.c)
0x9D set_slot_pointer_from_stream_offset  # orig FUN_00261cb8 — slot[idx] = iGpffffb0e8 + next_u32(); idx from expr; bounds 0x40
0x9E finish_process_slot  # orig FUN_00261d18 — clears slot[sel]; sel<0 => current slot (uGpffffbd88)
0x9F slot_is_occupied  # orig FUN_00261d88 — script_slot_table[idx]!=0; see [analyzed/ops/0x9F_slot_is_occupied.c](../analyzed/ops/0x9F_slot_is_occupied.c)
0xA0 find_free_slot  # orig FUN_00261de0 — first zero-entry in script_slot_table; see [analyzed/ops/0xA0_find_free_slot.c](../analyzed/ops/0xA0_find_free_slot.c)
0xA1 set_coroutine_code_ptr  # orig FUN_00261e30 — arm coroutine slot at DAT_00571e40; see [analyzed/ops/0xA1_set_coroutine_code_ptr.c](../analyzed/ops/0xA1_set_coroutine_code_ptr.c)
0xA2 clear_coroutine_code_ptr  # orig FUN_00261ea8 — disarm coroutine slot; see [analyzed/ops/0xA2_clear_coroutine_code_ptr.c](../analyzed/ops/0xA2_clear_coroutine_code_ptr.c)
0xA3 read_coroutine_aux  # orig FUN_00261f08 — read aux word of coroutine slot; see [analyzed/ops/0xA3_read_coroutine_aux.c](../analyzed/ops/0xA3_read_coroutine_aux.c)
0xA4 audio_submit  # orig FUN_00261f60 — calls audio_submit_type_A(expr, byte, 0x20); shared with 0xA6; see [analyzed/ops/0xA4_0xA6_audio_submit.c](../analyzed/ops/0xA4_0xA6_audio_submit.c)
0xA5 audio_submit_triple  # orig FUN_00262058 — expr+byte+expr → audio_submit_three; see [analyzed/ops/0xA5_audio_submit_triple.c](../analyzed/ops/0xA5_audio_submit_triple.c)
0xA6 audio_submit  # orig FUN_00261f60 — calls audio_submit_type_B(expr, byte, 0x800); shared with 0xA4; see [analyzed/ops/0xA4_0xA6_audio_submit.c](../analyzed/ops/0xA4_0xA6_audio_submit.c)
0xA7 tag_entities_by_mask  # orig FUN_00261fd8 — overwrite top nibble of entity flag word for matching mask; see [analyzed/ops/0xA7_tag_entities_by_mask.c](../analyzed/ops/0xA7_tag_entities_by_mask.c)
0xA8 register_lead_boot_handler  # orig FUN_00262f38 — install lead boot vector at script_slot_table[0x40]; see [analyzed/ops/0xA8_register_lead_boot_handler.c](../analyzed/ops/0xA8_register_lead_boot_handler.c)
0xA9 step_lead_toward_xy  # orig FUN_00262f80 — drive lead toward target XY (walk/run); see [analyzed/ops/0xA9_step_lead_toward_xy.c](../analyzed/ops/0xA9_step_lead_toward_xy.c)
0xAA abort_lead_motion  # orig FUN_00263118 — cancel lead motion + clear boot vector; see [analyzed/ops/0xAA_abort_lead_motion.c](../analyzed/ops/0xAA_abort_lead_motion.c)
0xAB teleport_lead_to_xyz  # orig FUN_00263148 — teleport lead+camera; mode=0 detaches camera; see [analyzed/ops/0xAB_teleport_lead_to_xyz.c](../analyzed/ops/0xAB_teleport_lead_to_xyz.c)
0xAC attach_entity_to_party_slot  # orig FUN_002631f0 — bind/unbind entity to party slot; see [analyzed/ops/0xAC_attach_entity_to_party_slot.c](../analyzed/ops/0xAC_attach_entity_to_party_slot.c)
0xAD speak_for_party_slot  # orig FUN_00263498 — see [analyzed/ops/0xAD_speak_for_party_slot.c](../analyzed/ops/0xAD_speak_for_party_slot.c)
0xAE speak_and_flag_party_slot  # orig FUN_00263518 — see [analyzed/ops/0xAE_speak_and_flag_party_slot.c](../analyzed/ops/0xAE_speak_and_flag_party_slot.c)
0xAF select_party_slot_as_current  # orig FUN_002635c0 — sets DAT_00355044 to chosen party member; see [analyzed/ops/0xAF_select_party_slot_as_current.c](../analyzed/ops/0xAF_select_party_slot_as_current.c)
0xB0 swap_lead_with_selected  # orig FUN_00263630 — swap lead and selected entity state; see [analyzed/ops/0xB0_swap_lead_with_selected.c](../analyzed/ops/0xB0_swap_lead_with_selected.c)
0xB1 bind_entity_action  # orig FUN_00263878 — anim_bind_action + flag bit 0x10; see [analyzed/ops/0xB1_bind_entity_action.c](../analyzed/ops/0xB1_bind_entity_action.c)
0xB2 set_entity_anim_rate  # orig FUN_002638d8 — overwrite anim snapshot rate at +0x10; see [analyzed/ops/0xB2_set_entity_anim_rate.c](../analyzed/ops/0xB2_set_entity_anim_rate.c)
0xB3 set_entity_anim_xyz_rate  # orig FUN_00263978 — write 3 floats (rx/ry/rz) into snapshot; see [analyzed/ops/0xB3_set_entity_anim_xyz_rate.c](../analyzed/ops/0xB3_set_entity_anim_xyz_rate.c)
0xB4 set_entity_anim_channel  # orig FUN_00263a58 — write single float at snapshot[channel]; see [analyzed/ops/0xB4_set_entity_anim_channel.c](../analyzed/ops/0xB4_set_entity_anim_channel.c)
0xB5 sample_bone_to_work_memory  # orig FUN_00263b18 — see [analyzed/ops/0xB5_sample_bone_to_work_memory.c](../analyzed/ops/0xB5_sample_bone_to_work_memory.c)
0xB6 clear_entity_anim_slot  # orig FUN_00263c10 — see [analyzed/ops/0xB6_clear_entity_anim_slot.c](../analyzed/ops/0xB6_clear_entity_anim_slot.c)
0xB7 set_entity_short_and_word  # orig FUN_00263c58 — see [analyzed/ops/0xB7_set_entity_short_and_word.c](../analyzed/ops/0xB7_set_entity_short_and_word.c)
0xB8 set_camera_distance  # orig FUN_00263cb8 — eval expr, normalize by fGpffff8d44, set DAT_0032538c/fGpffffb6b8 (distance) and fGpffffb6bc (near plane = distance-5.0); see analyzed/ops/0xB8_set_camera_distance.c
0xB9 set_global_color1_rgb  # orig FUN_00263d10 — see [analyzed/ops/0xB9_set_global_color1_rgb.c](../analyzed/ops/0xB9_set_global_color1_rgb.c)
0xBA set_global_color2_rgb  # orig FUN_00263d60 — see [analyzed/ops/0xBA_set_global_color2_rgb.c](../analyzed/ops/0xBA_set_global_color2_rgb.c)
0xBB set_fade_radius_pair  # orig FUN_00263db0 — see [analyzed/ops/0xBB_set_fade_radius_pair.c](../analyzed/ops/0xBB_set_fade_radius_pair.c)
0xBC increment_event_counter_capped  # orig FUN_00263e30 — see [analyzed/ops/0xBC_increment_event_counter_capped.c](../analyzed/ops/0xBC_increment_event_counter_capped.c)
0xBD object_method_dispatch  # orig FUN_00263e80 — select obj (FUN_0025d6c0) then call FUN_00242a18(obj, method, arg0, arg1); see analyzed/ops/0xBD_object_method_dispatch.c
0xBE call_function_table_entry  # orig FUN_00263ee0 — dispatch PTR_FUN_0031e730[index](arg)
0xBF allocate_light_slot  # orig FUN_00263f28 — shared with 0xC0; see [analyzed/ops/0xBF_0xC0_allocate_light_slot.c](../analyzed/ops/0xBF_0xC0_allocate_light_slot.c)
0xC0 allocate_light_slot  # orig FUN_00263f28 — shared with 0xBF; see [analyzed/ops/0xBF_0xC0_allocate_light_slot.c](../analyzed/ops/0xBF_0xC0_allocate_light_slot.c)
0xC1 bind_light_to_entity_bone  # orig FUN_00263fe8 — see [analyzed/ops/0xC1_bind_light_to_entity_bone.c](../analyzed/ops/0xC1_bind_light_to_entity_bone.c)
0xC2 set_light_alpha  # orig FUN_00264148 — see [analyzed/ops/0xC2_set_light_alpha.c](../analyzed/ops/0xC2_set_light_alpha.c)
0xC3 set_light_rgb  # orig FUN_00264190 — see [analyzed/ops/0xC3_set_light_rgb.c](../analyzed/ops/0xC3_set_light_rgb.c)
0xC4 set_light_intensity  # orig FUN_00264218 — see [analyzed/ops/0xC4_set_light_intensity.c](../analyzed/ops/0xC4_set_light_intensity.c)
0xC5 set_light_position_xyz  # orig FUN_00264298 — see [analyzed/ops/0xC5_set_light_position_xyz.c](../analyzed/ops/0xC5_set_light_position_xyz.c)
0xC6 copy_entity_position_to_light  # orig FUN_00264360 — see [analyzed/ops/0xC6_copy_entity_position_to_light.c](../analyzed/ops/0xC6_copy_entity_position_to_light.c)
0xC7 clear_light_intensity  # orig FUN_002643f0 — see [analyzed/ops/0xC7_clear_light_intensity.c](../analyzed/ops/0xC7_clear_light_intensity.c)
0xC8 set_text_color_index  # orig FUN_00264448 — see [analyzed/ops/0xC8_set_text_color_index.c](../analyzed/ops/0xC8_set_text_color_index.c)
0xC9 set_text_color_index_and_palette  # orig FUN_00264470 — see [analyzed/ops/0xC9_set_text_color_index_and_palette.c](../analyzed/ops/0xC9_set_text_color_index_and_palette.c)
0xCA set_dialogue_speaker_byte  # orig FUN_00264500 — see [analyzed/ops/0xCA_set_dialogue_speaker_byte.c](../analyzed/ops/0xCA_set_dialogue_speaker_byte.c)
0xCB find_entity_by_visible_flag_mask  # orig FUN_00264528 — see [analyzed/ops/0xCB_find_entity_by_visible_flag_mask.c](../analyzed/ops/0xCB_find_entity_by_visible_flag_mask.c)
0xCC get_lead_terrain_flags  # orig FUN_002645b8 — see [analyzed/ops/0xCC_get_lead_terrain_flags.c](../analyzed/ops/0xCC_get_lead_terrain_flags.c)
0xCD sample_motion_keyframe_into_entity  # orig FUN_00264628 — see [analyzed/ops/0xCD_sample_motion_keyframe_into_entity.c](../analyzed/ops/0xCD_sample_motion_keyframe_into_entity.c)
0xCE entity_misc_op_a  # orig FUN_00264700 — see [analyzed/ops/0xCE_entity_misc_op_a.c](../analyzed/ops/0xCE_entity_misc_op_a.c)
0xCF entity_apply_extra_resource  # orig FUN_00264740 — see [analyzed/ops/0xCF_entity_apply_extra_resource.c](../analyzed/ops/0xCF_entity_apply_extra_resource.c)
0xD0 entity_misc_op_b  # orig FUN_00264790 — see [analyzed/ops/0xD0_entity_misc_op_b.c](../analyzed/ops/0xD0_entity_misc_op_b.c)
0xD1 bind_entity_to_extra_resource_once  # orig FUN_002647d0 — see [analyzed/ops/0xD1_bind_entity_to_extra_resource_once.c](../analyzed/ops/0xD1_bind_entity_to_extra_resource_once.c)
0xD2 read_inline_uint_le  # orig FUN_00264858 — see [analyzed/ops/0xD2_read_inline_uint_le.c](../analyzed/ops/0xD2_read_inline_uint_le.c)
0xD3 write_inline_uint_le  # orig FUN_002648f8 — see [analyzed/ops/0xD3_write_inline_uint_le.c](../analyzed/ops/0xD3_write_inline_uint_le.c)
0xD4 query_status_bit_0x40  # orig LAB_00264998 — returns (DAT_003555fa & 0x40)
0xD5 set_renderer_byte_a  # orig FUN_00264d40 — see [analyzed/ops/0xD5_set_renderer_byte_a.c](../analyzed/ops/0xD5_set_renderer_byte_a.c)
0xD6 set_renderer_byte_b  # orig FUN_00264d68 — see [analyzed/ops/0xD6_set_renderer_byte_b.c](../analyzed/ops/0xD6_set_renderer_byte_b.c)
0xD7 set_palette_slot_byte  # orig FUN_00264d90 — see [analyzed/ops/0xD7_set_palette_slot_byte.c](../analyzed/ops/0xD7_set_palette_slot_byte.c)
0xD8 set_global_code_pointer_and_word  # orig FUN_00264de8 — see [analyzed/ops/0xD8_set_global_code_pointer_and_word.c](../analyzed/ops/0xD8_set_global_code_pointer_and_word.c)
0xD9 register_entity_callback  # orig FUN_00264e30 — see [analyzed/ops/0xD9_register_entity_callback.c](../analyzed/ops/0xD9_register_entity_callback.c)
0xDA set_global_byte_b6f0  # orig FUN_00264ea0 — see [analyzed/ops/0xDA_set_global_byte_b6f0.c](../analyzed/ops/0xDA_set_global_byte_b6f0.c)
0xDB invoke_save_or_resource_op  # orig FUN_00264ec8 — see [analyzed/ops/0xDB_invoke_save_or_resource_op.c](../analyzed/ops/0xDB_invoke_save_or_resource_op.c)
0xDC audio_dispatch_with_id  # orig FUN_00264ee8 — see [analyzed/ops/0xDC_audio_dispatch_with_id.c](../analyzed/ops/0xDC_audio_dispatch_with_id.c)
0xDD audio_dispatch_with_two_params  # orig FUN_00264f18 — see [analyzed/ops/0xDD_audio_dispatch_with_two_params.c](../analyzed/ops/0xDD_audio_dispatch_with_two_params.c)
0xDE set_audio_channel_parameter  # orig FUN_00264f50 — eval 2 exprs (value,channel), normalize value by fGpffff8d5c, call FUN_0023bbd8 to set audio channel state; see analyzed/ops/0xDE_set_audio_channel_parameter.c
0xDF initialize_battle_logo  # orig FUN_00264fa0 — calls FUN_0025d5b8 to init battle logo entity at 0x58C7E8 (type 0x49); see analyzed/ops/0xDF_initialize_camera_entity.c
0xE0 destroy_battle_logo  # orig FUN_00264fc0 — see [analyzed/ops/0xE0_destroy_battle_logo.c](../analyzed/ops/0xE0_destroy_battle_logo.c)
0xE1 boot_party_for_battle  # orig FUN_00265000 — see [analyzed/ops/0xE1_boot_party_for_battle.c](../analyzed/ops/0xE1_boot_party_for_battle.c)
0xE2 set_global_float_3556fc  # orig FUN_002650e0 — see [analyzed/ops/0xE2_set_global_float_3556fc.c](../analyzed/ops/0xE2_set_global_float_3556fc.c)
0xE3 set_global_byte_355641  # orig FUN_00265120 — see [analyzed/ops/0xE3_set_global_byte_355641.c](../analyzed/ops/0xE3_set_global_byte_355641.c)
0xE4 stage_swizzled_word_stream_upload  # orig FUN_00265148 — copy words into swizzled bank buffer and schedule DMA (FUN_00210b60)
0xE5 emit_audio_pair_with_inline_byte  # orig FUN_002651a0 — see [analyzed/ops/0xE5_emit_audio_pair_with_inline_byte.c](../analyzed/ops/0xE5_emit_audio_pair_with_inline_byte.c)
0xE6 set_minimap_marker_slot  # orig FUN_00265200 — see [analyzed/ops/0xE6_set_minimap_marker_slot.c](../analyzed/ops/0xE6_set_minimap_marker_slot.c)
0xE7 get_or_set_global_short_355064  # orig FUN_00265290 — shared with 0xE8; see [analyzed/ops/0xE7_0xE8_get_or_set_global_short_355064.c](../analyzed/ops/0xE7_0xE8_get_or_set_global_short_355064.c)
0xE8 get_or_set_global_short_355064  # orig FUN_00265290 — shared with 0xE7; see [analyzed/ops/0xE7_0xE8_get_or_set_global_short_355064.c](../analyzed/ops/0xE7_0xE8_get_or_set_global_short_355064.c)
0xE9 npc_consume_step_byte_1cc  # orig FUN_00265d88 — read+clear focus_npc[+0x1CC]; see [analyzed/ops/0xE9_npc_consume_step_byte_1cc.c](../analyzed/ops/0xE9_npc_consume_step_byte_1cc.c)
0xEA npc_get_focus_pool_index  # orig FUN_00265818 — (focus_npc-entity_pool_base)*inv(0xEC*8)>>3; see [analyzed/ops/0xEA_npc_get_focus_pool_index.c](../analyzed/ops/0xEA_npc_get_focus_pool_index.c)
0xEB set_npc_focus_entity  # orig FUN_00265840 — see [analyzed/ops/0xEB_set_npc_focus_entity.c](../analyzed/ops/0xEB_set_npc_focus_entity.c)
0xEC set_npc_step_byte  # orig FUN_00265880 — see [analyzed/ops/0xEC_set_npc_step_byte.c](../analyzed/ops/0xEC_set_npc_step_byte.c)
0xED npc_get_step_byte  # orig FUN_002658b0 — read focus_npc[+0x1BC] (counterpart to 0xEC writer); see [analyzed/ops/0xED_npc_get_step_byte.c](../analyzed/ops/0xED_npc_get_step_byte.c)
0xEE npc_step_toward_xy  # orig FUN_002658c0 — shared with 0xEF/0xF0/0xF1; see [analyzed/ops/0xEE_0xEF_0xF0_0xF1_npc_step_toward_xy.c](../analyzed/ops/0xEE_0xEF_0xF0_0xF1_npc_step_toward_xy.c)
0xEF npc_step_toward_xy  # orig FUN_002658c0 — shared with 0xEE/0xF0/0xF1; see [analyzed/ops/0xEE_0xEF_0xF0_0xF1_npc_step_toward_xy.c](../analyzed/ops/0xEE_0xEF_0xF0_0xF1_npc_step_toward_xy.c)
0xF0 npc_step_toward_xy  # orig FUN_002658c0 — shared with 0xEE/0xEF/0xF1; see [analyzed/ops/0xEE_0xEF_0xF0_0xF1_npc_step_toward_xy.c](../analyzed/ops/0xEE_0xEF_0xF0_0xF1_npc_step_toward_xy.c)
0xF1 npc_step_toward_xy  # orig FUN_002658c0 — shared with 0xEE/0xEF/0xF0; see [analyzed/ops/0xEE_0xEF_0xF0_0xF1_npc_step_toward_xy.c](../analyzed/ops/0xEE_0xEF_0xF0_0xF1_npc_step_toward_xy.c)
0xF2 npc_anim_slot_for_duration  # orig FUN_00265b90 — see [analyzed/ops/0xF2_npc_anim_slot_for_duration.c](../analyzed/ops/0xF2_npc_anim_slot_for_duration.c)
0xF3 npc_set_anim_until_active  # orig FUN_00265c30 — see [analyzed/ops/0xF3_npc_set_anim_until_active.c](../analyzed/ops/0xF3_npc_set_anim_until_active.c)
0xF4 npc_rotate_toward_angle  # orig FUN_00265cb0 — see [analyzed/ops/0xF4_npc_rotate_toward_angle.c](../analyzed/ops/0xF4_npc_rotate_toward_angle.c)
0xF5 npc_promote_state_if_0x38  # orig FUN_00265d98 — if focus_npc.state==0x38, replace with focus_npc[+0x1CE]; see [analyzed/ops/0xF5_npc_promote_state_if_0x38.c](../analyzed/ops/0xF5_npc_promote_state_if_0x38.c)
```

## Extended Dispatch Table (PTR_LAB_0031e538)

Extended opcode = 0x100 + index byte following 0xFF.

```
0x100 clear_global_param_by_index  # orig FUN_002620a8 — inline byte selects which of uGpffffad38..ad4c (1–6) to zero; see [analyzed/ops/0x100_clear_global_param_by_index.c](../analyzed/ops/0x100_clear_global_param_by_index.c)
0x101 set_2d_params_normalized  # orig FUN_00262118 — read 3 params, normalize 2 by scale, call FUN_0021abc8; shared with 0x105/0x107, branches by opcode ID; see analyzed/ops/0x101_0x105_0x107_set_2d_params_normalized.c
0x102 set_quad_params_normalized_with_entity  # orig FUN_00262250 — read 8 params (4 coords + 3 values + entity index), normalize 4 coords, select entity from pool, call FUN_0021ac00; shared with 0x106/0x108; see analyzed/ops/0x102_0x106_0x108_set_quad_params_normalized_with_entity.c
0x103 set_single_normalized_param  # orig FUN_00262488 — read 2 params, normalize second by DAT_00352c64, call FUN_0021b480; see analyzed/ops/0x103_set_single_normalized_param.c
0x104 set_triple_normalized_params_with_entity  # orig FUN_002624d8 — read 7 params (3 coords + 3 values + entity index), normalize coords by DAT_00352c68, select entity, call FUN_0021b4b8; see analyzed/ops/0x104_set_triple_normalized_params_with_entity.c
0x105 set_2d_params_normalized  # orig FUN_00262118 — shared with 0x101, normalizes by DAT_00352c50, calls FUN_0021d410; see analyzed/ops/0x101_0x105_0x107_set_2d_params_normalized.c
0x106 set_quad_params_normalized_with_entity  # orig FUN_00262250 — shared with 0x102, normalizes by DAT_00352c5c, calls FUN_0021d448; see analyzed/ops/0x102_0x106_0x108_set_quad_params_normalized_with_entity.c
0x107 set_2d_params_normalized  # orig FUN_00262118 — shared with 0x101, normalizes by DAT_00352c54, calls FUN_0021c710; see analyzed/ops/0x101_0x105_0x107_set_2d_params_normalized.c
0x108 set_quad_params_normalized_with_entity  # orig FUN_00262250 — shared with 0x102, normalizes by DAT_00352c60, calls FUN_0021c748; see analyzed/ops/0x102_0x106_0x108_set_quad_params_normalized_with_entity.c
0x109 submit_sprite_5xyz_uv  # orig FUN_002625b8 — 5 coords + uv, scale DAT_00352c6c; see [analyzed/ops/0x109_submit_sprite_5xyz_uv.c](../analyzed/ops/0x109_submit_sprite_5xyz_uv.c)
0x10A submit_six_coords_2params  # orig FUN_00262690 — scale DAT_00352c70 → FUN_00219fc8; see [analyzed/ops/0x10A_submit_six_coords_2params.c](../analyzed/ops/0x10A_submit_six_coords_2params.c)
0x10B submit_seven_coords_3params  # orig FUN_00262780 — scale DAT_00352c74 → FUN_002198a0; see [analyzed/ops/0x10B_submit_seven_coords_3params.c](../analyzed/ops/0x10B_submit_seven_coords_3params.c)
0x10C submit_seven_coords_4params  # orig FUN_00262898 — scale fGpffff8d08 → FUN_00219d60; see [analyzed/ops/0x10C_submit_seven_coords_4params.c](../analyzed/ops/0x10C_submit_seven_coords_4params.c)
0x10D submit_scaled_param_block  # orig FUN_002629c0 — parses 9 args, scales first four by fGpffff8d0c, calls FUN_0021e088 (see analyzed/ops/0x10D_submit_scaled_param_block.c)
0x10E submit_five_coords_5params  # orig FUN_00262a98 — scale fGpffff8d10 → FUN_0021f6e8; see [analyzed/ops/0x10E_submit_five_coords_5params.c](../analyzed/ops/0x10E_submit_five_coords_5params.c)
0x10F submit_eight_coords_6params  # orig FUN_00262b90 — scale fGpffff8d14 → FUN_0021ed50; see [analyzed/ops/0x10F_submit_eight_coords_6params.c](../analyzed/ops/0x10F_submit_eight_coords_6params.c)
0x110 submit_single_scaled_coord_2words  # orig FUN_00262cf0 — shared with 0x111; see [analyzed/ops/0x110_0x111_submit_single_scaled_coord_2words.c](../analyzed/ops/0x110_0x111_submit_single_scaled_coord_2words.c)
0x111 submit_single_scaled_coord_2words  # orig FUN_00262cf0 — shared with 0x110; see [analyzed/ops/0x110_0x111_submit_single_scaled_coord_2words.c](../analyzed/ops/0x110_0x111_submit_single_scaled_coord_2words.c)
0x112 set_global_uGpffffad50  # orig FUN_00262d88 — see [analyzed/ops/0x112_set_global_uGpffffad50.c](../analyzed/ops/0x112_set_global_uGpffffad50.c)
0x113 set_global_uGpffffad54  # orig FUN_00262db0 — see [analyzed/ops/0x113_set_global_uGpffffad54.c](../analyzed/ops/0x113_set_global_uGpffffad54.c)
0x114 submit_eight_coords_pair_word  # orig FUN_00262dd8 — scale fGpffff8d20 → FUN_00220f70; see [analyzed/ops/0x114_submit_eight_coords_pair_word.c](../analyzed/ops/0x114_submit_eight_coords_pair_word.c)
0x115 submit_simple_byte_param  # orig FUN_00262f10 — → FUN_002218f0; see [analyzed/ops/0x115_submit_simple_byte_param.c](../analyzed/ops/0x115_submit_simple_byte_param.c)
0x116 entity_set_pair_a  # orig FUN_002649a8 — shared with 0x117/0x118; see [analyzed/ops/0x116_0x117_0x118_entity_set_pair_a.c](../analyzed/ops/0x116_0x117_0x118_entity_set_pair_a.c)
0x117 entity_set_pair_a  # orig FUN_002649a8 — shared with 0x116/0x118 (uGpffffb788 target); see [analyzed/ops/0x116_0x117_0x118_entity_set_pair_a.c](../analyzed/ops/0x116_0x117_0x118_entity_set_pair_a.c)
0x118 entity_set_pair_a  # orig FUN_002649a8 — shared with 0x116/0x117 (DAT_00345a2c target); see [analyzed/ops/0x116_0x117_0x118_entity_set_pair_a.c](../analyzed/ops/0x116_0x117_0x118_entity_set_pair_a.c)
0x119 entity_set_pair_b  # orig FUN_00264a60 — shared with 0x11A/0x11B; see [analyzed/ops/0x119_0x11A_0x11B_entity_set_pair_b.c](../analyzed/ops/0x119_0x11A_0x11B_entity_set_pair_b.c)
0x11A entity_set_pair_b  # orig FUN_00264a60 — shared with 0x119/0x11B; see [analyzed/ops/0x119_0x11A_0x11B_entity_set_pair_b.c](../analyzed/ops/0x119_0x11A_0x11B_entity_set_pair_b.c)
0x11B entity_set_pair_b  # orig FUN_00264a60 — shared with 0x119/0x11A; see [analyzed/ops/0x119_0x11A_0x11B_entity_set_pair_b.c](../analyzed/ops/0x119_0x11A_0x11B_entity_set_pair_b.c)
0x11C entity_set_triplet  # orig FUN_00264b18 — shared with 0x11D/0x11E; see [analyzed/ops/0x11C_0x11D_0x11E_entity_set_triplet.c](../analyzed/ops/0x11C_0x11D_0x11E_entity_set_triplet.c)
0x11D entity_set_triplet  # orig FUN_00264b18 — shared with 0x11C/0x11E; see [analyzed/ops/0x11C_0x11D_0x11E_entity_set_triplet.c](../analyzed/ops/0x11C_0x11D_0x11E_entity_set_triplet.c)
0x11E entity_set_triplet  # orig FUN_00264b18 — shared with 0x11C/0x11D; see [analyzed/ops/0x11C_0x11D_0x11E_entity_set_triplet.c](../analyzed/ops/0x11C_0x11D_0x11E_entity_set_triplet.c)
0x11F entity_set_single  # orig FUN_00264be8 — shared with 0x120/0x121; see [analyzed/ops/0x11F_0x120_0x121_entity_set_single.c](../analyzed/ops/0x11F_0x120_0x121_entity_set_single.c)
0x120 entity_set_single  # orig FUN_00264be8 — shared with 0x11F/0x121; see [analyzed/ops/0x11F_0x120_0x121_entity_set_single.c](../analyzed/ops/0x11F_0x120_0x121_entity_set_single.c)
0x121 entity_set_single  # orig FUN_00264be8 — shared with 0x11F/0x120; see [analyzed/ops/0x11F_0x120_0x121_entity_set_single.c](../analyzed/ops/0x11F_0x120_0x121_entity_set_single.c)
0x122 entity_set_quad  # orig FUN_00264c88 — shared with 0x123/0x124; see [analyzed/ops/0x122_0x123_0x124_entity_set_quad.c](../analyzed/ops/0x122_0x123_0x124_entity_set_quad.c)
0x123 entity_set_quad  # orig FUN_00264c88 — shared with 0x122/0x124; see [analyzed/ops/0x122_0x123_0x124_entity_set_quad.c](../analyzed/ops/0x122_0x123_0x124_entity_set_quad.c)
0x124 entity_set_quad  # orig FUN_00264c88 — shared with 0x122/0x123; see [analyzed/ops/0x122_0x123_0x124_entity_set_quad.c](../analyzed/ops/0x122_0x123_0x124_entity_set_quad.c)
0x125 audio_play_for_entity  # orig FUN_00261330 — shared with 0x126; see [analyzed/ops/0x125_0x126_audio_play_for_entity.c](../analyzed/ops/0x125_0x126_audio_play_for_entity.c)
0x126 audio_play_for_entity  # orig FUN_00261330 — shared with 0x125; see [analyzed/ops/0x125_0x126_audio_play_for_entity.c](../analyzed/ops/0x125_0x126_audio_play_for_entity.c)
0x127 audio_play_positional  # orig FUN_002613d8 — shared with 0x128; see [analyzed/ops/0x127_0x128_audio_play_positional.c](../analyzed/ops/0x127_0x128_audio_play_positional.c)
0x128 audio_play_positional  # orig FUN_002613d8 — shared with 0x127; see [analyzed/ops/0x127_0x128_audio_play_positional.c](../analyzed/ops/0x127_0x128_audio_play_positional.c)
0x129 audio_dispatch_205d90  # orig FUN_00261500 — see [analyzed/ops/0x129_audio_dispatch_205d90.c](../analyzed/ops/0x129_audio_dispatch_205d90.c)
0x12A audio_dispatch_2063c8  # orig FUN_00261530 — see [analyzed/ops/0x12A_audio_dispatch_2063c8.c](../analyzed/ops/0x12A_audio_dispatch_2063c8.c)
0x12B audio_dispatch_206260  # orig FUN_00261570 — see [analyzed/ops/0x12B_audio_dispatch_206260.c](../analyzed/ops/0x12B_audio_dispatch_206260.c)
0x12C audio_dispatch_206238  # orig FUN_002615b0 — see [analyzed/ops/0x12C_audio_dispatch_206238.c](../analyzed/ops/0x12C_audio_dispatch_206238.c)
0x12D audio_dispatch_205f40  # orig FUN_002615d8 — see [analyzed/ops/0x12D_audio_dispatch_205f40.c](../analyzed/ops/0x12D_audio_dispatch_205f40.c)
0x12E audio_dispatch_205f98  # orig FUN_00261600 — see [analyzed/ops/0x12E_audio_dispatch_205f98.c](../analyzed/ops/0x12E_audio_dispatch_205f98.c)
0x12F audio_dispatch_2060a8  # orig FUN_00261638 — see [analyzed/ops/0x12F_audio_dispatch_2060a8.c](../analyzed/ops/0x12F_audio_dispatch_2060a8.c)
0x130 set_audio_byte_3555d5  # orig FUN_00261670 — clamp >0x80 (diag); see [analyzed/ops/0x130_set_audio_byte_3555d5.c](../analyzed/ops/0x130_set_audio_byte_3555d5.c)
0x131 set_audio_byte_3555d6  # orig FUN_002616b8 — clamp >0x80 (diag); see [analyzed/ops/0x131_set_audio_byte_3555d6.c](../analyzed/ops/0x131_set_audio_byte_3555d6.c)
0x132 audio_call_with_offset_mode1  # orig FUN_00261700 — → FUN_00206ae0; see [analyzed/ops/0x132_audio_call_with_offset_mode1.c](../analyzed/ops/0x132_audio_call_with_offset_mode1.c)
0x133 audio_call_with_offset_mode0  # orig FUN_00261760 — → FUN_00206ae0; see [analyzed/ops/0x133_audio_call_with_offset_mode0.c](../analyzed/ops/0x133_audio_call_with_offset_mode0.c)
0x134 audio_call_no_args  # orig FUN_002617c0 — → FUN_00206c28; see [analyzed/ops/0x134_audio_call_no_args.c](../analyzed/ops/0x134_audio_call_no_args.c)
0x135 audio_set_envelope_word  # orig FUN_002617e0 — → FUN_00206d98; see [analyzed/ops/0x135_audio_set_envelope_word.c](../analyzed/ops/0x135_audio_set_envelope_word.c)
0x136 audio_set_envelope_word_with_byte  # orig FUN_00261808 — stores uGpffffb667 + envelope; see [analyzed/ops/0x136_audio_set_envelope_word_with_byte.c](../analyzed/ops/0x136_audio_set_envelope_word_with_byte.c)
0x137 audio_call_no_args_b  # orig FUN_00261868 — → FUN_00206a90; see [analyzed/ops/0x137_audio_call_no_args_b.c](../analyzed/ops/0x137_audio_call_no_args_b.c)
0x138 submit_eight_words  # orig FUN_002652d0 — → FUN_00202f00; see [analyzed/ops/0x138_submit_eight_words.c](../analyzed/ops/0x138_submit_eight_words.c)
0x139 submit_single_word_a  # orig FUN_00265350 — → FUN_00202f88; see [analyzed/ops/0x139_submit_single_word_a.c](../analyzed/ops/0x139_submit_single_word_a.c)
0x13A set_global_byte_3555d2  # orig FUN_00265378 — see [analyzed/ops/0x13A_set_global_byte_3555d2.c](../analyzed/ops/0x13A_set_global_byte_3555d2.c)
0x13B set_global_byte_355657_clamped  # orig FUN_002653a0 — clamp >6 (diag); see [analyzed/ops/0x13B_set_global_byte_355657_clamped.c](../analyzed/ops/0x13B_set_global_byte_355657_clamped.c)
0x13C set_global_byte_355655  # orig FUN_002653e8 — see [analyzed/ops/0x13C_set_global_byte_355655.c](../analyzed/ops/0x13C_set_global_byte_355655.c)
0x13D entity_set_short_field  # orig FUN_00265410 — → FUN_00225bc8; see [analyzed/ops/0x13D_entity_set_short_field.c](../analyzed/ops/0x13D_entity_set_short_field.c)
0x13E set_global_byte_b655_ff  # orig FUN_00265468 — uGpffffb655 = 0xFF; see [analyzed/ops/0x13E_set_global_byte_b655_ff.c](../analyzed/ops/0x13E_set_global_byte_b655_ff.c)
0x13F entity_compute_pool_indices  # orig FUN_002604a8 — type==0x28 only; recovers pool indices via -0x5f75270d; see [analyzed/ops/0x13F_entity_compute_pool_indices.c](../analyzed/ops/0x13F_entity_compute_pool_indices.c)
0x140 entity_spawn_chain  # orig FUN_00260578 — shared with 0x141; see [analyzed/ops/0x140_0x141_entity_spawn_chain.c](../analyzed/ops/0x140_0x141_entity_spawn_chain.c)
0x141 entity_spawn_chain  # orig FUN_00260578 — shared with 0x140; see [analyzed/ops/0x140_0x141_entity_spawn_chain.c](../analyzed/ops/0x140_0x141_entity_spawn_chain.c)
0x142 entity_remove_actions  # orig FUN_002606d0 — see [analyzed/ops/0x142_entity_remove_actions.c](../analyzed/ops/0x142_entity_remove_actions.c)
0x143 submit_three_coords_scaled  # orig FUN_00265478 — scale DAT_00352cd8 → FUN_002186e0; see [analyzed/ops/0x143_submit_three_coords_scaled.c](../analyzed/ops/0x143_submit_three_coords_scaled.c)
0x144 load_path_buffer_from_script  # orig FUN_002654f8 — fills 0x01849A00 → floats 0x01889A00; diag if n>0x10; see [analyzed/ops/0x144_load_path_buffer_from_script.c](../analyzed/ops/0x144_load_path_buffer_from_script.c)
0x145 sample_path_position_to_workmem  # orig FUN_00265620 — → FUN_00266ce8 + FUN_0030bd20; see [analyzed/ops/0x145_sample_path_position_to_workmem.c](../analyzed/ops/0x145_sample_path_position_to_workmem.c)
0x146 submit_single_word_b  # orig FUN_00265738 — → FUN_00213640; see [analyzed/ops/0x146_submit_single_word_b.c](../analyzed/ops/0x146_submit_single_word_b.c)
0x147 set_global_byte_3555c4  # orig FUN_00265760 — see [analyzed/ops/0x147_set_global_byte_3555c4.c](../analyzed/ops/0x147_set_global_byte_3555c4.c)
0x148 get_global_byte_b6e4  # orig FUN_00265788 — returns uGpffffb6e4; see [analyzed/ops/0x148_get_global_byte_b6e4.c](../analyzed/ops/0x148_get_global_byte_b6e4.c)
0x149 set_global_byte_355656  # orig FUN_00265790 — see [analyzed/ops/0x149_set_global_byte_355656.c](../analyzed/ops/0x149_set_global_byte_355656.c)
0x14A submit_two_coords_scaled  # orig FUN_002657b8 — scale DAT_00352ce0 → FUN_0023ae60; see [analyzed/ops/0x14A_submit_two_coords_scaled.c](../analyzed/ops/0x14A_submit_two_coords_scaled.c)
```
