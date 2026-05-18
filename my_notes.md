## SCR notes

Expanded tracked IDs (added 0x119C, 0x0526, 0x0534, 0x053D).
Signature classification (SIG_A_9e0c011e0b, SIG_B_0b0b02920c) for seeded id04_records.
Full enumeration enumerate_all_id04 with lightweight classes (A, B, UNK).
Counts summary: A=274, B=1 (the 0x133F case), UNK=230.
Seeded id04_records now 9 (newly captured additional seeded IDs).
Observations:

Signature A (9e0c011e0b) is dominant for these subproc IDs.
Only one Signature B instance (the earlier distinctive 0x133F container).

> SIG_B_0b0b02920c
> Only one Signature B instance (the earlier distinctive 0x133F container).

0x133F is the subproc that is basically "permanent" in the opening map. Maybe this indicates some kind of permanent subproc?

---

4508 = 0x119C - CUTSCENE ADVANCER?

I noticed this subproc flashes on screen briefly a lot whenever characters advance their dialog. Maybe this subproc is responsible for advancing the dialogue or other control stuff?

Update: This is actually a no-op!

9c1100009e0c011e0b04

9e opcode just immediately removes itself from the stack. 0c 01 1e = -1. -1 tells it to remove "itself" from the stack.

This is seemingly used by the cutscene queue to add no-op flag checks or waits into the queue. Still working on how exactly that works.

---

77360e040000000b0b0e080000000b0c150b9e

opcode 77 seems to be used to set properties on objects:

77 opcode
arg 1: 0x36 opcode (arg 1: 0e(immediate u32) = 0x4) - read item 4 out of the scene work list
arg 2: 0e(immediate u32) = 0x8
arg 3: 0c(immediate u8) = 0x15

Finds an object ID in the scene worklist at index 4
Looks up the relevant offset in `set_active_effect_parameter.c`:

```c
  case 0x08:
    puGpffffb0d4[0x50] = val16;
    break; // +0x0A0
```

Sets the value to 0x15

I don't know how much relevance this has, but I spent a fair amount of debugging it so I wanted to write it down.

Copilot was also not very smart about it :)

---

Attempted to patch:
`00237EC8 2AE20202`
`slti v0,s7,0x0002` -> `slti v0,s7,0x0202`

All it does is make the text scroll faster. It seems that the text still yields for something else to tell it to advance...

---

Pretty good solution for now:

`1C` opcode is a "delay". Change all following bytes to `01` to minimize the delay

`1A` opcode seems to be "wait for audio to finish". These are usually at the ends of sentences. These are a little trickier, but I think can just be replaced with `20` for right now.

`0031DD60` japanese version - this appears to be where the character attributes for battle mode are stored.

---

`1C59536` the `12` here DISABLES the logo in the crab fight. How can we apply a universal patch for this?

It seems like there is a common refrain in battle scripts:

DF is the code that triggers the logo pop-in. (TODO: update the analyzed code to indicate this)
In English, the scripts have an if block around this function call:

01 0E 00 00 00 00 0C 01 12 0B 26 00 00 00 DF

01: if the next value is zero, skip the following value bytes ahead
0E 00 00 00 00 = 0
0C 01 = 1
12 = pop 1 off the stack, meaning remove the 1 we just got and go back to the zero
0B = return (0, because of the 12)
26 = skip 26 bytes ahead, which must be about how many bytes we need to skip the DF code

This is often handled by a case statement where a work flag at position 112 gets set to 28.
When it's 28, the case statement branches into this sequence. In JP, it just goes straight to the DF. In english, it hits this conditional.

How can we patch this universally? Battle scripts are loaded randomly in memory.

Ideas:

1. find an innocuous, relevant place to inject a jal function call to the logo setup
2. find a way to make it happen on button press, and move on from trying to make it happen in scripts
3. find a way to conditionally apply patches (seems impossible) (actually apparently it might not be that hard lol)

---

## SCR/PSM2 floor trigger surface notes

The map-trigger experiment points to a useful model: trigger regions are not separate script-authored volumes. They are ordinary PSM2 terrain/collision primitives whose Section D records carry a runtime surface flag word.

PSM2 Section D words `u16[10] | (u16[11] << 16)` become the runtime terrain/collision flag word. The PSM2 loader `FUN_0022b5a8` stores that value at runtime terrain record `+0x04`; the terrain sampler chain `FUN_00227070` / `FUN_00227840` copies sampled surface bits into the lead entity surface-state fields. SCR opcode `0x61` then tests those fields.

The important distinction: PSM2 tells us where a floor tile sets a bit, but SCR tells us what that bit means. The same mechanism is used for cutscene/dialogue stream triggers, fixed camera zones, room/stair behavior, and probably other local logic.

Current SCR2 findings:

- First post-control cutscene gate in `scr2.out` uses `0x61(mask=0x10, selector=1)` while excluding `0x20 | 0x40 | 0x80`; it arms stream `0xd0c0` at `0x3d7c` and corresponds to dialogue/cutscene target `0x0b76`.
- The focused PSM2 surface for that first trigger is `map_0002` primitive `1623`, flags `0x40120010`, bbox glTF `[-7,-1.5,-1]..[-6,-1.5,0]`.
- Other nearby SCR2 stream gates use the low `0xf0` group only: `0x20 -> 0xd220`, `0x40 -> 0xd300`, `0x80 -> 0xd530/0xd650`, `0x10|0x20 -> 0xd5e0`, `0x10|0x40 -> 0xd6e0`, `0x10|0x80 -> 0xd720`, `0x20|0x40 -> 0xd780`.

Current SCR4 findings:

- The dialogue around `0x00d5` is record `0x00c2..0x0113` in stream `0x2670`. Stream armer `0xA1@0x01469` is gated by branch `0x0145b`, which tests `0x61(mask=0x10000, selector=1)`.
- The matching focused PSM2 region in `map_0004` is two adjacent floor quads, primitives `1434` and `1441`, both flags `0x40012400`, covering glTF bbox roughly `[8,5,-1]..[10,5,1]`.
- SCR4 also uses low bits as camera-zone triggers in the scene tick block. The fixed camera position `11000, -3500, 8500` is in the `0x80` branch around `0x103c`: it sets `work[16]=3`, `work[20]=1`, calls `0x47(11000,-3500,8500)`, then uses `0x48` to point the camera target at `(11000, player_y, 5000)` with a lower clamp at `-3000`.
- The `0x20` branch around `0x10cc` drives a room/stair camera state machine using `work[20]`, `work[22]`, and flags `0x2ce..0x2d0`; one dynamic camera path uses `0x47(15000, player_y, player_z+1000)` and `0x48(player_x, player_y, player_z+1200)`.
- The `0x10` branch around `0x128f` appears to reset/hand back the camera override with `0x45(0)` and `work[20]=0` under position/state conditions.

Heuristic going forward:

- PSM2-only scan: find floor-like primitives with non-common surface bits, group connected quads by exact flag word and by tested bit masks.
- SCR classification: if a `0x61` branch fallthrough arms `0xA1`, treat it as a dialogue/cutscene/event stream trigger; if it calls `0x47/0x48`, treat it as a camera zone; if it mutates work vars/flags only, treat it as local behavior/state logic.
- Bit range is a hint, not a rule. SCR2 stream triggers use low `0x10/0x20/0x40/0x80`, while SCR4 uses those same low bits for camera zones and `0x10000` for the `0x00d5` dialogue stream gate.
