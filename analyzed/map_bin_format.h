/*
 * MAP.BIN File Format Analysis
 *
 * This file documents the structure of MAP.BIN, which contains the game's
 * map data. The format was determined by analyzing the file loading function
 * FUN_00223268 in the game's executable.
 *
 * File Structure:
 * 1. Header: A 4-byte integer specifying the number of entries in the lookup table.
 * 2. Lookup Table: An array of packed 4-byte entries that starts immediately
 *    after the header (at file offset 0x4).
 */

#ifndef MAP_BIN_FORMAT_H
#define MAP_BIN_FORMAT_H

#include "orphen_globals.h"

// Represents the 4-byte header of MAP.BIN
typedef struct
{
  uint32_t entry_count; // Number of entries in the lookup table.
} MapBinHeader;

/*
 * Represents a single 4-byte entry in the MAP.BIN lookup table.
 * The 32-bit value is a bitfield packed as follows:
 * - Bits 31-17 (15 bits): The starting sector of the map data.
 * - Bits 16-0 (17 bits): The size of the map data, in 4-byte words.
 */
typedef union
{
  uint32_t raw;
  struct
  {
    uint32_t size_in_words : 17;
    uint32_t sector_offset : 15;
  } fields;
} MapLookupEntry;

#define MAP_SECTOR_SIZE 2048

#endif // MAP_BIN_FORMAT_H
