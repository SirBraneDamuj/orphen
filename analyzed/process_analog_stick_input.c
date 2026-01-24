/*
 * Process Analog Stick Input - FUN_0023b3f0
 *
 * Converts raw PS2 controller analog stick input (0-255 range) into
 * magnitude and angle values suitable for game use. Implements deadzone
 * filtering and proper analog stick coordinate transformation.
 *
 * PS2 analog sticks report values in 0-255 range where 0x80 (128) is center.
 * This function:
 * 1. Centers the input by subtracting 0x80 to get -128 to +127 range
 * 2. Inverts Y-axis (common in game controllers)
 * 3. Calculates radial distance from center
 * 4. Applies circular deadzone of 60 units (out of 128)
 * 5. Scales active range (60-128) to normalized output (0-128)
 * 6. Calculates angle using atan2
 *
 * The deadzone prevents stick drift and requires deliberate movement.
 * Scaling formula: ((distance - 60) * 128) / 68
 * This maps the 68-unit active range (128 - 60) to full 0-128 output.
 *
 * Original function: FUN_0023b3f0
 * Address: 0x0023b3f0
 */

#include <stdint.h>

// Forward declarations
extern float SQRT(float value);           // Square root function
extern uint32_t atan2f(float y, float x); // FUN_00305408 - arctangent for angle calculation

/*
 * Process raw analog stick input into magnitude and angle
 *
 * Parameters:
 *   x_input - Raw X-axis input from controller (0-255, center at 128)
 *   y_input - Raw Y-axis input from controller (0-255, center at 128)
 *   magnitude_out - Output: Scaled magnitude/distance from center (0.0-128.0)
 *   angle_out - Output: Angle in radians from positive X-axis
 *
 * The function applies a circular deadzone with radius 60. Any input within
 * this deadzone results in zero magnitude. Outside the deadzone, magnitude
 * is scaled so that 60 units maps to 0 and 128 units maps to 128.
 *
 * Y-axis is inverted (0x80 - input) to convert from hardware convention
 * (larger values = stick down) to mathematical convention (larger values = up).
 */
void process_analog_stick_input(uint32_t x_input, uint32_t y_input,
                                float *magnitude_out, uint32_t *angle_out)
{
  int x_centered;
  int y_centered;
  float distance;
  uint32_t angle;

  // Center the X-axis input: convert 0-255 range to -128 to +127
  // Raw input of 0x00 becomes -128, 0x80 becomes 0, 0xFF becomes +127
  x_centered = (x_input & 0xff) - 0x80;

  // Center and invert Y-axis input
  // Inversion converts hardware Y-down convention to mathematical Y-up
  // Raw input of 0x00 becomes +128 (up), 0x80 becomes 0, 0xFF becomes -127 (down)
  y_centered = 0x80 - (y_input & 0xff);

  // Calculate radial distance from center using Pythagorean theorem
  distance = SQRT((float)(x_centered * x_centered + y_centered * y_centered));

  // Apply circular deadzone with radius of 60 units
  if (distance < 60.0)
  {
    // Within deadzone - treat as no input
    *magnitude_out = 0.0;
    *angle_out = 0;
  }
  else
  {
    // Outside deadzone - scale magnitude to 0-128 range
    // Formula: ((distance - deadzone) * max_output) / active_range
    // Where: deadzone = 60, max_output = 128, active_range = 68 (128 - 60)
    distance = ((distance - 60.0) * 128.0) / 68.0;

    // Clamp to maximum value of 128.0
    if (128.0 < distance)
    {
      distance = 128.0;
    }

    *magnitude_out = distance;

    // Calculate angle using atan2(y, x)
    // Returns angle in radians from positive X-axis, counter-clockwise
    // Range: -π to +π
    angle = atan2f((float)y_centered, (float)x_centered);
    *angle_out = angle;
  }

  return;
}
