# s05_e012: the gear tower top floor

Where `s14_e004` ends (`map_change(5, 12, ...)` at `0x5BE4`). The party rides
the lift up ("This looks like the top floor."), the room's gears turn until the
lever on the west wall (record 1, type 0x277, at (-6.73, 0)) is pulled, and the
lever starts stream `0x61D0`: the party walks to the Crystal Egg fragment in the
middle of the room, Sephy explains it, and the egg jumps them back to their own
time. The scene then hands off to `s03_e005`.

Checked against `savestates/s05_e012_before_lever.p2s` (hardware, lever in
reach) and `s05_e012_crystal_egg_line.p2s` (record 26, "The Crystal Egg?"). At
the lever, hw frame = port frame + 2368; by record 87 (`body_5013`) it is
+2585, the port's dialogue holds running short.

Headless, from the arrival to the hand-off:

```
orphen_port --disc-root disc --scene s05_e012 --from-scene s14_e004 --no-audio \
  --frames 9400 --place-slot 0,-6.0,0.0,0.3:830-850 --press-confirm 880 --scr-report
```

## 0x6F, the flat distance

`FUN_0025FF58`: the 0x6E shape -- four expressions over 100000 (gp `-0x7380`,
not 0x6E's `-0x7384`, but the same value) -- returning
`sqrt(dx*dx + dy*dy) * 100000` truncated. The room's per-frame `sub_4DAE` opens
with it, so it halted the tick on the first frame after the arrival.

## The gear pit: registers 0x14, 0x15, 0x16, 0x18

Inside 4.8 of the room's centre, `sub_4DAE` writes 1 into register 0x14
(`+0xBE`, damage), 18 into 0x15 (`+0xBC`, reaction `0x12`, the knockback) and
the outward angle into 0x17 on the lead, and `FUN_00251ED8` spends them as a
hit. The port wrote 0x17 only. `FUN_0025C8F8` and `FUN_0025C548` handle all
four as plain fields (reads: `lh` / `lbu` / `lhu` / `lh`).

The lead now lands about x = 5.8 from the centre and is back on its feet by
frame 960. Not checked on hardware. Already-ported scenes read these too --
`s01_e013` waits for the lead's `+0xBE`, `s01_e023` drains it on its NPC,
`s03_e001` clears a flag on it -- and read a constant zero before.

## 0xA5 lets the party onto the pit floor

The lever's `body_266F` runs `map_primitive_flag_mask(0x8000000, 0, 0x8000000)`
and `(0x4000000, 0, 0x4000000)`. `FUN_00262058` -> `FUN_0022DB50` walks the 0x78
records and, for every one whose `+0x04` terrain word meets the mask, sets or
clears the bits *in that same word*. Clearing `0x08000000` and `0x04000000`
takes away exactly what the lead's `+0x74` (`0x08000000`) and the followers'
(`0x0D000000`) reject.

The port read the operands and dropped them, so the cast played the run
animation at the pit's rim for the whole conversation, and every close-up
framed empty floor -- "everyone is invisible during the dialogue". Now all
three stop on their `run_to` targets and match hardware to three decimals:
Sephy (-0.40, 0), Orphen (-0.821, 0.134) facing -0.3081, Magnus
(-0.728, -0.279) facing 0.6519.

`s01_e013` uses it in a matched on/off pair to fence the lead out during a
beat.

## 0x138, the time-jump vortex

`FUN_002652D0` passes eight expressions to `FUN_00202F00`, one of eight
full-screen texture layers at `DAT_00356100`; `FUN_00202FF8` draws them every
frame. See `ported/render/original_screen_layers.h` for the record, the quad
(raw GS coordinates, because the mode word carries `0x10000000`) and the
bucket order. Here it is slot 0, additive, texture slot 8 -- page `0x26E`, the
teal vortex -- ramped from alpha 0 to 256 beside a matching `0xC9` smear.

- The texture table is read from header word 6 **+ 2**, so slot 8 is the
  table's ninth halfword after the leading one, not `0x61`.
- The last call passes alpha 256, which `0x31`'s `alpha << 24` wraps to zero:
  the layer ends invisible but live.
- Nothing clears a layer on a scene load and no retail script calls `0x139`,
  so the layer is carried into `s03_e005` at alpha zero.

Side by side with hardware at hw 9905 / port 7320 the vortex, the cyan cast
tint and the smear match; the port is a few frames further up the ramp.

## Still out

- `s03_e005` halts on opcode `0x4A` at `0x114C`.
- The knockback distance and the lift ride are unchecked on hardware.
