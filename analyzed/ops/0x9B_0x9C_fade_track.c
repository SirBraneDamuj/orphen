/*
 * Opcodes 0x9B, 0x9C — fade track controls
 *
 * 0x9B FUN_00261c38 advance_fade_track:
 *   Reads one expression (track id) and calls FUN_0025d480(id) which steps
 *   the per-channel fade queue (paired fade stepper analogous to
 *   advance_fullscreen_fade_step_and_submit.c).
 *
 * 0x9C FUN_00261c60 configure_fade_slot:
 *   Reads one expression (slot id), then reads one mode byte (0..2) from
 *   the script stream (pbGpffffbd60). Values > 2 trip the diagnostic at
 *   FUN_0026bfc0(0x34d128). Calls FUN_0025d590(slot, mode) to bind a
 *   track/mode combination.
 */

extern void FUN_0025c258(void *);
extern void FUN_0025d480(unsigned int);
extern void FUN_0025d590(unsigned int, unsigned char);
extern void FUN_0026bfc0(int, ...);
extern unsigned char *pbGpffffbd60;

void op_0x9B_advance_fade_track(void)
{
  unsigned int id[4];
  FUN_0025c258(id);
  FUN_0025d480(id[0]);
}

void op_0x9C_configure_fade_slot(void)
{
  unsigned int id[4];
  FUN_0025c258(id);
  unsigned char mode = *pbGpffffbd60++;
  if (mode > 2)
    FUN_0026bfc0(0x34d128);
  FUN_0025d590(id[0], mode);
}
