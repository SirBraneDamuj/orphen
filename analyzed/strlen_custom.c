/**
 * Custom string length function - calculates the length of a null-terminated string
 *
 * This appears to be a custom implementation of strlen(), likely used instead of
 * the standard library version for consistency or specific performance requirements
 * in the PS2 environment.
 *
 * Original function: FUN_002685e8
 * Address: 0x002685e8
 *
 * @param str Pointer to null-terminated string to measure
 * @return Length of the string (number of characters before null terminator)
 */
int strlen_custom(char *str)
{
  char currentChar;
  char *charPtr;
  int length;

  length = 0;
  currentChar = *str;
  charPtr = str + 1;

  while (currentChar != '\0')
  {
    length = length + 1;
    currentChar = *charPtr;
    charPtr = charPtr + 1;
  }

  return length;
}
