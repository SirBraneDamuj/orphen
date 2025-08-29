#!/usr/bin/env python3
"""Batch decompressor for Orphen SCR segment files.

For every file matching scr*.bin inside the sibling directory `scr/`,
decompress it with the Orphen headerless LZ algorithm (FUN_002f3118) and
write the result next to it as scrN.out (same stem, .out extension).

Existing .out files are skipped unless --force is provided.

Usage:
  python scripts/batch_decompress_scr.py            # decode any missing .out files
  python scripts/batch_decompress_scr.py --force    # re-decode all

Relies on the implementation in `orphen_lz_headerless_decoder.py`.
"""

from __future__ import annotations

import argparse
import io
import sys
from pathlib import Path

from orphen_lz_headerless_decoder import decode_stream  # type: ignore


def decompress_file(src_path: Path, dst_path: Path) -> None:
    with src_path.open('rb') as f_in, dst_path.open('wb') as f_out:
        data = f_in.read()
        decode_stream(io.BytesIO(data), f_out, decompressed_size=None, multi=False)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Batch decompress scr*.bin to scr*.out")
    parser.add_argument('--force', action='store_true', help='Re-decode even if .out exists')
    parser.add_argument('--dir', default='scr', help='Directory containing scr*.bin files (default: scr)')
    parser.add_argument('--keep-partial', action='store_true', help='Keep partial .out file on decode error (default deletes it)')
    args = parser.parse_args(argv)

    scr_dir = Path(args.dir)
    if not scr_dir.is_dir():
        print(f"Directory not found: {scr_dir}", file=sys.stderr)
        return 1

    bin_files = sorted(scr_dir.glob('scr*.bin'), key=lambda p: (len(p.stem), p.stem))
    if not bin_files:
        print("No scr*.bin files found", file=sys.stderr)
        return 1

    processed = 0
    skipped = 0
    errors = 0
    for bin_path in bin_files:
        out_path = bin_path.with_suffix('.out')
        if out_path.exists() and not args.force:
            skipped += 1
            continue
        try:
            decompress_file(bin_path, out_path)
            processed += 1
        except Exception as e:  # noqa: BLE001
            errors += 1
            print(f"ERROR decoding {bin_path.name}: {e}", file=sys.stderr)
            if not args.keep_partial and out_path.exists():
                try:
                    out_path.unlink()
                except OSError:
                    pass

    print(f"Done. Decoded: {processed}, Skipped: {skipped}, Errors: {errors}")
    return 0 if errors == 0 else 2


if __name__ == '__main__':
    raise SystemExit(main(sys.argv[1:]))
