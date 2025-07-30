#include "orphen_globals.h"

// Forward declarations for referenced functions
extern void graphics_buffer_overflow_handler(int error_code);          // Buffer overflow handler (FUN_0026bf90)
extern void FUN_00267da0(byte *buffer, unsigned long param, int size); // Unknown data processing function

/**
 * Process audio data and generate sound pattern
 *
 * This function processes audio data by:
 * 1. Allocating 16 bytes from the scratchpad buffer (0x70000000)
 * 2. Processing the input parameter through FUN_00267da0 (8 bytes)
 * 3. Converting the processed data into a 256-byte bit pattern array
 * 4. Each byte in the pattern represents bit manipulations of the source data
 *
 * The function appears to be part of the audio system, converting audio
 * identifiers or addresses into processed sound data. It's called when
 * audio errors occur or when specific sound effects need to be triggered.
 *
 * Memory layout:
 * - Uses PS2 scratchpad memory at 0x70000000 for temporary buffer
 * - Output stored in 256-byte array at 0x571a50
 * - Includes buffer overflow protection for scratchpad memory
 *
 * Original function: FUN_0023b8e0
 * Address: 0x0023b8e0
 *
 * @param audio_data Audio data identifier or memory address to process
 */
void process_audio_data(unsigned long audio_data)
{
  byte *buffer_ptr;
  byte *data_ptr;
  unsigned int bit_index;
  unsigned int pattern_value;
  unsigned int byte_index;
  unsigned int outer_loop;

  // Allocate 16 bytes from scratchpad buffer
  buffer_ptr = scratchpad_buffer_ptr;
  scratchpad_buffer_ptr = scratchpad_buffer_ptr + 0x10;

  // Check for buffer overflow in scratchpad memory (limit: 0x70003fff)
  if ((byte *)0x70003fff < scratchpad_buffer_ptr)
  {
    graphics_buffer_overflow_handler(0);
  }

  // Process audio data into the allocated buffer (8 bytes)
  FUN_00267da0(buffer_ptr, audio_data, 8);

  // Convert processed data into 256-byte bit pattern
  outer_loop = 0;
  do
  {
    pattern_value = 0;
    bit_index = 0;
    byte_index = outer_loop + 1;
    data_ptr = buffer_ptr;

    // Process 8 bytes of data for current pattern byte
    do
    {
      if ((outer_loop & *data_ptr) != 0)
      {
        // Set bit in pattern if corresponding bit is set in source
        pattern_value = pattern_value | 1 << (bit_index & 0x1f) & 0xff;
      }
      bit_index = bit_index + 1;
      data_ptr = buffer_ptr + bit_index;
    } while ((int)bit_index < 8);

    // Store computed pattern byte
    audio_pattern_array[outer_loop] = (char)pattern_value;
    outer_loop = byte_index;
  } while ((int)byte_index < 0x100);

  // Release allocated scratchpad buffer
  scratchpad_buffer_ptr = scratchpad_buffer_ptr + -0x10;
  return;
}

// Global variables for audio data processing:

/**
 * PS2 scratchpad buffer pointer for temporary data
 * Scratchpad memory is fast local memory on PS2 (0x70000000 - 0x70003fff)
 * Original: DAT_70000000
 */
extern byte *scratchpad_buffer_ptr;

/**
 * Audio pattern array - 256 bytes of processed audio data
 * Stores the converted bit patterns for audio processing
 * Original: DAT_00571a50
 */
extern char audio_pattern_array[256];
