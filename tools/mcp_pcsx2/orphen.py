#!/usr/bin/env python3
"""Orphen-specific helpers on top of the generic PCSX2 control client.

Everything game-specific lives here rather than in the emulator fork, which stays
general purpose. The addresses are the ones documented in CLAUDE.md,
docs/memory_map.md and docs/debug_system.md.

The entity table deliberately does not reimplement any formatting: it fills the
regions scripts/dump_ee_entities.py reads into a sparse image and runs that
script, so the output stays column-identical to the port's own
`writeDiagnosticSnapshot` (press `G`) and the two can still be diffed directly.
"""

import os
import struct
import subprocess
import sys
import tempfile

from .client import EE_RAM_SIZE, Pcsx2Control

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DUMP_ENTITIES_SCRIPT = os.path.join(REPO_ROOT, "scripts", "dump_ee_entities.py")

# FUN_00266240's entity pool.
POOL_BASE = 0x0058BEB0
POOL_STRIDE = 0x1D8
SLOT_COUNT = 128
POOL_BYTES = POOL_STRIDE * SLOT_COUNT

# The two-byte unlock for the in-game debug menu. Equivalent to the pnach in
# possible_cheats.txt, applied live:
#   patch=1,EE,003555DA,byte,1
#   patch=1,EE,003555DC,byte,1
# Once set, Select opens the menu dispatcher and L2+Select the stepping loop;
# Circle toggles an entry and Start exits.
DEBUG_ACTIVE = 0x003555DA  # cGpffffb66a
DEBUG_OUTPUT = 0x003555DC  # cGpffffb66c

# SCEN WORK DISP: 4 x uint32, so 128 toggles.
SCENE_WORK_FLAGS = 0x0031E770
SCENE_WORK_INDEX = 0x00355128

# Pad state as the game sees it, which is the useful cross-check when driving input.
PAD_RAW_HELD = 0x003555F4
PAD_RAW_PRESSED = 0x003555F6
PAD_MAPPED_HELD = 0x003555F8
PAD_MAPPED_PRESSED = 0x003555FA

# Regions dump_ee_entities.py reads, beyond the pool itself: the script base it
# subtracts to turn the scheduler cursor into a blob offset, and the scheduler /
# fade block it prints as the two values that date a dump exactly.
SCRIPT_BASE_ADDR = 0x00355058
SCHEDULER_BLOCK = 0x00571DC0
SCHEDULER_BLOCK_BYTES = 0xB0


def sparse_ee_image(client, regions):
    """Builds a 32MB image with only `regions` populated.

    dump_ee_entities.py indexes its buffer by absolute PS2 address, so it needs a
    full-size image -- but it only ever touches a few kilobytes of it, and pulling
    the whole 32MB over the socket for every call would be wasteful. Anything not
    listed reads back as zero, which the script already treats as an empty slot.
    """
    image = bytearray(EE_RAM_SIZE)
    results = client.read_many(regions)
    for (address, length), data in zip(regions, results):
        if isinstance(data, Exception):
            raise data
        image[address:address + length] = data
    return image


def entities_table(client, slots=None):
    """Returns the entity pool formatted exactly as scripts/dump_ee_entities.py does."""
    if not os.path.isfile(DUMP_ENTITIES_SCRIPT):
        raise RuntimeError("cannot find %s" % DUMP_ENTITIES_SCRIPT)

    image = sparse_ee_image(client, [
        (POOL_BASE, POOL_BYTES),
        (SCRIPT_BASE_ADDR, 4),
        (SCHEDULER_BLOCK, SCHEDULER_BLOCK_BYTES),
    ])

    handle, path = tempfile.mkstemp(prefix="orphen_ee_", suffix=".bin")
    try:
        with os.fdopen(handle, "wb") as stream:
            stream.write(image)

        command = [sys.executable, DUMP_ENTITIES_SCRIPT, path]
        if slots:
            command.extend(["--slots", slots])
        completed = subprocess.run(command, capture_output=True, text=True, check=False)
        if completed.returncode != 0:
            raise RuntimeError("dump_ee_entities.py failed: %s" % completed.stderr.strip())
        return completed.stdout
    finally:
        try:
            os.unlink(path)
        except OSError:
            pass


def enable_debug_menu(client, enabled=True):
    """Flips the debug-active and debug-output gates."""
    value = bytes([1 if enabled else 0])
    client.write(DEBUG_ACTIVE, value)
    client.write(DEBUG_OUTPUT, value)
    return {"debug_active": DEBUG_ACTIVE, "debug_output": DEBUG_OUTPUT, "enabled": bool(enabled)}


def debug_menu_state(client):
    active, output = client.read_many([(DEBUG_ACTIVE, 1), (DEBUG_OUTPUT, 1)])
    for value in (active, output):
        if isinstance(value, Exception):
            raise value
    return {"debug_active": active[0], "debug_output": output[0]}


def scene_work_flags(client):
    """Reads the 128 SCEN WORK DISP toggles and the currently selected index."""
    flags, index = client.read_many([(SCENE_WORK_FLAGS, 16), (SCENE_WORK_INDEX, 4)])
    for value in (flags, index):
        if isinstance(value, Exception):
            raise value

    words = struct.unpack("<4I", flags)
    set_bits = [bit for bit in range(128) if words[bit // 32] & (1 << (bit % 32))]
    return {
        "words": ["0x%08x" % word for word in words],
        "set_flags": set_bits,
        "selected_index": struct.unpack("<I", index)[0],
    }


def pad_state(client):
    """Reads the pad words the game itself latched this frame."""
    data = client.read(PAD_RAW_HELD, 8)
    raw_held, raw_pressed, mapped_held, mapped_pressed = struct.unpack("<4H", data)
    return {
        "raw_held": "0x%04x" % raw_held,
        "raw_pressed": "0x%04x" % raw_pressed,
        "mapped_held": "0x%04x" % mapped_held,
        "mapped_pressed": "0x%04x" % mapped_pressed,
    }


def main(argv=None):
    """A small CLI, mostly so these can be exercised without an MCP host."""
    import argparse

    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--port", type=int, default=28015)
    parser.add_argument("command", choices=["entities", "debug-on", "debug-off",
                                            "debug-state", "scene-flags", "pad"])
    parser.add_argument("--slots", help="comma-separated slot numbers for 'entities'")
    arguments = parser.parse_args(argv)

    with Pcsx2Control(port=arguments.port) as client:
        if arguments.command == "entities":
            sys.stdout.write(entities_table(client, arguments.slots))
        elif arguments.command == "debug-on":
            print(enable_debug_menu(client, True))
        elif arguments.command == "debug-off":
            print(enable_debug_menu(client, False))
        elif arguments.command == "debug-state":
            print(debug_menu_state(client))
        elif arguments.command == "scene-flags":
            print(scene_work_flags(client))
        elif arguments.command == "pad":
            print(pad_state(client))
    return 0


if __name__ == "__main__":
    sys.exit(main())
