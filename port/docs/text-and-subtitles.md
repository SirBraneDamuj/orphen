# Text, dialogue and subtitles

### The dialogue font and the book prompt

`ported/text/original_dialogue_text.*` is the sheet, the metrics and the
prompt; `MapViewer::drawDialogueSprites` blits what it produces.

`FUN_00221fd8` binds texture `0x173` into slot `0x2E` and `0x172` into `0x2F`.
Eleven columns of 22x22 cells indexed by `character - 0x20`, continuing in the
second slot once the index runs off the bottom of the first at character
`0x99` (`FUN_00238a08:52`, whose `+ 0xE` then modulo-256 is "subtract a sheet,
keep the 14-unit remainder of the last row").

**The font is proportional and the width table is not in the executable.**
`FUN_00238c90` computes it at boot: for each cell it scans for the rightmost
column holding a texel whose palette alpha clears 100, and stores `column + 2`
(or 6 for a blank) at `0x0031C518 + character`. The port runs the same scan
over the decoded slot. It agrees with the live table in the EE dump on all 121
cells of `0x2E`. Text advances at 90% of the measured width —
`(width * 0x5A) / 100` in `FUN_00238a08`, which is `FUN_00238608`'s
`(cellWidth * 100) / 22` with the shipped 20-wide cell.

The screen units are the debug overlay's: `FUN_00207938` writes x at `<< 4` and
y at `<< 3` about the 2048-pixel GS centre, so the same 640x448 virtual screen,
and `FUN_00239020` negates y on the way in — a larger entry y is further *up*.
`FUN_00237b38` opens a window at entry `(-0x130, -0x78)`, which is screen
`(16, 344)`; glyphs add 8 to the x and the prompt adds `0x10`.

The prompt is the flipping book, not a character. `FUN_002391d0` builds it as a
sprite in slot `0x2A` (texture `0x178`), 15x15 texels drawn at 20x22, and
`FUN_00237fc0:77-95` animates it from the four-entry table at `0x0031C630` —
cells at `(96,32)`, `(96,48)`, `(112,48)`, `(112,32)`, so the frames run *round*
the 2x2 block rather than across it. The timer advances by the frame tick,
holds each frame for `0x80` and wraps past `0x200`: four frames each, sixteen
frames a cycle.

`FUN_00238608`'s other branch — characters at or above `0xFC`, which draw a
32x32 face-button icon from slot `0x2C` through the UV table at `0x0031C220` —
is documented in the header but not reachable from a caption.

### Cutscene subtitles, and why the US release has none

`ported/text/original_dialogue_window.*` is the real text engine: the 300-entry
glyph slot array, the stream walk, word wrap, the typewriter and the scroll,
ported from `FUN_00237de8`, `FUN_00238a08`, `FUN_00238f98`, `FUN_00238f18` and
`FUN_00239760`. `DialogueStream` owns it and steps it; `buildDialogueSprites`
hands what it produces to the same blit the chest caption uses.

**The subtitles are in the US data and the US code, and one four-line test
hides them.** `FUN_00238a08` opens with

```
lVar1 = FUN_00266368(0x509);
if ((lVar1 == 0) && (lVar1 = FUN_00266368(0x50a), lVar1 == 0)) return;
```

and `FUN_002391d0` gates the prompt the same way. Nothing in `SLUS_200.11` ever
sets `0x50A`. `FUN_00237b38` *clears* `0x509` on every start, and the only code
that sets it is `FUN_002452f0`, an unrelated full-screen caption. So a stream is
visible only if it turns the flag on itself, with control code `1B 09 05` —
which is exactly how the chest windows in `SCR.BIN` resource 1 begin, and it is
why those captions were already on screen.

`scr2.out`'s 84 cutscene records do not. Their six `0x1B` codes set `0x6A`,
`0x79` and `0x6E`, all scheduler gates; not one sets `0x509`. The Japanese
build's glyph enqueue, `src-jp/FUN_0023ade8.c`, is the same function with those
four lines absent — the text was cut for the US release by adding a test, not by
editing the scripts.

**The port drops the gate.** That is the only deliberate divergence in the file.

The codes `s01_e012` actually uses are `0x00 0x01 0x02 0x07 0x0C 0x13 0x16 0x17
0x18 0x1A 0x1B` — a small set, all handled. Anything else is stepped over by its
operand width and named in the report.

Layout comes straight out of `FUN_00237b38`: a window at entry `(-0x130, -0x78)`
= screen `(16, 344)`, 600 units wide, `uGpffffbce0 = 2` rows deep. `0x13` clears
the array, draws the name in `0x80606000` — dark cyan, R=0 — on row 0, then
steps to row 1, and from there every glyph is indented ten units. Past row 2
`FUN_00238f98` scrolls: each slot's row index drops by one, the one leaving row 1
is retired, the rest move 22 units up. Row 0 is never touched, which is what
holds the name still under a scrolling line.

Two divergences the port's structure forces, both in the header:

- The walk **skips** `0x16`/`0x17`/`0x18`/`0x19` by width instead of running
  them, because `DialogueStream` already read the clip out of `VOICE.BIN` when
  the record opened. `0x1A` does block, on that clip's remaining length rather
  than on `DAT_00356788`.
- `0x01` raises the book prompt and, in the original, holds for Cross — 28 of
  the 84 records use it. The port spawns the sprite and lets the record close on
  its clip, taking the Cross press for granted (which is `FUN_00237fc0:108-115`,
  flags and all — see below). A cutscene that stopped for input every fourth
  line would not be the same scene, and the port has no input model for it.

**Making `0x1A` block is what fixed the record tails.** A record's hold is not
its clip; it is where its *bytes* end, and a record can put codes after the
`0x1A`. Dortin's is `... 1A 0C 3C 02` — a full second of held text after the
audio stops. Closing on the clip alone dropped that second. With the walk
driving the close instead, every record picks up at least the two steps the walk
needs to consume its `0x1A` and then its terminator, and Dortin's picks up 65.
Across the whole scene that is 264 frames of held text, and flag `0x515` — the
handoff to player control — moves from 13122 to **13317**:

```
dialogue lines: 42  (38 timed by their voice clip, 0 estimated, 4 empty)
  38 held open past the clip until the walk reached the terminator, 264 frames in total
```

Checkable at any frame:

```
orphen_port --disc-root disc --scene s01_e012 --screenshot shot.png:700
```

### A record does not close the window, and that is where the speaker lives

Five of `s01_e012`'s records carry no `0x13` at all -- "Hey... Volcan... You
notice anything peculiar?", both of Volcan's `[vomits]`, and two of Sephy's --
and the game keeps the previous speaker's name on screen through every one of
them. The port drew them with no name.

The name survives because **the window does not come down between records.** A
record ends on control code `0x00`, which is `FUN_00239178`; at the outermost
nesting level it raises flag `0x8FE` and gate bit `0x2000` and returns, leaving
`pcGpffffaec0` pointing at the terminator. So when the next record opens,
`FUN_00237b38`'s `bVar1` -- the test on that pointer, taken before the
assignment -- is false, and the whole reset block is skipped: no
`FUN_00238f18`, no origin, no colour, no pen or line. The new text carries on
into a window that still holds the old one, `FUN_00238f98`'s scroll ages the
body out from under it, and row 0 is the row the scroll never touches.

There are exactly three ways out, and they are not interchangeable:

| | window | slots |
|---|---|---|
| `0x00` — `FUN_00239178` | stays up | kept |
| `0x01` + Cross — `FUN_00237fc0:108-115` | down | **kept** |
| `0x02` — `LAB_00239328` | down | cleared |

`LAB_00239328` is the only one that wipes the slot array, and only because it
nulls `pcGpffffaec0` *before* calling `FUN_00237b38(0)`, which flips that same
`bVar1` test the other way. An explicit `FUN_00237b38(0)` from the script does
not clear either -- the next open does. This scene closes the window between
each group of same-speaker lines with a record whose entire body is a bare
`0x02`; there are four of them, and the port already logged them as empty lines.

The flags differ too, and the difference is load-bearing: `0x00` raises only
`0x8FE`, while both closes also raise `0x8FF` and gate bit `0x4000`. The port
used to raise all of them at every record end. Record 207 of the scheduler
stream gates on `0x8FF` and is the handoff that sets flag `0x515`, so raising
it early is harmless but raising it *never* stalls the chain -- which is what
happened when `0x00` was first split out and the `0x01` prompt path had not
been. `0x01` is that path: the original's Cross press nulls the pointer and
raises `0x8FF`/`0x8FE`/`0x6000` without touching the slots, and the port stands
in for the press immediately, exactly as it always has.

None of this moves a single frame. The full run is byte-identical: 42 lines on
the same frames, 208 event records, flag `0x515` still at 13317.

For the camera half of `FUN_00234400`, `FUN_00217d70`'s own save/restore pair
covers it.

Checkable without a window, and `--press-confirm` now works under
`--screenshot` too:

```
orphen_port --disc-root disc --scene s01_e024 --frames 620 \
    --spawn -4.5,-10.5,0 --press-confirm 60,440 --actor-report
```

That spawns beside chest slot 17, facing it, and the report ends with that
chest on animation 6 and every other one still on 4.

**Both presses matter.** The first opens the chest; the second dismisses the
item caption, which state `0x12` waits on. With only the first, the cutscene
parks at `0x12` forever and the last four state changes never happen — that
looks exactly like a regression and is not one. The ten transitions land on
frames 61, 89, 111, 365, 366, 399, 441, 469, 475 and 497.

### The cinematic bars, and where they put the subtitles

`ported/render/original_letterbox.*` is `FUN_0025cfb8`, which `FUN_0025b778`
runs at the end of every script tick. Two flat black sprites, full width, one
against the top edge and one against the bottom -- so the bars are **part of the
game's picture**, not a border drawn around it.

Two globals, and opcode `0x6D` (`FUN_0025fd10`) is the only writer of either:
`DAT_00355054` is the mode and `DAT_00355CFC` is a `0..0x780` ramp. An operand
of `-1` raises the mode and starts the ramp at zero, so the bars slide in; `-2`
starts it full, so they are already there; `1` runs the ramp back down and
clears the mode at the bottom. The ramp steps by `DAT_003555bc * 8`, which at
the nominal `0x20` ticks is 7.5 frames end to end. The drawn height is
`ramp >> 5`, so 60 units of the 640x448 virtual screen -- 30 of the field's 224
scanlines -- leaving a 328-unit picture between them. `s01_e012` arms them with
`-1` on frame 1.

**The subtitles move for them.** `FUN_00238a08:36-45` reads the same mode as it
enqueues each glyph and nudges the whole window clear of whichever bar it would
run into: the bottom window (origin y `-0x78`) rises `0x1E`, which puts its
third line's bottom edge at 380 against the bar's 388, and the top window
(origin y `0xD0`) drops `0x2D` for the same reason at the other end. The port
had the arithmetic already and never had anything to drive it, so a cutscene
line was drawn flush against where the bar belongs. It is a *live* read, not
something latched when the record opened, which is why `setMovieMode` is
published every frame immediately before `FUN_00237fc0`'s walk.

**Draw order comes out of the GS sort buckets.** `FUN_00207938` head-inserts
into one of 0x1010 buckets that `FUN_00200c48` chains in ascending order, so a
higher bucket draws later and, within one bucket, a *later* submission draws
*earlier*. The bars are bucket `0x1007` -- the same one the full-screen fade
uses (`FUN_0025d0e0` calls `FUN_00207de8(0x1007)`) -- and both `FUN_002239c8`
and `FUN_00224320` submit the fade first, so the bars go down first and the fade
tints them. Every text overlay is bucket `0x1009`: the dialogue glyphs
(`FUN_00237b38` seeds each slot's word 1 with `-0x1009`) and the debug text
(`FUN_00268410` passes the same), so both draw over the bars. `render()` is in
that order.

**One screen for all three.** `MapViewer::originalScreenFit` maps the 640x448
virtual screen onto the game's 4:3 box, and the bars, the subtitles and the
ported debug overlay all go through it. The two text overlays used to fit that
screen *uniformly* into the whole window instead, which is a different rectangle
-- wider than the picture on a 16:9 window, and vertically offset from it on a
4:3 one -- and once the bars exist the disagreement is the difference between
text that clears them and text that does not. The scale is deliberately not
uniform: the field was 640x224 and the display stretched it back to 4:3, which
is where the half-height y unit came from in the first place.

