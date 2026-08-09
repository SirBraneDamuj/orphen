/*
 * The dialogue font's proportional width table, and the "waiting for input"
 * book prompt.
 *
 * Originals: FUN_00238c90 (0x00238c90)  measure a sheet, fill the width table
 *            FUN_00238e50 (0x00238e50)  read one width out of it
 *            FUN_002391d0 (0x002391d0)  build the prompt sprite
 *            FUN_00237fc0 (0x00237fc0)  step its animation, lines 77-107
 *
 * ---------------------------------------------------------------------------
 * The sheets
 * ---------------------------------------------------------------------------
 *
 * FUN_00221fd8 loads two textures through a path of their own:
 *
 *     buffer = FUN_00210218(3, 0x173);  FUN_00238c90(buffer, 0);  FUN_002102d0(0x2E, buffer);
 *     buffer = FUN_00210218(3, 0x172);  FUN_00238c90(buffer, 1);  FUN_002102d0(0x2F, buffer);
 *
 * Everything else at boot goes through FUN_00210280, which is load-and-bind in
 * one call. These two are split so FUN_00238c90 can get at the decoded pixels
 * on the way past.
 *
 * Both sheets are 11 columns of 22x22 cells indexed by `character - 0x20`, 121
 * cells each. Slot 0x2E covers 0x20..0x98 and 0x2F picks up at 0x99, which
 * FUN_00238608 reaches by rebasing the character by 0x79 and FUN_00238a08 by
 * letting v run past 0xF1 and wrapping it modulo 256.
 *
 * ---------------------------------------------------------------------------
 * FUN_00238c90 -- where the widths come from
 * ---------------------------------------------------------------------------
 *
 * The table at 0x0031C518 is *not* in the executable's data. It is measured at
 * boot, from the artwork:
 *
 *     scratch = FUN_00268010(0x10000);              // 64K, one 256x256 plane
 *     FUN_00208ed0(sheet, &indices, ...);           // the index plane and CLUT
 *     for (row = 0xFF; row >= 0; --row)             // copy it back to front
 *       FUN_00267da0(scratch + row * 0x100, indices + (0xFF - row) * 0x100, 0x100);
 *
 * That copy is the BMPA bottom-up unflip: GS v = 0 is the *last* stored row.
 *
 *     out = &DAT_0031C538 + bank * 0x79;            // 0x31C518 + 0x20, per bank
 *     for (cellRow = 0; cellRow < 11; ++cellRow)
 *       for (cellCol = 0; cellCol < 11; ++cellCol) {
 *         rightmost = 0;
 *         for (y = 0; y < 22; ++y)
 *           for (x = 21; x > rightmost; --x)
 *             if ((index[x] & 0xF0) == 0 && clut[index[x]].a > 100) { rightmost = x; break; }
 *         *out++ = rightmost ? rightmost + 2 : 6;
 *       }
 *
 * So a character's width is the rightmost inked column plus two, and a blank
 * cell is six -- the space. `(index & 0xF0) == 0` restricts the test to the
 * first sixteen palette entries; on both font sheets every entry above that is
 * transparent anyway, so an alpha-only test reproduces the shipped table
 * exactly. Checked against the live table in the EE dump: all 121 cells of
 * slot 0x2E agree, both ways.
 *
 * The alpha threshold is on the PS2's 0..0x80 scale, so 100 is about 78%.
 *
 * FUN_00238e50 is one byte read: `((char *)&PTR_DAT_0031C518)[character]`.
 *
 * Drawn text uses 90% of the measured width. FUN_00238a08 advances by
 * `(width * 0x5A) / 100`; FUN_00238608 computes the same 0x5A from its
 * arguments as `(cellWidth * 100) / 22` with the shipped 20-wide cell, so the
 * two agree by construction rather than by coincidence.
 *
 * ---------------------------------------------------------------------------
 * The prompt
 * ---------------------------------------------------------------------------
 *
 * FUN_002391d0 takes the first free entry in the 300-entry glyph array at
 * DAT_00354E44 and fills it as a sprite rather than a glyph:
 *
 *     entry[0]  = 0x42A;              // slot 0x2A, texture 0x178
 *     entry[2]  = DAT_00355C38 + pen + 0x10;   // eight further right than a glyph
 *     entry[3]  = DAT_00355C3C + line * -0x16;
 *     entry[4]  = 0x14;  entry[5] = 0x16;      // drawn 20x22
 *     entry[6]  = 0x60;  entry[7] = 0x20;      // the first cell
 *     entry[8]  = 0x0F;  entry[9] = 0x0F;      // sampled 15x15
 *     entry[12] = *DAT_00354E30 == 1 ? 0x80808080 : 0x80608060;
 *
 * and FUN_00237fc0 animates it:
 *
 *     uGpffffaecc += frameTicks;  if (uGpffffaecc > 0x200) uGpffffaecc = 0;
 *     frame = (uGpffffaecc >> 7) & 3;
 *     entry[6] = ((short *)&DAT_0031C630)[frame * 2 + 0];
 *     entry[7] = ((short *)&DAT_0031C630)[frame * 2 + 1];
 *
 * The table holds (96,32), (96,48), (112,48), (112,32) -- a 2x2 block of 16x16
 * cells walked *round* rather than across, so the book's pages flip forward and
 * back. Four frames each at 0x80 ticks, sixteen frames a cycle at the nominal
 * 0x20.
 *
 * When Cross arrives (uGpffffb68a & 0x40) FUN_00237fc0 clears the entry's
 * active byte and puts its texture and size back to a text glyph's, which is
 * how the slot is returned to the pool.
 *
 * ---------------------------------------------------------------------------
 * Screen units
 * ---------------------------------------------------------------------------
 *
 * FUN_00239020 hands the entry to FUN_00207938 with the y *negated*, and
 * FUN_00207938 writes x at `<< 4` and y at `<< 3` about the 2048-pixel GS
 * centre. So one x unit is an output pixel, one y unit is half of one, and the
 * result is the same 640x448 virtual screen the debug overlay lands on:
 *
 *     screenX = entryX + 320
 *     screenY = 224 - entryY
 *
 * FUN_00237b38 opens a window at (-0x130, -0x78) = screen (16, 344). Later
 * lines step the entry y by -0x16, which moves them *down*.
 */

/* Signatures, for reference; bodies are in src/. */

void FUN_00238c90(void *decodedSheet, char bank);
unsigned char FUN_00238e50(unsigned int character);
void FUN_002391d0(void);
