#!/usr/bin/env python3
"""MCP server exposing a running PCSX2 instance as tools.

Requires the control server built into the fork at C:/Users/zptha/projects/pcsx2
(`pcsx2/ControlServer.cpp`), enabled with `EnableControlServer=true` under
`[EmuCore]` in `PCSX2.ini`.

    pip install 'mcp>=2'
    python -m tools.mcp_pcsx2.server

The general shape of a session: `pcsx2_pause`, then read/step/read. Reading while
the VM runs gives a value torn relative to the game's own frame, which is exactly
the problem frame-stepping exists to avoid.
"""

import base64
import os
import tempfile

from mcp.server.mcpserver import MCPServer

from . import orphen
from .client import ControlError, Pcsx2Control, TransportError, read_gs_dump

mcp = MCPServer("pcsx2")

_client = None


def _connect():
    """Returns a live client, reconnecting if the emulator was restarted."""
    global _client
    if _client is None:
        _client = Pcsx2Control(port=int(os.environ.get("PCSX2_CONTROL_PORT", "28015")))
    try:
        _client.connect()
        _client.ping()
    except TransportError:
        # The emulator went away between calls; drop the socket and try once more so a
        # restart does not require restarting this server too.
        _client.close()
        _client.connect()
        _client.ping()
    return _client


def _state_line(reply):
    return "frame=%s vsync=%s state=%s" % (reply.get("frame"), reply.get("vsync"), reply.get("state"))


def _hexdump(data, base_address):
    lines = []
    for offset in range(0, len(data), 16):
        row = data[offset:offset + 16]
        hex_part = " ".join("%02x" % byte for byte in row)
        text = "".join(chr(b) if 32 <= b < 127 else "." for b in row)
        lines.append("%08x  %-47s  %s" % (base_address + offset, hex_part, text))
    return "\n".join(lines)


# -- generic emulator control ---------------------------------------------


@mcp.tool()
def pcsx2_status() -> str:
    """Report what the emulator is doing: VM state, frame counters, loaded game."""
    reply = _connect().status()
    return (
        "%s\nhas_vm=%s title=%r serial=%s disc_crc=%s elf_crc=%s hardcore=%s"
        % (_state_line(reply), reply.get("has_vm"), reply.get("title"), reply.get("serial"),
           reply.get("disc_crc"), reply.get("elf_crc"), reply.get("hardcore"))
    )


@mcp.tool()
def pcsx2_read(address: str, length: int, save_to: str = "") -> str:
    """Read PS2 memory. Returns a hexdump, or writes to save_to for anything large.

    address accepts hex ("0x58BEB0") or decimal. Pause first if the value needs to be
    coherent with the game's own frame.
    """
    client = _connect()
    base_address = int(address, 0)
    data = client.read(base_address, length)

    if save_to:
        with open(save_to, "wb") as handle:
            handle.write(data)
        return "wrote %d bytes from 0x%08X to %s (%s)" % (
            len(data), base_address, save_to, _state_line({"frame": client.last_frame,
                                                           "vsync": client.last_vsync,
                                                           "state": client.last_state}))

    if len(data) > 4096:
        return ("%d bytes is too much to show inline; pass save_to to write it to a file, "
                "or read a smaller range." % len(data))

    return "%s\n%s" % (_state_line({"frame": client.last_frame, "vsync": client.last_vsync,
                                    "state": client.last_state}), _hexdump(data, base_address))


@mcp.tool()
def pcsx2_write(address: str, hex_bytes: str) -> str:
    """Write raw bytes to PS2 memory. hex_bytes is unspaced hex, e.g. "0101".

    Two caveats: a failed write may have partially landed, and writes do not invalidate
    recompiled blocks, so overwriting EE code has no effect until that block is flushed.
    """
    client = _connect()
    payload = bytes.fromhex(hex_bytes.replace(" ", ""))
    reply = client.write(int(address, 0), payload)
    return "wrote %d bytes to %s (%s)" % (reply["len"], reply["addr"], _state_line(reply))


@mcp.tool()
def pcsx2_pause() -> str:
    """Pause emulation and wait for it to actually be paused."""
    return _state_line(_connect().pause())


@mcp.tool()
def pcsx2_resume() -> str:
    """Resume emulation."""
    return _state_line(_connect().resume())


@mcp.tool()
def pcsx2_step(frames: int = 1) -> str:
    """Advance exactly `frames` frames, then pause again.

    vsync advances by exactly this many; frame (the emulator's g_FrameCount) trails by
    one at the moment the step completes, because it increments at vsync end.
    """
    return _state_line(_connect().step(frames))


@mcp.tool()
def pcsx2_reset() -> str:
    """Reset the virtual machine. Refused while a memory card is mid-write."""
    return _state_line(_connect().reset())


@mcp.tool()
def pcsx2_screenshot(save_to: str = "") -> str:
    """Capture the current frame as a PNG and return the path to read it from."""
    client = _connect()
    path = save_to
    if not path:
        handle, path = tempfile.mkstemp(prefix="pcsx2_shot_", suffix=".png")
        os.close(handle)

    png, reply = client.screenshot()
    with open(path, "wb") as stream:
        stream.write(png)
    return "%s\n%dx%d PNG written to %s" % (_state_line(reply), reply["width"], reply["height"], path)


@mcp.tool()
def pcsx2_gs_dump(path: str, decompress: bool = True) -> str:
    """Capture a single-frame GS dump: every draw the GS sees for one frame.

    This is what a screenshot cannot give you -- the register writes and vertex batches
    behind the image, which port/attic/gsparse.py walks to attribute a draw to its TEX0,
    CLUT and blend state. Reach for it when a question is "which sheet is this drawn
    from" rather than "what is on screen".

    Recording starts at the *next* frame, so step to the frame before the one you want.
    While paused the emulator advances a handful of frames to let the dump close and then
    pauses again, so load_state -> step -> gs_dump reproduces exactly.

    The draws are deterministic across runs from the same state; the GS memory snapshot
    embedded in the header is not quite, because the hardware renderer does not flush its
    texture cache back before freezing. That affects replaying the dump, not parsing it.

    With decompress, a .gs.zst or .gs.xz is also written out as a plain .gs so gsparse can
    be pointed straight at it.
    """
    reply = _connect().gs_dump(path)
    written = reply["path"]
    lines = ["GS dump: %s (%d bytes, %d frames advanced)"
             % (written, reply["size"], reply["frames_advanced"])]

    if decompress and not written.lower().endswith(".gs"):
        raw = read_gs_dump(written)
        plain = written[:written.lower().rindex(".gs") + 3]
        with open(plain, "wb") as stream:
            stream.write(raw)
        lines.append("uncompressed copy for gsparse: %s (%d bytes)" % (plain, len(raw)))

    lines.append(_state_line(reply))
    return chr(10).join(lines)


@mcp.tool()
def pcsx2_save_state(path: str) -> str:
    """Save a state to an absolute path, returning only once the file is written.

    Use this to bookmark a scenario before poking at it, so the same starting point can
    be restored later. Pause first: the compression runs on the emulation thread.
    """
    reply = _connect().save_state(path)
    return "saved state to %s (%s)" % (reply["path"], _state_line(reply))


@mcp.tool()
def pcsx2_load_state(path: str) -> str:
    """Restore a state from an absolute path. This is how a scenario gets reset.

    The stable Orphen entry point is savestates/entry_stable.p2s in this repo. Note that
    `frame` jumps to whatever the state recorded while `vsync` keeps climbing, and that a
    state which starts loading and then fails leaves the VM reset.
    """
    reply = _connect().load_state(path)
    return "loaded %s (%s)" % (reply["path"], _state_line(reply))


@mcp.tool()
def pcsx2_press(buttons: str, hold: int = 3, gap: int = 5, step_after: int = 8) -> str:
    """Press pad buttons for a few frames, release, and advance past it.

    buttons is comma-separated, e.g. "select" or "l2,select". Known names: up, down,
    left, right, triangle, circle, cross, square, select, start, l1, l2, r1, r2, l3, r3,
    analog. The game sees the press about two frames after it is queued, so step_after
    should leave slack past hold+gap.
    """
    client = _connect()
    names = [b.strip() for b in buttons.split(",") if b.strip()]
    queued = client.press(names, hold=hold, gap=gap)
    reply = client.step(queued + step_after)
    return "pressed %s (%d frames queued, stepped %d)\n%s" % (
        "+".join(names), queued, queued + step_after, _state_line(reply))


@mcp.tool()
def pcsx2_send_input(frames_json: str, port: int = 0) -> str:
    """Queue an exact per-frame pad script, for sequences `pcsx2_press` cannot express.

    frames_json is a JSON array, one entry per frame:
    [{"buttons": ["l2", "select"], "repeat": 4}, {"buttons": [], "repeat": 4}]
    Frames are absolute -- a button not named is released. Analog sticks take raw bytes
    with 127 neutral: {"analog": {"lx": 255}} is full right. Step exactly as many frames
    as were queued to play the whole thing.
    """
    import json as _json
    frames = _json.loads(frames_json)
    reply = _connect().send_input(frames, port=port)
    return "queued %s frames total (%s)" % (reply["queued"], _state_line(reply))


@mcp.tool()
def pcsx2_clear_input() -> str:
    """Drop anything still queued and release the pad."""
    reply = _connect().clear_input()
    return "dropped %s queued frames (%s)" % (reply["dropped"], _state_line(reply))


@mcp.tool()
def pcsx2_dump_ee(save_to: str) -> str:
    """Dump all 32MB of EE main RAM, laid out exactly like a save state's eeMemory.bin.

    Addresses map 1:1 onto file offsets, so the repo's offline scripts read it unchanged.
    """
    written = _connect().dump_ee(save_to)
    return "wrote %d bytes to %s" % (written, save_to)


# -- Orphen-specific ------------------------------------------------------


@mcp.tool()
def orphen_entities(slots: str = "") -> str:
    """Dump the Orphen entity pool, formatted identically to the native port's snapshot.

    Output matches scripts/dump_ee_entities.py column for column, so it can be diffed
    straight against the port's `G` snapshot. slots is an optional comma-separated list.
    """
    return orphen.entities_table(_connect(), slots or None)


@mcp.tool()
def orphen_enable_debug_menu(enabled: bool = True) -> str:
    """Unlock the in-game debug menu by setting the two gate bytes.

    Once on, Select opens the menu dispatcher and L2+Select the stepping loop; Circle
    toggles an entry, Start exits.
    """
    result = orphen.enable_debug_menu(_connect(), enabled)
    return "debug menu %s (0x%08X, 0x%08X)" % (
        "enabled" if result["enabled"] else "disabled",
        result["debug_active"], result["debug_output"])


@mcp.tool()
def orphen_debug_menu_state() -> str:
    """Report whether the debug gates are currently set."""
    state = orphen.debug_menu_state(_connect())
    return "debug_active=%d debug_output=%d" % (state["debug_active"], state["debug_output"])


@mcp.tool()
def orphen_scene_flags() -> str:
    """Read the 128 SCEN WORK DISP toggles and the selected index."""
    flags = orphen.scene_work_flags(_connect())
    return "words=%s\nset=%s\nselected_index=%d" % (
        " ".join(flags["words"]), flags["set_flags"], flags["selected_index"])


@mcp.tool()
def orphen_pad_state() -> str:
    """Read the pad words the game itself latched this frame."""
    state = orphen.pad_state(_connect())
    return " ".join("%s=%s" % (key, value) for key, value in state.items())


if __name__ == "__main__":
    mcp.run()
