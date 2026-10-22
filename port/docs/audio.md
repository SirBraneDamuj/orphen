# Sound effects and music

### Cues

`FUN_00228e28` loads 711 eight-byte records out of **SCR.BIN resource 199**:
bank, program, note, volume, cap. `FUN_00205118` loads three banks from
**SND.BIN resources 1, 2 and 3**; bank 0 is where the common sound effects
live. Both files are in the disc root already.

`FUN_00267a80` measures against the **camera**, not the player: silent past 14
units, panned by the angle between the sound and the camera's yaw, with a floor
inside three units that keeps a close sound in both ears while the camera
swings. `FUN_002057c8` then scales by the record's volume and the master.

### What plays, and through what

Nothing here is a per-sound hook. Each of these is the *mechanism* the original
uses, ported once, with the specific sounds falling out of it:

| sound | mechanism | ported in |
|-------|-----------|-----------|
| footsteps | `FUN_00256ff8`: an animation keyframe carrying `0x100`, then the surface table | `ported/entity/original_entity_sound.*` |
| jump | the same surface table, column 2, from `FUN_00256bb8`'s jump branch | the player controller |
| the flying enemies' buzz | `FUN_002cd0a0` retriggering `FUN_002cde50` on a per-entity period | `FUN_002cd0a0_enemy62` |
| the chest lid | `FUN_002d59e0` on animation 5's event keyframe | `FUN_002d1ea8_treasure_chest` |
| the item fanfare | `FUN_00257b10` at `0x00255240` | the chest cutscene's state `0x11` |

**Footsteps are authored into the animation.** `FUN_00256ff8` fires only when
the entity stepped onto a keyframe whose trailing word has `0x100`; in
`grp_0001` the walk animation carries it on keyframes 1 and 3 of four, and the
run on 4 and 9 of ten. `0x200` alongside it distinguishes the two feet, and
picks the dust effect rather than the sound.

The cue then comes from the **surface**: `FUN_00255d88` takes the top nibble of
the collision record's word 1 -- the `terrainFlags` the ground query already
returns -- as a material, indexes `DAT_0031E028[material][kind]`, and
`FUN_00251c80` offsets that by the character's class. Orphen is class 0, base
`0x3F`, so on material 0 he walks with cue 63 and runs with 67 -- 0.14 s and
0.13 s of waveform. Walking around `s01_e024` reaches materials 0 and 3.

`+0x04` bit `0x1000` gates it, and `FUN_0022a418:204` sets that bit on pool
slot 0 and nowhere else -- so **only the lead player has footsteps**, which is
also why `FUN_00256ff8` is only ever called from player states. The port was
not setting `+0x04` on the lead player at all, which is what
`FUN_0022a418_stamp_lead_player_flags` now fixes; the party members' `0x00A4`
correctly stays silent.

The buzz is one cue retriggered: type `0x62` rolls a period of 24..31 frames
once into `+0x1C6` and fires cue `0x196` whenever the *global* frame counter
divides by it. The waveform is 1.3 s long, so the repeats overlap into a drone,
and because the test is against the frame counter rather than a per-entity
timer, a group of them beats against itself. **Porting that roll shifts the
RNG stream**, so the enemies and the bandana land in different places than the
pre-sound baseline -- the original consumes `FUN_00216868` there and the port
previously did not.

### Waveforms

Bank 0 is a standard Sony VAB, version 7, 14 programs. Its programs are **key
split** -- every tone has `min == max`, so the note picks the waveform rather
than transposing one. Program 11 note 60 is waveform 13 and program 0 note 67
is waveform 54, and their centre notes put them at 11027 Hz and 22055 Hz
against SPU2's 48 kHz pitch base. A VAB records a sample rate as a centre note.

**The tone's `shift` is added, not subtracted:**

```
rate = 48000 * 2^(((note - centre) * 128 + shift) / (12 * 128))
```

The field reads like a fine tune *on* the centre note, which would subtract it
-- and `shift` is 69 almost everywhere, so getting that backwards puts
everything 1.08 semitones flat. Two checks say added: 86 of the 280 resolvable
cues then land within 1% of a standard authoring rate against 9 the other way,
and five of the SPU2 voice pitch registers in a PCSX2 savestate are reproduced
*exactly* against none. Cue 8 lands on 22055 Hz and cue 159 on 11027, which are
22050 and 11025 to within a cent.

The decode was checked against a PCSX2 savestate: **SND.BIN resource 1's body
section is byte-identical to the 241504 bytes at SPU2 RAM `0x19000`**, and its
header matches the copy at IOP RAM `0x93900` apart from the `ProgAtr` block,
which libsnd rewrites when it opens a VAB. `0x93900` is also what `DAT_00355A1C`
holds in the EE dump, so what the EE calls a bank's "SPU address" is an IOP one.

### Running it

Audio opens on any windowed run, capture runs included -- the mixer is on its
own thread and cannot reach anything a capture compares, and two captures of
the same frame still hash identically with it open. `--no-audio` turns it off.

Headless runs never open a device. To hear one anyway:

```
orphen_port --disc-root . --scene s01_e024 --frames 620     --spawn -4.5,-10.5,0 --press-confirm 60,440     --sound-dump out/sfx/chest_cutscene.wav --sound-report
```

`--hold-stick <angle>,<magnitude>` drives the analog stick for every headless
or capture frame, which is how the footsteps get exercised without a pad --
magnitude is the original's 0..128 and above 100 is a run. A third field,
`<first>-<last>`, limits it to those frames in a headless run (the stick is
released outside them), which is how a menu gets a stick nudge after it opens:

```
orphen_port --disc-root . --scene s01_e024 --frames 400 --spawn -4.5,-10.5,0 --hold-stick 1.0,60 --sound-dump out/sfx/walk_and_buzz.wav --sound-report
```

`--sound-report` prints every cue with what it resolved to, which is how the
C++ path was checked against an independent Python decode -- both land on
waveform 13 at 18704 samples and waveform 54 at 50652 samples, same rates.
`--sound-dump` renders one frame of mixer output per simulation step; the two
bursts in that WAV start at frames 166 and 391, matching the cue log.

### What is not ported

- **Absolute loudness.** The chain reproduces the game's relative volumes, but
  nothing models the IOP's own master, so the overall level is a guess.

## Music, and where every note of it was hiding

The port played sound effects and no music, and the reason was one section of
one file.

A SND.BIN bank resource has three sections (`FUN_00205548`). The port read
section 0 (the waveforms) and section 1 (the VAB header). **Section 2 is either
the literal bytes `NSEQ` -- the marker for "no sequence here" -- or a real Sony
`SEQp` chunk.** The three banks `FUN_00205118` loads at boot are exactly the
three that are `NSEQ`, so a sound-effect-only implementation reads every bank
that has no music in it and none of the banks that do.

### A scene asks for its own music

`FUN_0025b2f0` copies 16 bytes -- eight `u16` requests -- out of **scene script
header word 10**, and `FUN_00206840` acts on them. Slot *i* draws from music
category `min(i, 2)`, the low 15 bits index that category's table, and **bit 15
means "start it now"**. Everything else is loaded ready for a later opcode.

The tables themselves come from SCR.BIN resource 199 -- the same resource the
sound-effect cue table comes from, built in the same pass by `FUN_00228e28`.
Header word 7 points at three offsets, one per category; each is a run of
eight-byte records ending at the first with a zero resource id. Retail has 51,
33 and 201 records, which is exactly what `DAT_00354c00`'s bounds check expects.

For `s01_e012`:

```
slot 2  0x801a  cat 2 index  26  -> SND resource 112  vol 70   PLAY
slot 6  0x007f  cat 2 index 127  -> SND resource 213  vol 80
```

Slot 2 is the only one with bit 15 set, and it is the wind that runs under the
whole scene. Slot 6 is started later by the script: opcode **0x129** at blob
offset `0x5e56`, `(slot 6, fader 1000)` -- the piece under Sephy's scene.

### The wind is four notes long

SND resource 112's whole sequence, decoded by hand:

```
02  c0 00       program change ch0 -> program 0
02  b0 07 7f    CC7  volume 127
02     0a 40    CC10 pan 64
02     0b 7f    CC11 expression 127
02  90 3c 64    note on ch0, note 60, velocity 100
81 3e (Δ190)   b0 63 14   CC99 = 20    loop start
01              06 7f     CC6  = 127   forever
86 01 (Δ769)    63 1e     CC99 = 30    loop end
00  ff 2f 00    end of track
```

One held note. Looping is Sony's controller convention (CC99 = 20/30 around the
loop, CC6 for the count) rather than anything in the header.

The *sustain* is not in the sequence at all -- it is in the PS-ADPCM flag byte,
which the decoder had been discarding. Bit 2 marks the block a repeat returns to
and bit 1 says the end block repeats. **The trap:** a one-shot's terminator is
`0x07`, which sets bit 1 *and* points the loop at its own final block. That is
the hardware spelling "stop", so a loop that starts inside the last block is not
a loop, and treating it as one makes every sound effect stutter forever.

### ADSR stopped being optional

One-shots never needed an envelope: they run a waveform to its end block and
stop. A held sequencer note has no end block to reach, so the envelope is the
only thing that ever ends it. `AdsrEnvelope` runs the SPU's own algorithm --
`1 << max(0, (rate >> 2) - 11)` samples between steps of
`step << max(0, 11 - (rate >> 2))`, exponential rise slowing 4x above `0x6000`,
exponential fall scaling by the current level.

### The fader is 0..1000, not 0..127

`FUN_00206048` keeps two numbers per slot: the record's volume byte (0..127) and
a fader running 0..1000. What reaches the sequencer is `fader * base / 1000`, so
a fader of 1000 means "this slot's authored volume" -- which is the 1000 that
both `FUN_00206840` and opcode 0x129 pass. `FUN_002063c8` (0x12A, up) and
`FUN_00206260` (0x12B, down) ramp it, over a frame count worked out from the
0..127 delta rather than the fader delta.

### The reverb, and the table that was never in the EE image

A music record is eight bytes. `+0` is the SND resource and `+2` the volume;
`+4` is a **reverb type** and `+6` a **reverb depth**. `FUN_00205938:90-113` is
their only reader:

- `+4 >= 0` sends the type to the IOP as command `0x7314` with `type | 0x100`,
  then command `6` with 1 to turn the effect on (0 when the type is 0).
- `+6 & 0xFFFE` goes out as `FUN_00204ca8(10, depth << 8, depth << 8)` -- one
  effect volume per channel.

Both are cached in `sGpffffbab4` and `sGpffffbab6`, so a slot asking for what is
already set resends nothing, and a `+4` of `-1` skips the block outright and
leaves whatever the last scene chose. **78 of the 285 music records select a
preset** -- 26 of 51 in category 0, 28 of 33 in category 1, 24 of 201 in
category 2. (An earlier count here said 71, which was the type-4 records alone.)

#### The coefficients are on the disc, not in the ELF

The EE never holds a single reverb coefficient. It forwards a type number, and
the table that turns that number into hardware registers lives in the game's own
IOP sound driver, `cdrom0:\RSPU2DRV.IRX` -- which is on the ISO but was never in
the flattened disc root here. `scripts/extract_spu2_reverb_presets.py` pulls it
out of either, and generates `port/src/ported/sound/spu2_reverb_presets.h`.

Two tables in the module's `.data`:

- **0x16390** -- ten `u32` buffer sizes in eight-byte units, `0x4D8` through
  `0x780`. These are the stock Sony sizes: `0x4D8 * 8` is room's `0x26C0`.
- **0x163C0** -- ten `0x44`-byte entries, `{ u32 fieldMask; u16 params[32] }`.
  The mask is zero in every shipped entry, which the driver reads as "write
  every field".

So the modes are the familiar ten: 0 off, 1 room, 2-4 studio small/medium/large,
5 hall, 6 space echo, 7 echo, 8 delay, 9 pipe. `s14_e031`'s type 4 is **studio
large**. Across the whole game only 4 (x71), 3 (x4), 5 (x2), 2 (x1) and 0 (x2)
are ever asked for.

The driver's writer at `0xF6F0` walks the entry front to back against ascending
register indices, which is what pins the field order down with nothing left to
guess: `+4`/`+6` are `APF1_SIZE`/`APF2_SIZE`, `+8` through `+0x16` are the ten
volume coefficients at `0x774`-`0x782`, `+0x18` through `+0x3E` are the twenty
delay-line addresses at `0x2EC`-`0x338` in order, and `+0x40`/`+0x42` are
`IN_COEF_L`/`IN_COEF_R`. Addresses go out as `value << 2` -- eight-byte units to
16-bit words -- and `ESA` as `(memtop - (size * 8 - 2)) >> 1`, so the buffer is
exactly the PS1's byte geometry and a preset unit is four samples.

#### Routing is per tone, and the flag is the VAB's

A VagAtr's `mode` byte at `+1` is **4** for a tone that feeds the effect bus and
**0** for one that does not. Those are the only two values anywhere in the game.
The three boot banks are 400 tones of solid 0, which is why sound effects have
never wanted this, and of the 283 banks the music tables reference only **61**
carry a wet tone at all -- a scene can select a preset and still route nothing to
it, which is exactly what `s01_e012` does.

This is a PS1-shaped VAB, where the per-voice reverb bit is purely an extra
send: a wet tone is still heard dry. `mode` 4 means "and also wet", not "wet
instead".

#### s14_e031

Its track is category 2 index 84 -> SND resource 170, `vol 70, reverb type 4,
depth 60`: a send of `0x3C00` out of `0x7FFF` on both channels, and **11 of its
12 tones at mode 4**. (An earlier note here said every tone in the bank; one is
dry.) The SEQp is seven channels at 48 ticks per quarter and 480000 us per
quarter, so a tick is 10 ms -- and **channel 6 is a note-for-note copy of
channel 3 delayed by 32 ticks**. 512 note-ons against 507, no pitch mismatches,
the offset exactly 32 on every one, the same instrument on both (programs 3 and
6 are a single tone each on VAG 4), and only the channel volume different: CC7
120 against 73. It is an authored slap delay. Wet, it is the tail of a room.
Dry, it was the melody played twice 320 ms apart.

#### What the port runs

`Spu2Reverb` is the documented SPU reverb topology: two cross-coupled IIR comb
lines feeding a four-tap early-echo comb, then two all-pass sections, all of them
reading and writing one delay buffer that scrolls a sample per tick. The
different-side lines cross -- the left one feeds back off the *right* source tap
-- and straightening that out collapses the stereo image, which is how you notice
it has been got wrong. Field names in the code are the driver's, so `sameLDst`
is the address the hardware's `SAME_L_DST` holds.

Two deliberate departures, both audible only under a null test: it runs in float
where the hardware is 16-bit fixed point with a saturating multiply, and the wet
return is linearly interpolated across the sample pair a tick covers rather than
resampled through the hardware's FIR. It does tick at half the output rate, as
the hardware does.

Measured on `s14_e031`, 15 s of `--music-solo` at frames 900, wet against
`--no-reverb`:

| | dry | wet |
|---|---|---|
| RMS | 5885 | 6758 |
| peak | 24119 | 30526 |
| L/R correlation | 0.858 | 0.632 |

The difference signal is 3278 RMS, a little over half the dry level, which is
where a `0x3C00`-of-`0x7FFF` send should land. Nothing clips and the IIR does not
run away. Correlating that difference against the dry mix gives no dominant
single lag -- a diffuse `+-0.1` out to 640 ms, with one `+0.18` bump at 320 ms,
which is the authored slap delay showing through the tail rather than the reverb
producing an echo of its own.

The simulation is untouched: `--frames 600 --actor-report --scr-report` on
`s14_e031` is byte-identical to the same report from the build before this
change, and the `--music-solo` dumps for `s01_e024` and `s01_e012` hash the same
before and after -- the second of those despite selecting studio large at depth
30, because not one of its tones asks for the bus.

One edge the shipped data never reaches: a preset selected at depth zero would
freeze the delay buffer here where the hardware would keep circulating it. No
record in the game does that.

### Scene-streamed sound effects live in the music banks

A cue record's byte +7 is **not** a bank id. `FUN_002057c8:56` hands it to
`FUN_00205778`, which searches the *music slot requests* (`DAT_00356a18`,
**indices 2..7**) for the one whose low 15 bits match, and returns `slot + 3` —
the bank index. So a "scene-streamed" effect is not a separate system: it plays
out of a music slot's VAB.

In `s01_e012` cues 677..680 carry `+7 = 127`, slot 6 requests category-2 index
127, and that is SND resource 213 — the same bank whose *sequence* is the cue
under Sephy's scene. Its **program 4** holds Volcan's sword: draw, sheathe and
flourish on notes 60/61/62, plus a hard-panned stereo pair on 63. They come out
at 22057 Hz, which is 22050 to within a cent — a good check on the pitch maths.

### Pool slot 1 is the camera, and that is how a sound plays non-positionally

`FUN_00228e28:203-210` builds slot 1 at boot: type `0xFFFF`, position zeroed,
`+0x4C`/`+0x50` pinned to `-60` so the floor never touches it. Those writes are
spelled as `DAT_0058c088`, `DAT_0058c0a8..b0` and `DAT_0058c0d4/d8`, which are
that slot's `+0x00`, `+0x20..+0x28` and `+0x4C`/`+0x50`.

So **`DAT_0058c0a8` — the listener `FUN_00267a80` measures against — is slot 1's
position.** A script cue aimed at selector 1 plays *at the listener*: distance
zero, full volume, dead centre. That is the engine's idiom for "this sound is
not positional", and `s01_e012` uses it for the storm's thunder (cues 586/587)
and for Volcan's sword (677/678/679).

Leave that slot empty and every one of those cues is measured from the world
origin instead. The sword still happens to play, because the Dortin/Volcan
camera sits ~5 units from the origin — but panned wrongly (`62/13` instead of
`60/60`). The thunder does not, because the establishing shot puts the camera at
`(0, 60, 0.5)`, 60 units out, past `FUN_00267a80`'s 14-unit cutoff.

**The pool stride is `0x1D8`, not `0xEC`.** `FUN_0025d6c0` reads
`&DAT_0058beb0 + selector * 0xEC` where the pointer is `undefined2 *` — 0xEC
*halfwords*. `analyzed/entity_pool_and_descriptors.c` has always said so; this
section exists because an ad-hoc dump script here used 236 instead of 472 and
made slot 1 look empty in a save state where it plainly is not. Read that file
before indexing the pool by hand.

`--sound-report` prints the source and listener positions for exactly this
reason: `src(0,0,0)` on a cue that should be centred is the signature.

### A VAB program layers, and getting that wrong sounds like a stuck note

A program is a *set* of tones, and keying a note sounds **every** tone whose
range covers it. Returning the first match is silently wrong, and this data
layers constantly — SND resource 2 has 631 overlapping tone pairs.

The one that gives itself away is the wind. SND resource 112's program 0 holds
**two** tones over the same 0..120 range, at **pan 0 and pan 127**: a stereo
recording split into two mono waveforms. Play only the first and the ambient bed
is mono and hard left — right-channel RMS measures exactly `0.0`. It stops
sounding like wind and starts sounding like a held note.

That is worth knowing as a symptom: *"a note hangs until the scene ends"* was
this, not a sequencer fault. The loop machinery was working the whole time (the
wind takes 27 loops in a 13,000-frame run). Confirm with:

```
orphen_port --disc-root . --scene s01_e012 --frames 600 --music-solo \
    --sound-dump out/audio/wind.wav
```

`--no-reverb` holds the effect bus off whatever the scene's music record asks
for, so a wet track can be dumped twice and differenced. It is a divergence by
construction -- diagnostic only.

`--music-solo` mutes the effect pool and the voice line so a dump holds only the
sequence slots — dialogue is centred and full-scale and buries the music in any
measurement of the whole mix.

### The meta encoding is not standard MIDI's

A tempo change is **`FF 51 <u24>` with no length byte**, where a .mid writes
`FF 51 03 <u24>`. Read the first tempo byte as a length and everything after it
desynchronises.

Across all 283 sequences in the game only two meta types occur: `FF 2F` in 282
of them, and `FF 51` **four times, all four in SND resource 117**. So anything
else is a parse failure, not a meta to skip — the port now stops the slot and
flags `DESYNCED` in `--sound-report` rather than carrying the damage forward.

Resource 117 is the cue under Sephy's scene: 34.1 s, with a ritardando ending
(50, 49, 46, 44, 42 BPM). Its first tempo change is at tick 875 ≈ 21.9 s, and
the scene plays it for 26.9 s — so the wrong reading plays four fifths of the
piece correctly and then falls apart. That is exactly how it presented: "stops a
few seconds early, then holds a note", because the garbage after the desync keys
notes on that never receive a note-off.

A second bug kept that note ringing. `FUN_00206260` returns early when the slot
state is below 2, because on the real machine the IOP owns the voices and has
already silenced them. The port's voices are its own, and a track that ran to its
end left them in *release* — with a slow release rate they ring on, and the
ramp-down that should have stopped them returned early too. A script asking for
silence now gets silence.

### Sephy's cue: slot 7, not slot 6

Two different pieces play near each other and it is easy to chase the wrong one:

- **slot 7 / SND 117** — frames 6805..8420, stopped by **subproc 1495** at blob
  offset `0x534d`. This is the piece under Sephy's scene.
- **slot 6 / SND 213** — frames 11564..11873, a five-second sting later on.

### Slot 6's five seconds are by design

Worth recording so it is not re-investigated. Opcode `0x129` starts slot 6 at
frame 11564 and `0x12B` fades it at 11873 — 309 frames. The gap is script, not
drift: a `0x90` at `0x5e1d` arms ramp 1 with `current 126, target 0, step 1`, the
`0x91`/`0x01` pair at `0x5e7e` polls it for exactly 126 frames to frame 11690,
then dialogue runs and the fade follows 183 frames later. Every operand checks
out against all five `0x12B` sites (`0x671d` gives slot 2 → fader 500, matching
the frame-13122 scene handover).

So the track never reaches its own loop end at tick 2309 (24 s) — it is stopped
at 5 s. `--sound-report` says so directly: `loops taken 0, end of track not
reached`. The tail after that loop end exists for the case where the loop count
expires: it holds the note-off for the ch0 drone that was keyed at tick 5 and
swelled in over the intro.

### A cue's bank byte can name a music slot

Chasing the rumble that goes with that shake turned up a second gap. Cue 702
was resolving and then reporting **"bank not loaded"**, because the engine held
three banks and the cue asked for bank 4.

There are eleven. `FUN_002057c8:44` indexes one table for every cue --
`(&DAT_003567d4)[bank * 0xb]` -- and `FUN_00205938` loads **music slot N's bank
into entry N + 3** (`FUN_00205310(desc, param_1 + 3, ...)`). `FUN_00205118`
fills 0..2 from SND.BIN resources 1/2/3 at boot; slots 0..7 fill 3..10. The
`slot + 3` that `FUN_00205778` returns is an index into that same table, not a
slot number, which is the tell.

The port modelled only the indirect route: a cue's **+0x07** alternate id
through `FUN_00205778` to a music slot. But a cue's **+0x00** bank byte reaches
the same table directly, and cue 702 uses it -- bank 4 is music slot 1, SND.BIN
resource 66, which s01_e012 loads at scene start. `bankFor` now maps any bank
at or above the three boot banks to slot `bank - 3`.

After the fix all four rumbles play (waveform 2, 18536 samples at 14.7 kHz,
about 1.3 s each), and no cue in either regression scene reports a missing
bank. Cue 702 is aimed at selector 1, the camera entity, so it plays
unattenuated at the listener -- the same "not positional" idiom the storm's
thunder uses.

### Checking it

```
orphen_port --disc-root . --scene s01_e012 --frames 20000 --sound-report
orphen_port --disc-root . --scene s01_e012 --frames 600 --sound-dump out/audio/wind.wav
```

The report's music section names every slot, what it resolved to, and every
play/ramp with its frame. For `s01_e012` that is slot 2 at frame 0, slot 7 up at
6805 and down at 8420, slot 6 (Sephy) at 11564, and the wind ramping down at
13122 as the scene hands over.

The sequencer runs on the mixer's clock, like the voice line, so none of it can
reach the simulation: `--frames 20000` is byte-identical across runs with and
without it.

