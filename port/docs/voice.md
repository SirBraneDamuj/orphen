# Voice

`VOICE.BIN` lines: how the dialogue stream starts and waits on them, how the
table and clips are read, and the multi-clip banks spells use. The voice opcodes
in the scene script are text control codes, not `0xBD` methods -- an early
reading had `0xBD` `0x70`/`0x72` as a voice handshake, and they are waypoint
path-follow (see [Scene script](scene-script.md)).

## Why cutscenes keep time

A line of dialogue holds for exactly as long as its voice clip. That is not an
inference from the pacing — the record says so, in control codes whose handlers
tail-call the audio system:

```
13 <name> 00      speaker
17                wait out the load the previous record started
16 ch w id32      cache the clip for the NEXT record, on the other channel
18 ch             start the clip cached for THIS record   -> j FUN_00206d98
<text>
1a                block until DAT_00356788 clears           -> FUN_00206a90
```

The stream **double-buffers**: a record's `0x16` arms the clip the *next* record
will speak, alternating between channels 0 and 1, so what a record plays was
armed a record earlier. `eeMemory.bin` was captured during `s01_e012`'s opening
and `DAT_00356480` reads `{50, 79, 0}` there — channel 1 holds what the record
on screen is speaking, channel 0 what the next one will. Walking all 83 records
of `scr2.out` with the real control widths, every one has exactly one `0x18` and
exactly one `0x1A`, and the channel it starts is always already armed.

`0x18` was documented in `analyzed/text_ops/` as a "conditional control byte
set" that consumed a byte and returned. It does consume the byte, and then:

```
00239a64: j     0x00206d98        ; FUN_00206d98(channel)
00239a68: daddu a0, a2, zero      ; delay slot: a0 = the operand byte
```

Two instructions were missing from the earlier reading, and they were the ones
that mattered. The file is now `text_op_18_start_voice_line.c`.

## The table

`VOICE.BIN`'s own first sectors:

```
word 0   entry count (3310)
word i   (sector << 15) | (sizeBytes >> 4)     -- data at sector * 2048
```

**This is not the flat-archive packing.** `FlatBinArchive` splits its entries 15
bits of sector over 17 bits of size-in-words; this is the other way round.
Reading one with the other's shifts gives plausible-looking offsets, so
`VoiceIndex` checks rather than assumes: the entries must tile the file without
overlapping. They do, and they end at 149,237,200 bytes against a 149,237,760
byte file.

`FUN_00221b90`'s bootstrap looks circular and is worth spelling out. It writes a
*fake* entry 0 of `1` into the empty buffer, which decodes to "sector 0, 16
bytes", and reads that — landing the file's first word, the entry count, in the
buffer. It rewrites that as `(count + 4) >> 2`, the whole table's length in
16-byte units, and reads again. Two reads, no table needed to find the table.

The table also sits in both EE dumps, since the game loads it at boot and parks
the pointer in `piGpffffbc30` (`0x00355BA0`). `VoiceIndex::loadFromEeDump` reads
it from there, so a disc root missing the 142 MiB archive still gets exact
timing and only loses the audio. `--voice-index <path>` overrides the search.

## Playback

Clips are raw SPU ADPCM, 16-byte blocks, no VAG header, played at the rate
`FUN_00207010`'s pitch register asks for: `0x760 / 0x1000 * 48000` = **22125 Hz**.
`decodePsAdpcm` was already there for sound effects.

`FUN_00207010` programs a *reserved* SPU2 voice rather than going through
`FUN_002057c8`'s 22-voice effect pool, so a long line cannot be stolen by a
footstep. `SoundEngine` keeps that separation with one dedicated slot that owns
its own samples.

**Nothing here reaches the simulation.** The hold comes from the table, not from
how much of the buffer the mixer has consumed, so a headless run keeps identical
timing — and headless runs do not decode at all, since a clip is a megabyte of
work per line otherwise thrown away.

`tools/voice_extract.py <id> <out.wav>` dumps a clip, and `--list` prints the
first forty with their lengths.

## What it changed

`s01_e012`'s 42 dialogue dispatches: **38 now timed by their clip, 0 estimated,
4 empty**. The four empties are one `0x33` site whose inline text is a bare
`0x02` terminate — the scene closing the window, not speaking — which used to
hold for a second each.

Line by line the old `60 + 2 * characters` estimate was nowhere near: "Monsters?"
held 90 frames against a real 34, and Volcan's opening rant held 310 against a
real 639. Flag `0x515`, the handoff to player control, moved from frame 10426 to
**13122** — and later to 13317 once the subtitle walk drove the record's close
rather than the clip alone (see [Text, dialogue and subtitles](text-and-subtitles.md)).
It reads 13250 as of 2026-10-23.

Remaining gap: a *texted* record with no clip behind it still falls back to the
estimate, because there the original waits for the player's Cross press rather
than for audio. `s01_e012` has none; a scene that does will say so in the report.

## The spell voice is a multi-clip VOICE.BIN bank

Casting speaks two lines, and neither is a sound cue. They are VOICE.BIN clips
played through the same reserved streaming voice a line of dialogue uses, driven
by a per-slot state machine in `DAT_0031DA60` that states 111 and 113 walk:

| step | what it does |
|---|---|
| 1 | ask for the bank (`FUN_00206ae0`), on acceptance → 2 |
| 2 | poll the load (`FUN_00206c28`), when it settles → 3 |
| 3 | at the charge marker, if nothing else is speaking, play clip 0 → 100 |
| 100 | on release, play clip 1 → 101; → 102 if something is speaking |

The bank id is `DAT_0031DA54`, which `FUN_002432d8:125` took from the spell
table's family column at `0x00325230`. Hand of Pyro is bank 7.

**A VOICE.BIN entry is not always one clip.** `FUN_00206aa0` copies the first
`0x40` bytes of a loaded entry into `DAT_00314BD0 + channel * 0x40` and
`FUN_00206f08` reads them as a directory: word 0 is the clip count, then one
word per clip packing `(offset in 16-byte units << 16) | size in 16-byte units`.
`FUN_00206d98` validates it before trusting it — `count - 1 < 0xF` **and** the
halfword at `+6` equal to `align16(count * 4 + 0x13)`, the directory's own
length — and an entry that fails is played whole, which is what an ordinary line
of dialogue is. Read out of the real file:

| bank | spell | clips | clip 0 | clip 1 |
|---|---|---|---|---|
| 4 | Sword of the Fallen Devil | 2 | 31,408 B | 4,064 B |
| 7 | Hand of Pyro | 2 | 31,408 B | 4,064 B |
| 9 | Bite of Lightning | 5 | 31,408 B | 4,064 B |

Clip 0 is a ~2.5 s incantation, clip 1 a ~0.3 s shout. **A short charge never
reaches the shout**: the release checks `FUN_00206a90` and drops the line rather
than queueing it, so with the incantation still running it writes 102 and says
nothing. That is the original's own branch, not a port limitation — hold
Triangle past ~2.5 s and both play.

`DAT_00356788` is modelled as a **tick countdown taken from the clip's own
length**, not as a mixer query. The original's flag is simulation state cleared
when the stream ends; reading the mixer instead would make `--frames` output
depend on whether audio was enabled. Verified: an audible run and `--no-audio`
produce byte-identical reports.
