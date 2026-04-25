# PCSX2 animation-binding hunt — quick field guide

Goal: find the function that actually drives the animations we can see
on screen during s00_e000 (the swooping ORPHEN battle logo + at least
two other idle-posed entities). Our first guess `FUN_0020da68` does
NOT fire for any of them, so we need to widen the search.

Validated subject: **grp_0183** (the ORPHEN logo glyph rig).
According to our parser it has 11 submeshes total, of which 7 carry
pose tables (sm2..sm8) — that lines up with the 6 letters in ORPHEN
plus a backdrop slab.

## Prep

1. Launch PCSX2 1.7+ with the US NTSC ISO.
2. Tools → Memory Search / Debug → open the **Debugger** (Ctrl+Shift+D).
3. Get to the s00_e000 scene where you can see the swooping logo and
   the two idle entities.

## Step 0 — sanity-check the breakpoint UI

PCSX2 has multiple BP modes (Execute / Read / Write, EE vs IOP vs VU).
We want **EE → Execute**.

Set a BP on `0x00204db8` (the main game loop, `FUN_00204db8`). It
fires every frame. If THIS doesn't hit either, the BP isn't being
applied — the issue is the debugger config, not our code map.

Once that's confirmed, remove it and proceed.

## Candidate sampler / render functions

We were wrong that `FUN_0020da68` is the canonical animation sampler.
It's *one* sampler (only invoked when `entity+0x9c != 0` and the
render path goes through `FUN_0020eec0`). Other entry points:

| BP address | Function | Why it might be the right one |
| ---------- | -------- | ----------------------------- |
| `0x0020c810` | `FUN_0020c810` — per-entity render setup | Reads `entity+0x15c` (PSC3 base), `entity+0x9c` (anim table), `entity+0xa0` (anim id). Calls FUN_0020eec0 + FUN_0020e840 + FUN_0020dfb0. **Set this one first** — it's the funnel for the standard render path. |
| `0x0020e840` | `FUN_0020e840` — alt path called right after `FUN_0020eec0` | Possibly a per-vertex transform path that does its own pose lookup. |
| `0x0020dfb0` | `FUN_0020dfb0` — final tail call from setup | Same. |
| `0x0020dc88` | `FUN_0020dc88` — also called by `FUN_0029c7a8` for entity+0x140 reads | Used to fetch a position/quat with a different argument pattern. |
| `0x00212058` | `FUN_00212058` — per-primitive render loop | The actual draw call. If this hits but no upstream sampler does, the pose math is happening inline in this function (likely VU0 macro mode). |
| `0x00204db8` | `FUN_00204db8` — main game loop | Sanity check (Step 0). |

## Suggested order

1. Step 0 (`0x00204db8`) — confirm BPs work at all.
2. Set BP at `0x0020c810` — this is the per-entity draw entrypoint.
   It SHOULD fire for the logo. When it does:
   - `a0` = render-context ptr (param_1 — global render scratch)
   - `a1` = entity ptr (param_2)
   - At entry, read `entity+0x15c` to confirm the PSC3 base. It should
     match the address where `grp_0183.psc3`'s magic `PSC3` (`0x33435350`)
     sits in RAM.
3. From there, single-step over each `jal` and watch for which child
   function actually transforms vertices — that one will read from
   `entity+0x9c` or `psc3+0x2c` (the keyframe pool).

## Data to capture

Once you've found a hit on the LOGO entity (verified via PSC3 base
match in step 2), dump:

1. `entity + 0x80` to `entity + 0x180` — 256 bytes of entity state.
   This captures the unknown fields around `+0x9c` `+0xa0` `+0xa8`
   `+0x140` `+0x15c`.
2. The 32 bytes at `[entity+0x9c]` (deref the pointer first), AND the
   512 bytes after that. This is the anim-table region.
3. The `psc3_base` value AND the file region you'd need to dump to
   reconstruct the in-memory PSC3 (we already have the file — we just
   need to confirm the addresses match what's loaded).

PCSX2 debugger: right-click in memory view → "Copy bytes as hex" or
use the Python console:
```
ee.ReadMemory(<addr>, <size>)
```
Save each as a separate `.bin` file named after the offset, e.g.:
- `entity_state.bin`           (0x100 bytes @ entity+0x80)
- `anim_table_deref.bin`        (0x200 bytes @ *(u32*)(entity+0x9c))

## Bonus question to answer

When `0x0020c810` (or whichever sampler ends up being the right one)
fires, walk **up the call stack** — write down the return
addresses. That tells us who's driving the per-frame update (bytecode
VM? scene-update loop? type-0x49 dedicated handler?), which gives us
the `src/FUN_*` to read next.

## When you're done

Drop the dumps into `out/capture/s00_e000_logo/` and I'll decode them.
With even just the raw 512-byte window at `entity+0x9c` we can
probably match the pattern against the sibling `grp_0129.bin`,
`grp_018d.bin`, `grp_018e.bin` candidates and confirm whether the
timeline lives in one of those or somewhere else entirely.

## Safety / what to skip

- Don't worry about texture breakpoints this round — animation hunt only.
- If `0x0020c810` fires too often (it'll hit once per visible entity
  per frame, so several times per frame), gate it on `a1 == <logo entity ptr>`
  once you've ID'd the logo's entity address. The logo entity lives at
  fixed address `0x58C7E8` (per `analyzed/battle_logo_state_manager.c`),
  so `a1 == 0x58c7e8` is your gate.
- No need for screenshots.
