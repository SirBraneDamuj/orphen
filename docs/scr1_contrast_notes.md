# SCR1 contrast notes

This note contrasts `scr/scr1.out` with the richer `scr/scr2.out` map. Original FUN\_\* names are preserved where relevant.

## Layout

- File size: `0x1096` bytes, much smaller than `scr2.out` (`0xd916`).
- Header words:
  - `[0] 0x002c`
  - `[1] 0x0040`
  - `[2] 0x0223`
  - `[3] 0x03d5`
  - `[4] 0x03d6`
  - `[5] 0x04fc`
  - `[6] 0x0500`
  - `[7] 0x1094`
  - `[8] 0x0534`
  - `[9] 0x0538`
  - `[10] 0x053c`
- There is no SCR2-style dialogue segment. `header[5]` points at `0x04fc`, which contains a zero word, while `header[0]` is the init entrypoint at `0x002c`. So `header[0]` cannot be treated as a dialogue pointer-table end for SCR1.
- The footer is only two bytes, `00 10`, matching the one timed stream offset `0x1000`.

## Header Entrypoints

- `0x002c`: scene init entrypoint (`FUN_0025b6d0` path). Current decode is not fully trusted because it contains opcode `0xE5` followed by a low-range expression byte; keep this as a caveat.
- `0x0040`: scene start entrypoint (`FUN_0025b728` path). It sets six global event flags: `1280+415`, `1280+418`, `1280+419`, `1280+420`, `1280+421`, `1280+422`.
- `0x0223`, `0x03d5`, `0x03d6`: immediate block ends in the raw entrypoint decode.

## Timed Stream

SCR1 still uses the same `FUN_0025ce30` timed scheduler row shape seen in SCR2:

```text
[u16 delay_frames][u16 flags][u32 target_offset]
```

- Opcode `0xA1` at `0x021a` arms scheduler slot `0` with stream `0x1000`.
- Stream `0x1000` has 17 non-sentinel records and a sentinel at `0x1088`.
- Most records target `0x0550`, a short effect/audio body:

```python
ctx.dispatch_tagged_event(value=ctx.pack_rgb(255, 255, 255), tag=128, addr=0x00000550)
ctx.audio_play_for_entity(tag=586, entity_index=1, flags=150, addr=0x0000055f)
ctx.finish_process_slot(slot=-1, addr=0x00000569)
```

- One record targets `0x0573`, a `work[3]` switch/state machine.

## Subproc-like State Machine At 0x0573

The body at `0x0573` is selected by stream `0x1000` record 4. It switches on `work[3]`:

```python
cases={
    0: 0x000005b4,
    1: 0x000005dd,
    2: 0x000005fc,
    3: 0x0000061b,
    4: 0x0000063a,
    5: 0x00000659,
}
default=0x0000066d
```

The cases alternate minimap marker slot 2 between flag `0x80` and `0`, using `work[11]` as the value, then advance `work[3]`. Case 5 clears the marker and finishes the process slot. Case 0 also plays audio tag `0x024a` for entity index `1`.

## Less Obvious Similarities To SCR2

- Same 11-word SCR header shape and same five VM entrypoint slots.
- Same global flag and script work array mechanisms (`0x3E`, `0x37`, `0x36`).
- Same process-slot/subproc machinery (`0x9D`, `0x9E`) and scheduler arming opcode (`0xA1`).
- Same scheduler record format, even without dialogue targets.
- Same low-op structural state-machine patterns: `0x02` jump table and `0x03` relative jump.
- Instead of dialogue records, the timed stream primarily drives visual/audio pulses and a small marker/audio state machine.

## Tooling Notes

`tools/scr_decompile.py` now has SCR1-relevant signatures for effect, audio, light, and minimap opcodes, plus structural handling for low-op `0x02` jump tables and `0x03` relative jumps.
