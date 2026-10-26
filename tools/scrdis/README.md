# scrdis — scene script disassembler and explorer

Takes a scene (`sNN_eMMM`), pulls its script out of `MCB0.BIN`/`MCB1.BIN`, and
writes either a self-contained HTML explorer or structured pseudocode.

```bat
python -m tools.scrdis s01_e012                 :: out\scrdis\s01_e012.html
python -m tools.scrdis s01_e012 --text          :: pseudocode to stdout
python -m tools.scrdis path\to\dump.bin         :: a --scr-dump from the port
python -m tools.scrdis scr_jp\scr109.out        :: a JP script (same VM, decodes cleanly)
python -m tools.scrdis s01_e012 --artifact -o x.html   :: no doctype/head/body, for an Artifact publish
```

Checks (run these after changing `opcodes.py`):

```bat
python -m tools.scrdis.elfscan                  :: shapes vs the retail handlers
python -m tools.scrdis.survey                   :: decode all 190 scripts; 0 errors expected
python -m tools.scrdis.crosscheck s01_e012 report.txt   :: vs a port --scr-report
python -m tools.scrdis.battle_logo            :: the US battle-logo sites, as the port's tables
```

## What it shows

- **Code**: every routine reachable from the five header entries, following
  calls (`0x32`), installed bodies (`0x9D`, `0xA8`), NPC bodies (`0x65`,
  `0x66`) and event-stream targets. `if`/`else`/`while`/`switch` are recovered
  from the jumps; each routine has a *flat* toggle showing the raw labelled
  form. Hover a line for its opcode, handler address, where its operand shape
  came from, and its bytes.
- **Event streams** (`0xA1`): the `[delay][gate][target]` records
  `FUN_0025ce30` pays out, with each target's dialogue text or body.
- **Dialogue**: every record in header word 5's table, with speaker, text and
  control codes (voice cues, pauses, flag sets, choices).
- **Paths**: point lists named by `0x41`, `0x43`, `0xBD` method `0x70` and `0x144`.
- **Blob map**: which bytes are accounted for. Unclaimed runs have hex dumps.
- **Unreferenced code (speculative)**: bodies a linear sweep of the gaps
  found that decode cleanly to an `end` but that nothing reachable names. They
  are probably started by engine code (an entity type's handler, the battle
  VM), but treat them as candidates. A body listed in `known.py` (keyed by
  scene and offset) is badged with what it turned out to be and how that was
  checked.

## Reading the pseudocode

| spelling | meaning |
|---|---|
| `work[i]` | script work word `i` (`DAT_00355060`, 128 words) — opcodes `0x36`/`0x37` |
| `flag_byte[i]` | the flag *byte* holding bit `i` (`0x38`/`0x39`) |
| `flag(id)`, `set_flag(id)`… | one event flag bit (`0x3D`..`0x40`); all return the value *before* the write |
| `reg(entity, i)` | object register `i`, which is a field of the entity (`0x76`..`0x7C`) |
| `current` | entity selector `0x100`: keep the current selection |
| `1.25` | a `0x0F` literal: world units (stored ×1000, scaled ×100, read over 100000) |
| `90deg` | a `0x11` literal: degrees, converted to engine angle units |
| `rgb(r, g, b)` | a `0x30`/`0x31` packed colour (the stream stores blue first) |
| `\|\|` | operator `0x1B`, which the VM computes as a **bitwise** OR |
| `body_XXXX`, `sub_XXXX` | a script body / a `0x32` subroutine at that offset |
| `stream_XXXX`, `msg_XXXX`, `path_XXXX` | an event stream, a dialogue record, a point list |

## Where the facts come from

- **VM grammar** (statements, postfix expressions, literals, jumps, switch
  layout): the port's `port/src/ported/script/scene_command_interpreter.cpp`,
  which runs real scenes.
- **Operand shapes** (`opcodes.py`), each tagged with its source: `port` for
  the 207 opcodes the port implements, `elf` for straight-line handlers read
  off the ELF by `elfscan.py`, `ghidra` for unported handlers that branch,
  decompiled by hand. `0xF6`..`0xFE` fall off the end of the standard table
  into the extended one and are listed as aliases.
- **Names** are hypotheses — the port's current reading where it has one, a
  neutral name built from the touched global where nobody knows. Several
  names in `analyzed/opcode_dispatch_tables.md` are known wrong and were not
  copied (e.g. `0x60` is a ground-height query, `0x94` the camera shake).
- **Text control-code widths**: `FUN_00237de8_controlWidth` in
  `port/src/ported/text/original_dialogue_window.cpp`.

## Validation, as of 2026-10-25

- All 190 scene scripts decode with **0 errors**: 116,631 instructions, 8,959
  routines, 504 event streams, and no jump lands inside another instruction.
- Every routine structures (each instruction is emitted exactly once, or the
  routine falls back to flat).
- Against the port's `--scr-report`: every opcode site the port executed in
  `s01_e012`, `s01_e024`, `s14_e001`, `s14_e002` and `s03_e001` (438 sites) is
  an instruction start here with the same opcode.

## Not decoded yet

- Header word 7's section table entry 0, the **battle encounter** — it holds
  scripts for `FUN_0023FD30`'s separate 19-opcode battle VM.
- Header words 8 and 9.
- Whatever starts the speculative bodies.
- `0xD2`/`0xD3` read and write the blob itself; the offsets they touch are
  shown as `data_XXXX` but not interpreted.
