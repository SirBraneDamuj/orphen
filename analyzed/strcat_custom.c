/*
 * Function Analysis: FUN_00268500 -> strcat_custom
 * Original Location: 0x00268500
 *
 * ANALYSIS:
 * This function implements a string concatenation operation, equivalent to strcat() from the C standard library.
 * It appends the source string to the end of the destination string.
 *
 * PARAMETERS:
 * - param_1: Destination string buffer (char*) - will be modified
 * - param_2: Source string to append (char*)
 *
 * BEHAVIOR:
 * 1. If destination string is empty (first char is '\0'), it copies source to destination
 * 2. Otherwise, it finds the end of the destination string by advancing the pointer
 * 3. Once at the end, it copies the source string starting from that position
 * 4. The null terminator from source is copied, properly terminating the concatenated string
 *
 * ALGORITHM BREAKDOWN:
 * - First checks if destination is empty (*param_1 == '\0')
 * - If empty: directly copies source to destination (acts like strcpy)
 * - If not empty: advances param_1 pointer until it finds the null terminator
 * - Then copies source string starting from that position
 *
 * NOTES:
 * - This is an unsafe string concatenation function (like strcat) with no bounds checking
 * - The destination buffer must be large enough to hold both strings plus null terminator
 * - Buffer overflow is possible if destination buffer is too small
 * - The goto/label structure suggests this was optimized or the decompiler had trouble
 *   with the loop structure, but the logic is standard string concatenation
 * - Common pattern in PS2 game code where custom string functions were implemented
 *
 * SUGGESTED RENAME: strcat_custom or string_concat
 */

// Analyzed version with improved naming and comments
void strcat_custom(char *dest, char *src)
{
  char currentChar;

  // If destination is empty, just copy source to it
  if (*dest == '\0')
  {
    // Copy source string to destination
    currentChar = *src;
    while (1)
    {
      src = src + 1;
      *dest = currentChar;
      dest = dest + 1;
      if (currentChar == '\0')
        break;
      currentChar = *src;
    }
    return;
  }

  // Find end of destination string
  dest = dest + 1;
  do
  {
    if (*dest == '\0')
    {
      // Found end, now copy source string
      currentChar = *src;
      while (1)
      {
        src = src + 1;
        *dest = currentChar;
        dest = dest + 1;
        if (currentChar == '\0')
          break;
        currentChar = *src;
      }
      return;
    }
    dest = dest + 1;
  } while (1);
}

/*
 * EQUIVALENT STANDARD C CODE:
 *
 * char* strcat(char* dest, const char* src) {
 *     char* original_dest = dest;
 *
 *     // Find end of destination string
 *     while (*dest != '\0') {
 *         dest++;
 *     }
 *
 *     // Copy source string to end of destination
 *     while ((*dest++ = *src++) != '\0');
 *
 *     return original_dest;
 * }
 *
 * The decompiled version has some quirks:
 * 1. Uses goto/label structure (likely decompiler artifact)
 * 2. Handles empty destination as special case (optimization)
 * 3. Explicit character copying with temporary variable
 * 4. Returns void instead of returning destination pointer
 */
