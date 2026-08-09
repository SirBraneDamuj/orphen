/*
 * Sound effects: from a cue number to a waveform.
 *
 * Originals: FUN_00267d38 (0x00267d38)  what behaviours call
 *            FUN_00267a80 (0x00267a80)  distance attenuation and panning
 *            FUN_002057c8 (0x002057c8)  the cue table and the volume scaling
 *            FUN_00206128 (0x00206128)  the negative-cue path, a sequence cue
 *            FUN_00228e28 (0x00228e28)  loads the cue table, SCR.BIN res 199
 *            FUN_00205118 (0x00205118)  loads the banks, SND.BIN res 1/2/3
 *            FUN_00205548 (0x00205548)  splits a bank into its three sections
 *            FUN_00205310 (0x00205310)  uploads them and opens the VAB
 *
 * ---------------------------------------------------------------------------
 * Where the work happens
 * ---------------------------------------------------------------------------
 *
 * The EE does not mix anything. Every one of these functions ends at a SIF
 * command to the IOP -- FUN_00204c50, FUN_00204ca8, FUN_00204d10, FUN_00204d88
 * -- and the IOP runs a libsnd-based driver that owns SPU2. So the whole EE
 * side of "play a sound" is: look a cue up, work out two channel volumes, and
 * send
 *
 *     FUN_00204d88(0x4069, (vabId << 8) | program, note << 8, volLeft, volRight)
 *
 * The 0x40xx commands map onto libsnd one for one: 0x4063 opens a VAB header,
 * 0x4066 transfers its body, 0x4069 is a key-on, 0x4031/0x4041/0x4042/0x4043
 * drive sequences.
 *
 * ---------------------------------------------------------------------------
 * The cue table
 * ---------------------------------------------------------------------------
 *
 * FUN_00228e28:81 loads SCR.BIN resource 199, decompresses it, and copies a
 * block out of it to the heap:
 *
 *     FUN_00223268(1, 199, 0x1849A00);
 *     FUN_002F3118(0x1849A00, 0x1859A00);            // decompress
 *     DAT_00354D60 = *(u32 *)(0x1859A00 + *(u32 *)0x1859A28);        // bytes
 *     DAT_00354BF0 = heap;
 *     memcpy(heap, 0x1859A04 + *(u32 *)0x1859A28, DAT_00354D60);
 *     DAT_00354D60 >>= 3;                            // -> 711 records
 *
 * so the dword at resource offset 0x28 points at a length-prefixed array of
 * 8-byte records. Only five bytes are read anywhere:
 *
 *     +0  bank        logical bank 0..3, used when +7 is zero
 *     +1  program     VAB program within that bank
 *     +2  note        MIDI note
 *     +3  volume      scale applied to both channels
 *     +5  cap         ceiling on the scaled volume, 0 for none
 *     +7  alternate   non-zero: FUN_00205778 picks a scene-streamed bank
 *
 * The same resource also carries three tables of 8-byte records at the dwords
 * +0x1C, +0x20 and +0x24, which FUN_00205938 uses to swap banks per scene.
 *
 * ---------------------------------------------------------------------------
 * The banks
 * ---------------------------------------------------------------------------
 *
 * FUN_00205118 opens three at boot:
 *
 *     FUN_00205548(buf, 1);  FUN_00205310(buf, 0, DAT_00355A1C);      // bank 0
 *     FUN_00205548(buf, 2);  FUN_00205310(buf, 1, DAT_003567BC);      // bank 1
 *     FUN_00205548(buf, 3);  FUN_00205310(buf, 2, DAT_003567E8);      // bank 2
 *
 * Bank 0 is SND.BIN resource 1, and it is the one every common sound effect
 * lives in. FUN_00205288 additionally preloads SND resources 0x85 and 0x86
 * into two EE buffers that survive, which is why those two -- and only those
 * two -- are readable whole in an EE dump.
 *
 * A bank resource is a 16-byte header and three sections; see
 * port/src/ported/sound/original_sound_bank.h for the offset encoding, which
 * Ghidra renders misleadingly. Section 0 is a Sony VAB body, section 1 the VAB
 * header, section 2 sequence data. **They are not compressed**: FUN_00223268
 * only DMAs sectors, and unlike every other caller FUN_00205548 does not run
 * FUN_002F3118 over the result.
 *
 * DAT_003567B0 + n * 0x2C is the per-bank record. Its +0x24 is the runtime VAB
 * id FUN_00204ca8(0x4063, ...) returned, and that is the value FUN_002057c8
 * shifts into the key-on. In the s01_e024 dump banks 0..3 hold ids 0..3 and
 * their header lengths -- +0x14 -- are 9760, 7200, 18464 and 6688, which is
 * 14, 9, 31 and 8 programs against the VAB layout. The cue table never asks
 * bank 0 for a program above 13.
 *
 * ---------------------------------------------------------------------------
 * FUN_00267a80: where the sound is
 * ---------------------------------------------------------------------------
 *
 * FUN_00267d38(cue, entity) is the wrapper behaviours call. With an entity it
 * runs FUN_00267a80(entity.x, entity.y, entity.z, cue, 100); with a null one it
 * goes straight to FUN_002057c8 at 0x7F/0x7F. Dozens of one-line wrappers exist
 * purely to bake in a cue number -- FUN_002d59e0 is `FUN_00267d38(0x9F, e)`.
 *
 * The listener is the **camera**, DAT_0058C0A8, not the player:
 *
 *     d3    = |entity - eye|;            if (d3 >= 14) return;        // silent
 *     level = trunc((14 - d3) * 128 / 14) * scale / 100;
 *     flat  = |entity.xy - eye.xy|;
 *     near  = max(0, trunc((3 - flat) * 100 / 3));  if (level < near) near = level;
 *     pan   = flat > 0.1 ? trunc((cos(2 * rel) - 1) * 40) : 0;   rel > 0 -> negate
 *     left  = (pan + 110) * level / 128;
 *     right = (110 - pan) * level / 128;
 *     volL  = left  >= near ? min(left, 127)  : near;
 *     volR  = right >= near ? min(right, 127) : near;
 *
 * where `rel` is the angle from the camera's yaw to the sound. `near` is a
 * floor rather than a ceiling: inside three units a sound stops panning away
 * entirely, which is what keeps a cue on the player audible in both ears while
 * the camera swings.
 *
 * FUN_002057c8 then scales by the record and the master volume DAT_003555D5:
 *
 *     out = channel * record[+3] * DAT_003555D5 / 0x4000;    if (cap) out = min(out, cap);
 *
 * and before any of it, polls 22 voice slots through FUN_00204c50(0x6418, ...)
 * and drops the cue if none is free.
 *
 * ---------------------------------------------------------------------------
 * Footsteps, and the surface table
 * ---------------------------------------------------------------------------
 *
 * There is no footstep function. There is an *animation event* and a lookup:
 *
 *     FUN_00256ff8(entity, running)                        // the whole thing
 *       if (!(entity[+0xAA] & 0x100) || !(entity[+0x06] & 8)) return;
 *       if (entity[+0x04] & 0x1000)
 *         FUN_00267d38(FUN_00255d88(entity, running), entity);
 *       FUN_00257098(entity, (entity[+0xAA] & 0x200) ? 9 : 8, ...);   // dust
 *
 * +0xAA is the trailing word of the animation keyframe the entity just stepped
 * onto and +0x06 bit 3 is "it stepped this frame", so the sound is authored
 * into the animation. In grp_0001:
 *
 *     animation 0x0B (walk)  4 keyframes of 14 frames; [1] = 0x0300, [3] = 0x0100
 *     animation 0x0E (run)  10 keyframes of 4-5;       [4] = 0x0100, [9] = 0x0300
 *
 * -- two plants per cycle either way, with 0x200 telling the two apart. That
 * second bit only picks the dust effect; both feet make the same sound.
 *
 * +0x04 bit 0x1000 is the audible gate. FUN_0022a418:204 sets it on pool slot 0
 * and nothing sets it anywhere else, so **only the lead player has footsteps** --
 * which is why FUN_00256ff8 is called from player states and nowhere else. In
 * the s01_e024 dump slot 0's +0x04 is 0x3024 and the party members' is 0x00A4.
 *
 *     FUN_00255d88(entity, kind)
 *       material = entity[+0x68] ? 2
 *                : entity[+0x0A] >= 0 ? D[entity[+0x0A] & 0x3FFF].word1 >> 28
 *                : FUN_00227798(...) >= 0 ? that record's nibble : 7;
 *       if (material == 0xD) material = 3;
 *       return FUN_00251c80(entity, DAT_0031E028[material][kind]);
 *
 * so the material is the **top nibble of the collision record's word 1** -- the
 * same word the port already carries as `terrainFlags`. DAT_0031E028 is eight
 * rows of four sound indices:
 *
 *     material 0   0x00 0x04 0x08 0x0C        material 4   0x16 0x17 0x18 0x19
 *     material 1   0x01 0x05 0x09 0x0D        material 5   0x1A 0x1B 0x1C 0x1D
 *     material 2   0x02 0x06 0x0A 0x0E        material 6   0x1E 0x1F 0x20 0x21
 *     material 3   0x03 0x07 0x0B 0x0F        material 7   0    0    0    0
 *
 * and it stops there: 0x0031E0E8 is the player state table and the two rows
 * before it are unrelated floats. Material 7 is the no-ground substitution, so
 * the zero row is deliberate; materials 8 and up would read past the end, which
 * the game apparently never does.
 *
 * The kinds are the columns. FUN_00256ff8 passes the run flag, so 0 is a
 * walking step and 1 a running one; FUN_00256bb8's jump branch passes 2, on
 * takeoff rather than landing.
 *
 *     FUN_00251c80(entity, index)
 *       class = FUN_002298d0(entity.typeId);
 *       return index + (class == 4 ? 0x6F : class == 5 ? 0x0F : 0x3F);
 *
 * Orphen is type 1, class 0, base 0x3F -- so on material 0 he walks with cue
 * 63, runs with 67 and jumps with 71. Their waveforms are 0.14, 0.13 and 0.55
 * seconds, which is the shape you would expect.
 *
 * ---------------------------------------------------------------------------
 * The flying enemies
 * ---------------------------------------------------------------------------
 *
 * Type 0x62's wrapper, FUN_002cd0a0, ends with
 *
 *     if (!(entity[+0x08] & 1) && entity[+0x60] != 6) {
 *       FUN_002cdb28(entity);                                    // the wings
 *       if (entity[+0x1C6] == 0) entity[+0x1C6] = (FUN_00216868() & 7) + 0x18;
 *       if (DAT_003555B4 % entity[+0x1C6] == 0) FUN_002cde50(entity);   // 0x196
 *     }
 *
 * so the buzz is one cue retriggered on a period the entity rolls once, 24 to
 * 31 frames. The waveform is 1.3 seconds long, so the repeats overlap into a
 * drone rather than a pulse, and because the test is against the *global* frame
 * counter rather than a per-entity timer, a group of them beats against itself.
 * FUN_002cde40 (0x197) is the death cry, fired when the stagger timer runs out.
 *
 * ---------------------------------------------------------------------------
 * What the chest cutscene plays
 * ---------------------------------------------------------------------------
 *
 * Two cues, and no others in the whole 0x0C..0x15 state range:
 *
 *   cue 0x9F  FUN_002d59e0(chest), from FUN_002d1ea8 on animation 5's event
 *             keyframe -- the lid coming up. Bank 0, program 11, note 60.
 *   cue 8     FUN_00257b10(player), at 0x00255240, on the frame state 0x11
 *             raises the item caption. Bank 0, program 0, note 67.
 *
 * Bank 0's programs are **key split**: every tone has min == max, so the note
 * chooses the waveform. Program 11 note 60 is waveform 13 and program 0 note 67
 * is waveform 54. Their centre notes are 86 and 81, which puts them at 11027 Hz
 * and 22055 Hz against the SPU2's 48 kHz pitch base -- the centre note is how a
 * VAB records a sample rate.
 *
 * The pitch is
 *
 *     rate = 48000 * 2^(((note - centre) * 128 + shift) / (12 * 128))
 *
 * and the sign on `shift` is the part worth pinning down, because VagAtr's
 * field reads like a fine tune *on* the centre note, which would subtract it
 * and land a semitone flat. It does not: the natural pitch is
 * `centre - shift/128`. Two checks, both against data rather than the spec --
 * with it added, 86 of the 280 resolvable cues land within 1% of a standard
 * authoring rate against 9 the other way; and five of the SPU2 voice pitch
 * registers in a PCSX2 savestate come out *exactly*, register value for
 * register value, against none. 0x075A and 0x03AD are two of them, which are
 * the 22050 and 11025 of cues 8 and 159.
 *
 * Checked end to end: SND.BIN resource 1's body section is byte-identical to
 * the 241504 bytes at SPU2 RAM 0x19000 in a PCSX2 savestate, and its header is
 * identical to the copy at IOP RAM 0x93900 apart from the ProgAtr block, which
 * libsnd rewrites when it opens a VAB. 0x93900 is also what DAT_00355A1C holds
 * in the EE dump, so the EE's "SPU address" for a bank is really an IOP one.
 */

/* Signatures, for reference; bodies are in src/. */

void FUN_00267d38(long cue, long entity);
void FUN_00267a80(float x, float y, float z, long cue, long scale);
unsigned long FUN_002057c8(int cue, int volLeft, int volRight);
void FUN_00205548(unsigned int *sections, long sndResource);
unsigned int FUN_00205310(unsigned int *sections, unsigned long bank, int spuAddress);
