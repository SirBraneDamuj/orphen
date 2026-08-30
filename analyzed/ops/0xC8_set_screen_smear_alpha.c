/*
 * Opcode 0xC8 — set_screen_smear_alpha
 *
 * Original: FUN_00264448 (0x00264448)
 *
 * Reads one expression and stores its low byte into DAT_00355661, the *target*
 * alpha of the full-screen frame-feedback blit — the previous frame drawn back
 * over the current one. See analyzed/frame_feedback_blit.c for FUN_00201a38,
 * which is what consumes it, and 0xC9 for the variant that also sets the
 * transform.
 *
 * NOT a text or palette opcode. This file previously carried the name
 * `set_text_color_index` and described DAT_00355661 as "the active text colour
 * palette index used by the dialogue/menu renderer". Nothing in the dialogue or
 * menu renderer reads DAT_00355661; its only reader is FUN_002000c0:214, which
 * uses it to decide whether to call FUN_00201a38 at all, and FUN_00201a38
 * itself, which uses it as the blend alpha.
 *
 * DAT_00355661 is `bGpffffb6f1` (gp - 0x490F, gp = 0x00359F70). Ghidra renders
 * it under four names across the decompilation — DAT_00355661, uGpffffb6f1,
 * bGpffffb6f1 and cGpffffb6f1 — which is part of why the connection was easy
 * to miss.
 *
 * Alpha is a GS blend factor over 128, not 255. 0x80 replaces the frame
 * entirely with its predecessor; the 0x7E the ship scenes use is 98%, which
 * compounds into a trail lasting a second or more.
 *
 * How scripts drive it: SCR2 (s01_e012 and the other ship scenes) snaps it to
 * 0x7E once, then re-issues 0xC8 every frame with the current value of an
 * 0x90/0x91/0x92 parameter ramp decaying to zero, and finally writes 0 to
 * clear. The ramp *is* the fade-out — FUN_00201a38's own ramp on DAT_00354b88
 * never engages for these callers, because that global stays zero unless
 * opcode 0xBE's table entry 12 seeds it.
 *
 * Other writers of DAT_00355661 (so the same effect, driven from native code):
 * FUN_0023c340 (battle/spell impacts, paired with a zoom), FUN_002340e0 (the
 * battle transition), and a long tail of enemy and effect modules.
 */

extern unsigned char DAT_00355661; /* bGpffffb6f1 — frame-feedback target alpha */
extern void bytecode_interpreter(void *out); /* orig FUN_0025c258 */

unsigned int opcode_0xC8_set_screen_smear_alpha(void)
{
  unsigned char alpha[16];

  bytecode_interpreter(alpha);
  DAT_00355661 = alpha[0];
  return 0;
}
