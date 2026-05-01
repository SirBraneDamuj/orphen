/*
 * mv3_play_movie_by_id
 * Original: FUN_002f1808
 *
 * Top-level dispatcher for numbered MV3 movies. It allocates aligned movie work
 * buffers, resolves the movie path from the table at 0x00326f80, loads per-movie
 * audio/scene metadata from nearby tables, and calls the playback session helper.
 *
 * Observed paths in strings.json are M01.MV3 through M19.MV3. The selector is
 * one-based, so movie_id 1 maps to M01.MV3.
 */

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

extern u32 uGpffffb7b8;
extern u32 uGpffffb7bc;
extern int iGpffffbeb0;
extern int iGpffffbeb4;
extern int iGpffffbeb8;
extern u32 uGpffffbebc;
extern int iGpffffbec0;
extern u8 uGpffffbec4;
extern u8 uGpffffbec5;
extern u16 uGpffffbecc;
extern char cGpffffb61c;
extern char cGpffffb66a;
extern int iGpffffb284;
extern u32 uGpffffb638;
extern u32 uGpffffb686;
extern u32 uGpffffb68a;
extern u32 uGpffffb6a8;
extern void *uGpffffacbc;
extern void *uGpffffacc0;

extern const char *PTR_s__MV3_M01_MV3_1_00326f80[];
extern u16 DAT_00326fd0[];    /* Per-movie audio/pitch-like parameter */
extern u16 DAT_00326ff8[];    /* Per-movie metadata passed to FUN_002663a0 */

extern void FUN_002f9308(int, int);
extern void FUN_00305110(void);
extern void FUN_00222be8(void);
extern void FUN_00267da0(void *dest, void *src, unsigned int bytes);
extern void FUN_0026bfc0(unsigned int format_addr, ...);
extern void FUN_002663a0(unsigned short value);
extern void FUN_002f1f38(const char *disc_path);
extern long FUN_00266368(int flag_id, ...);
extern void FUN_00200c48(void *handle);

void mv3_play_movie_by_id(int movie_id) {
    unsigned int saved_arena;
    int table_index_bytes;
    long flag_value;

    FUN_002f9308(0, 0);
    FUN_00305110();
    FUN_00222be8();

    saved_arena = (uGpffffb7b8 + 0x80) - (uGpffffb7b8 & 0x7f);
    uGpffffb7bc = saved_arena;
    FUN_00267da0((void *)saved_arena, (void *)0x70000000, 0x4000);

    iGpffffbeb0 = (uGpffffb7bc + 0x4080) - (uGpffffb7bc & 0x7f);
    iGpffffbeb4 = (iGpffffbeb0 + 0x3f680) - ((iGpffffbeb0 + 0x3f600U) & 0x7f);
    iGpffffbeb8 = (iGpffffbeb4 + 0x151880) - ((iGpffffbeb4 + 0x151800U) & 0x7f);
    uGpffffbebc = (iGpffffbeb8 + 0x3f680) - ((iGpffffbeb8 + 0x3f600U) & 0x7f);
    iGpffffbec0 = (uGpffffbebc + 0x480) - (uGpffffbebc & 0x7f);
    uGpffffb7bc = iGpffffbec0 + 0x17c300;

    if (0x18499ff < uGpffffb7bc) {
        FUN_0026bfc0(0x34fde0, iGpffffbec0 - 0x16cd700);
    }

    uGpffffbec5 = 0;
    if (cGpffffb66a == 0) {
        if (movie_id == 0x12 && cGpffffb61c == 0) {
            uGpffffbec5 = 1;
            cGpffffb61c = 1;
        }
        if (movie_id == 10 || movie_id == 0xf) {
            uGpffffbec5 = 1;
        }
    }

    do {
        table_index_bytes = (movie_id - 1) * 2;
        FUN_002663a0(*(u16 *)((u8 *)DAT_00326ff8 + table_index_bytes));
        uGpffffbecc = *(u16 *)((u8 *)DAT_00326fd0 + table_index_bytes);
        uGpffffbec4 = (u8)movie_id;

        FUN_002f1f38(PTR_s__MV3_M01_MV3_1_00326f80[movie_id - 1]);

        if (iGpffffb284 == 0xc) {
            break;
        }
        if (movie_id == 2) {
            movie_id = 0xd;
        } else {
            if (movie_id != 0x11 || FUN_00266368(0x55a, saved_arena) != 0) {
                break;
            }
            flag_value = FUN_00266368(0x55b);
            movie_id = flag_value == 0 ? 1 : 3;
        }
    } while (1);

    FUN_00267da0((void *)0x70000000, (void *)saved_arena, 0x4000);
    FUN_00222be8();
    FUN_00200c48(uGpffffacbc);
    FUN_00200c48(uGpffffacc0);
    uGpffffb638 = 0;
    uGpffffb686 = 0;
    uGpffffb68a = 0;
    uGpffffb6a8 = 0;
}
