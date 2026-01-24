/*
 * Text Opcode 0x1D: Insert Special Icon/Glyph - FUN_00239b00
 *
 * Dialogue/text system opcode handler that inserts a special icon or decorative
 * glyph into the active text display. This appears to be used for icons like
 * item symbols, status indicators, or special markers in dialogue boxes.
 *
 * The function:
 * 1. Reads a glyph/icon ID from the text stream (1 byte after opcode)
 * 2. Looks up UV texture coordinates for the icon via FUN_00231dd0
 * 3. Finds an available glyph slot in the active overlay array
 * 4. Initializes the slot with position, size, UV coords, and timing
 * 5. Advances horizontal position for next character
 * 6. Calls FUN_00238f98 to flush/wrap if line is full
 *
 * Glyph slot structure (60 bytes, stride 0x3C):
 * - +0x00: Sprite/render type (0x82C)
 * - +0x08-0x0B: X position
 * - +0x0C-0x0F: Y position
 * - +0x10-0x13: Width (0x14 = 20 pixels)
 * - +0x14-0x17: Height (0x16 = 22 pixels)
 * - +0x18-0x1B: U coordinate
 * - +0x1C-0x1F: V coordinate
 * - +0x20-0x23: Texture width (0x20 = 32)
 * - +0x24-0x27: Texture height (0x20 = 32)
 * - +0x30: Color (0x80808080 = white/full brightness)
 * - +0x34: Line index
 * - +0x36: Timing baseline
 * - +0x3A: Active flag (1=active)
 * - +0x3B: Layer/tag ID
 *
 * This is similar to dialogue_glyph_enqueue.c but specifically for special
 * icons rather than regular text glyphs.
 *
 * Original function: FUN_00239b00
 * Address: 0x00239b00
 * Text opcode: 0x1D
 */

#include <stdint.h>

// Forward declarations
extern void FUN_00231dd0(uint8_t icon_id, int16_t *uv_coords); // Lookup UV coordinates for icon
extern void FUN_00238f98(void);                                // Flush/wrap line if needed

// Global variables
extern int32_t *puGpffffaed4;    // Glyph slot array base pointer
extern int32_t iGpffffaec0;      // Text stream read pointer
extern uint8_t gp0xffffaed0[16]; // Icon ID lookup table
extern uint8_t uGpffffaec4;      // Current layer/tag ID

// Positioning and layout variables
extern int16_t iGpffffbcdc; // Horizontal position accumulator (X offset)
extern int16_t sGpffffbcd8; // Current line index (Y offset multiplier)
extern int32_t iGpffffbcd0; // Delay adjustment gating flag
extern int32_t iGpffffbcc8; // Base X position for text box
extern int32_t iGpffffbccc; // Base Y position for text box
extern int32_t iGpffffbce4; // Horizontal limit for line wrapping
extern int32_t iGpffffb0e4; // Layout adjustment flag

/*
 * Insert special icon/glyph into dialogue display
 *
 * Reads an icon ID from the text stream and creates a glyph slot for rendering.
 * The icon is positioned at the current horizontal offset with proper vertical
 * alignment based on the current line index.
 *
 * Text stream format:
 *   [0x1D] [icon_id]
 *   - opcode 0x1D: Insert special icon
 *   - icon_id: Index into gp0xffffaed0 lookup table
 *
 * Side effects:
 * - Advances iGpffffaec0 by 2 bytes (opcode + icon_id)
 * - Increments iGpffffbcdc by 0x14 (20 pixels)
 * - May call FUN_00238f98 to wrap to next line
 * - Allocates one glyph slot from puGpffffaed4 array
 */
void text_op_insert_special_icon(void)
{
  int32_t y_position;
  int32_t *glyph_slot;
  int16_t uv_coords[2]; // [0]=U, [1]=V texture coordinates
  int32_t slot_index;

  // Get base pointer to glyph slot array
  glyph_slot = puGpffffaed4;

  // Read icon ID from text stream (byte at offset +1 after opcode)
  // Look up UV coordinates in icon table
  FUN_00231dd0(gp0xffffaed0[*(uint8_t *)(iGpffffaec0 + 1)], uv_coords);

  // Find first available glyph slot
  slot_index = 0;
  iGpffffaec0 = iGpffffaec0 + 2; // Advance text pointer past opcode and icon_id

  while (slot_index = slot_index + 1, *(char *)((int)glyph_slot + 0x3a) != '\0')
  {
    // Slot is active, move to next slot (stride = 0x3C = 60 bytes = 15 int32s)
    glyph_slot = glyph_slot + 0xf;

    if (299 < slot_index)
    {
      // No available slots (300 max slots)
      return;
    }
  }

  // Found available slot - initialize it
  *(uint8_t *)((int)glyph_slot + 0x3a) = 1; // Mark slot as active

  // Set timing baseline (horizontal offset in 20-pixel units)
  *(int16_t *)((int)glyph_slot + 0x36) = (int16_t)iGpffffbcdc;

  // Set line index (for vertical positioning)
  *(int16_t *)(glyph_slot + 0xd) = sGpffffbcd8;

  // Apply delay adjustment if conditions met
  if ((iGpffffbcd0 != 0) && (sGpffffbcd8 != 0))
  {
    *(int16_t *)((int)glyph_slot + 0x36) = (int16_t)iGpffffbcdc + 0x14;
  }

  // Calculate Y position
  // Formula: base_y + (line_index * -22) for downward lines
  y_position = iGpffffbccc + *(int16_t *)(glyph_slot + 0xd) * -0x16;

  // Calculate X position (base_x + horizontal_offset)
  glyph_slot[2] = iGpffffbcc8 + *(int16_t *)((int)glyph_slot + 0x36);
  glyph_slot[3] = y_position;

  // Apply layout-specific Y adjustments
  if (0 < iGpffffb0e4)
  {
    if (iGpffffbccc == 0xd0)
    {
      // Top-aligned layout adjustment
      y_position = y_position + -0x2d;
    }
    else
    {
      if (iGpffffbccc != -0x78)
        goto LAB_apply_sprite_params;
      // Bottom-aligned layout adjustment
      y_position = y_position + 0x1e;
    }
    glyph_slot[3] = y_position;
  }

LAB_apply_sprite_params:
  // Set sprite rendering parameters
  *glyph_slot = 0x82c;                                // Sprite type/render mode
  *(uint8_t *)((int)glyph_slot + 0x3b) = uGpffffaec4; // Layer/tag ID

  // Advance horizontal position for next character (20 pixels)
  iGpffffbcdc = iGpffffbcdc + 0x14;

  glyph_slot[0xc] = 0x80808080;          // Color (white/full brightness)
  glyph_slot[6] = (int32_t)uv_coords[0]; // U texture coordinate
  glyph_slot[7] = (int32_t)uv_coords[1]; // V texture coordinate
  glyph_slot[4] = 0x14;                  // Width (20 pixels)
  glyph_slot[5] = 0x16;                  // Height (22 pixels)
  glyph_slot[8] = 0x20;                  // Texture width (32 pixels)
  glyph_slot[9] = 0x20;                  // Texture height (32 pixels)

  // Check if horizontal position exceeds line limit
  if (iGpffffbcdc < iGpffffbce4)
  {
    return;
  }

  // Horizontal limit reached - flush/wrap to next line
  FUN_00238f98();
  return;
}
