/*
 * Music and ambience: the SEQ path
 *
 * Original functions:
 *   FUN_00228e28  boot; builds the sound-effect cue table AND the three music
 *                 category tables, both out of SCR.BIN resource 199
 *   FUN_0025b2f0  scene load; copies the scene's eight music requests out of the
 *                 scene script header
 *   FUN_00206840  acts on those requests: load what changed, play what is marked
 *   FUN_00205938  load one slot: open its bank, hand section 2 to the sequencer,
 *                 set reverb, optionally start it
 *   FUN_00205d90  play a loaded slot          (script opcode 0x129)
 *   FUN_002063c8  ramp a slot's volume up     (script opcode 0x12A)
 *   FUN_00206260  ramp a slot's volume down   (script opcode 0x12B)
 *   FUN_00206048  set a slot's volume from the 0..1000 fader
 *   FUN_00206680  the per-scene-change reconciliation of the same eight slots
 *
 * ============================================================================
 * 1. Music is sequence data, not streamed audio
 * ============================================================================
 *
 * A SND.BIN bank resource has three sections (FUN_00205548). Section 0 is the
 * VAB body, section 1 the VAB header -- and section 2 is either the literal four
 * bytes "NSEQ", meaning there is no sequence here, or a real Sony SEQp chunk.
 *
 *   SND res   1  section 2 = "NSEQ" (16 bytes)   <- boot bank 0, sound effects
 *   SND res   2  section 2 = "NSEQ" (16 bytes)   <- boot bank 1
 *   SND res   3  section 2 = "NSEQ" (16 bytes)   <- boot bank 2
 *   SND res 112  section 2 = "SEQp" (64 bytes)   <- s01_e012's wind
 *   SND res 213  section 2 = "SEQp" (1056 bytes) <- s01_e012's Sephy cue
 *
 * The three banks FUN_00205118 loads at boot are exactly the three that carry no
 * sequence. So every note of every piece of music in the game lives in a section
 * that a sound-effect-only implementation never reads.
 *
 * ============================================================================
 * 2. Where a scene's music comes from
 * ============================================================================
 *
 * FUN_0025b2f0 is four lines that matter:
 *
 *   FUN_00267da0(0x31e678, *(int *)(DAT_00355058 + 0x28) + DAT_00355058, 0x10);
 *
 * DAT_00355058 is the scene script base, so this copies 16 bytes -- eight u16
 * requests -- from **scene script header word 10** into DAT_0031e678. The rest
 * of the function is a bounds check against DAT_00354c00, the per-category
 * record counts.
 *
 * FUN_00206840 then walks the eight:
 *
 *   category = min(slot, 2)
 *   index    = request & 0x7FFF        index into that category's table
 *   bit 15   = start it now
 *
 * Anything without bit 15 is loaded and left idle for a later opcode 0x129.
 *
 * s01_e012's table, at 0x396c:
 *
 *   slot 0  0x0001  cat 0 index   1  -> SND res   5  vol 55
 *   slot 1  0x000c  cat 1 index  12  -> SND res  66  vol 60
 *   slot 2  0x801a  cat 2 index  26  -> SND res 112  vol 70   PLAY
 *   slot 3  0x001b  cat 2 index  27  -> SND res 113  vol 75
 *   slot 4  0x001c  cat 2 index  28  -> SND res 114  vol 80
 *   slot 5  0x001d  cat 2 index  29  -> SND res 115  vol 75
 *   slot 6  0x007f  cat 2 index 127  -> SND res 213  vol 80
 *   slot 7  0x001f  cat 2 index  31  -> SND res 117  vol 60
 *
 * Slot 2 is the only one with bit 15, and it is the ambient wind that runs under
 * the whole scene. The scene's own script starts the others: opcode 0x129 at
 * blob offset 0x5e56 is `(slot 6, fader 1000)` -- SND resource 213, the piece
 * under Sephy's scene.
 *
 * ============================================================================
 * 3. The category tables
 * ============================================================================
 *
 * FUN_00228e28:89-117. SCR.BIN resource 199 header **word 7** (+0x1C) points at
 * three u32 offsets, one per category. Each is a run of eight-byte records
 * terminated by one whose first u16 is zero:
 *
 *   +0  u16  SND.BIN resource id
 *   +2  u8   volume, 0..127  (becomes DAT_003567b6/b7, the slot's base)
 *   +4  s16  reverb type, -1 for "leave it alone"
 *   +6  u16  reverb depth; FUN_00205938 masks it with 0xFFFE, and FUN_00206680
 *            reads **bit 0** separately as a flag
 *
 * Counts in the retail data: category 0 has 51 records, category 1 has 33,
 * category 2 has 201. Those match DAT_00354c00's bounds check exactly, and every
 * index s01_e012 asks for is inside its category.
 *
 * ============================================================================
 * 4. The volume model
 * ============================================================================
 *
 * FUN_00206048 is the whole thing:
 *
 *   +0xB4 = fader                         0..1000
 *   +0xB8 = fader * base_left  / 1000     what the sequencer gets, 0..127
 *   +0xBA = fader * base_right / 1000
 *
 * where base_left == base_right == the record's +2 byte. So a fader of 1000
 * means "this slot's authored volume", which is the 1000 that FUN_00206840:53
 * and opcode 0x129 both pass.
 *
 * FUN_002063c8 (up) and FUN_00206260 (down) ramp the fader toward a target, over
 * a frame count derived from the 0..127 delta rather than the fader delta:
 *
 *   step   = |current - target| * base / 1000
 *   scale  = speed >= 8 ? 1 : speed >= 4 ? 2 : speed >= 2 ? 3 : 4
 *   frames = step * scale, halved when speed > 13, then + 10
 *
 * ============================================================================
 * 5. The sequence format
 * ============================================================================
 *
 * **The meta encoding is not standard MIDI's.** A tempo change is
 *
 *     FF 51 <u24 microseconds-per-quarter>          -- no length byte
 *
 * where a .mid file would write `FF 51 03 <u24>`. Reading the first tempo byte
 * as a length desynchronises everything after it. Across all 283 sequences in
 * the game only two meta types occur at all -- `FF 2F` in 282 of them and
 * `FF 51` four times, **all four in SND resource 117** -- so anything else is a
 * parse failure rather than a meta to skip over.
 *
 * That single track is the one this was found on. Resource 117 is the cue under
 * Sephy's scene, 34.1 s long with a ritardando ending (50, 49, 46, 44, 42 BPM).
 * Its first tempo change sits at tick 875, about 21.9 s in, and the scene plays
 * the piece for 26.9 s -- so a standard-MIDI reading plays four fifths of it
 * correctly and then falls apart, which is exactly how it presents: the music
 * "stops early" and a note hangs, because the garbage that follows keys notes
 * on that never get their note-off.
 *
 * Standard Sony SEQp otherwise. Fifteen-byte big-endian header, then one MIDI
 * track:
 *
 *   +0x00  'SEQp'   (reads "pQES" byte by byte)
 *   +0x04  u32  version
 *   +0x08  u16  ppqn
 *   +0x0A  u24  microseconds per quarter note
 *   +0x0D  u16  rhythm
 *   +0x0F  delta-time / event pairs, with running status
 *
 * Looping is Sony's controller convention, not anything in the header:
 * CC99 = 20 is a loop start, CC99 = 30 a loop end, and CC6 carries the count
 * with 127 meaning forever.
 *
 * SND resource 112 in full -- this is the entire wind:
 *
 *   02  c0 00        program change ch0 -> program 0
 *   02  b0 07 7f     CC7  volume 127
 *   02     0a 40     CC10 pan 64
 *   02     0b 7f     CC11 expression 127
 *   02  90 3c 64     note on ch0, note 60, velocity 100
 *   81 3e            delta 190
 *       b0 63 14     CC99 = 20   loop start
 *   01     06 7f     CC6  = 127  forever
 *   86 01            delta 769
 *       63 1e        CC99 = 30   loop end
 *   00  ff 2f 00     end of track
 *
 * One held note over a looped waveform. The sustain comes from the PS-ADPCM
 * flag byte, not from the sequence: bit 2 marks the block a repeat returns to
 * and bit 1 says the end block repeats. A one-shot's terminator is 0x07, which
 * sets bit 1 *and* points the loop at its own final block -- the hardware's way
 * of spelling "stop", so a loop that starts inside the last block is not a loop.
 *
 * ============================================================================
 * 6. A VAB program layers -- every matching tone sounds
 * ============================================================================
 *
 * A program is a *set* of tones, and keying a note sounds **every** tone whose
 * note range covers it. Taking only the first is silently wrong, and this data
 * layers constantly:
 *
 *   SND res 112 program 0:  2 tones, both notes 0-120, pan 0 and pan 127
 *   SND res   2 (bank 1):   631 overlapping tone pairs
 *   SND res   3 (bank 2):     8 overlapping tone pairs
 *
 * Resource 112's pair is a **stereo recording split into two mono waveforms**,
 * one hard left and one hard right. Play only the first and the scene's ambient
 * wind comes out mono and hard-panned left -- measurably so: right-channel RMS
 * is exactly 0. It stops sounding like wind and starts sounding like a held
 * note, which is precisely how it gets reported as a stuck note.
 *
 * The EE side cannot tell you this: FUN_002057c8 sends one 0x4069 with a
 * program and a note and the IOP driver does the tone lookup. It is VAB
 * semantics, and the data only makes sense that way -- nobody authors a hard-
 * left and a hard-right tone over identical ranges unless both play.
 *
 * ============================================================================
 * 7. The scene-streamed sound effects share the music banks
 * ============================================================================
 *
 * A cue record's byte +7 is not a bank id. When it is non-zero FUN_002057c8:56
 * hands it to FUN_00205778, which searches the *music slot requests* --
 * DAT_00356a18, the shadow of the scene's own eight, **indices 2 through 7** --
 * for the one whose low 15 bits match, and returns `slot + 3`. That is the
 * bank array index, so the effect plays out of a music slot's VAB.
 *
 * So these are not a separate streaming system at all: a scene-streamed sound
 * effect lives in the same SND.BIN resource as one of the scene's pieces of
 * music. In s01_e012, cues 677..680 carry +7 = 127; slot 6 requests category-2
 * index 127; that is SND resource 213 -- the bank whose *sequence* is the cue
 * under Sephy's scene and whose **program 4** holds Volcan's sword draw, sheathe
 * and flourish on notes 60, 61, 62, plus a hard-panned stereo pair on 63.
 *
 * FUN_00205778 falls through to FUN_002683a8 (an error) when no loaded slot
 * matches, so a cue whose bank the scene did not stream is simply not playable.
 *
 * ============================================================================
 * 8. Pool slot 1 is the camera, and that is how a cue plays non-positionally
 * ============================================================================
 *
 * FUN_00267d38(cue, entity) branches on the entity pointer: non-zero plays
 * positionally through FUN_00267a80, zero plays flat at 0x7F/0x7F. The script
 * path never produces a null -- FUN_0025d6c0 always yields a pool address --
 * so "flat" is not how a script plays a non-positional sound.
 *
 * It aims at **slot 1** instead. FUN_00228e28:203-210 builds that slot at boot
 * as the camera entity: type 0xFFFF, position zeroed, +0x4C/+0x50 pinned to -60
 * so the floor never touches it. Those writes appear as DAT_0058c088,
 * DAT_0058c0a8..b0 and DAT_0058c0d4/d8 -- that slot's +0x00, +0x20..+0x28 and
 * +0x4C/+0x50.
 *
 * So DAT_0058c0a8, the listener FUN_00267a80 measures against, *is* slot 1's
 * position, and a cue on selector 1 plays at zero distance and full volume.
 * s01_e012 uses it for the storm's thunder (586/587) and Volcan's sword
 * (677/678/679). A save state taken during the establishing shot has slot 1 at
 * (0, 60, 0.5), exactly equal to DAT_0058c0a8 and to the on-screen cPOS.
 *
 * **The pool stride is 0x1D8, not 0xEC** -- FUN_0025d6c0's `* 0xEC` is on an
 * `undefined2 *`, so it counts halfwords. See
 * analyzed/entity_pool_and_descriptors.c, which has always said so.
 *
 * ============================================================================
 * 9. What is not transcribed
 * ============================================================================
 *
 * - FUN_0023baf8, which opcodes 0xDC and 0xDD reach, is an **empty stub** in the
 *   retail build. Those two opcodes do nothing at all. The dispatch-table files
 *   name them "audio_dispatch_*", which is misleading.
 * - FUN_0023bbd8 (opcode 0xDE) is not audio either despite its name: it is a
 *   four-channel timer over DAT_00571b50, parallel to the event scheduler.
 * - Reverb. FUN_00205938:90-113 sets an SPU2 reverb type and depth per slot
 *   through commands 0x7314 and 0x0A. Not ported; the sequences play dry.
 */
