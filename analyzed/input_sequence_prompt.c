#include "orphen_globals.h"

#ifndef ORPHEN_PRIMITIVE_TYPEDEFS
#define ORPHEN_PRIMITIVE_TYPEDEFS
typedef unsigned char byte;
typedef unsigned char undefined1;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned int undefined4;
typedef unsigned long long undefined8;
#endif

// Forward references (retain original FUN_* names in comments for traceability)
extern char *FUN_0025ba58(int id);                                                          // returns pointer to pattern string (id 0x12 here)
extern undefined8 FUN_0025b9e8(int text_id);                                                // fetch text resource (0x3e used for width calc)
extern int FUN_00238c08(undefined8 text_res, int mode);                                     // compute displayed width / glyph count (?)
extern void FUN_00203b20(void);                                                             // per-frame tick / pad poll side-effect
extern void FUN_002686a0(void);                                                             // conclude / close prompt state
extern void FUN_00238430(int x, int y, int w, int h, uint color, int scale_x, int scale_y); // small quad/text helper
extern void FUN_00231da0(int symbol, short *out_uv);                                        // map symbol index -> UV (DAT_0031c220/222)
extern void FUN_00239020(undefined4 *packet);                                               // submit GPU packet
extern void FUN_0026bf90(int code);                                                         // scratchpad overflow handler

// Local analyzed stand‑alone stubs for globals (replace with shared header once centralized)
ushort g_input_sequence_progress = 0;                  // DAT_0035506e (0..N, 0xFFFF = failure, reset to 0 on restart)
int g_prompt_layout_width = 0;                         // DAT_00355d20 (derived from text resource width of text id 0x3E)
ushort g_controller_state = 0;                         // DAT_003555f6 (lower 8 bits: buttons; 0x800: cancel flag)
ushort g_input_mask_table[256] = {0};                  // &DAT_0031e6d0 (2 bytes per pattern character code)
undefined4 *scratchpad_ptr = (undefined4 *)0x70000000; // DAT_70000000

/*
 * input_sequence_prompt_handle (analyzed)
 * Original: FUN_00267360
 * Purpose:
 *   Validates an incremental player input sequence (pattern sourced via FUN_0025ba58(0x12)) against
 *   a per-character controller mask table (&DAT_0031e6d0). Renders a horizontal row of previously
 *   matched symbols (icons from texture sheet 0x82C) along with background bars when param_show_ui != 0.
 *
 * Parameters:
 *   param_show_ui (long) : if non-zero, draws the prompt visualization; if negative, forces reset only.
 *
 * Global State:
 *   g_input_sequence_progress (u16): current index into pattern string; 0xFFFF marks failure state.
 *   g_controller_state: bitfield of current buttons; bit 0x800 triggers immediate cancel.
 *   g_input_mask_table: lookup of required button mask for each pattern character (char value *2).
 *   g_prompt_layout_width: cached width from a reference text resource (text id 0x3E) used for layout math.
 *
 * Return Codes (u32 coerced from signed):
 *   0  : no progression / idle
 *   1  : advanced successfully by one symbol
 *   10 : sequence completed successfully this call (also invokes FUN_002686a0)
 *   0xFFFFFFFF (-1)      : wrong input — sequence failed (progress set to 0xFFFF, closes via FUN_002686a0)
 *   0xFFFFFFF6 (-10)     : cancelled via controller_state bit 0x800 (progress set to 0xFFFF)
 *
 * Drawing Notes:
 *   - Background bars are three calls to FUN_00238430 forming a frame/track for symbols.
 *   - Each accepted symbol generates a scratchpad GPU packet (type 0x82C) reused in a loop; only X advances.
 *   - Symbol scaling constants: logical cell 0x14 (20) wide by 0x16 (22) high; texture sheet stride 0x20.
 *
 * Reset Behavior:
 *   - Triggered if progress < 0 (failed previously) or caller passes param_show_ui < 0.
 *   - Re-fetches reference text (id 0x3E) to recalc layout width, zeros progress.
 */

unsigned int input_sequence_prompt_handle(long param_show_ui)
{
  unsigned int result = 0;
  long pattern_len_counter = 1;       // counts pattern characters to find end
  char *pattern = FUN_0025ba58(0x12); // pattern string (NUL terminated)
  char ch = *pattern;

  // Determine total pattern length (side-effect: FUN_00203b20 called per char after first)
  while (ch != '\0')
  {
    FUN_00203b20();
    pattern_len_counter = pattern_len_counter + 1;
    ch = pattern[(int)pattern_len_counter - 1];
  }

  // Initialization / reset path
  if ((short)g_input_sequence_progress < 0 || param_show_ui < 0)
  {
    undefined8 text_res = FUN_0025b9e8(0x3e);
    g_prompt_layout_width = FUN_00238c08(text_res, 0);
    g_input_sequence_progress = 0;
    if (param_show_ui < 0)
    {
      return 0; // only reset requested
    }
  }

  // Cancel via special bit (0x800)
  if ((g_controller_state & 0x800) != 0)
  {
    g_input_sequence_progress = 0xFFFF;
    FUN_002686a0();
    return 0xFFFFFFF6; // cancelled
  }

  // Progression check: have remaining characters & some button pressed (low byte != 0)
  if ((short)g_input_sequence_progress < pattern_len_counter && (g_controller_state & 0xFF) != 0)
  {
    ushort required_mask = g_input_mask_table[(unsigned char)pattern[g_input_sequence_progress]];
    if ((g_controller_state & required_mask) != required_mask)
    {
      // Wrong input -> fail sequence
      g_input_sequence_progress = 0xFFFF;
      FUN_002686a0();
      return 0xFFFFFFFF; // failure
    }
    // Accept symbol
    unsigned int prev = g_input_sequence_progress;
    g_input_sequence_progress = (ushort)(prev + 1);
    result = 1;
    if (pattern[(prev + 1) & 0xFFFF] == '\0')
    {
      result = 10; // completed
      FUN_002686a0();
    }
  }

  // Optional rendering
  if (param_show_ui != 0)
  {
    // Layout math reproduces original integer sequence; kept verbatim for fidelity
    FUN_00238430((g_prompt_layout_width - 2) * -10, 0, 0, g_prompt_layout_width - 2, 0x80808080, 0x14, 0x16);
    int base_x = (int)(pattern_len_counter + 2) * -10;
    FUN_00238430(base_x, -0x16, g_prompt_layout_width - 2, 1, 0x80808080, 0x14, 0x16);
    FUN_00238430((int)pattern_len_counter * 0x14 + base_x, -0x16, g_prompt_layout_width - 1, 1, 0x80808080, 0x14, 0x16);

    undefined4 *packet = scratchpad_ptr;
    scratchpad_ptr += 0x10; // allocate 0x40 bytes (16 dwords)
    if (scratchpad_ptr > (undefined4 *)0x70003fff)
    {
      FUN_0026bf90(0); // overflow handler
    }
    // Base packet setup (icon cell template)
    packet[1] = 0xFFFFEFF7;
    packet[0] = 0x82c;         // texture id / packet type
    packet[2] = base_x + 0x14; // initial X (original logic adds 0x14 once before first symbol)
    packet[3] = -0x16;         // Y position (0xFFFFFFEA)
    packet[4] = 0x14;          // width 20
    packet[5] = 0x16;          // height 22
    packet[8] = 0x20;          // texture stride width
    packet[0xC] = 0x80808080;  // color tint
    packet[9] = 0x20;          // texture stride height
    *(undefined1 *)(packet + 10) = 0;
    packet[0xB] = 0;

    // Render accepted symbols
    if ((short)g_input_sequence_progress > 0)
    {
      long rendered = 0;
      while (rendered < (short)g_input_sequence_progress)
      {
        short uv[2];
        FUN_00231da0(pattern[rendered], uv);
        packet[6] = (int)uv[0]; // U
        packet[7] = (int)uv[1]; // V
        FUN_00239020(packet);
        rendered++;
        packet[2] += 0x14; // advance X
      }
    }
    scratchpad_ptr -= 0x10; // release
  }

  return result;
}
