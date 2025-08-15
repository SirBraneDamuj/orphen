#!/usr/bin/env python3
"""
Minimal structural disassembler for Orphen SCR (source-validated).

Scope:
- Walks the structural stream starting at a given file offset.
- Implements only what is provable from src/FUN_0025bc68.c and FUN_0025bf70.c.
- Emits:
  - 32: BLOCK_OPEN (push cont=pc+5), VM_ADVANCE, record next 4 bytes as VM prologue payload, then continue at cont.
  - 04: BLOCK_END: pop or stop if top-most.
  - <0x0b (except 0x04): LOW opcode placeholder.
  - 0xff <b>: EXT opcode placeholder.
  - >=0x32 (other): HIGH opcode placeholder.

Notes:
- We don’t emulate the VM; we only mark where VM_ADVANCE happens and show the 4 bytes the structural code will skip via the continuation.
- For now, we treat any nested 0x32 correctly (stack of continuations).
"""
from __future__ import annotations
import argparse
from pathlib import Path

LOW_NAMES = {4: "BLOCK_END", 0x32: "BLOCK_OPEN", 0xff: "EXT"}

class StructuralDisasm:
    def __init__(self, data: bytes):
        self.data = data

    def u8(self, off: int) -> int:
        return self.data[off]

    def disasm_block(self, start: int, limit: int | None = None):
        pc = start
        stack = []  # continuation addresses
        seen = set()
        out = []
        limit = limit if limit is not None else len(self.data)
        while 0 <= pc < limit:
            if pc in seen:
                out.append(f"{pc:08x}: ; loop detected, stopping")
                break
            seen.add(pc)
            op = self.u8(pc)
            if op < 0x0B:
                if op == 0x04:  # BLOCK_END
                    out.append(f"{pc:08x}: 04  ; BLOCK_END")
                    if not stack:
                        # top-most: stop (matches early return in FUN_0025bc68)
                        pc += 1
                        break
                    pc = stack.pop()
                    continue
                else:
                    out.append(f"{pc:08x}: {op:02x}  ; LOW_{op:02x}")
                    pc += 1
                    continue
            elif op == 0xFF:
                if pc + 1 >= limit:
                    out.append(f"{pc:08x}: ff  ; EXT <truncated>")
                    break
                ext = self.u8(pc + 1)
                out.append(f"{pc:08x}: ff {ext:02x}  ; EXT_{ext:02x}")
                pc += 2
                continue
            elif op == 0x32:
                # Push continuation and perform VM advance; the 4 bytes after 0x32 are VM payload
                cont = pc + 5
                stack.append(cont)
                if pc + 5 > limit:
                    payload = self.data[pc+1:limit]
                else:
                    payload = self.data[pc+1:pc+5]
                hex_payload = " ".join(f"{b:02x}" for b in payload)
                annot = ""
                if len(payload) == 4:
                    tag_u32 = int.from_bytes(payload, "little", signed=False)
                    annot = f", SUBPROC_TAG={tag_u32} (0x{tag_u32:08x})"
                    # If payload looks like 0x0B 0x04 <ID16>, expose the ID16
                    if payload[0:2] == b"\x0b\x04":
                        id16 = payload[2] | (payload[3] << 8)
                        annot += f", SUBPROC_ID16={id16} (0x{id16:04x})"
                out.append(
                    f"{pc:08x}: 32  ; BLOCK_OPEN -> push cont={cont:08x}, VM_ADVANCE, prologue=[{hex_payload}]{annot}"
                )
                pc = pc + 1  # structural sets pb = pb + 1 before VM advance
                # We don’t emulate VM; resume structural at continuation
                pc = cont
                continue
            else:
                out.append(f"{pc:08x}: {op:02x}  ; HIGH_{op-0x32:02x}")
                pc += 1
                continue
        return out


def main():
    ap = argparse.ArgumentParser(description="Minimal structural disassembler for Orphen SCR")
    ap.add_argument("file", type=Path, help="SCR file (e.g., scr2.out)")
    ap.add_argument("start", type=lambda x: int(x, 0), help="Start offset (e.g., 0x1234)")
    ap.add_argument("--limit", type=lambda x: int(x, 0), default=None, help="Optional end offset (exclusive)")
    args = ap.parse_args()

    data = args.file.read_bytes()
    d = StructuralDisasm(data)
    lines = d.disasm_block(args.start, args.limit)
    print("\n".join(lines))

if __name__ == "__main__":
    main()
