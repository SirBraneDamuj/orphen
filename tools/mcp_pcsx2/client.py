#!/usr/bin/env python3
"""Transport for the PCSX2 loopback JSON control server.

The server is a fork-only addition to PCSX2 (`pcsx2/ControlServer.cpp`), enabled
with `EnableControlServer=true` under `[EmuCore]` in `PCSX2.ini`. It speaks one
JSON object per line over TCP on 127.0.0.1, and every reply carries the frame
counter and VM state, so a caller never has to ask separately what the machine
was doing when its request landed.

This module has no dependencies beyond the standard library, so it doubles as a
way to exercise the protocol by hand:

    python -m tools.mcp_pcsx2.client status
    python -m tools.mcp_pcsx2.client read 0x58BEB0 472
    python -m tools.mcp_pcsx2.client step 10

Frame accounting, which is easy to get wrong: `vsync` is the server's own
monotonic count and is the authoritative step measure -- `step N` guarantees
`vsync` advances by exactly N. `frame` is the emulator's `g_FrameCount`, which
increments at vsync *end* while a frame advance auto-pauses at vsync *start*,
so it trails by one at the moment a step completes, and it resets to 0 when the
VM is reset. Report `frame` because it cross-references against PCSX2 logs and
pnaches; count with `vsync`.
"""

import argparse
import base64
import json
import os
import socket
import sys

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 28015

# The server clamps every operation to at most 120s, so anything longer than that
# on the socket means the server itself is gone rather than merely busy.
DEFAULT_SOCKET_TIMEOUT = 130.0

# PS2 EE main RAM. Addresses map 1:1 onto offsets in an `eeMemory.bin`.
EE_RAM_BASE = 0x00000000
EE_RAM_SIZE = 32 * 1024 * 1024

# Matches MAX_READ_BYTES in ControlServer.cpp.
MAX_READ_BYTES = 16 * 1024 * 1024

# Failures the server reports for a request it could not even interpret, as
# opposed to one it understood but could not carry out.
PROTOCOL_ERROR_CODES = frozenset({"bad_request", "unknown_op", "protocol_error"})


class ControlError(Exception):
    """Base for every failure this module raises."""


class TransportError(ControlError):
    """The socket died: the emulator exited, or was never listening."""


class ProtocolError(ControlError):
    """The server rejected the request as malformed, or sent something unparseable."""


class OpError(ControlError):
    """A well-formed request that the emulator could not carry out.

    `code` is the stable machine-readable reason -- no_vm, bad_state, bad_address,
    too_large, busy, timeout, interrupted, unsupported, internal.
    """

    def __init__(self, code, message, state=None, frame=None, vsync=None):
        super().__init__("%s: %s" % (code, message))
        self.code = code
        self.message = message
        self.state = state
        self.frame = frame
        self.vsync = vsync


def format_address(address):
    """Accepts an int or a string; sends the canonical hex form either way."""
    if isinstance(address, str):
        return address
    return "0x%08X" % address


class Pcsx2Control:
    """A synchronous client. One request, one reply, in order."""

    def __init__(self, host=DEFAULT_HOST, port=DEFAULT_PORT, timeout=DEFAULT_SOCKET_TIMEOUT):
        self.host = host
        self.port = port
        self.timeout = timeout
        self._sock = None
        self._buffer = b""
        self._next_id = 1
        # Updated from every reply, so callers can read the last known state without
        # spending a round trip on it.
        self.last_frame = None
        self.last_vsync = None
        self.last_state = None

    # -- connection -------------------------------------------------------

    def connect(self):
        if self._sock is not None:
            return self
        try:
            self._sock = socket.create_connection((self.host, self.port), timeout=self.timeout)
        except OSError as error:
            raise TransportError(
                "cannot reach the control server at %s:%d (%s). Is PCSX2 running with "
                "EnableControlServer=true?" % (self.host, self.port, error)
            ) from error
        # The server sets TCP_NODELAY on its end; do the same here so a small request
        # is not held back waiting for more to coalesce with.
        self._sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self._buffer = b""
        return self

    def close(self):
        if self._sock is not None:
            try:
                self._sock.close()
            finally:
                self._sock = None
                self._buffer = b""

    def __enter__(self):
        return self.connect()

    def __exit__(self, *unused):
        self.close()

    # -- framing ----------------------------------------------------------

    def _send_line(self, payload):
        data = (json.dumps(payload, separators=(",", ":")) + "\n").encode("utf-8")
        try:
            self._sock.sendall(data)
        except OSError as error:
            self.close()
            raise TransportError("the connection dropped while sending: %s" % error) from error

    def _read_line(self):
        while True:
            newline = self._buffer.find(b"\n")
            if newline >= 0:
                line = self._buffer[:newline]
                self._buffer = self._buffer[newline + 1:]
                return line
            try:
                chunk = self._sock.recv(1 << 20)
            except OSError as error:
                self.close()
                raise TransportError("the connection dropped while receiving: %s" % error) from error
            if not chunk:
                self.close()
                raise TransportError("the server closed the connection")
            self._buffer += chunk

    # -- requests ---------------------------------------------------------

    def request(self, op, **fields):
        """Issues one request and returns the reply dict, raising on failure."""
        if self._sock is None:
            self.connect()

        request_id = self._next_id
        self._next_id += 1

        payload = {"id": request_id, "op": op}
        for key, value in fields.items():
            if value is not None:
                payload[key] = value

        self._send_line(payload)
        line = self._read_line()

        try:
            reply = json.loads(line.decode("utf-8"))
        except (ValueError, UnicodeDecodeError) as error:
            raise ProtocolError("the server sent an unparseable reply: %s" % error) from error
        if not isinstance(reply, dict):
            raise ProtocolError("the server sent a reply that was not an object")

        self.last_frame = reply.get("frame")
        self.last_vsync = reply.get("vsync")
        self.last_state = reply.get("state")

        if reply.get("id") != request_id:
            # In-order replies are part of the contract, so this means we have lost sync
            # and nothing after it can be trusted.
            self.close()
            raise ProtocolError(
                "reply id %r does not match request id %r; the stream is out of sync"
                % (reply.get("id"), request_id)
            )

        if not reply.get("ok", False):
            error_block = reply.get("error") or {}
            code = error_block.get("code", "internal")
            message = error_block.get("message", "no message")
            if code in PROTOCOL_ERROR_CODES:
                raise ProtocolError("%s: %s" % (code, message))
            raise OpError(code, message, reply.get("state"), reply.get("frame"), reply.get("vsync"))

        return reply

    # -- operations -------------------------------------------------------

    def ping(self):
        return self.request("ping")

    def status(self):
        return self.request("status")

    def read(self, address, length):
        """Reads `length` bytes and returns them as bytes."""
        if length > MAX_READ_BYTES:
            return self._read_chunked(address, length)
        reply = self.request("read_bytes", addr=format_address(address), len=length)
        return base64.b64decode(reply["data_b64"])

    def _read_chunked(self, address, length):
        pieces = []
        remaining = length
        cursor = address if isinstance(address, int) else int(address, 16)
        while remaining > 0:
            take = min(remaining, MAX_READ_BYTES)
            reply = self.request("read_bytes", addr=format_address(cursor), len=take)
            pieces.append(base64.b64decode(reply["data_b64"]))
            cursor += take
            remaining -= take
        return b"".join(pieces)

    def read_many(self, reads):
        """`reads` is a sequence of (address, length). Returns a list of bytes-or-OpError.

        Per-entry failures are reported in the list rather than raised, because a batch
        that walks a struct graph usually wants the reads that did land.

        This is not an atomic snapshot -- the reads happen back to back on the server's
        socket thread, within microseconds of each other, but the EE keeps running unless
        you paused first.
        """
        payload = [{"addr": format_address(address), "len": length} for address, length in reads]
        reply = self.request("read_many", reads=payload)
        results = []
        for entry in reply.get("results", []):
            if entry.get("ok"):
                results.append(base64.b64decode(entry["data_b64"]))
            else:
                block = entry.get("error") or {}
                results.append(OpError(block.get("code", "internal"), block.get("message", "")))
        return results

    def write(self, address, data):
        """Writes raw bytes.

        Two caveats worth repeating at every call site: a failure may have partially
        landed, and writes do not invalidate recompiled blocks, so overwriting EE code
        has no effect until that block is naturally flushed.
        """
        encoded = base64.b64encode(bytes(data)).decode("ascii")
        return self.request("write_bytes", addr=format_address(address), data_b64=encoded)

    def pause(self, wait=True, timeout_ms=None):
        return self.request("pause", wait=wait, timeout_ms=timeout_ms)

    def resume(self, wait=True, timeout_ms=None):
        return self.request("resume", wait=wait, timeout_ms=timeout_ms)

    def step(self, frames=1, timeout_ms=None):
        """Advances exactly `frames` frames and leaves the VM paused."""
        return self.request("step", frames=frames, timeout_ms=timeout_ms)

    def reset(self, wait=True, timeout_ms=None):
        return self.request("reset", wait=wait, timeout_ms=timeout_ms)

    def screenshot(self, path=None, width=None, height=None, quality=None,
                   aspect=None, crop=None, timeout_ms=None):
        """Captures a frame.

        With no `path`, the PNG comes back inline and is returned as bytes -- atomic with
        the frame stamp in the same reply. With a `path`, the server writes the file and
        the reply carries the path instead.
        """
        reply = self.request("screenshot", path=path, width=width, height=height,
                             quality=quality, aspect=aspect, crop=crop, timeout_ms=timeout_ms)
        if path is None:
            return base64.b64decode(reply["data_b64"]), reply
        return None, reply

    def save_state(self, path, backup=False, timeout_ms=None):
        """Writes a save state to an absolute path, returning only once it is on disk.

        Unlike PINE's slot ops this is genuinely synchronous: the server compresses on the
        CPU thread rather than handing off to a detached writer, so a reply means the file
        is complete. That costs the emulation thread a few hundred milliseconds, which is
        why the intended pattern is to pause first.
        """
        return self.request("save_state", path=os.path.abspath(path), backup=backup,
                            timeout_ms=timeout_ms)

    def load_state(self, path, timeout_ms=None):
        """Restores a save state from an absolute path.

        A load that gets as far as reading the file and then fails leaves the VM *reset* --
        that is VMManager's behaviour, not a choice this client makes. The server checks the
        file exists first so a mistyped path cannot cost a session, but a truncated or
        version-mismatched state still can. The error message says when it happened.

        `frame` jumps to whatever the state recorded; `vsync` keeps counting monotonically.
        """
        return self.request("load_state", path=os.path.abspath(path), timeout_ms=timeout_ms)

    def send_input(self, frames, port=0):
        """Queues per-frame pad states, drained one per emulated frame.

        Each entry is a dict: {"buttons": ["select"], "analog": {"lx": 255}, "repeat": 4}.
        Frames are absolute, not deltas -- a button not named is released. When the queue
        drains, the pad is explicitly released, so nothing stays stuck down.

        Paired with step() this makes a button sequence exactly reproducible: queue the
        frames, then step exactly that many.
        """
        return self.request("send_input", frames=list(frames), port=port)

    def clear_input(self):
        """Drops anything still queued and releases the pad."""
        return self.request("clear_input")

    def press(self, buttons, hold=2, gap=2, port=0):
        """Queues one button press: `hold` frames down, then `gap` frames up.

        Two frames is the usual safe minimum -- a game polling once per frame needs the
        state to persist across its own read, and a one-frame press can be missed if the
        game debounces. Returns the number of frames queued, which is what to step.
        """
        if isinstance(buttons, str):
            buttons = [buttons]
        frames = []
        if hold > 0:
            frames.append({"buttons": list(buttons), "repeat": hold})
        if gap > 0:
            frames.append({"buttons": [], "repeat": gap})
        self.send_input(frames, port=port)
        return hold + gap

    def dump_ee(self, path):
        """Writes all of EE main RAM to `path`, laid out like a save state's eeMemory.bin.

        Addresses map 1:1 onto file offsets, so the existing offline scripts
        (scripts/dump_ee_entities.py and friends) read the result unchanged.
        """
        data = self.read(EE_RAM_BASE, EE_RAM_SIZE)
        with open(path, "wb") as handle:
            handle.write(data)
        return len(data)


# -- command line ---------------------------------------------------------


def _hexdump(data, base_address, limit=512):
    lines = []
    shown = min(len(data), limit)
    for offset in range(0, shown, 16):
        row = data[offset:offset + 16]
        hex_part = " ".join("%02x" % byte for byte in row)
        text_part = "".join(chr(b) if 32 <= b < 127 else "." for b in row)
        lines.append("%08x  %-47s  %s" % (base_address + offset, hex_part, text_part))
    if shown < len(data):
        lines.append("... %d more bytes" % (len(data) - shown))
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--host", default=DEFAULT_HOST)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    subparsers = parser.add_subparsers(dest="command", required=True)

    subparsers.add_parser("ping")
    subparsers.add_parser("status")
    subparsers.add_parser("pause")
    subparsers.add_parser("resume")
    subparsers.add_parser("reset")

    read_parser = subparsers.add_parser("read")
    read_parser.add_argument("address")
    read_parser.add_argument("length", type=lambda v: int(v, 0))

    write_parser = subparsers.add_parser("write")
    write_parser.add_argument("address")
    write_parser.add_argument("hex_bytes", help="raw hex, e.g. 0101")

    step_parser = subparsers.add_parser("step")
    step_parser.add_argument("frames", nargs="?", type=int, default=1)

    shot_parser = subparsers.add_parser("screenshot")
    shot_parser.add_argument("path")

    dump_parser = subparsers.add_parser("dump-ee")
    dump_parser.add_argument("path")

    save_parser = subparsers.add_parser("save-state")
    save_parser.add_argument("path")
    save_parser.add_argument("--backup", action="store_true",
                             help="rename any existing file to <path>.backup first")

    load_parser = subparsers.add_parser("load-state")
    load_parser.add_argument("path")

    arguments = parser.parse_args(argv)

    try:
        with Pcsx2Control(arguments.host, arguments.port) as client:
            if arguments.command == "read":
                address = int(arguments.address, 0)
                data = client.read(address, arguments.length)
                print(_hexdump(data, address))
            elif arguments.command == "write":
                address = int(arguments.address, 0)
                client.write(address, bytes.fromhex(arguments.hex_bytes))
                print("wrote %d bytes to 0x%08X" % (len(arguments.hex_bytes) // 2, address))
            elif arguments.command == "step":
                reply = client.step(arguments.frames)
                print("frame=%s vsync=%s state=%s" % (reply["frame"], reply["vsync"], reply["state"]))
            elif arguments.command == "screenshot":
                _, reply = client.screenshot(path=arguments.path)
                print("wrote %s (%dx%d)" % (reply["path"], reply["width"], reply["height"]))
            elif arguments.command == "save-state":
                reply = client.save_state(arguments.path, backup=arguments.backup)
                print("saved state to %s" % reply["path"])
            elif arguments.command == "load-state":
                reply = client.load_state(arguments.path)
                print("loaded %s (frame=%s vsync=%s state=%s)"
                      % (reply["path"], reply["frame"], reply["vsync"], reply["state"]))
            elif arguments.command == "dump-ee":
                written = client.dump_ee(arguments.path)
                print("wrote %d bytes to %s" % (written, arguments.path))
            else:
                print(json.dumps(client.request(arguments.command), indent=2))
    except ControlError as error:
        print("error: %s" % error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
