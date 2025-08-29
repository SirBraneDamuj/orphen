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

// Forward declarations (retain original FUN_* references)
extern void FUN_00239020(undefined4 *packet); // Submit prepared UI primitive packet
extern void FUN_0026bf90(int code);           // Scratchpad overflow handler

// Scratchpad pointer (local stub if not provided by a shared header yet)
static undefined4 *scratchpad_ptr = (undefined4 *)0x70000000; // DAT_70000000

/*
 * animated_icon_strip_renderer (analyzed)
 * Original: FUN_00238878
 *
 * Purpose:
 *   Renders a horizontal strip of one or more 0x16x0x16 (22x22 texel) icon frames sourced from a
 *   contiguous 11-frame row inside the shared UI icon texture atlas (texture id passed in param_texture_id).
 *   While emitting 'count' instances (param_repeat_count), it advances the animation frame per draw and wraps
 *   across two coordinates (U primary in packet[6], V/secondary row offset in packet[7]) implementing a
 *   11x? frame sheet. The frame index progression logic produces a cycling animation effect across repeated
 *   draws in a single call (often invoked each frame with param_3 carrying a global frame counter).
 *
 * Frame math:
 *   - Base frame index comes from param_frame_counter (param_3). Each logical frame corresponds to a 0x16 texel stride.
 *   - Total horizontal span per 11-frame cycle = 11 * 0x16 = 0xF2 (242) texels.
 *   - U coordinate (packet[6]) = (frame_counter * 0x16) % 0xF2 (wrap inside the 11-frame row).
 *   - V row offset accumulator (packet[7]) increments by 0x16 every time U wraps past 0xF1, up to 0xEA then resets.
 *     This effectively allows stacking cycles vertically after 11 frames, creating a 11x11 grid if needed.
 *
 * Parameters (renamed for clarity):
 *   param_screen_x        (param_1) : Initial X position (packet[2])
 *   param_screen_y        (param_2) : Initial Y position (packet[3])
 *   param_frame_counter   (param_3) : Global/incrementing frame counter driving animation
 *   param_repeat_count    (param_4) : Number of consecutive icons to draw (horizontally spaced)
 *   param_color           (param_5) : 32-bit RGBA tint (packet[0xC])
 *   param_x_advance       (param_6) : Per-icon X delta added after each draw (packet[2] += advance)
 *   param_icon_height     (param_7) : Height (packet[5]) — typically 0x16
 *   param_texture_id      (param_8) : Texture / packet type (packet[0]) — e.g., 0x82C for UI atlas
 *
 * Packet fields used:
 *   [0] texture id / primitive kind
 *   [1] mode constant 0xFFFFEFF7 (standard UI blend)
 *   [2] X position (updated per iteration)
 *   [3] Y position
 *   [4] Width  (param_x_advance or explicit width?) — here set to param_x_advance; kept verbatim
 *   [5] Height (param_icon_height)
 *   [6] U (animated horizontal frame within 0xF2 span)
 *   [7] V row offset accumulator (increments on wrap)
 *   [8] Texture height (fixed 0x16)
 *   [9] Texture width  (fixed 0x16)
 *   [0xB] Flag = 1 (marks animated / textured variant; original code sets before param population)
 *   [0xC] Color tint (param_color)
 *   *(packet+10) low control byte = 0
 *
 * Animation wrap logic (verbatim):
 *   After each draw: U += 0x16; if U > 0xF1 then U resets to 0 and V += 0x16; if V > 0xE9 then V resets to 0.
 *   Initial U derived from modulo; initial V derived from (frame_counter / 11) * 0x16 (with integer truncation path).
 *
 * Note:
 *   Division step iVar1 = (frame_counter * 0x16) / 0xF2 and correction constructing packet[7] reproduces the decompiler’s
 *   signed division rounding toward zero vs floor behavior; result is effectively floor(frame_counter / 11) * 0x16.
 */

void animated_icon_strip_renderer(int param_screen_x,
                                  int param_screen_y,
                                  int param_frame_counter,
                                  int param_repeat_count,
                                  undefined4 param_color,
                                  int param_x_advance,
                                  undefined4 param_icon_height,
                                  undefined4 param_texture_id)
{
  undefined4 *packet = scratchpad_ptr;
  scratchpad_ptr += 0x10; // reserve 0x40 bytes (16 dwords)
  if (scratchpad_ptr > (undefined4 *)0x70003fff)
  {
    FUN_0026bf90(0);
  }

  // Base mode & flags
  packet[1] = 0xFFFFEFF7; // standard UI blend/mode constant
  packet[0xB] = 1;        // flag set in original (purpose: textured sprite indicator)

  // Compute initial frame subdivision
  int frame_mul = param_frame_counter * 0x16; // scaled frame
  int div = frame_mul / 0xF2;                 // truncated division (frames / 11)
  int adjust = div + 0xFF;
  if (div >= 0)
    adjust = div;                                    // sign adjustment from original pattern
  packet[7] = (div + (adjust >> 8) * -0x100) * 0x16; // vertical (row) offset = floor(div) * 0x16
  packet[6] = frame_mul % 0xF2;                      // horizontal frame within row (0..0xF1)

  // Core static fields
  packet[2] = param_screen_x;
  packet[3] = param_screen_y;
  packet[0xC] = param_color;
  packet[0] = param_texture_id;
  packet[5] = param_icon_height;    // height
  packet[9] = 0x16;                 // tex width (square cell per atlas tile)
  packet[4] = param_x_advance;      // width / step value (original stored param_6 here)
  packet[8] = 0x16;                 // tex height
  *(undefined1 *)(packet + 10) = 0; // control byte

  // Emit repeated icons with progressive frame stepping
  while (param_repeat_count > 0)
  {
    FUN_00239020(packet);

    int u_now = packet[6] + 0x16; // advance U by one frame stride
    if (u_now > 0xF1)
    { // wrap after 11 frames (0xF2 span)
      int v_now = packet[7] + 0x16;
      packet[6] = 0;
      if (v_now > 0xE9)
      {
        v_now = 0; // wrap vertical accumulator
      }
      packet[7] = v_now;
    }
    else
    {
      packet[6] = u_now;
    }

    packet[2] += param_x_advance; // move X for next icon cell
    param_repeat_count--;
  }

  scratchpad_ptr -= 0x10; // release scratchpad reservation
}
