/*
 * Entity Physics and Collision Processing System
 * Original function: FUN_002262c0
 * Address: 0x002262c0
 *
 * This is the core entity physics processing function that handles movement, collision detection,
 * height calculations, and position updates for game entities. It's called from the main entity
 * update loop for each active entity that requires physics processing.
 *
 * Key Responsibilities:
 * 1. Height/ground collision detection and response
 * 2. 3D position updates with physics calculations
 * 3. Movement direction computation using trigonometric functions
 * 4. Multi-directional collision testing (4-way collision detection)
 * 5. Special handling for the main player entity (0x58beb0)
 * 6. Velocity and acceleration processing
 * 7. Sound effect triggering for physics events
 *
 * Entity offsets, checked against the decompilation (2026-09-30):
 * - 0x04: attribute flags, copied to workspace +0x160. 0x100 = skip everything,
 *         0x08 = no gravity, 0x04 = may retry a refused move on a rotated
 *         heading, 0x20 = shorten a step-up by cos(slope), 0x400 = hold instead
 *         of stepping up, 0x02 = do not publish +0x84..+0x90
 * - 0x08: bit 0x20 = take part in the embedded-corner push-out
 * - 0x0A: ground primitive | (half << 14), -1 for none
 * - 0x0C: result flags, rebuilt every call from the workspace word +0x12C
 * - 0x20/0x24/0x28: position (x, ground-plane y, height)
 * - 0x2C: height eased up small steps at DAT_00352458 (0.04) a frame
 * - 0x30/0x34/0x38: this frame's movement request, zeroed on exit
 * - 0x44: vertical velocity; 0x48: gravity (0.00075 on the lead)
 * - 0x4C: ground height; 0x50: last call's 0x4C
 * - 0x54/0x58: radius / height
 * - 0x64: blocking entity slot (cleared to 0 every call)
 * - 0x68: entity being ridden (FUN_00228cf0), 0 for none
 * - 0x6C/0x70: settled surface words -- winning corner (ties ORed) / AND of all four
 * - 0x74/0x78: terrain reject mask / required mask
 * - 0x7C: 100.0 from FUN_00229c40 (every dump agrees); gates a move on the
 *         destination's corner spread, so in practice a corner over nothing
 * - 0x80: walkable slope limit in radians (0.8727 = 50 deg), NOT a step height
 * - 0x84-0x90: the four corner heights of the last settled footprint
 *
 * Result flags (+0x0C):
 * - 0x0001: landed on +0x4C this call (or, gravity off, standing exactly on it)
 * - 0x0002: horizontal move refused (or stepping up, provisionally)
 * - 0x0004: vertical collision -- landed, or a rise given back
 * - 0x0008: rose this call; 0x0010: fell this call
 * - 0x0020 / 0x0040: an entity clamp narrowed X / Y (FUN_00228380.. / FUN_00228838..)
 * - 0x0100: riding an entity; 0x0200: required mask failed
 * - 0x0400: water entry; 0x0800: landing dust spawned
 * - 0x4000: the retry fan ran; 0x8000: refused by the +0x7C spread gate
 * - 0x10000: held (LAB_00226988) -- no move, +0x38 = 0.02; read back next call
 * - 0x20000: +0x2C is easing up a step
 *
 * The move decision (0x00226884..0x00226cb4), per destination:
 *
 *   lVar7 = FUN_00227390(dest);   // 1 only if w[5] (highest corner) <= +0x28
 *   if (+0x4C - w[6] > +0x7C || +0x7C < w[5] - w[6])   refuse, |= 0x8002
 *   else if (lVar7)                                     accept, +0x4C = w[5]
 *   else if (+0x0C & 0x10000)                           hold
 *   else { |= 2;
 *     if (+0x28 == +0x50 && w[5] - +0x28 < 0.26 && w[2] <= +0x80)
 *       raise +0x28 to w[5] + 0.063, re-query (step * cos(w[2]) if +0x04 & 0x20),
 *       accept if it answers 1 and is still under 0.26 above the old feet
 *   }
 *
 * So a surface above the feet is reachable only by the 0.26 step, and only
 * while settled: an actor in mid-jump is refused until its feet clear the
 * destination. A refusal with +0x04 & 4 retries at heading x0.3, +20 deg x0.7,
 * -20 deg, +60 deg x0.5, -60 deg; the entity clamps re-run on every pass.
 * Gravity runs every call, grounded or not; the landing is the plain
 * `+0x28 + +0x38 <= +0x4C`, and a grounded step down of less than 0.125 is
 * folded into +0x38 so slopes are followed without leaving the ground.
 */

#include "orphen_globals.h"
#include <math.h>

// External function declarations
extern float FUN_00227070(float x, float z, void *entity);                   // Distance/height calculation
extern long FUN_00227390(float x, float z, void *entity, void *stack_frame); // Ground collision detection
extern float FUN_00305130(float angle);                                      // Cosine function
extern float FUN_00305218(float angle);                                      // Sine function
extern int FUN_00305408(float y, float x);                                   // Arctangent function (atan2)
extern float FUN_00216608(float x, float y);                                 // Distance/magnitude calculation
extern void FUN_00228380(void *stack_frame);                                 // Positive X movement handler
extern void FUN_002285d8(void *stack_frame);                                 // Negative X movement handler
extern void FUN_00228838(void *stack_frame);                                 // Positive Z movement handler
extern void FUN_00228a90(void *stack_frame);                                 // Negative Z movement handler
extern short FUN_0030bd20(float value);                                      // Float to fixed-point conversion
extern void FUN_0021ed50(float r, float g, float b, float size, float height, float intensity,
                         float x, float z, int duration, int fade, int type1, int type2, int color); // Particle effect
extern void FUN_00219af0(float x, float z, float y, float param4, float radius, int param6,
                         float param7, int duration, int param9, int param10, int param11, int is_special); // Sound effect
extern void FUN_002d4108(float x, float z, float radius);                                                   // Special effect (screen shake?)

// Global variables
extern char g_debug_physics_enabled;  // DAT_003555d1
extern char g_physics_paused;         // DAT_003555d0
extern int g_frame_counter;           // DAT_003555b4
extern float g_water_level;           // DAT_003556fc
extern float g_physics_constants[20]; // DAT_00352424 onwards - various physics constants

// Entity array and data
extern void *entity_array_base;  // DAT_0058beb0 - Base of entity array
extern void *surface_data_base;  // DAT_003556b0 - Surface/ground data array
extern void *movement_data_base; // DAT_003556e0 - Movement pattern data

/*
 * Process Entity Physics and Collision
 *
 * Main physics processing function for a single entity. Handles all aspects of
 * entity movement, collision detection, height calculations, and position updates.
 * Uses sophisticated multi-directional collision testing and smooth movement.
 *
 * @param entity_ptr Pointer to the entity structure to process
 * @param stack_frame Pointer to temporary calculation space (from scratchpad)
 */
void process_entity_physics_and_collision(void *entity_ptr, void *stack_frame)
{
  /*
   * This function is extremely complex (1273 lines) and handles comprehensive
   * entity physics including:
   *
   * 1. Ground/surface collision detection and height calculation
   * 2. Multi-directional movement collision testing (4-way)
   * 3. Trigonometric movement calculations using cos/sin
   * 4. Gravity and drag physics simulation
   * 5. Special handling for main player entity (0x58beb0)
   * 6. Complex collision response with circular movement testing
   * 7. Physics state flag management
   * 8. Sound effects and particle systems for physics events
   * 9. Water level detection and splash effects
   * 10. Smooth movement interpolation and step detection
   *
   * Key entity offsets identified:
   * - 0x04: Status flags (0x100=skip, 0x800=disabled)
   * - 0x20/0x24/0x28: X/Z/Y positions
   * - 0x30/0x34/0x38: this frame's X/Z/Y movement request
   * - 0x44: vertical velocity, 0x48: gravity
   * - 0x4C: Ground height, 0x5C: facing
   * - 0x68: entity being ridden, 0x6C: settled surface word
   *
   * Stack frame workspace offsets:
   * - 0x4D/0x4E: Test positions, 0x50/0x51: Movement deltas
   * - 0x162: Collision direction flags, 0x163: Movement attempts
   *
   * The function uses PS2 scratchpad memory for workspace and performs
   * sophisticated physics calculations including trigonometric functions
   * for movement direction computation and collision response.
   */

  // Basic implementation structure - full function is too complex for complete analysis
  int entity_data = (int)entity_ptr;
  int *workspace = (int *)stack_frame;

  // Copy entity status flags to workspace
  short entity_flags = *(short *)(entity_data + 0x04);
  *(short *)((char *)workspace + 0x58 * 4) = entity_flags;

  // Skip processing if entity has skip flag
  if ((entity_flags & 0x100) != 0)
  {
    return;
  }

  // Store entity pointer in workspace
  workspace[0x4A] = entity_data;
  workspace[0x4B] = 0;              // Clear physics flags
  *(int *)(entity_data + 0x64) = 0; // Clear entity movement flags

  // The function continues with complex physics processing including:
  // - Surface behavior mode detection
  // - Ground height calculations
  // - Multi-directional collision testing
  // - Trigonometric movement calculations
  // - Physics state updates
  // - Sound and visual effect triggers

  // Key external function calls:
  // - FUN_00227070: Distance/height calculation
  // - FUN_00227390: Ground collision detection (called multiple times)
  // - FUN_00305130/FUN_00305218: Cosine/sine for movement direction
  // - FUN_00305408: Arctangent for angle calculation
  // - FUN_0021ed50: Particle effects (water splashes, etc.)
  // - FUN_00219af0: Sound effects for physics events

  return;
}
