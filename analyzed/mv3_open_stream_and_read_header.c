/*
 * mv3_open_stream_and_read_header
 * Original: FUN_002f1a70
 *
 * Opens an MV3 file from the CD stream layer, reads the first sector, validates the
 * little-endian "MV30" header, and stores the block/chunk layout into globals used by
 * the movie playback loop.
 *
 * Header fields, confirmed by the debug string at 0x0034fe30:
 *   +0x00 char[4] magic      = "MV30" (integer compare 0x3033564d)
 *   +0x04 u32     blk_byte   = bytes per interleaved block
 *   +0x08 u32     pcm_ofs    = PCM chunk offset inside each block
 *   +0x0c u32     pcm_1sz    = PCM bytes copied from each block
 *   +0x10 u32     mpg_ofs    = MPEG chunk offset inside each block
 *   +0x14 u32     mpg_1sz    = MPEG bytes copied from each block
 *
 * Side effects:
 *   - Sets the raw block buffer to 0x005c9a00.
 *   - Uses 0x007c9a00 first as the header sector buffer, then as the MPEG ring buffer.
 *   - Sets the PCM ring buffer to 0x01949a00.
 *   - Opens/primes the CD stream and leaves it positioned after the header sector.
 *   - On success sets uGpffffb638 to 0x78 and returns the file byte count.
 */

typedef unsigned char  u8;
typedef unsigned int   u32;

extern u8 DAT_005c9a00[];
extern int DAT_007c9a00[];
extern u8 DAT_01949a00[];

extern u8 *puGpffffbed0;      /* MV3 interleaved block buffer */
extern int *piGpffffbed4;     /* Header sector, later MPEG ring buffer */
extern u8 *puGpffffbed8;      /* PCM ring buffer */
extern u32 uGpffffbedc;       /* Bytes filled in current interleaved block */
extern u32 uGpffffbee0;       /* MPEG ring write offset */
extern u32 uGpffffbee4;       /* Buffered MPEG byte count */
extern u32 uGpffffbee8;       /* PCM ring write offset */
extern u32 uGpffffbeec;
extern u32 uGpffffbef0;       /* Buffered PCM byte count */
extern u32 uGpffffbefc;       /* CD stream start/LBA-like value */
extern int iGpffffbf00;       /* Original file byte count */
extern int iGpffffbf04;       /* Remaining unread byte count */
extern int iGpffffbf08;       /* MV3 block size */
extern int iGpffffbf0c;       /* PCM chunk offset */
extern int iGpffffbf10;       /* PCM chunk size */
extern int iGpffffbf14;       /* MPEG chunk offset */
extern int iGpffffbf18;       /* MPEG chunk size */
extern u32 uGpffffb638;

extern long FUN_002fa7e8(void *out_stream_start, const char *disc_path);
extern void FUN_002fb9c0(int mode);
extern unsigned int FUN_002075a8(int bytes);
extern long FUN_002fc3a0(int, int, unsigned int);
extern void FUN_002fc3c8(u32 stream_start, void *mode_bytes);
extern long FUN_002fc450(unsigned int sectors, void *dest, int split_reads, void *status_out);
extern void FUN_002fc420(void);
extern void FUN_0026c088(unsigned int format_addr, ...);

int mv3_open_stream_and_read_header(const char *disc_path) {
    long ok;
    unsigned int scratch_addr;
    u32 file_info[2];
    u32 stream_start;
    int file_size;
    u8 open_mode[3];
    u8 cd_status[16];

    puGpffffbed0 = DAT_005c9a00;
    piGpffffbed4 = DAT_007c9a00;
    puGpffffbed8 = DAT_01949a00;
    uGpffffbedc = 0;
    uGpffffbee0 = 0;
    uGpffffbee4 = 0;
    uGpffffbee8 = 0;
    uGpffffbeec = 0;
    uGpffffbef0 = 0;

    do {
        file_info[1] = 0;
        ok = FUN_002fa7e8(file_info, disc_path);
        stream_start = file_info[0];
        file_size = (int)file_info[1];
        if (ok != 0 && file_size != 0) {
            break;
        }
        FUN_002fb9c0(0);
    } while (1);

    uGpffffbefc = stream_start;
    iGpffffbf00 = file_size;
    iGpffffbf04 = file_size;
    FUN_002fb9c0(0);

    do {
        scratch_addr = FUN_002075a8(0x28000);
        ok = FUN_002fc3a0(0x50, 5, scratch_addr);
    } while (ok == 0);

    open_mode[0] = 0;
    open_mode[1] = 0;
    open_mode[2] = 0;
    FUN_002fc3c8(uGpffffbefc, open_mode);

    do {
        ok = FUN_002fc450(1, piGpffffbed4, 1, cd_status);
    } while (ok == 0);

    iGpffffbf04 -= (int)ok * 0x800;

    if ((u32)piGpffffbed4[0] == 0x3033564d) {
        iGpffffbf08 = piGpffffbed4[1];
        iGpffffbf0c = piGpffffbed4[2];
        iGpffffbf10 = piGpffffbed4[3];
        iGpffffbf14 = piGpffffbed4[4];
        iGpffffbf18 = piGpffffbed4[5];

        FUN_0026c088(0x34fe30, iGpffffbf08, iGpffffbf0c, iGpffffbf10, iGpffffbf14, iGpffffbf18);

        if (iGpffffbf18 * 5 < 0x600001 && (iGpffffbf10 << 3) < 0x200001) {
            FUN_0026c088(0x34fe90, disc_path, iGpffffbf00);
            uGpffffb638 = 0x78;
            return iGpffffbf00;
        }

        FUN_0026c088(0x34fe70, disc_path);
    } else {
        FUN_0026c088(0x34fe18, disc_path);
    }

    FUN_002fc420();
    return 0;
}
