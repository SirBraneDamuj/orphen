#!/usr/bin/env python3
"""
Find a byte sequence in files and directories.

Usage:
  python scripts/find_bytes.py --hex "20 2f 50 53 43 33 1d 00 db 00 d4 af 01 00 ec c8 01 00 44 00" --paths FILE_OR_DIR ...

Outputs JSON lines with: {"path": str, "offset": int}
"""
from __future__ import annotations
import argparse
import binascii
import json
import mmap
import os
import sys
from typing import Iterable, Iterator, List


def parse_hex_bytes(s: str) -> bytes:
    s = s.strip().replace(" ", "").replace("_", "").replace("-", "")
    if len(s) % 2 != 0:
        raise ValueError("hex string must have even length (spaces are allowed)")
    return binascii.unhexlify(s)


def iter_files(paths: List[str]) -> Iterator[str]:
    for p in paths:
        if not os.path.exists(p):
            continue
        if os.path.isfile(p):
            yield p
        else:
            for root, _dirs, files in os.walk(p):
                for f in files:
                    yield os.path.join(root, f)


def find_in_file(path: str, needle: bytes) -> Iterator[int]:
    try:
        with open(path, "rb") as f:
            with mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as mm:
                start = 0
                while True:
                    idx = mm.find(needle, start)
                    if idx == -1:
                        break
                    yield idx
                    start = idx + 1
    except (OSError, ValueError):
        # Skip unreadable or special files
        return


def main() -> None:
    ap = argparse.ArgumentParser(description="Find a hex byte sequence in files")
    ap.add_argument("--hex", required=True, help="hex bytes, spaces allowed")
    ap.add_argument("--paths", nargs="+", required=True, help="files/directories to scan")
    args = ap.parse_args()

    needle = parse_hex_bytes(args.hex)
    any_found = False

    for path in iter_files(args.paths):
        for off in find_in_file(path, needle):
            any_found = True
            print(json.dumps({"path": path, "offset": off}))
            sys.stdout.flush()

    if not any_found:
        # Non-zero exit indicates no matches (useful for scripts)
        sys.exit(1)


if __name__ == "__main__":
    main()
