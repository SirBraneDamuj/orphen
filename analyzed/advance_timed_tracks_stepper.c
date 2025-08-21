// Timed tracks stepper (analyzed)
// Original: FUN_002446e8
// Called from the main/update loop: FUN_0023fd30 (early each frame)
//
// Summary:
// - Steps all active timed tracks in the global list (root DAT_00354fa8):
//   - current += (DAT_003555bc & 0xFFFF); clamp to total.
//   - If reaching total, clear auxiliary fields and mark track complete (state=0).
//   - For in-progress tracks, evaluate the keyframe curve via FUN_00266ce8 at t = current/total and
//     update the bound object’s deltas/positions; optionally snap to value depending on flags.
//   - Smooth/approach orientation using atan2 wrapper FUN_00305408 and approach/lerp FUN_0023a320 with
//     step scaled by DAT_0035271c and DAT_003555bc.
//   - Optionally trigger FUN_00267d38 based on bitfields.
//
// Data model (per-track, base = *(u8*)(DAT_00354fa8+0x58), stride 0x2D8):
// - +0x00: u16 state (0 → inactive/complete)
// - +0x02: u16 keyCount (used for index clamp)
// - +0x04: u16 total
// - +0x06: u16 current
// - +0x08: u16 flags (bit0: snap-to-keyframe; bit1: angle update mode gate)
// - +0x09: u8  phaseParam (affects extra angular offset when < 0xB5)
// - +0x0B: u8  eventBitIndex (0..31) for trigger mask test
// - +0x0C: u16 eventId (passed to FUN_00267d38)
// - +0x10: void* boundObject (entity/pool entry)
// - +0xD4: curve bank header for FUN_00266ce8 (track+0x6A shorts)
//
// Object side (boundObject points into a pool entry; offsets are in 2-byte steps):
// - +0x10..: float posX, +0x12: posY, +0x14: posZ
// - +0x18..: float dX,  +0x1A: dY,  +0x1C: dZ
// - +0x2E: float angle
// - +0x03: u16 flags word; bits used in trigger conditions
// - +0x55: u16 trigger bitfield tested by eventBitIndex
//
// Key routines involved:
// - FUN_00266ce8(t, bank, out3): sample 3-component curve at normalized t
// - FUN_00305408(dy, dx): atan2f-like angle
// - FUN_0023a320(currentAngle, targetAngle, step): approach/lerp with clamp (returns delta step)
// - DAT_003555bc: global tick/delta (used here masked to 16 bits)
// - DAT_0035271c: global scale used in angle step computation
//
// Notes:
// - If the bound object’s slot flags have bit 2 set (mask 0x4), progression is paused and aux fields cleared.
// - If track flags bit0 set, the code snaps positions to the sampled value and zeros deltas before continuing.
// - When (current == total), clears two aux shorts at +0x10 and +0x12 (puVar7[8], puVar7[9]) and sets state=0.

#include <stdint.h>

// Globals (extern names preserved)
extern int DAT_00354fa8;          // track system root (0 if off)
extern unsigned int DAT_003555bc; // global tick/delta
extern float DAT_0035271c;        // angle step scale
extern uint8_t DAT_0058bf46;      // global slot flags for base pool
extern int16_t DAT_0058beb0;      // base pool sentinel

// Unanalyzed helpers (extern)
extern void FUN_00266ce8(float t, int16_t *bank, float *outXYZ);
extern float FUN_00305408(float dy, float dx);
extern float FUN_0023a320(float current, float target, float step);
extern void FUN_00267d38(uint16_t eventId, int16_t *obj);

// Original signature: void FUN_002446e8(void)
void advance_timed_tracks_stepper(void)
{
  if (DAT_00354fa8 == 0)
    return;

  int trackCount = *(int *)(DAT_00354fa8 + 0x54);
  if (trackCount == 0)
    return;

  uint8_t angleScaleByte = 0; // only used to preserve structure; real value comes from track byte
  const float angleScale = DAT_0035271c;
  uint8_t idxByte = 0;

  uint8_t *tracksBase = (uint8_t *)(*(int *)(DAT_00354fa8 + 0x58));
  for (unsigned int i = 0; i < (unsigned int)trackCount; i++)
  {
    uint8_t *track = tracksBase + i * 0x2D8;

    uint16_t *t16 = (uint16_t *)track;
    uint16_t state = t16[0];
    if (state == 0)
      continue;

    // Bound object pointer is at +0x10
    int16_t *obj = *(int16_t **)(track + 0x10);
    if (obj == 0)
      continue;

    // If object is the base pool sentinel and inactive, skip this track (matches raw short-circuit)
    uint8_t slotFlags;
    if (*obj == 0)
    {
      slotFlags = DAT_0058bf46;
      if (obj != &DAT_0058beb0)
      {
        // skip stepping but still advance loop
        continue;
      }
    }
    else
    {
      // Per-object flags byte at +0x96 (short* + 0x4B)
      slotFlags = *(uint8_t *)(obj + 0x4B);
    }

    // If paused (bit 2), clear aux and continue
    if ((slotFlags & 0x04) != 0)
    {
      t16[8] = 0; // aux
      t16[9] = 0; // aux
      continue;
    }

    uint16_t total = t16[2];
    uint16_t current = t16[3];
    if (current == total)
    {
      t16[8] = 0;
      t16[9] = 0;
      t16[0] = 0; // complete
      continue;
    }

    // Advance current by tick (masked to 16 bits), clamp to total
    unsigned int stepQ = (current + (DAT_003555bc & 0xFFFFu));
    t16[3] = (uint16_t)stepQ;
    if (total < (stepQ & 0xFFFFu))
    {
      t16[3] = total;
    }

    // Sample curve at normalized t
    float outX, outY, outZ;
    FUN_00266ce8((float)t16[3] / (float)total, (int16_t *)(t16 + 0x6A), &outX);

    // Update object deltas relative to current position
    float *objF = (float *)obj;
    float curX = *(float *)(obj + 0x10);
    float curY = *(float *)(obj + 0x12);
    float curZ = *(float *)(obj + 0x14);
    *(float *)(obj + 0x18) = outX - curX;
    *(float *)(obj + 0x1A) = outY - curY;
    // If object flag bit 3 (mask 0x8) set in short at +0x04? Raw checks (psVar4[2] & 8)
    if ((obj[2] & 0x0008) != 0)
    {
      *(float *)(obj + 0x1C) = outZ - curZ;
    }

    // Track flags: bit0 = snap positions immediately
    uint16_t trackFlags = t16[4];
    uint8_t phaseParam = *(uint8_t *)(track + 0x09);
    if ((trackFlags & 0x0001) != 0)
    {
      // Zero deltas; snap current/related positions to sampled values
      *(float *)(obj + 0x1C) = 0.0f;
      *(float *)(obj + 0x1A) = 0.0f;
      *(float *)(obj + 0x18) = 0.0f;
      *(float *)(obj + 0x10) = outX;
      *(float *)(obj + 0x12) = outY;
      *(float *)(obj + 0x26) = outZ;
      *(float *)(obj + 0x14) = outZ;
      *(float *)(obj + 0x28) = outZ;
    }

    // Choose dx/dy for angle target
    float dx, dy;
    if (phaseParam < 0xB5)
    {
      dy = *(float *)(obj + 0x1A);
      dx = *(float *)(obj + 0x18);
    }
    else
    {
      int idx = (int)phaseParam - 0xB5;
      int keyCount = (int)t16[1] - 1;
      if (idx > keyCount)
        idx = keyCount;
      // Pull keyframe component pair relative to current pos
      dx = *(float *)(t16 + 10 + idx * 6) - curX;
      dy = *(float *)(t16 + 12 + idx * 6) - curY;
    }

    float targetAngle = FUN_00305408(dy, dx);
    float angleStep = FUN_0023a320(*(float *)(obj + 0x2E), targetAngle,
                                   ((float)((int)t16[0] * (int)DAT_003555bc) * angleScale) / 360.0f);

    // If angle mode (bit1) is not set, apply the step and possibly add extra phase offset
    if ((trackFlags & 0x0002) == 0)
    {
      if (angleStep == 0.0f)
      {
        *(float *)(obj + 0x2E) = targetAngle;
      }
      else
      {
        *(float *)(obj + 0x2E) = *(float *)(obj + 0x2E) + angleStep;
      }
      if (phaseParam < 0xB5)
      {
        *(float *)(obj + 0x2E) += ((float)((unsigned int)phaseParam << 1) * angleScale) / 360.0f;
      }
    }

    // Optional event trigger
    uint16_t eventId = t16[6];
    if (eventId != 0)
    {
      // Object flags in obj[3]; require bit2 set and bit4 clear, and a bit test in a 16-bit mask at +0x55
      if (((obj[3] & 0x0010) == 0) && ((obj[3] & 0x0004) != 0))
      {
        uint8_t bitIndex = *(uint8_t *)(track + 0x0B) & 0x1F;
        uint16_t mask = *(uint16_t *)(obj + 0x55);
        if (((int)mask >> bitIndex) & 1)
        {
          FUN_00267d38(eventId, obj);
        }
      }
    }
  }
}
