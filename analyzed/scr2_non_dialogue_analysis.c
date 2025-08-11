// Analysis of non-dialogue region of scr2.out beyond offset 0x1680
// This file documents inferred structure; original data lives in binary asset scr2.out
// No direct code; serves as reverse engineering notes.

/*
Overview:
  Dialogue bytecode (glyph/control stream) appears confined to [0x0000,0x167F].
  At 0x1680 a new structured region begins. Initial 84 * 4-byte little-endian values form a strictly
  ascending table of offsets all < 0x1680 (<= 0x167F). Thus: pointer table (count=84 entries) indexing
  dialogue/snippet records located in the earlier dialogue zone.

  Layout:
    0x1680: pointer_table[84] (uint32 LE) -> each points to a mini-record beginning with 0x13 (speaker) or 0x17 (no speaker) etc.
    0x17D0: 0x00 00 00 00 sentinel terminator (matches first word after table) followed by mixed data blocks.

  Immediately after table (from 0x17D0):
    Sequence of 32-bit LE values forming small ascending integers with gaps: 0x00000004,0x00000005,... 0x00000016,0x00000022,0x00000023,0x0000002A,0x00000026,...
    These look like ID lists / opcode indices / resource IDs rather than offsets (many exceed 0x1680? No: numeric values not used as pointers here; they are small).

    After list of IDs: pattern bytes: 'OQ' or 'QO' confusion due to endianness (observed 4F 51 03 01 at 0x1800+1C). Word 0x01514F (LE) or 0x4F510301 sequence may encode a tag or magic + version.

    Following bytes show repeated sub-structure motifs embedding the trio (0x5A 0C ..) plus 0x0B (line/entry separators) and 0x17 tokens reminiscent of dialogue control opcodes, but mixed with higher bytes (>=0x70, >=0x90) giving the impression of a second-layer compiled script or table-driven state machine.

Hypotheses:
  1. Pointer table indexes a catalogue of dialogue line descriptors (speaker + voice metadata) reused later by non-dialogue script logic for sequencing without embedding names inline again.
  2. Post-table region encodes higher-order scene scripting: sequences of (line_ref_ids, timing codes, branching, conditions) expressed in a more compact custom VM distinct from glyph-level engine.
  3. Small ascending integers list (4..16,22,23,2A,26,27,28,62, etc.) may be symbolic constants (e.g., opcode IDs, stage/camera cue IDs, emotion/voice variants) used by subsequent bytecode blocks.
  4. Repetition pattern: blocks starting with 0x5A 0C ("Z\f") possibly mark a command header: 0x5A could be an opcode selecting a table; 0x0C length or parameter count.

Correlations to known processors:
  - Known dialogue control dispatch only covers opcodes < 0x1F with table at PTR_FUN_0031c640; new region shows bytes far above that range early (0x5A, 0x4F, 0x51) suggesting either multi-byte opcodes or data arguments not opcodes.
  - Need to locate code reading this second half: search for 0x00030E4D (appears as second word after terminator) inside code references to identify loader.

Next Steps Proposed:
  - Implement an offline structural dumper (Python) to segment post-0x17D0 region into blocks via recurring delimiter bytes (e.g., 0x17 00 00 00, 0x0B separators) and emit frequency stats.
  - Cross-reference pointer table targets with extractor output to assign line indices; then verify if post-table script references them by ordinal (position in pointer table) vs absolute address.
  - Search code for constants 0x4D0E03, 0x015A0C, 0x00030E4D to identify processor.
  - If a function loads pointer table count=84, look for immediate constant 84 (0x54) or for comparison up to last pointer 0x167F.
*/

// (No compiled code produced here.)
