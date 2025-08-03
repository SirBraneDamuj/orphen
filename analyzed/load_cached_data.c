/**
 * @file load_cached_data.c
 * @brief Loads data from cache using packed archive/file ID
 * @original_name FUN_00222d68
 *
 * This function provides cached data loading that bypasses file I/O.
 * It's called when cache flags are set in the main loader (FUN_00223268).
 * The function looks up cached data using a packed ID and copies it to the output buffer.
 *
 * Cache structure appears to use a complex indexing scheme:
 * - Base table at DAT_00315b04
 * - Indexed by sGpffffbc3e * 8 + sGpffffbc3c * 800
 * - Each cache entry has an 8-byte header followed by data
 * - Entry format: [unknown_8_bytes][size_4_bytes][data...]
 *
 * @param archive_type Archive type identifier (0-6)
 * @param file_id File ID within the archive
 * @param output_buffer Buffer to copy the cached data into
 * @return Size of data copied, or -1 if not found in cache
 */

// Cache management globals
extern int DAT_00315b04;  // Cache data table base address
extern short sGpffffbc3c; // Cache index component 1 (GP-based global)
extern short sGpffffbc3e; // Cache index component 2 (GP-based global)

// Cache lookup function
extern long FUN_00222c08(unsigned int packed_id); // Returns pointer to cached data entry

int load_cached_data(int archive_type, unsigned int file_id, unsigned char *output_buffer)
{
  unsigned char current_byte;
  int data_size;
  long cache_entry_ptr;
  unsigned char *source_data_ptr;
  int bytes_remaining;

  // Calculate cache table address using complex indexing scheme
  // Table structure: base + (index2 * 8) + (index1 * 800)
  // This suggests a 2D table with 800-byte rows and 8-byte columns
  int *cache_table_entry = (int *)(&DAT_00315b04 + sGpffffbc3e * 8 + sGpffffbc3c * 800);

  // Check if cache table entry exists and lookup the specific data
  if ((*cache_table_entry == 0) ||
      (cache_entry_ptr = FUN_00222c08(archive_type << 0x10 | file_id & 0xffff), cache_entry_ptr == 0))
  {
    // Cache miss - either table invalid or entry not found
    return -1;
  }
  else
  {
    // Cache hit - extract data from cache entry
    // Cache entry layout:
    // +0: Unknown 8-byte header
    // +8: Data starts here
    // +4: Size field (within the 8-byte header)
    source_data_ptr = (unsigned char *)((int)cache_entry_ptr + 8); // Skip 8-byte header to data
    data_size = *(int *)((int)cache_entry_ptr + 4);                // Size at offset 4 in header

    // Copy cached data byte by byte to output buffer
    for (bytes_remaining = data_size; 0 < bytes_remaining; bytes_remaining = bytes_remaining + -1)
    {
      current_byte = *source_data_ptr;
      source_data_ptr = source_data_ptr + 1;
      *output_buffer = current_byte;
      output_buffer = output_buffer + 1;
    }
  }

  return data_size;
}
