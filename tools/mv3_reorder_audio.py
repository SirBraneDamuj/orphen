#!/usr/bin/env python3
"""Reorder demuxed Orphen MV3 audio into normal interleaved stereo PCM.

The MV3 container stores each 0x30000-byte audio chunk as alternating channel
stripes, not as ordinary LRLR samples. The confirmed movie layout is:

- signed 16-bit little-endian samples
- stereo
- 48000 Hz
- 0x200-byte left/right channel stripes inside each 0x30000-byte chunk

This tool converts the raw `.pcm` emitted by `mv3_demux.py` into a conventional
s16le stereo stream suitable for ffmpeg with `-f s16le -ar 48000 -ac 2`.
"""

from __future__ import annotations

import argparse
from pathlib import Path


DEFAULT_CHUNK_SIZE = 0x30000
DEFAULT_STRIPE_SIZE = 0x200
BYTES_PER_SAMPLE = 2


class Mv3AudioError(ValueError):
    pass


def reorder_channel_stripes(data: bytes, chunk_size: int, stripe_size: int) -> bytes:
    if chunk_size <= 0:
        raise Mv3AudioError("chunk size must be positive")
    if stripe_size <= 0:
        raise Mv3AudioError("stripe size must be positive")
    if stripe_size % BYTES_PER_SAMPLE != 0:
        raise Mv3AudioError("stripe size must be a multiple of the sample size")
    if chunk_size % (stripe_size * 2) != 0:
        raise Mv3AudioError("chunk size must contain complete left/right stripe pairs")
    if len(data) % chunk_size != 0:
        raise Mv3AudioError("input size must contain complete MV3 audio chunks")

    output = bytearray(len(data))
    write_offset = 0

    for chunk_start in range(0, len(data), chunk_size):
        chunk = data[chunk_start : chunk_start + chunk_size]
        for pair_start in range(0, chunk_size, stripe_size * 2):
            left = chunk[pair_start : pair_start + stripe_size]
            right = chunk[pair_start + stripe_size : pair_start + stripe_size * 2]

            for sample_offset in range(0, stripe_size, BYTES_PER_SAMPLE):
                output[write_offset : write_offset + BYTES_PER_SAMPLE] = left[
                    sample_offset : sample_offset + BYTES_PER_SAMPLE
                ]
                output[write_offset + BYTES_PER_SAMPLE : write_offset + BYTES_PER_SAMPLE * 2] = right[
                    sample_offset : sample_offset + BYTES_PER_SAMPLE
                ]
                write_offset += BYTES_PER_SAMPLE * 2

    return bytes(output)


def main() -> int:
    parser = argparse.ArgumentParser(description="Reorder demuxed Orphen MV3 audio channel stripes.")
    parser.add_argument("input_pcm", type=Path, help="Raw .pcm file emitted by mv3_demux.py")
    parser.add_argument("output_pcm", type=Path, help="Output conventional s16le stereo .pcm file")
    parser.add_argument(
        "--chunk-size",
        type=lambda text: int(text, 0),
        default=DEFAULT_CHUNK_SIZE,
        help="MV3 audio chunk size; regular Mxx movies use 0x30000",
    )
    parser.add_argument(
        "--stripe-size",
        type=lambda text: int(text, 0),
        default=DEFAULT_STRIPE_SIZE,
        help="Left/right stripe size; confirmed value is 0x200",
    )
    args = parser.parse_args()

    data = args.input_pcm.read_bytes()
    reordered = reorder_channel_stripes(data, args.chunk_size, args.stripe_size)
    args.output_pcm.parent.mkdir(parents=True, exist_ok=True)
    args.output_pcm.write_bytes(reordered)
    print(f"wrote {args.output_pcm} ({len(reordered)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())