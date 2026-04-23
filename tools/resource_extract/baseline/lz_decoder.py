#!/usr/bin/env python3
"""
Orphen LZ (headerless) decoder – faithful port of FUN_002f3118

Observed from src/FUN_002f3118.c (decompiled in this repo):
- Stream terminator byte is 0x00 (stop decoding when encountered)
- 0x80 set: LZ match start
    length = (flag >> 5) | 4  # values 4..7
    displacement = ((flag & 0x1F) << 8) | nextByte  # 1..0x1FFF, exact distance (no +1)
    Then zero or more LZ continue flags may follow:
      (nextFlag & 0xE0) == 0x60 => copy length = (nextFlag & 0x1F) using same displacement
- 0x40 set (and 0x80 clear): RLE
    len = ((flag & 0x10) == 0) ? ((flag & 0x0F) + 4)
                                : (((flag & 0x0F) << 8) + nextByte + 4)
    value = nextByte
    write value, len times
- else: Raw data
    len = ((flag & 0x20) == 0) ? (flag & 0x1F)
                                : (((flag & 0x1F) << 8) + nextByte)
    then copy len raw bytes from input

Circular buffer semantics:
- Max displacement encoded is 0x1FFF; distance = displacement (1..0x1FFF)
- Copy reads from (write_pos - distance) circularly, writing `length` bytes
- Every output byte (from raw/rle/copy) is also written into the buffer

Extras:
- --multi option treats input as concatenated streams separated by 0x00 terminators
- --size clamps output to a known decompressed size
"""

from __future__ import annotations

import argparse
import io
import sys
from typing import BinaryIO, Optional


class CircularBuffer:
    """Simple circular buffer supporting byte writes and LZ-style back-references.

    The C# code constructs CircularBuffer(0x1FFF). We interpret that value as the
    maximum displacement, so the effective buffer capacity is max_disp + 1 to allow
    distances in [1, max_disp+1], mapping from encoded displacement [0, max_disp].
    """

    def __init__(self, max_displacement: int):
        if max_displacement < 0:
            raise ValueError("max_displacement must be >= 0")
        # Capacity supports distances up to max_displacement + 1
        self.capacity = max_displacement + 1
        self.buf = bytearray(self.capacity)
        self.pos = 0  # next write position
        self.filled = 0  # number of valid bytes written (<= capacity)

    def write_byte(self, b: int) -> None:
        self.buf[self.pos] = b & 0xFF
        self.pos = (self.pos + 1) % self.capacity
        if self.filled < self.capacity:
            self.filled += 1

    def copy(self, out: BinaryIO, displacement: int, length: int) -> None:
        # Encoded displacement uses 13 bits; value 0 maps to full window (0x2000)
        distance = displacement if displacement != 0 else self.capacity
        if distance <= 0 or distance > self.capacity:
            raise ValueError(f"Invalid distance {distance} for capacity {self.capacity}")
        # Start reading from write_pos - distance
        src = (self.pos - distance) % self.capacity
        for _ in range(length):
            # If buffer not yet filled, reading from uninitialized region would be undefined.
            # This shouldn't occur in valid streams, but default to 0 if it does.
            value = self.buf[src] if self.filled > 0 else 0
            out.write(bytes((value,)))
            self.write_byte(value)
            src = (src + 1) % self.capacity


def _read_u8(f: BinaryIO) -> Optional[int]:
    b = f.read(1)
    if not b:
        return None
    return b[0]


def calculate_decompressed_size(data: bytes | BinaryIO, multi: bool = False) -> int:
    """Scan input and return decompressed size, without producing output.

    Accepts bytes or a seekable binary stream. For streams, current position will be advanced
    to the end. If you need to preserve position, pass a BytesIO copy.
    """
    # Read bytes into memory for scanning
    if isinstance(data, (bytes, bytearray, memoryview)):
        buf = bytes(data)
    else:
        buf = data.read()  # type: ignore[union-attr]

    n = len(buf)
    pos = 0
    total = 0

    while pos < n:
        flag = buf[pos]
        pos += 1

        if flag == 0:
            if multi:
                # Start a new substream
                continue
            break

        if (flag & 0x80) == 0x80:
            # LZ match start
            if pos >= n:
                break
            # consume low displacement byte
            pos += 1
            total += ((flag >> 5) | 4)
            # include any immediate LZ-continue flags
            while pos < n and (buf[pos] & 0xE0) == 0x60:
                total += (buf[pos] & 0x1F)
                pos += 1
        elif (flag & 0x40) == 0x40:
            # RLE
            if (flag & 0x10) == 0:
                length = (flag & 0x0F) + 4
            else:
                if pos >= n:
                    break
                length = ((flag & 0x0F) << 8) + buf[pos] + 4
                pos += 1
            # value byte
            if pos >= n:
                break
            pos += 1
            total += length
        else:
            # Raw data
            if (flag & 0x20) == 0:
                length = flag & 0x1F
            else:
                if pos >= n:
                    break
                length = ((flag & 0x1F) << 8) + buf[pos]
                pos += 1
            # skip payload
            step = min(length, n - pos)
            pos += step
            total += step

    return total


def decode_stream(
    input_stream: BinaryIO,
    output_stream: BinaryIO,
    decompressed_size: Optional[int] = None,
    *,
    multi: bool = False,
) -> None:
    """Decode from input stream to output stream.

    If decompressed_size is provided, stops when output reaches that size.
    Otherwise, decodes until input is exhausted.
    """
    circ = CircularBuffer(0x1FFF)
    prev_disp = 0
    pending_flag: Optional[int] = None

    def out_pos() -> int:
        # output_stream may or may not be seekable; track size by tell when possible
        try:
            return output_stream.tell()
        except (OSError, io.UnsupportedOperation):
            return -1  # unknown
    
    def remaining() -> Optional[int]:
        if decompressed_size is None:
            return None
        pos = out_pos()
        if pos < 0:
            return None
        return max(0, decompressed_size - pos)

    while True:
        # Stop early if we've reached the desired decompressed size
        rem = remaining()
        if rem == 0:
            break

        if pending_flag is not None:
            flag = pending_flag
            pending_flag = None
        else:
            flag_b = input_stream.read(1)
            if not flag_b:
                break
            flag = flag_b[0]

        if flag == 0:
            if multi:
                # reset state for next substream
                prev_disp = 0
                circ = CircularBuffer(0x1FFF)
                pending_flag = None
                continue
            break

        if (flag & 0x80) == 0x80:
            # LZ match start
            length = ((flag >> 5) | 4)
            low = _read_u8(input_stream)
            if low is None:
                raise EOFError("Unexpected EOF reading displacement low byte")
            prev_disp = (((flag & 0x1F) << 8) | low)
            rem = remaining()
            if rem is not None and rem < length:
                if rem == 0:
                    break
                circ.copy(output_stream, prev_disp, rem)
                break
            else:
                circ.copy(output_stream, prev_disp, length)
            # After an LZ-start, consume any immediate continue flags
            while True:
                # peek next flag
                next_b = input_stream.read(1)
                if not next_b:
                    break
                nf = next_b[0]
                if (nf & 0xE0) != 0x60:
                    # stash for next outer iteration
                    pending_flag = nf
                    break
                # process this continue
                clen = nf & 0x1F
                if clen:
                    rem = remaining()
                    if rem is not None and rem < clen:
                        if rem == 0:
                            return None
                        circ.copy(output_stream, prev_disp, rem)
                        return None
                    else:
                        circ.copy(output_stream, prev_disp, clen)
                # else zero-length continue? just keep going
        elif (flag & 0x40) == 0x40:
            # RLE
            if (flag & 0x10) == 0x00:
                length = (flag & 0x0F) + 4
            else:
                low = _read_u8(input_stream)
                if low is None:
                    raise EOFError("Unexpected EOF reading RLE length low byte")
                length = ((flag & 0x0F) << 8) + low + 4
            value = _read_u8(input_stream)
            if value is None:
                raise EOFError("Unexpected EOF reading RLE value byte")
            b = bytes((value,))
            rem = remaining()
            to_write = length if rem is None else min(length, rem)
            for _ in range(to_write):
                output_stream.write(b)
                circ.write_byte(value)
            if rem is not None and to_write < length:
                break
        else:
            # Raw data
            if (flag & 0x20) == 0x00:
                length = flag & 0x1F
            else:
                low = _read_u8(input_stream)
                if low is None:
                    raise EOFError("Unexpected EOF reading raw length low byte")
                length = ((flag & 0x1F) << 8) + low
            if length:
                rem = remaining()
                to_read = length if rem is None else min(length, rem)
                if to_read:
                    chunk = input_stream.read(to_read)
                    # If input is truncated, write what we have and stop
                    if len(chunk) < to_read:
                        if chunk:
                            output_stream.write(chunk)
                            for v in chunk:
                                circ.write_byte(v)
                        break
                    output_stream.write(chunk)
                    for v in chunk:
                        circ.write_byte(v)
                # If we capped the read, stop without consuming the rest of this raw block
                if rem is not None and to_read < length:
                    break

        # If decompressed_size provided but output_stream isn't seekable, we can't reliably stop early.
        # In that case, we just continue until input ends.


def decode_bytes(data: bytes, decompressed_size: Optional[int] = None, *, multi: bool = False) -> bytes:
    out = io.BytesIO()
    decode_stream(io.BytesIO(data), out, decompressed_size=decompressed_size, multi=multi)
    return out.getvalue()


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(description="Orphen LZ headerless decoder (FUN_002f3118)")
    p.add_argument("input", help="Input file (compressed)")
    p.add_argument("output", nargs="?", help="Output file (decompressed); stdout if omitted")
    p.add_argument("--size", type=int, default=None, help="Optional known decompressed size")
    p.add_argument("--multi", action="store_true", help="Treat input as concatenated headerless streams")
    p.add_argument("--calc-size-only", action="store_true", help="Only calculate decompressed size and print it")
    args = p.parse_args(argv)

    with open(args.input, "rb") as f:
        data = f.read()

    if args.calc_size_only:
        size = calculate_decompressed_size(data, multi=args.multi)
        print(size)
        return 0

    if args.output:
        out_f: BinaryIO
        with open(args.output, "wb") as out_f:
            decode_stream(io.BytesIO(data), out_f, decompressed_size=args.size, multi=args.multi)
        return 0
    else:
        # Write to stdout in binary
        out = decode_bytes(data, decompressed_size=args.size, multi=args.multi)
        sys.stdout.buffer.write(out)
        return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
