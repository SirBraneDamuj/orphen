# Driving PCSX2 directly

This replaces the manual loop of "take a save state, unzip it, hand over
`eeMemory.bin`" with a socket an agent can talk to while the game is running.

Two pieces:

| Piece | Where | What it is |
|---|---|---|
| `ControlServer` | the PCSX2 fork at `C:/Users/zptha/projects/pcsx2` | `pcsx2/ControlServer.{h,cpp}`, a loopback TCP server speaking line-delimited JSON. Game-agnostic. |
| `tools/mcp_pcsx2/` | this repo | The Python client, the Orphen-specific helpers, and an MCP server wrapping both. |

The split is deliberate: every Orphen address lives on this side, so the
emulator fork stays a small general-purpose diff that can be rebased onto
upstream.

## Setup

1. Build the fork (Visual Studio solution `PCSX2_qt.sln`, x64). Use **Release**:
   it emulates at a full 60fps against Debug's ~12, save and load drop from
   ~0.4s to ~0.08s, and `Console` output still reaches `emulog.txt` -- only
   `DevCon`/`DbgCon` are stripped, which makes the log more readable, not less.
   Emulation is bit-identical between the two: the same state stepped the same
   number of frames gives the same entity pool either way.
2. Enable the server. In `PCSX2.ini`, under `[EmuCore]`:

   ```ini
   EnableControlServer = true
   ControlServerPort = 28015
   ```

   It is off by default, and the listener only ever binds to `127.0.0.1`. There
   is no authentication and `write_bytes` is an arbitrary memory write into the
   emulator process, so leave it off when you are not using it.

3. Launch PCSX2. The console log prints
   `ControlServer: listening on 127.0.0.1:28015.`

The setting is picked up whenever settings are applied, so toggling it does not
need a restart. (PINE, by contrast, only reloads on a game change.)

## Checking it works

No Python needed:

```bash
printf '{"id":1,"op":"ping"}\n{"id":2,"op":"status"}\n' | ncat 127.0.0.1 28015
```

Or with the bundled client:

```bash
python -m tools.mcp_pcsx2.client status
python -m tools.mcp_pcsx2.client read 0x58BEB0 472
python -m tools.mcp_pcsx2.client step 10
```

## The MCP server

```bash
pip install mcp
python -m tools.mcp_pcsx2.server
```

Register it in `.mcp.json` at the repo root:

```json
{
  "mcpServers": {
    "pcsx2": {
      "command": "python",
      "args": ["-m", "tools.mcp_pcsx2.server"],
      "cwd": "C:/Users/zptha/projects/orphen/decompiled"
    }
  }
}
```

Tools: `pcsx2_status`, `pcsx2_read`, `pcsx2_write`, `pcsx2_pause`,
`pcsx2_resume`, `pcsx2_step`, `pcsx2_reset`, `pcsx2_screenshot`,
`pcsx2_save_state`, `pcsx2_load_state`, `pcsx2_press`, `pcsx2_send_input`,
`pcsx2_clear_input`, `pcsx2_dump_ee`, plus `orphen_entities`, `orphen_enable_debug_menu`,
`orphen_debug_menu_state`, `orphen_scene_flags`, `orphen_pad_state`.

`orphen_entities` does not reimplement any formatting — it fills the regions
`scripts/dump_ee_entities.py` reads into a sparse image and runs that script, so
its output stays column-identical to the port's `writeDiagnosticSnapshot` (press
`G`) and the two can still be diffed directly.

## The stable entry state

`savestates/entry_stable.p2s` is the known-good starting point, read-only so the
slot rotation cannot clobber it. `savestates/README.md` describes the scene and
records the determinism check behind it. Launch into it with:

```bash
"C:/Users/zptha/projects/pcsx2/bin/pcsx2-qtx64.exe"   -statefile "C:/Users/zptha/projects/orphen/decompiled/savestates/entry_stable.p2s"   -- "D:/PS2/Orphen - Scion of Sorcery (USA).iso"
```

Two launches from it, each stepped 30 frames from a pause, gave a byte-identical
entity pool. Within a session, `load_state` resets to it without restarting the
emulator -- so the usual loop is: load it, poke at something, load it again.

## Driving the pad

`send_input` queues pad states, one drained per emulated frame in
`PollInputOnCPUThread()` just after `InputManager::PollSources()` — so a real
controller plugged in at the same time cannot clobber an injected frame.

```json
{"op":"send_input","frames":[{"buttons":["select"],"repeat":4},{"buttons":[],"repeat":4}]}
```

Frames are **absolute, not deltas**: a button not named is released. Analog
sticks take raw bytes with `127` neutral, so `{"analog":{"lx":255}}` is full
right. `repeat` expands one entry into N identical frames. Known button names:
`up down left right triangle circle cross square select start l1 l2 r1 r2 l3 r3
analog`.

Queue the frames, then `step` exactly that many, and the sequence is
reproducible to the byte — the same state plus the same script produced an
identical screenshot across runs. `status` reports `input_queued` so a client can
tell how much is left to play.

Opening the Orphen debug menu, end to end:

```python
client.load_state("savestates/entry_stable.p2s")
n = client.press("select", hold=4, gap=4)   # queues 8 frames, returns 8
client.step(n + 20)
```

### Things that bite specifically here

**The game sees a press about two frames after it is queued.** Injection happens
at vsync; the game polls SIO during its own frame and only then copies to its pad
words. The *duration* is exact — `repeat: 3` was observed as exactly three frames
held at `0x3555F4` — but it is shifted. Step past the press rather than expecting
to read it on the next frame.

**One-frame presses are a bad idea.** A game that debounces its own input can
miss them. Three frames down and five up is a reliable default, and is what
`client.press()` does.

**Nothing stays stuck.** When the queue drains, the pad is explicitly released,
because `InputManager` only writes a bind when something changed — without that
release the last injected frame's buttons would stay held forever. The release
lands before the game's next read, so it costs no emulated frame.

**Injection only moves while the VM runs.** The queue drains one entry per frame,
so nothing happens while paused. `step` is what plays the script.

## Capturing a frame's draws

`gs_dump` writes a single-frame GS dump: the register writes and vertex batches
the GS actually saw, which is what `port/attic/gsparse.py` walks. Use it when the
question is "which sheet is this drawn from" rather than "what is on screen" — a
screenshot cannot answer the first.

```python
c.load_state("savestates/entry_stable.p2s")
c.pause()
c.step(20)                       # land on the frame before the one you want
r = c.gs_dump("dumps/smoke")     # -> dumps/smoke.gs (and dumps/smoke.png)
draws = gsparse.GS()
```

Three things about the timing:

- **Recording starts at the next vsync**, not this one. Step to the frame
  *before* the interesting one.
- **A "single frame" dump is not one vsync long.** `GSDumpBase` closes the file
  only after an even number of fields have gone by with its last-frame flag set,
  and it starts with two extra frames in hand — about five vsyncs in practice.
  The server polls the renderer rather than guessing, and reports how many frames
  it burned as `frames_advanced` (6 in the common case).
- **A paused VM emits no vsyncs**, so the op advances frames itself and leaves
  the machine paused again. When the VM is already running it just waits, and
  `frames_advanced` comes back 0.

### Compression, and what is actually deterministic

The extension follows the emulator's `GSDumpCompression` setting — `.gs`,
`.gs.zst` or `.gs.xz` — so the reply carries the real path rather than assuming.
`client.read_gs_dump()` handles all three (`zstandard` is imported lazily, and is
the one third-party package the client can want). Setting `GSDumpCompression = 0`
in `PCSX2.ini` writes plain `.gs` and skips the question.

Two dumps taken from the same save state at the same frame have a
**byte-identical packet stream** — that is the guarantee the harness rests on.
Their headers differ slightly: the frozen copy of GS local memory carries
framebuffer residue that the hardware renderer never flushed back, so a few
hundred bytes wobble between runs. It does not affect parsing the draws. If it
ever matters for *replaying* a dump, `UserHacks_ReadTCOnClose = true` makes the
renderer read its texture cache back before freezing.

## Wire protocol

TCP on `127.0.0.1`, one JSON object per line, `\n`-terminated. Requests are
answered in order; `id` is echoed so replies can be correlated. Clients are
served one at a time — a second connection waits in the listen backlog.

Request:

```json
{"id": 1, "op": "read_bytes", "addr": "0x58BEB0", "len": 472}
```

Every reply, successful or not, carries the frame and state block:

```json
{"id":1,"ok":true,"frame":91432,"vsync":5510,"state":"paused",
 "addr":"0x0058beb0","len":472,"data_b64":"AAEC..."}
```

```json
{"id":1,"ok":false,"frame":91432,"vsync":5510,"state":"paused",
 "error":{"code":"bad_address","message":"unreadable memory in [0x1fc00000, 0x1fc00004)"}}
```

`state` is one of `shutdown`, `initializing`, `running`, `paused`, `resetting`,
`stopping`.

### Operations

| op | request | reply |
|---|---|---|
| `ping` | — | `protocol_version`, `pcsx2_version` |
| `status` | — | `has_vm`, `title`, `serial`, `disc_crc`, `elf_crc`, `hardcore`, `input_queued` |
| `read_bytes` | `addr`, `len` (≤16MB), `format`: `b64`\|`hex` | `addr`, `len`, `data_b64`\|`data_hex` |
| `read_many` | `reads[]` (≤1024 entries, ≤64MB total) | `results[]`, each `{ok, addr, len, data_b64}` or `{ok:false, error}` |
| `write_bytes` | `addr`, `data_b64`\|`data_hex` | `addr`, `len` |
| `pause` / `resume` | `wait` (default true), `timeout_ms` (5000) | — |
| `step` | `frames` (1..100000), `timeout_ms` (15000) | `frames` |
| `reset` | `wait`, `timeout_ms` (30000) | — |
| `screenshot` | `width`, `height`, `aspect`, `crop`, `quality`, `path`? | `width`, `height`, `format`, `data_b64` or `path` |
| `save_state` | `path` (absolute), `backup` (default false), `timeout_ms` (60000) | `path` |
| `load_state` | `path` (absolute), `timeout_ms` (60000) | `path` |
| `gs_dump` | `path` (absolute, extension optional), `timeout_ms` (30000) | `path`, `size`, `frames_advanced` |
| `send_input` | `frames[]` (each `buttons[]`, `analog{lx,ly,rx,ry}`, `repeat`), `port` | `queued` |
| `clear_input` | — | `dropped` |

Addresses accept a hex string (`"0x58BEB0"`, `0x` optional) or an integer;
replies always use lowercase hex.

### Errors, in three tiers

A client should distinguish these, because they call for different responses:

1. **Transport** — EOF or a socket error. No JSON. Reconnect.
2. **Protocol** — `bad_request`, `unknown_op`, `protocol_error`. The request was
   not usable. `protocol_error` is a framing violation, and the server closes the
   connection after sending it.
3. **Operation** — the request was fine but could not be carried out; the
   connection stays up. `no_vm`, `bad_state`, `bad_address`, `too_large`, `busy`,
   `timeout`, `interrupted`, `unsupported` (achievements hardcore mode),
   `internal`.

`client.py` maps these onto `TransportError`, `ProtocolError` and `OpError`
(which carries `.code`).

## Things that will bite

**`frame` is not the step counter.** `vsync` is the server's own monotonic
count and is authoritative: `step N` advances it by exactly N. `frame` is the
emulator's `g_FrameCount`, which increments at vsync *end* while a frame advance
auto-pauses at vsync *start* — so it trails by one at the moment a step
completes. It also resets to 0 on VM reset, while `vsync` keeps climbing. Report
`frame` because it cross-references against PCSX2 logs and pnaches; count with
`vsync`.

**Booting with `-statefile` is one frame ahead of an in-session `load_state`.**
The command line loads the state and then lets the VM run; by the time a client
connects and pauses, a vsync has already gone by. So `-statefile` + `step 30`
lands on the same state as an in-session `load_state` + `step 31`. Every frame
index has its own reproducible contents, so an off-by-one here is silent -- take
a `vsync` reading immediately after the load and count from that, rather than
comparing across the two entry paths.

**A failed `load_state` resets the VM.** That is `VMManager::LoadState()`'s own
behaviour: if it gets as far as reading the file and the read fails, it resets
rather than leaving a half-restored machine. The server checks the file exists
first, so a mistyped path comes back as `bad_request` with nothing lost, but a
truncated or version-mismatched state still costs the session. The error message
says when that happened. Memory ops return `bad_state` until the reset finishes.

**`load_state` rewinds `frame` but not `vsync`.** `frame` is restored from
whatever the state recorded; `vsync` is the server's own counter and keeps
climbing across loads. Neither is a reliable "time since load" — take a `vsync`
reading right after the load and subtract.

**Reads while running are torn.** The EE may be mid-write to the struct you are
reading. Pause first. The intended loop is pause → read → step → read.

**`pause` is not instant.** Queued work is drained once per vsync, so a pause
issued while running takes up to a frame. Always let the `pause` reply come back
before reading; that is what `wait: true` (the default) is for.

**`read_many` is not an atomic snapshot.** The reads happen back to back on one
thread, within microseconds — but microseconds is not zero. Pause first if
coherence matters.

**Writes do not invalidate recompiled code.** Overwriting EE instructions has no
effect until that block is naturally flushed. Data writes are fine.

**A failed write may have partially landed.** The underlying copy is page by
page and can fail midway, so treat `bad_address` on a write as indeterminate.

**Memory access is refused unless the VM is `running` or `paused`.** During
`resetting` the vtlb map is being rewritten underneath, and a read there would
return torn data; you get a clean `bad_state` instead.

## Current limits

All of it — memory and VM control, screenshots, save states by path,
per-frame input injection and single-frame GS dumps — is built and verified against the running game.

Known gaps:

- **DualShock2 only.** Bind indices come from `PadDualshock2::Inputs`; injection
  is skipped for any other pad type rather than pressing whatever happens to sit
  at those offsets.
- **One client at a time.** A second connection waits in the listen backlog
  rather than being refused.
- **A timed-out `step` is not cancelled.** The frame advance keeps counting down
  in the background; issue a `pause` if that matters. The default step timeout
  scales as `15000 + frames × 100` ms, capped at 120s. That was sized for a
  Debug build at ~12fps; in Release it is generous. Steps beyond ~1200 frames
  need an explicit `timeout_ms` or to be issued in chunks.
- **Multi-frame GS dumps are not exposed.** `GSQueueSnapshot` takes a frame
  count and the op always passes 1. Multi-frame is a couple of lines away if a
  question ever needs it, but the files get large quickly.
- **Writes do not invalidate recompiled code**, and `read_many` is not an atomic
  snapshot — see the notes above.
