/*
 * a01_pcm_stream_script_gate
 * Original: FUN_00267650
 *
 * Script-facing gate for the standalone A01.MV3 PCM stream. This function is
 * entry 5 in the opcode 0xBE function table at PTR_FUN_0031e730. It does not
 * use the MV30 movie parser; instead it opens \MV3\A01.MV3;1 as a raw PCM-like
 * CD stream and delegates playback to the separate PCM stream state machine.
 *
 * Command behavior inferred from the branch structure:
 *   command >= 10: open/cache the A01 file info unless scene flag bit 0x40 is set
 *   command == 1: start playback with volume parameter 0x50
 *   command == 2: synchronously stop playback
 *   otherwise: pump the PCM stream state machine
 *
 * A01.MV3 real-file validation:
 *   - size: 0x00f60000 bytes, exactly 246 chunks of 0x10000 bytes
 *   - no MV30 header; streaming begins at file byte 0
 *   - signed 16-bit little-endian stereo at 48000 Hz
 *   - same 0x200-byte alternating left/right channel stripes as Mxx movie audio
 */

typedef unsigned int u32;
typedef unsigned long long u64;

extern u32 scene_work_flags_003555d8;       /* Original: DAT_003555d8 */
extern u64 a01_pcm_file_info_00355d28;      /* Original: DAT_00355d28 */
extern const char a01_mv3_disc_path_0034d4d8[]; /* Original: s_\MV3\A01.MV3;1_0034d4d8 */

extern u64 FUN_00222f70(const char *disc_path);
extern void FUN_002f2ca0(u64 file_info, int volume_param);
extern void FUN_002f2d88(void);
extern u32 FUN_002f2ef0(void);

/* Original signature: undefined8 FUN_00267650(long param_1) */
u64 a01_pcm_stream_script_gate(long command)
{
    if (command < 10)
    {
        if (command == 1)
        {
            FUN_002f2ca0(a01_pcm_file_info_00355d28, 0x50);
        }
        else
        {
            if (command != 2)
            {
                return FUN_002f2ef0();
            }
            FUN_002f2d88();
        }
    }
    else if ((scene_work_flags_003555d8 & 0x40) == 0)
    {
        a01_pcm_file_info_00355d28 = FUN_00222f70(a01_mv3_disc_path_0034d4d8);
    }

    return 0;
}