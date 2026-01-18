// dialogue_per_frame_renderer — analyzed version of FUN_00237fc0
// Original signature: void FUN_00237fc0(void)
//
// Purpose:
//   Main per-frame dialogue/text system coordinator. Called from the main game loop each frame.
//   Orchestrates:
//   1. Global frame timing increments for dialogue pacing
//   2. Multi-layer glyph/overlay rendering with priority order (layer 3→0)
//   3. Optional debug menu navigation (DAT_005716c0-gated scrolling and text display)
//   4. Text stream advancement control opcodes (0x01, 0x04, 0x05) for special events
//   5. Glyph emission timing throttle via FUN_00237de8 (dialogue_text_advance_tick)
//
// Flow Overview:
//   - If dialogue active (pcGpffffaec0 != NULL):
//       • Advance global dialogue timer (iGpffffbce8) and optional fade timer (iGpffffbd00)
//       • If debug menu active: call FUN_00239110 (menu handler)
//       • Render all active glyphs/overlays in 4 priority layers (3→0):
//           - Loop through 300 glyph slots (stride 0x3C at iGpffffaed4)
//           - For each slot where +0x3A!=0 (active) and +0x3B==layer: call FUN_00239020 (submit GPU packet)
//       • If debug menu: handle D-pad navigation and render menu text
//       • Process stream control opcodes if cursor active (iGpffffbcec >= 0):
//           - 0x01: terminate stream, set flags 0x8FF/0x8FE, set bit 0x6000
//           - 0x04: call FUN_00238f18 (clear/filter glyph slots)
//           - 0x05: call FUN_00238f98 (advance glyph timers)
//       • While timing budget available (iGpffffbce8 > 0x1F):
//           - Decrement per-glyph wait counters (iGpffffbcf8/bffc)
//           - Call FUN_00237de8 (dialogue_text_advance_tick) to emit next glyph
//           - Deduct 0x20 from frame budget each iteration (pacing throttle)
//
// Key Globals:
//   pcGpffffaec0   - Active dialogue stream pointer (NULL = inactive)
//   iGpffffbce8    - Frame timing accumulator (advanced by uGpffffb64c each frame)
//   iGpffffbd00    - Optional fade/timeout timer
//   iGpffffaed4    - Glyph slot array base (300 entries, stride 0x3C)
//   DAT_005716c0   - Debug menu enable flag
//   iGpffffbcec    - Stream control cursor state (-1 = inactive)
//   iGpffffbcf8/fc - Per-glyph wait counters
//   uGpffffb64c    - Frame delta (vsync-based, likely 16-17ms worth of ticks)
//
// Rendering Priority:
//   Processes layers 3→2→1→0 in that order. Higher layer numbers render first (back-to-front?).
//   Each slot stores its layer ID at offset +0x3B.
//
// Debug Menu (DAT_005716c0 != 0):
//   - D-pad up/down scroll through menu entries
//   - Button check via FUN_0023b9f8(0x5000, 1) (controller input)
//   - Renders menu text at position 0x130 - text_width, -0x5C
//   - On confirm (uGpffffb686 & 0x40): stores selection to work array, clears menu
//
// PS2-specific Notes:
//   - Multi-layer rendering suggests hardware sprite priority or Z-order management
//   - Stride 0x3C (60 bytes) per glyph slot implies structured GPU packet or sprite descriptor
//   - 300 slots = significant display capacity for dialogue-heavy scenes
//
// Related Functions:
//   FUN_00239020  - dialogue_glyph_renderer_submit (GPU packet submission)
//   FUN_00237de8  - dialogue_text_advance_tick (stream parsing/glyph emission)
//   FUN_00238f18  - dialogue_clear_or_filter_glyph_slots
//   FUN_00238f98  - advance_active_overlay_timers (glyph countdown advancement)
//   FUN_00239110  - debug menu handler
//
// Wrapper provided for clarity; original name preserved for cross-reference.

#include <stdint.h>
#include <stdbool.h>

// Globals (dialogue system)
extern char *pcGpffffaec0;         // active stream pointer
extern int iGpffffbce8;            // frame timing accumulator
extern int iGpffffbd00;            // fade/timeout timer
extern unsigned int uGpffffb64c;   // frame delta
extern int iGpffffaed4;            // glyph slot array base
extern unsigned int uGpffffb684;   // controller state flags
extern unsigned int uGpffffb686;   // controller confirm flags
extern unsigned int uGpffffb68a;   // additional controller flags
extern int iGpffffb0f0;            // work memory array base
extern unsigned int uGpffffb0f4;   // status bitfield
extern unsigned short uGpffffaecc; // menu animation counter
extern int *puGpffffbcf0;          // pointer to menu glyph slot
extern int iGpffffbcec;            // stream control cursor state
extern int iGpffffbcf8;            // glyph wait counter 1
extern int iGpffffbcfc;            // glyph wait counter 2
extern int iGpffffbcdc;            // glyph timing baseline
extern int iGpffffbcd8;            // glyph line index
extern int iGpffffaec8;            // glyph default wait time

// Globals (debug menu)
extern int DAT_005716c0; // debug menu enable/entry count
extern int DAT_005716c4; // debug menu cursor index
extern int DAT_005716c8; // debug menu selection id
extern int DAT_005716d0; // debug menu text offset

// External functions
extern void FUN_00239110(void);                                                                   // debug menu handler
extern void FUN_00239020(int slot_ptr);                                                           // submit glyph GPU packet (analyzed: dialogue_glyph_renderer_submit)
extern long FUN_0023b9f8(int button_mask, int mode);                                              // controller input check
extern void FUN_002256c0(void);                                                                   // debug menu update/scroll handler
extern undefined8 FUN_0025b9e8(int text_id);                                                      // fetch text resource
extern int FUN_00238e68(undefined8 text_res, int mode);                                           // calculate text width
extern void FUN_00238608(int x, int y, undefined8 text_res, int color, int scale_x, int scale_y); // render text
extern void FUN_002256b0(void);                                                                   // debug menu confirm handler
extern void FUN_00238f18(long param);                                                             // clear/filter glyph slots
extern void FUN_002663a0(unsigned int flag_index);                                                // set_global_event_flag
extern void FUN_0023bae8(void);                                                                   // unknown cleanup routine
extern void FUN_00238f98(void);                                                                   // advance_active_overlay_timers
extern void FUN_00237de8(void);                                                                   // dialogue_text_advance_tick

void dialogue_per_frame_renderer(void)
{
  // Early exit if no active dialogue stream
  if (pcGpffffaec0 == NULL)
    return;

  // Advance global dialogue timing
  iGpffffbce8 += uGpffffb64c;

  // Advance fade/timeout timer if active
  if (iGpffffbd00 >= 0 && iGpffffbd00 < 0x119400)
  {
    iGpffffbd00 += uGpffffb64c;
  }

  // Debug menu handling
  if (DAT_005716c0 != 0)
  {
    FUN_00239110();
  }

  // === Multi-layer glyph rendering (priority layers 3→0) ===
  for (int layer = 3; layer >= 0; layer--)
  {
    int slot_index = 0;
    int slot_ptr = iGpffffaed4;

    // Iterate all 300 glyph slots
    for (int i = 0; i < 300; i++)
    {
      // Check if slot active (+0x3A) and matches current layer (+0x3B)
      if (*(char *)(slot_ptr + 0x3A) != 0 && *(char *)(slot_ptr + 0x3B) == layer)
      {
        FUN_00239020(slot_ptr); // submit GPU packet for this glyph/overlay
      }
      slot_ptr += 0x3C; // advance to next slot (stride 60 bytes)
    }
  }

  // === Debug menu navigation and display ===
  if (DAT_005716c0 != 0)
  {
    long input = FUN_0023b9f8(0x5000, 1); // check D-pad up/down
    if (input != 0)
    {
      // Scroll menu with D-pad
      if ((uGpffffb684 & 0x1000) == 0) // D-pad down
      {
        if ((uGpffffb684 & 0x4000) != 0) // D-pad up
        {
          DAT_005716c4++;
          DAT_005716d0 -= 0x16; // scroll up (22 pixels per entry)
          if (DAT_005716c4 >= DAT_005716c0)
          {
            // Wrap to bottom
            DAT_005716c4 = 0;
            DAT_005716d0 += DAT_005716c0 * 0x16;
          }
        }
      }
      else // D-pad down
      {
        DAT_005716c4--;
        DAT_005716d0 += 0x16; // scroll down
        if (DAT_005716c4 < 0)
        {
          // Wrap to top
          DAT_005716c4 = DAT_005716c0 - 1;
          DAT_005716d0 -= DAT_005716c0 * 0x16;
        }
      }
      FUN_002256c0(); // update menu display
    }

    // Render menu text
    if ((uGpffffb686 & 0x40) == 0)
    {
      undefined8 text_res = FUN_0025b9e8(0x28); // fetch menu text resource
      int text_width = FUN_00238e68(text_res, 0x14);
      FUN_00238608(0x130 - text_width, -0x5C, text_res, 0xFFFFFFFF80808080, 0x14, 0x16);
      return;
    }

    // Menu confirm - store selection and clean up
    FUN_002256b0();
    *(int *)(DAT_005716c8 * 4 + iGpffffb0f0) = DAT_005716c4 + 1;
    FUN_00238f18(0); // clear glyph slots
    DAT_005716c0 = 0;
    iGpffffbce8 = 0;
  }

  // === Stream control opcode processing ===
  if (iGpffffbcec >= 0)
  {
    // Advance menu cursor animation counter
    short cursor_advance = (short)uGpffffaecc + 0x7F;
    if ((short)uGpffffaecc >= 0)
      cursor_advance = (short)uGpffffaecc;
    int cursor_step = (int)cursor_advance >> 7;

    uGpffffaecc += (unsigned short)uGpffffb64c;
    if ((uGpffffaecc & 0xFFFF) > 0x200)
      uGpffffaecc = 0;

    // Update menu cursor position if slot allocated
    if (puGpffffbcf0 != NULL)
    {
      int adjusted_step = cursor_step % 4;
      puGpffffbcf0[6] = (int)*(short *)((char *)&DAT_0031c630 + adjusted_step * 4);     // cursor X offset
      puGpffffbcf0[7] = (int)*(short *)((char *)&DAT_0031c630 + adjusted_step * 4 + 2); // cursor Y offset

      // Confirm button pressed - configure cursor slot for rendering
      if ((uGpffffb68a & 0x40) != 0)
      {
        *(unsigned char *)((int)puGpffffbcf0 + 0x3A) = 0;        // mark inactive initially?
        *puGpffffbcf0 = 0x2E;                                    // sprite primitive code
        puGpffffbcf0[4] = 0x14;                                  // width
        puGpffffbcf0[9] = 0x16;                                  // height2
        puGpffffbcf0[5] = 0x16;                                  // height
        puGpffffbcf0[8] = 0x16;                                  // width2
        iGpffffbcdc = (int)*(short *)((int)puGpffffbcf0 + 0x36); // baseline timing
        iGpffffbcd8 = (int)*(short *)(puGpffffbcf0 + 0xD);       // line index
      }
    }

    iGpffffbcec = -1; // clear control cursor state

    // Process stream control opcode
    char control = *pcGpffffaec0;
    if (control == 0x01)
    {
      // Stream terminate - set event flags and clear stream
      pcGpffffaec0 = NULL;
      FUN_0023bae8();        // cleanup routine
      FUN_002663a0(0x8FF);   // set flag 0x8FF
      FUN_002663a0(0x8FE);   // set flag 0x8FE
      uGpffffb0f4 |= 0x6000; // set status bits
      return;
    }
    else if (control == 0x04)
    {
      // Clear/filter glyph slots with parameter
      FUN_00238f18((long)&pcGpffffaec0[1]); // pass pointer to byte after opcode
    }
    else if (control == 0x05)
    {
      // Advance active glyph timers
      FUN_00238f98();
    }

    iGpffffbce8 = 0; // reset frame timer
    pcGpffffaec0++;  // advance past control opcode
  }

  // === Per-frame glyph emission throttle ===
  // Process glyphs while timing budget available (decrements by 0x20 per iteration)
  while (iGpffffbce8 > 0x1F && pcGpffffaec0 != NULL)
  {
    bool any_active = false;
    int *wait_counter = &iGpffffbcf8;

    // Decrement two per-glyph wait counters
    for (int i = 0; i < 2; i++)
    {
      if (*wait_counter != 0)
      {
        int new_val = *wait_counter - uGpffffb64c;
        *wait_counter = new_val;
        if (new_val < 1)
          *wait_counter = 0;
        any_active = true;
      }
      wait_counter++;
    }

    // Emit next glyph if no wait timers active
    if (!any_active)
    {
      FUN_00237de8(); // dialogue_text_advance_tick
      if (iGpffffbcf8 == 0)
        iGpffffbcf8 = iGpffffaec8; // reset default wait time
    }

    iGpffffbce8 -= 0x20; // deduct timing budget
  }
}

// Wrapper preserving original name
void FUN_00237fc0(void)
{
  dialogue_per_frame_renderer();
}
