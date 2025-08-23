// Analyzed re-expression of FUN_00238f18
// Original signature: void FUN_00238f18(long param)
//
// Purpose:
//   Clear or selectively clear dialogue glyph slots and reset several dialogue layout counters.
//   Operates on the global glyph slot array rooted at iGpffffaed4 (300 entries, stride 0x3C).
//
// Behavior (from raw src/FUN_00238f18.c):
//   1. Derive a control value c from *param (if param != 0):
//        c = *param; iVar1 = c+1; iVar2 = c+4; if (iVar1 >= 0) iVar2 = iVar1;
//        *param = (c+1) - 4 * ((iVar2 >> 2) & 0xFF)   (effectively (c+1) mod 4 if c>=0 else c+1)
//      If param == 0: c is set to -1.
//   2. Iterate 300 glyph entries (index 0..299):
//        For each slot (base p):
//           If c < 0: zero byte at p+0x3A (unconditional clear mode)
//           Else if c == *(p+0x3B): zero byte at p+0x3A (selective clear for matching category / lane)
//   3. Reset dialogue layout counters/globals:
//        uGpffffbcdc = 0;
//        uGpffffbcec = 0xFFFFFFFF;
//        uGpffffbcd8 = 0;
//        uGpffffbcd0 = 0;
//
// Interpretation:
//   - Each glyph slot appears to store at +0x3B a small classification (0..3) and +0x3A a per-glyph active flag.
//   - Passing param==NULL triggers a full clear (all flags at +0x3A set to 0).
//   - Passing param!=NULL cycles (*param) through 0..3 (mod 4) and clears only entries whose +0x3B matches that value.
//   - The tail resets tracking counters: likely current line width, sentinel min/max, or similar.
//
// Side effects:
//   - Mutates up to 300 bytes (flags) and four global state fields.
//   - Writes back to *param (if provided) to advance the selective clear cycle.
//
// TODO:
//   - Map precise semantics of bytes at offsets 0x3A/0x3B within a glyph slot structure.
//   - Confirm roles of uGpffffbcdc / uGpffffbcec / uGpffffbcd8 / uGpffffbcd0 via usage tracing.
//
// Wrapper name: dialogue_clear_or_filter_glyph_slots (keep original FUN_* in comment for cross-ref).

#include <stdint.h>

extern int iGpffffaed4; // base of glyph slot array (300 * 0x3C)
extern unsigned int uGpffffbcdc;
extern unsigned int uGpffffbcec;
extern unsigned int uGpffffbcd8;
extern unsigned int uGpffffbcd0;

void dialogue_clear_or_filter_glyph_slots(int *cycle_selector /* may be NULL */)
{
  char c = -1;
  if (cycle_selector)
  {
    c = (char)*cycle_selector;
    int next = c + 1;
    int temp = c + 4;
    if (next >= 0)
      temp = next; // mirrors raw conditional choose
    // Write back: (c+1) - 4*((temp>>2)) ; for non-negative c this becomes (c+1) % 4
    *cycle_selector = next - ((temp >> 2) << 2);
  }

  int remaining = 299;
  int p = iGpffffaed4;
  while (remaining-- >= 0)
  {
    if (c < 0)
    {
      *(unsigned char *)(p + 0x3A) = 0; // full clear mode
    }
    else if (c == *(signed char *)(p + 0x3B))
    {
      *(unsigned char *)(p + 0x3A) = 0; // selective clear (matching category)
    }
    p += 0x3C;
  }

  uGpffffbcdc = 0;
  uGpffffbcec = 0xFFFFFFFF;
  uGpffffbcd8 = 0;
  uGpffffbcd0 = 0;
}

// Preserve original symbol name
void FUN_00238f18(int param_1) { dialogue_clear_or_filter_glyph_slots((int *)param_1); }
