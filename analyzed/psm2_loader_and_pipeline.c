// Analyzed from: src/FUN_0022b5a8.c
// Original symbol: FUN_0022b5a8
//
// Purpose
//   High-level processor for PSM2 payloads. Decodes the segment into the KSEG1 staging region
//   (0x01849A00), validates 'PSM2' magic (0x324D5350), then constructs a series of global tables
//   under the 0x003556**/0x00355*** regions using a bump allocator (DAT_0035572c). The routine
//   finishes by invoking a sequence of post-build passes.
//
// Strictly code-derived observations (no speculative semantics)
// - Decoder: Calls FUN_002f3118() up front (same top-level decoder used elsewhere).
// - Magic: Reads dword at 0x01849A00 and compares with 0x324D5350; on mismatch logs an error.
// - Header-relative fields: Uses numerous offsets from the decoded blob header, stored in globals
//   named DAT_01849aXX. These serve as base-relative pointers to sections; below we list those
//   referenced in this function:
//     +0x04 (DAT_01849a04)
//     +0x08 (DAT_01849a08)
//     +0x0C (DAT_01849a0C)
//     +0x10 (DAT_01849a10)
//     +0x14 (DAT_01849a14)
//     +0x18 (DAT_01849a18)
//     +0x1C (DAT_01849a1C)
//     +0x2C (DAT_01849a2C)
//     +0x30 (DAT_01849a30)
//     +0x34 (DAT_01849a34)
//     +0x38 (DAT_01849a38)
// - Workspace: Uses DAT_0035572c as an arena cursor (aligned to 4 or 16 bytes as needed). Many
//   intermediate bases are captured as DAT_003556** and DAT_00355*** globals.
// - Post passes: Ends with calls FUN_0022c3d8(), FUN_0022c6e8(), FUN_0022d258(), FUN_00211230().
//
// Section map (by header offset)
//
// [A] +0x04 table → DAT_003556d8/uVar8, DAT_003556d4, DAT_003556d0
//   - If present, reads s16 countA at base+0x04.
//   - For each entry i in [0..countA): copies 6 dwords from the source (pointer walks through
//     the on-disk entries) into an arena array with 0x20-byte stride starting at uVar8.
//     Then zero-fills dword slots at +0x18 and +0x1C and writes 0xFFFF at +0x1A (u16).
//   - Records: stride is 0x20; globals updated:
//       DAT_003556d8 = base of the array (4-byte aligned)
//       DAT_003556d4 = countA
//       DAT_003556d0 = s16 loaded from header+0x06
//   - DAT_003556a4 = end of this array, 16-byte aligned (used as a subsequent allocation base).
//
// [B] +0x30 variable-length triples → DAT_0035568c and two arrays (each 0x10 per row)
//   - If present, reads a count via FUN_0022b4e0 into aiStack_a0[0] (s16 stored to DAT_0035568c).
//   - For each row, reads 3 integers via FUN_0022b4e0 and stores them at offsets 0,4,8,
//     with the dword at +0x0C zeroed.
//   - Allocates space for two such tables back-to-back:
//       first table at DAT_003556a4, then second at DAT_003556a8 = DAT_003556a4 + count*0x10
//       DAT_0035572c advances past both.
//
// [C] +0x08 mixed table → DAT_00355694/98/9C/A0
//   - If present, reads s16 countC at base+0x08.
//   - Computes and aligns four pointers:
//       DAT_00355694: byte array of length countC
//       DAT_00355698: u16 array of length countC
//       DAT_0035569c: array of records, stride 0x10 (two dwords at +0 and +4, dword at +0x0C initially 0)
//       DAT_003556a0: a duplicate array of 0x10-byte records (populated by copying from 5569c later)
//   - For each entry:
//       * Reads two ints via FUN_0022b4e0 → stores at 5569c[+0], 5569c[+4]
//       * Reads a u16 (next decoder call) → stores at 55698[i]
//       * Stores a following byte (low 8 bits of next short) into 55694[i]
//       * Zeros 5569c[+0x0C]
//   - Then copies each 0x10-byte record from 5569c to 556a0, splitting 64-bit into two 32-bit slots.
//
// [D] +0x0C 0x80/0x78-stride records → DAT_003556ac/003556b0 family
//   - If present, reads s16 countD at base+0x0C.
//   - Lays out two regions:
//       DAT_003556ac: array of 0x80-byte records (aligned)
//       DAT_003556b0: array of 0x78-byte records
//     and advances DAT_0035572c past both.
//   - For each record i:
//       * Copies four u16s from header stream into two destinations: offsets [+0x24..+0x2A] of 0x80-record,
//         and [+8..+E] of 0x78-record.
//       * Writes two u32s into 0x78-record [+0] and 0x80-record [+0x10]/[+0x70].
//       * Writes three spaced u16s into 0x80-record at [+0x30], [+0x3C], [+0x48] (every +0x0C).
//       * Writes a u16 and a dword into 0x78-record [+0x10] and [+4], and two bytes at [+0x12]/[+0x13]
//       * Reads a u16 index k, then copies a 0x10-byte record from the earlier [A]/[B] area at
//         DAT_003556a4 + k*0x10 into the head of the 0x80-record; also stores k in the 0x80-record.
//
// [E] +0x14 16-byte rows of small byte fields → DAT_003556b4/55690
//   - If present, reads a count via FUN_0022b520 into DAT_00355690.
//   - For each row, reads 6 pairs of bytes (total 12 bytes); writes them packed into a 0x10-byte row
//     in DAT_003556b4, zeroing the remaining pairs for columns 6..7.
//
// [F] +0x18 large 0x1000 shorts table and trailing short list → DAT_003556f0/556ec
//   - If absent: fills 0x1000 shorts at &DAT_00345a18 with 0xFFFF and sets DAT_003556ec=0.
//   - If present: copies 0x1000 shorts to &DAT_00343a18 (alias of &DAT_00345a18), then reads a trailing
//     short count and copies that many shorts into DAT_003556f0 (4-byte aligned).
//
// [G] +0x2C grouped tri indices → DAT_003556f4/556f8
//   - If absent: writes 0 and moves on.
//   - If present: reads s16 groupCount; for each group, s16 n then n*3 s16 values appended. Stores the
//     base pointer to DAT_003556f4, aligns and stores an auxiliary output at DAT_003556f8, then calls
//     FUN_002256d0(DAT_003556f4) to get a size, advances, and calls FUN_002256f0(DAT_003556f4,DAT_003556f8).
//
// [H] +0x34 0x10-byte rows of four ints → DAT_003556e8/556e4
//   - If present: reads count via FUN_0022b4e0, then per row reads 4 ints (3 first, then 1) into the
//     0x10-byte records starting at DAT_003556e8; stores count to DAT_003556e4.
//
// [I] +0x38 0x20-byte rows of 2x2 ints → DAT_003556c8/556b8
//   - Initializes a 0x100-byte block at DAT_003556c8 with zeros.
//   - If present: reads count via FUN_0022b4e0 into DAT_003556b8. For each row i, reads 2x2 ints
//     (two columns of two ints) and stores them interleaved into a 0x20-byte stride at
//     DAT_003556c8 + i*0x20.
//
// [J] +0x1C 0x74-byte records with vector data and relative buffers → DAT_003556e0/556dc
//   - If present: reads s16 countJ; for each record:
//       * Zero-fills 0x74 bytes, then copies 0x1C bytes of 16-bit values from the header stream
//         (13 shorts). Zeros the next 0x56 bytes and sets byte at +0x5A to 3.
//       * Allocates a float buffer (16-byte aligned) and stores its pointer in record[+0x1C].
//         Uses record[+0x00] as an index into [A] table (sVar2 * 0x20 + DAT_003556d8) and stores
//         the record index back into that [A] entry’s u16 at +0x1A.
//       * Loads a pointer into a float source array derived from [C] (pfVar32 = DAT_0035569c + idx*2).
//         Stores that pointer in record[+0x20]. Then subtracts base components stored in this record
//         (three floats at [+4,+8,+0xC]) from the source triplets, writing results sequentially into the
//         allocated buffer (four floats per element; the 4th slot is advanced but not filled here).
//       * Finally writes one more triplet to the buffer from the earlier [A]-entry’s [+8,+0xC,+0x10]
//         minus the same base.
//     Records count stored as DAT_003556dc; DAT_003556e0 is base of these records.
//   - If countJ < 0x80, arena advances by padding space for the difference (0x1D floats per record).
//   - After this, aligns and reserves a separate contiguous area sized by 0x18 bytes per up-to-0x80
//     records (DAT_00354c4c captures the base). This code manipulates the arena pointer arithmetic,
//     but does not populate that area here.
//
// [K] +0x10 3-byte rows → DAT_00355bdc/00355be0
//   - If present: reads s16 countK and then copies 3 bytes per row into a linear region starting at
//     DAT_00355bdc (aligned). Stores count to DAT_00355be0.
//
// Finalization
//   - Calls FUN_0022c3d8(); resets DAT_0035572c to DAT_00355bdc, then FUN_0022c6e8(); FUN_0022d258();
//     FUN_00211230().
//
// Notes
// - FUN_0022b4e0 and FUN_0022b520 provide decoded values from the on-disk stream (variable-length or
//   packed encodings); this function treats their outputs as ints/shorts/bytes and places them without
//   further transformation.
// - Several sections mirror patterns seen in PSC3/PSB4 (grouped triangle index lists at +0x2C). The
//   presence of float subtractions and triplet writes in [J] indicate per-record float buffers are built
//   from previously prepared float arrays; however, this file does not itself introduce any heuristic
//   interpretation beyond the exact reads/writes observed.
//
// Keep all callee names as FUN_* until individually analyzed.
