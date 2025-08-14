# Structural Block Prologue (Former "Header") Reinterpretation

Source-grounded derivation based on `FUN_0025bc68` (structural interpreter) rather than statistical guessing.

Key Code Excerpt (simplified):

```
if (b == 0x32) {           // open block
    push( pb + 5 );        // continuation = opcode 0x32 + 4 following bytes
    depth--;
    pb = pb + 1;           // advance to first byte after 0x32
    FUN_0025c220();        // relative jump in main VM (DAT_00355cd0 += *DAT_00355cd0)
    // loop continues, now reading the next byte *as an opcode*
}
```

Close handling (opcode 0x04) pops the stored continuation pointer and resumes at `pb+5`, i.e. it deliberately skips over the 4 bytes following the original 0x32 on subsequent passes. Thus those 4 bytes are executed exactly once—immediately after block entry—and then treated as already-consumed when unwinding.

Conclusion:
The 4 bytes after 0x32 are not a packed numeric length/flags field; they are four ordinary structural opcodes (a prologue mini-program). Our earlier “header” numeric interpretation conflated an opcode sequence with a scalar.

Empirical Ladder Example:
`32 5d 00 00 00 ... 04`

Execution order after encountering 0x32:

1. Push continuation = (address of 0x32) + 5.
2. Set `pb` to 5d (first prologue opcode) and execute 0x5d (high-range dispatch via PTR_LAB_0031e228 index 0x5d-0x32 = 0x2b).
3. Execute three 0x00 opcodes (low-range table `PTR_LAB_0031e1f8` index 0).
4. Continue with block body opcodes until a 0x04 is hit.
5. 0x04 causes pop → `pb = continuation = original+5` (skipping already-run four prologue opcodes) then normal sibling processing resumes.

Implications for Prior Metrics:

- “payload_len” we computed included the prologue bytes; ratios based on treating b0..b3 as a single integer are invalid.
- Distinct first-byte values (5d, 48, 33, 1e, 09, 18, etc.) are just first prologue opcode choices encoding block type / behavior.
- Trailing zero bytes in ladder prologues indicate repeated opcode 0x00 (likely a no-op or simple initializer). Non-zero trailing bytes correspond to additional prologue operations, not bitfields.

Recommended Next Steps (code-driven):

1. Extract opcode frequency per prologue position (1..4 after each 0x32).
2. Map each distinct opcode to its dispatch target function (derive from index arithmetic: high opcode fn = `(&PTR_LAB_0031e228)[opcode-0x32]`; low opcode fn = `(&PTR_LAB_0031e1f8)[opcode]`).
3. Disassemble / annotate the highest-frequency prologue function targets to infer semantic roles (e.g., block type selector, stack depth mgmt, conditional gating, parameter loader).
4. Rebuild block dataset separating prologue vs body: store prologue opcode list, body_start pointer, and termination pointer.
5. Re-run structural analyses (nesting, family clustering) using only the first prologue opcode as “block type”.

Validation Avenues in Code:

- Search for functions invoked exclusively via indices matching ladder first opcodes to correlate with observed nesting patterns.
- Trace side effects (globals written) in those prologue functions to see if they configure convergence targets or counters.

Action Items (pending implementation):

- Write `extract_block_prologues.py` to emit JSON: `{start, prologue:[op1,op2,op3,op4], body_start, end}`.
- Generate frequency table & cross-reference counts.

This document supersedes earlier header length hypotheses; treat previous numeric header correlations as exploratory dead-end now archived for provenance.
