/*
 * Calculate Analog Direction Flags - FUN_0023b4e8
 *
 * Converts analog stick magnitude and angle into digital directional flags.
 * This function takes analog input (magnitude + angle) and generates PS2
 * controller-style direction bits, simulating the D-pad based on analog position.
 *
 * The function divides the analog stick range into 8 cardinal/diagonal directions
 * based on angle thresholds. It only activates when magnitude exceeds 100 units,
 * providing a deadzone for analog-to-digital conversion.
 *
 * Direction mapping (approximate angles):
 * - 0x1000: Up (angle near 90°)
 * - 0x2000: Up-Right diagonal (~45°)
 * - 0x4000: Right (angle near 0°)
 * - 0x6000: Down-Right diagonal (~315°/-45°)
 * - 0x8000: Down (angle near 270°/-90°)
 * - 0x9000: Down-Left diagonal (~225°/-135°)
 * - 0xC000: Left (angle near 180°/-180°)
 * - 0x3000: Up-Left diagonal (~135°)
 *
 * These flags match PS2 D-pad bit patterns:
 * - 0x1000 = D-pad Up
 * - 0x2000 = D-pad Right
 * - 0x4000 = D-pad Down
 * - 0x8000 = D-pad Left
 * Diagonal directions combine adjacent flags (e.g., 0x9000 = Down + Left).
 *
 * Original function: FUN_0023b4e8
 * Address: 0x0023b4e8
 */

#include <stdint.h>

// Angle threshold constants (addresses from globals.json)
extern float DAT_00352624; // 2π wraparound constant for negative angles
extern float DAT_00352628; // Upper boundary for left/down-left quadrants
extern float DAT_0035262c; // Boundary between up-left and up-right
extern float DAT_00352630; // Lower threshold for up-right diagonal
extern float DAT_00352634; // Lower threshold for pure up direction
extern float DAT_00352638; // Lower threshold for down-left diagonal
extern float DAT_0035263c; // Upper boundary for right/down-right quadrants
extern float DAT_00352640; // Lower threshold for pure right direction
extern float DAT_00352644; // Lower threshold for down-right diagonal

/*
 * Convert analog stick input to directional flags
 *
 * Parameters:
 *   magnitude - Distance from center (from process_analog_stick_input)
 *   angle - Angle in radians from positive X-axis, range -π to +π
 *
 * Returns:
 *   uint16_t - Direction flags (PS2 D-pad bit patterns):
 *              0x0000 if magnitude < 100 (insufficient input)
 *              0x1000 = Up
 *              0x2000 = Up-Right
 *              0x4000 = Right
 *              0x6000 = Down-Right
 *              0x8000 = Down
 *              0x9000 = Down-Left
 *              0xC000 = Left
 *              0x3000 = Up-Left
 *
 * The magnitude threshold of 100 units ensures only deliberate analog input
 * generates directional flags, preventing noise/drift from triggering actions.
 *
 * Angle ranges are divided into 8 sectors with threshold constants that
 * define boundaries between cardinal and diagonal directions.
 */
uint16_t calculate_analog_direction_flags(float magnitude, float angle)
{
  uint16_t direction_flags;

  // Initialize to no direction
  direction_flags = 0;

  // Only process if magnitude exceeds threshold (100 units)
  // This provides hysteresis/deadzone for analog-to-digital conversion
  if (100.0 < magnitude)
  {
    // Normalize negative angles by adding 2π
    // Converts -π to +π range into 0 to 2π range for simpler comparisons
    if (angle < 0.0)
    {
      angle = angle + DAT_00352624; // Add 2π (approximately 6.28318...)
    }

    // Determine direction based on angle sectors
    // The following nested conditionals divide the circle into 8 sectors

    if (angle < DAT_00352628)
    {
      // Left half or lower quadrants (angles roughly 0 to π)

      if (angle < DAT_0035262c)
      {
        // Upper-left or lower sectors
        direction_flags = 0x2000; // Default: Up-Right diagonal

        if ((DAT_00352630 <= angle) && (direction_flags = 0x3000, DAT_00352634 <= angle))
        {
          // Pure up direction (angle near π/2 or 90°)
          return 0x1000;
        }
        // Otherwise returns 0x2000 (Up-Right) or 0x3000 (Up-Left)
      }
      else
      {
        // Down-left sector
        direction_flags = 0x9000; // Default: Down-Left diagonal

        if (DAT_00352638 <= angle)
        {
          // Pure down direction (angle near 3π/2 or 270°/-90°)
          return 0x8000;
        }
        // Otherwise returns 0x9000 (Down-Left)
      }
    }
    else if (angle < DAT_0035263c)
    {
      // Left side (angles near π or 180°/-180°)
      direction_flags = 0xC000; // Default: Left direction

      if (DAT_00352640 <= angle)
      {
        // Pure right direction (angle near 0° or 2π)
        return 0x4000;
      }
      // Otherwise returns 0xC000 (Left)
    }
    else
    {
      // Right-side diagonals or wraparound
      direction_flags = 0x2000; // Default: Up-Right diagonal

      if (angle < DAT_00352644)
      {
        // Down-right diagonal (angle near 315° or -45°)
        return 0x6000;
      }
      // Otherwise returns 0x2000 (Up-Right at wraparound boundary)
    }
  }

  return direction_flags;
}
