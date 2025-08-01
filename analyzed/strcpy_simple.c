/*
 * Simple String Copy - FUN_00268558
 *
 * Copies a null-terminated string from source to destination.
 * This is a basic implementation of strcpy functionality.
 *
 * The function copies characters one by one until it encounters
 * a null terminator, which is also copied to properly terminate
 * the destination string.
 *
 * Original function: FUN_00268558
 */

#include "orphen_globals.h"

/*
 * Copies null-terminated string from source to destination
 *
 * Parameters:
 *   dest - Destination buffer to copy string to
 *   src - Source string to copy from
 *
 * Copies characters including the null terminator.
 * No bounds checking - caller must ensure destination has sufficient space.
 */
void strcpy_simple(char *dest, char *src)
{
  char current_char;

  do
  {
    current_char = *src;
    src = src + 1;
    *dest = current_char;
    dest = dest + 1;
  } while (current_char != '\0');
}
