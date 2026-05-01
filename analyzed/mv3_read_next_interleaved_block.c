/*
 * mv3_read_next_interleaved_block
 * Original: FUN_002f1c98
 *
 * Reads enough CD sectors to complete one MV3 interleaved block, then demuxes the
 * fixed PCM and MPEG slices described by the MV30 header.
 *
 * Disk layout inferred here:
 *   file sector 0: MV30 header sector
 *   sector 1..N: repeated blocks of size iGpffffbf08 (blk_byte)
 *
 * For each complete block:
 *   block + iGpffffbf0c, length iGpffffbf10 -> PCM ring at puGpffffbed8
 *   block + iGpffffbf14, length iGpffffbf18 -> MPEG ring at piGpffffbed4
 *
 * The code copies in 16-byte units but advances ring offsets by the exact chunk
 * sizes. Offline demuxers should concatenate exactly pcm_1sz and mpg_1sz bytes.
 */

typedef unsigned char u8;
typedef unsigned int  u32;
typedef unsigned long long u64;

extern u8 *puGpffffbed0;      /* Interleaved block buffer */
extern u8 *puGpffffbed8;      /* PCM ring buffer */
extern int *piGpffffbed4;     /* Header sector, later MPEG ring buffer */
extern u32 uGpffffbedc;       /* Bytes read into current block */
extern u32 uGpffffbee0;       /* MPEG ring write offset */
extern int iGpffffbee4;       /* Buffered MPEG byte count */
extern u32 uGpffffbee8;       /* PCM ring write offset */
extern int iGpffffbef0;       /* Buffered PCM byte count */
extern int iGpffffbf04;       /* Remaining unread file bytes */
extern int iGpffffbf08;       /* blk_byte */
extern int iGpffffbf0c;       /* pcm_ofs */
extern int iGpffffbf10;       /* pcm_1sz */
extern int iGpffffbf14;       /* mpg_ofs */
extern int iGpffffbf18;       /* mpg_1sz */
extern int iGpffffb638;

extern long FUN_002fc450(unsigned int sectors, void *dest, int split_reads, void *status_out);
extern void FUN_002f1ed0(void);
extern void FUN_00267da0(void *dest, void *src, unsigned int bytes);
extern void FlushCache(int mode);

static void copy_16_aligned(void *dest, const void *src, int byte_count) {
    const u64 *readp = (const u64 *)src;
    u32 *writep = (u32 *)dest;
    int qwords = (byte_count + 15) >> 4;

    while (qwords-- > 0) {
        u64 lo_hi = *readp++;
        u32 third = *(const u32 *)readp;
        u32 fourth = *(const u32 *)((const u8 *)readp + 4);
        readp++;

        writep[0] = (u32)lo_hi;
        writep[1] = (u32)(lo_hi >> 32);
        writep[2] = third;
        writep[3] = fourth;
        writep += 4;
    }
}

unsigned int mv3_read_next_interleaved_block(void) {
    long sectors_read;
    unsigned int mpeg_chunk_size;
    u8 cd_status[16];

    if (iGpffffbf10 * 7 < iGpffffbef0 || (int)(iGpffffbf18 * 3) < iGpffffbee4) {
        return 0xffffffff;
    }

    sectors_read = FUN_002fc450((iGpffffbf08 - uGpffffbedc) >> 11,
                                puGpffffbed0 + uGpffffbedc,
                                0,
                                cd_status);
    if (sectors_read == 0) {
        if (iGpffffb638 == 0) {
            FUN_002f1ed0();
        }
        return 0;
    }

    iGpffffbf04 -= (int)sectors_read * 0x800;
    uGpffffbedc += (int)sectors_read * 0x800;
    iGpffffb638 = 0x78;

    if (uGpffffbedc < (u32)iGpffffbf08) {
        return 0;
    }

    uGpffffbedc = 0;
    FlushCache(0);

    copy_16_aligned(puGpffffbed8 + uGpffffbee8,
                    puGpffffbed0 + iGpffffbf0c,
                    iGpffffbf10);
    uGpffffbee8 += iGpffffbf10;
    iGpffffbef0 += iGpffffbf10;
    if ((u32)(iGpffffbf10 << 3) <= uGpffffbee8) {
        uGpffffbee8 = 0;
    }

    copy_16_aligned((u8 *)piGpffffbed4 + uGpffffbee0,
                    puGpffffbed0 + iGpffffbf14,
                    iGpffffbf18);
    uGpffffbee0 += iGpffffbf18;
    iGpffffbee4 += iGpffffbf18;

    if ((u32)(iGpffffbf18 * 5) <= uGpffffbee0) {
        mpeg_chunk_size = (unsigned int)iGpffffbf18;
        copy_16_aligned(piGpffffbed4,
                (u8 *)piGpffffbed4 + mpeg_chunk_size,
                        (int)mpeg_chunk_size);
        uGpffffbee0 = mpeg_chunk_size;
    }

    FlushCache(0);
    return (unsigned int)iGpffffbf18;
}
