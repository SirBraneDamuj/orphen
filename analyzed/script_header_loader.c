// Analyzed Function: Script Header / Resource Loader
// Original decompiled name: FUN_00228e28
// Source file: src/FUN_00228e28.c
// Summary:
//   Performs initialization of the high‑level script system: allocates / maps a script blob,
//   copies compressed/packed script data into RAM, relocates a 0x2C-byte header (11 uint32
//   entries), fixes up a secondary pointer chain referenced via header[7], and sets globals
//   that later drive pointer table lookups (via FUN_0025b9e8) and record expansion. It also
//   proceeds with additional system setup (graphics/audio/UI unrelated to header), which we
//   deliberately omit here for focus.
//
// Header (file layout) vs in-memory structure:
//   File starts with 11 little-endian uint32 offsets (relative to file start) occupying 0x2C bytes.
//   After load, each is relocated by adding the in-memory base pointer (base = result of
//   FUN_00267f90(uGpffffade0)). We call this base pointer script_base.
//
//   Indices (i = 0..10) after relocation:
//     [0] pointer_table_end / first block after pointer table (absolute ptr)
//     [1] block1_start (absolute)
//     [2] block2_start (absolute)
//     [3] block3_start (absolute)
//     [4] block4_start (absolute)
//     [5] pointer_table_start (absolute)  (matches observed 0x1680 etc.)
//     [6] descriptor_block_start (absolute) — precedes triple arrays by 0x34 bytes
//     [7] footer_start (absolute) — contains pointer @ +0x3C needing second-level relocation
//     [8] array_a_start (absolute)
//     [9] array_b_start (absolute)
//     [10] array_c_start (absolute)
//
// Relocation loop:
//   for (i = 0; i < 11; ++i) header[i] += script_base;
//
// Secondary pointer chain:
//   *(header[7] + 0x3C) is itself an offset list (terminated by 0) that is relocated in-place:
//     p = (int*)( *(header[7] + 0x3C) + script_base );
//     *(header[7] + 0x3C) = p; walk until 0; each entry += script_base.
//
// Dialogue pointer table usage:
//   FUN_0025b9e8(index) obtains script_base again then reads pointer table base from *(script_base + 0x14).
//   That implies header[5] value (pointer_table_start) is stored at offset +0x14 inside the relocated
//   header region in memory (i.e., header entries laid out consecutively from base+0x00).
//
// Globals of interest (retain original names for traceability):
//   uGpffffade0  : handle / identifier used by FUN_00267f90 to recover script_base.
//   (piVar5)     : local variable holding script_base (renamed script_base_ptr below).
//   uGpffffadf8, uGpffffadf4, uGpffffadfc : subsequent structures built after header relocation.
//   puGpffffaed4 : working pointer for building 0x2C-sized records.
//
// External (unanalyzed) helper functions (names kept as-is):
//   FUN_00268010(size)         : allocate / reserve memory (returns handle or pointer encoded)
//   FUN_00267f90(handle)       : convert handle to raw pointer (script_base)
//   FUN_00267da0(dst, src, sz) : copy memory
//   FUN_00267e78(ptr, size)    : zero / clear memory region
//   FUN_0026bfc0(const char*)  : error / logging (used if allocation fails)
//
// Side effects:
//   - Sets uGpffffade0
//   - Relocates header in-place
//   - Relocates chained pointers off footer (header[7])
//   - Initializes various global workspace buffers (not fully detailed here)
//
// PS2 considerations:
//   The relocation pattern (base + relative) is typical of packed resource lumps.
//   The 0x2C header size aligns nicely to cache line considerations once rounded (still small),
//   and subsequent alignment to 16 bytes ( & 0xFFFFFFF0 ) is visible in later allocation steps.
//
// Further work:
//   - Deep analysis of the 0x34-byte descriptor block at header[6]
//   - Identification of semantics for the three parallel arrays at indices 8–10
//   - Footer structure characterization (pointer list referenced at +0x3C inside footer)
//
// Original signature (for reference):
//   void FUN_00228e28(void);
//
// Below: Extracted + reduced version of the header‑relevant core from FUN_00228e28 with
// renamed locals for clarity. Non-header initialization code is elided.

#include <stdint.h>

// Extern globals (names preserved)
extern uint32_t uGpffffade0; // script handle
extern int iGpffffb7b0;      // size of loaded blob (?) used in later allocations

// Extern helper functions (unanalyzed)
extern long FUN_00268010(int size);                   // allocation / mapping
extern int FUN_00267f90(uint32_t handle);             // handle -> base pointer
extern void FUN_00267da0(int dst, int src, int size); // copy
extern void FUN_00267e78(int dst, int size);          // clear/fill zero
extern void FUN_0026bfc0(int msg_id);                 // error/logging stub

void load_script_header_and_relocate(void)
{ // analyzed replacement for FUN_00228e28 (header slice)
  // Acquire / allocate resource (size not shown in reduced slice)
  long alloc_result = FUN_00268010(iGpffffb7b0);
  uGpffffade0 = (uint32_t)alloc_result;
  if (alloc_result == 0)
  {
    FUN_0026bfc0(0x34bf78); // original error path constant
    return;
  }

  // Resolve base pointer for the mapped script blob
  int script_base_ptr = FUN_00267f90(uGpffffade0);

  // Copy raw file image into memory (source address / length elided in this slice)
  // FUN_00267da0(script_base_ptr, file_src, iGpffffb7b0);

  // Relocate 11 header dwords in place (0x2C bytes)
  int *header_words = (int *)script_base_ptr; // header starts at base
  for (int i = 0; i < 11; ++i)
  {
    header_words[i] += script_base_ptr;
  }

  // Secondary pointer chain from footer: *(header[7] + 0x3C)
  int footer_ptr = header_words[7];                 // absolute footer start
  int *chain_location = (int *)(footer_ptr + 0x3C); // stored (relative) pointer list
  int *list_base = (int *)(*chain_location + script_base_ptr);
  *chain_location = (int)list_base; // replace with absolute address

  // Walk list until terminator (0). Each non-zero entry is an offset to fix.
  if (*list_base != 0)
  {
    int *p = list_base;
    int val;
    while ((val = *p) != 0)
    {
      *p++ = val + script_base_ptr;
    }
  }
}

// Notes:
// - This reduced function omits many unrelated initializations present in the full FUN_00228e28.
// - All original names of called helper functions are preserved; further analysis should
//   eventually rename them once their broader roles are confirmed.
// - Validation strategy: set a breakpoint after the relocation loop, dump 0x40 bytes at
//   script_base_ptr, confirm the 11 dwords are absolute (each > script_base_ptr and ascending
//   except for index 5 placed earlier than indices 0–4 relative ordering in file).
