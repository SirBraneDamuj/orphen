/*
 * Opcodes 0x5E & 0x5F - Calculate Polar Component (Cosine/Sine)
 * Original function: FUN_0025f380
 *
 * These opcodes evaluate two expressions (magnitude and angle), convert the angle
 * to fixed-point, compute either cosine (0x5E) or sine (0x5F), multiply by the
 * magnitude and a scale factor, then submit the result.
 *
 * Unlike 0x5D which adds velocity to entity position, these opcodes just compute
 * and submit a single polar coordinate component without modifying any entity state.
 *
 * BEHAVIOR:
 * 1. Evaluate two expressions: magnitude and angle
 * 2. Normalize: mag_norm = magnitude / DAT_00352bcc (~100.0)
 *               angle_norm = angle / DAT_00352bcc
 * 3. Convert angle to fixed-point: angle_fixed = FUN_00216690(angle_norm)
 * 4. If opcode == 0x5E: component = mag_norm * cos(angle_fixed)
 *    If opcode == 0x5F: component = mag_norm * sin(angle_fixed)
 * 5. Scale result by DAT_00352bd0
 * 6. Submit via FUN_0030bd20(component * scale)
 *
 * PARAMETERS (inline):
 * - magnitude (int expression) - The polar radius/magnitude value
 * - angle (int expression) - The polar angle value
 *
 * RETURN VALUE:
 * None (void). Result is submitted via FUN_0030bd20.
 *
 * GLOBAL READS:
 * - DAT_00355cd8: Current opcode (0x5E or 0x5F) to select cos vs sin
 * - DAT_00352bcc: Normalization divisor for both magnitude and angle (~100.0)
 * - DAT_00352bd0: Output scale factor
 *
 * CALL GRAPH:
 * - FUN_0025c258: Expression evaluator (called twice for magnitude & angle)
 * - FUN_00216690: Convert float angle to fixed-point format
 * - FUN_00305130: Cosine function (fixed-point → float) [0x5E only]
 * - FUN_00305218: Sine function (fixed-point → float) [0x5F only]
 * - FUN_0030bd20: Submit result value to expression stack/output
 *
 * USE CASES:
 * - Calculate X component of polar vector (0x5E for cosine)
 * - Calculate Z component of polar vector (0x5F for sine)
 * - Project values onto axes for camera/graphics calculations
 * - Compute direction vectors without modifying entity state
 * - Used when you need individual components rather than combined velocity (cf. 0x5D)
 *
 * COMPARISON WITH 0x5D:
 * - 0x5D: Calculates BOTH cos and sin, adds to entity position (stateful, 3 params)
 * - 0x5E/0x5F: Calculate ONE component, submit result (stateless, 2 params)
 * - 0x5E/0x5F useful when script needs individual axis values for further calculations
 *
 * TYPICAL SCRIPT SEQUENCES:
 *
 * Example 1: Calculate both components separately
 *   push magnitude
 *   push angle
 *   0x5E              # x = magnitude * cos(angle)
 *   store x
 *   push magnitude
 *   push angle
 *   0x5F              # z = magnitude * sin(angle)
 *   store z
 *
 * Example 2: Calculate single directional component
 *   push distance
 *   push facing_angle
 *   0x5E              # forward_x = distance * cos(facing)
 *   add to camera_x
 *
 * NOTES:
 * - Both opcodes share the same handler (FUN_0025f380)
 * - Opcode selection (cos vs sin) determined by DAT_00355cd8 value
 * - No frame-rate compensation (unlike 0x5D which uses DAT_003555bc)
 * - Result is submitted, not directly stored - script must handle return value
 */

extern short DAT_00355cd8; // Current opcode being executed
extern float DAT_00352bcc; // Normalization divisor (~100.0 for mag & angle)
extern float DAT_00352bd0; // Output scale factor

extern void FUN_0025c258(int *out_result);  // Expression evaluator
extern int FUN_00216690(float angle);       // Float → fixed-point angle
extern float FUN_00305130(int angle_fixed); // Cosine (fixed → float)
extern float FUN_00305218(int angle_fixed); // Sine (fixed → float)
extern void FUN_0030bd20(float value);      // Submit result value

void calculate_polar_component(void) // orig: FUN_0025f380
{
  short current_opcode;
  int magnitude_raw;
  int angle_raw;
  float magnitude_norm;
  float angle_norm;
  int angle_fixed;
  float trig_component;
  float result;

  // Save current opcode to determine cos (0x5E) vs sin (0x5F)
  current_opcode = DAT_00355cd8;

  // Evaluate two expressions from bytecode stream
  FUN_0025c258(&magnitude_raw); // First parameter: magnitude/radius
  FUN_0025c258(&angle_raw);     // Second parameter: angle

  // Normalize magnitude and angle by divisor
  magnitude_norm = (float)magnitude_raw / DAT_00352bcc;
  angle_norm = (float)angle_raw / DAT_00352bcc;

  // Convert normalized angle to fixed-point format for trig functions
  angle_fixed = FUN_00216690(angle_norm);

  // Select trigonometric function based on opcode
  if (current_opcode == 0x5E)
  {
    // Opcode 0x5E: Calculate cosine component (X-axis in typical XZ plane)
    trig_component = (float)FUN_00305130(angle_fixed);
  }
  else
  { // current_opcode == 0x5F
    // Opcode 0x5F: Calculate sine component (Z-axis in typical XZ plane)
    trig_component = (float)FUN_00305218(angle_fixed);
  }

  // Calculate: result = magnitude * trig(angle) * scale_factor
  result = magnitude_norm * trig_component * DAT_00352bd0;

  // Submit result to expression stack/output
  FUN_0030bd20(result);

  return;
}
