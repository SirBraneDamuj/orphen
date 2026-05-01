/*
 * mv3_playback_session
 * Original: FUN_002f1f38
 *
 * Runs one complete MV3 movie session for a selected disc path. The function resets
 * display/audio state, opens and validates the MV3 stream, initializes SCE MPEG/IPU
 * work memory, installs an interrupt handler, runs the decode/render loop, then
 * tears everything back down.
 */

typedef unsigned long long u64;

extern int DAT_00314d90;
extern int DAT_00314e80;
extern unsigned int uGpffffbec0;

extern void FlushCache(int mode);
extern void FUN_002f9308(int, int);
extern void FUN_002f9230(unsigned int, int);
extern void FUN_00222fc8(void);
extern int mv3_open_stream_and_read_header(const char *disc_path); /* FUN_002f1a70 */
extern void FUN_00206840(void);
extern void FUN_00203aa0(int mode);
extern void FUN_002f26d8(u64 clear_color);
extern void FUN_002f8990(void);
extern void FUN_002f8880(short, unsigned short, unsigned short, unsigned short);
extern void FUN_002fc760(void);
extern unsigned long long FUN_002fc7b0(void *mpeg_context, unsigned int work_base, int work_size);
extern unsigned long long AddIntcHandler(int cause, unsigned int handler, long next);
extern void mv3_main_decode_loop(void *mpeg_context); /* FUN_002f2198 */
extern void RemoveIntcHandler(int cause, unsigned long long handler_id);
extern void FUN_002fc9b0(void *mpeg_context);
extern void FUN_002fc420(void);

void mv3_playback_session(const char *disc_path) {
    int opened_size;
    unsigned long long handler_id;
    unsigned char mpeg_context[80];

    DAT_00314d90 = 0;
    DAT_00314e80 = 0;
    FlushCache(0);

    FUN_002f9308(0, 0);
    FUN_002f9230(0x314c90, 0);
    FUN_002f9308(0, 0);
    FUN_002f9230(0x314c90, 1);
    FUN_002f9308(0, 0);
    FUN_00222fc8();

    opened_size = mv3_open_stream_and_read_header(disc_path);
    if (opened_size == 0) {
        FUN_00206840();
        return;
    }

    FUN_00203aa0(0);
    FUN_002f26d8(0xffffffff80000000ULL);
    FUN_00203aa0(0);
    FUN_002f8990();
    FUN_00203aa0(0);
    FUN_002f8880(0, 1, 2, 0);
    FUN_002f26d8(0xffffffff80000000ULL);
    FUN_002fc760();
    FUN_002fc7b0(mpeg_context, uGpffffbec0, 0x17c300);

    handler_id = AddIntcHandler(2, 0x2f27b0, -1);
    mv3_main_decode_loop(mpeg_context);
    RemoveIntcHandler(2, handler_id);

    FUN_00203aa0(0);
    FUN_002f26d8(0xffffffff80000000ULL);
    FUN_002fc9b0(mpeg_context);
    FUN_002fc420();
    FUN_00203aa0(1);
    FUN_002f8990();
    FUN_00203aa0(1);
    FUN_002f8880(0, 1, 2, 1);
    FUN_002f9308(0, 0);
    FUN_002f9230(0x314c90, 0);
    FUN_002f9308(0, 0);
    FUN_002f9230(0x314c90, 1);
    FUN_002f9308(0, 0);
    FUN_00203aa0(4);
}
