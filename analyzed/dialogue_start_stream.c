// Analyzed re-expression of FUN_00237b38
// Original signature: void FUN_00237b38(long stream_ptr)
//
// Verified Role (raw src comparison):
//   Start, restart, or terminate a dialogue/text stream by setting global pointer pcGpffffaec0 and
//   (on first ever activation) initializing related per-session state buffers and glyph slot metadata.
//   Also toggles three resource/overlay IDs (0x509 always enabled during (re)start path; 0x8FF/0x8FE
//   enabled when starting, disabled when terminating) and adjusts bits 0x6000 in uGpffffb0f4.
//
// Control cases:
//   1) stream_ptr == 0:
//        - Treated as termination: pcGpffffaec0 set to 0, resources 0x8FF & 0x8FE disabled via FUN_002663a0,
//          bits 0x6000 set in uGpffffb0f4.
//   2) stream_ptr != 0 and *stream_ptr != 0x02:
//        - Start / (re)start path. Always enables 0x509; if this is the first ever activation
//          (previous pointer was null) performs one-time initialization:
//             uGpffffbcc8 = -0x130, uGpffffbccc = -0x78
//             uGpffffbce0 = 2; uGpffffbce4 = 600; uGpffffaec8 = 0; uGpffffbcd4 = 0x80808080
//             Calls FUN_00238f18(0)
//             Clears 300 glyph slots at iGpffffaed4, stride 0x3C:
//                *(slot+0x04)=0xFFFFEFF7, *(slot+0x2C)=1
//             uGpffffbd00 = 0xFFFFFFFF
//          Then enables 0x8FF & 0x8FE (FUN_002663d8) and clears bits 0x6000 in uGpffffb0f4.
//   3) stream_ptr != 0 and *stream_ptr == 0x02:
//        - Abort: resets pcGpffffaec0 back to 0 (does NOT run initialization or resource toggles beyond prior set).
//
// Always after the above (for cases 1 & 2, not abort case):
//   DAT_005716c0 = 0; uGpffffbcf4 = 8; uGpffffbce8 = 0.
//
// Sentinel meaning (byte 0x02): Only observed blocking start; exact semantic not yet derived—kept as guard.
// Glyph slot pattern: 0xFFFFEFF7 likely a sentinel marker (e.g. invalid bounding box / clear state). Value 1 at +0x2C
// may be an initial counter or “free” flag. No assumptions baked into code beyond copying pattern.
//
// Conservatively avoid speculative naming of resource IDs (0x509, 0x8FF, 0x8FE) until their usage surfaces elsewhere.
//
// Side effects summary:
//   - pcGpffffaec0, position/format globals, glyph slot array init, resource enable/disable, bitfield mutation,
//     state mode (uGpffffbcf4), counters (uGpffffbce8, DAT_005716c0), sentinel timestamp (uGpffffbd00).
//
// Wrapper: dialogue_start_stream() plus original name preserved for cross references.

#include "orphen_globals.h"

// External globals (types provisional)
extern char *pcGpffffaec0;        // active stream pointer
extern unsigned int uGpffffbcc8;  // init: -0x130
extern unsigned int uGpffffbccc;  // init: -0x78
extern unsigned int uGpffffbce0;  // init: 2
extern int iGpffffbce4;           // init: 600
extern unsigned int uGpffffaec8;  // cleared to 0
extern unsigned int uGpffffbcd4;  // init color 0x80808080
extern int iGpffffaed4;           // glyph slot array base
extern unsigned int uGpffffbd00;  // timing sentinel
extern unsigned int uGpffffb0f4;  // global flags bitfield (UI / overlay)
extern unsigned int DAT_005716c0; // list size / scroll count
extern unsigned int uGpffffbcf4;  // state mode
extern unsigned int uGpffffbce8;  // elapsed counter

// External funcs (rename analyzed flag bit operations; keep originals in comments)
extern void clear_global_event_flag(int bit_index);                    // FUN_002663d8
extern void set_global_event_flag(int bit_index);                      // FUN_002663a0
extern void dialogue_clear_or_filter_glyph_slots(int *cycle_selector); // FUN_00238f18

static void dialogue_start_stream_impl(long streamPtr)
{
  bool wasNull = (pcGpffffaec0 == 0);
  pcGpffffaec0 = (char *)streamPtr;
  if (streamPtr == 0 || *pcGpffffaec0 != 0x02)
  {
    // Always enable 0x509 at (re)start / terminate path
    clear_global_event_flag(0x509); // was FUN_002663d8
    if (wasNull)
    {
      uGpffffbcc8 = 0xFFFFFED0; // -0x130
      uGpffffbccc = 0xFFFFFF88; // -0x78
      uGpffffbce0 = 2;
      iGpffffbce4 = 600;
      uGpffffaec8 = 0;
      dialogue_clear_or_filter_glyph_slots(0); // clear glyph records (was FUN_00238f18)
      uGpffffbcd4 = 0x80808080;
      // Initialize 300 glyph slots (index 0..299)
      int count = 299;
      int p = iGpffffaed4;
      while (count-- >= 0)
      {
        *(unsigned int *)(p + 0x04) = 0xFFFFEFF7; // sentinel
        *(unsigned int *)(p + 0x2C) = 1;          // flag / init value
        p += 0x3C;
      }
      uGpffffbd00 = 0xFFFFFFFF;
    }
    if (streamPtr == 0)
    {
      // Termination path: disable 0x8FF / 0x8FE and set bits 0x6000
      set_global_event_flag(0x8FF); // was FUN_002663a0
      set_global_event_flag(0x8FE);
      uGpffffb0f4 |= 0x6000;
    }
    else
    {
      // Start path: enable 0x8FF / 0x8FE and clear bits 0x6000
      clear_global_event_flag(0x8FF); // was FUN_002663d8
      clear_global_event_flag(0x8FE);
      uGpffffb0f4 &= 0x9FFF; // clear bits 0x6000
    }
    DAT_005716c0 = 0;
    uGpffffbcf4 = 8;
    uGpffffbce8 = 0;
  }
  else
  {
    // Abort: first byte sentinel 0x02
    pcGpffffaec0 = 0;
  }
}

void dialogue_start_stream(long ptr) { dialogue_start_stream_impl(ptr); }

// Wrapper preserving original name
void FUN_00237b38(long param_1) { dialogue_start_stream_impl(param_1); }
