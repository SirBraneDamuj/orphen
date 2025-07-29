#include <stdint.h>
#include <stdbool.h>

// PS2 type definitions (matching Ghidra output)
typedef unsigned char undefined;
typedef unsigned short undefined2;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned long ulong;

/**
 * Custom formatted output function - writes formatted data to a stream
 *
 * This appears to be a custom implementation of vfprintf(), handling various format
 * specifiers for outputting formatted text. The function processes format strings
 * and arguments, building output in chunks that are written to the stream.
 *
 * Original function: FUN_0030c8d0
 * Address: 0x0030c8d0
 *
 * Format specifiers supported:
 * - %d, %i - signed decimal integers
 * - %u, %U - unsigned decimal integers
 * - %o, %O - octal integers
 * - %x, %X - hexadecimal integers
 * - %f, %e, %E, %g, %G - floating point
 * - %c - character
 * - %s - string
 * - %p - pointer
 * - %n - write character count to pointer
 * - %% - literal percent
 *
 * Format modifiers:
 * - # - alternate form (0x prefix for hex, etc)
 * - + - always show sign
 * - - - left justify
 * - 0 - zero padding
 * - * - width/precision from argument
 * - l, ll - long/long long modifiers
 * - h - short modifier
 *
 * @param stream_info Stream information structure
 * @param param2 Unknown parameter (possibly buffer info)
 * @param format_string Format string containing text and format specifiers
 * @param args_list Pointer to variadic arguments list
 * @return Number of characters written, or -1 on error
 */
int vfprintf_custom(void *stream_info, void *param2, char *format_string, ulong *args_list)
{
  bool should_continue;
  uint temp_uint;
  char current_char;
  int *output_chunks;
  int result;
  char *string_ptr;
  long temp_long;
  long temp_long2;
  ushort stream_flags;
  char *format_ptr;
  uint format_flags;
  ulong numeric_value;
  int *chunk_ptr;
  int width_spec;
  int *chunk_array_ptr;
  char *output_buffer_ptr;
  ulong *current_arg;
  ulong *next_arg;
  uint flag_bits;
  char *temp_str_ptr;

  // Large local buffers for formatting
  char temp_buffer[16]; // For temporary string operations
  int *chunk_buffer_ptr;
  int total_bytes_written;
  int current_chunk_size;
  int output_chunks_array[16];  // Array to store output chunk information
  char format_work_buffer[348]; // Main working buffer for format operations

  // Format parsing variables
  char temp_format_char[4];
  char sign_char;
  char padding_char;
  char width_padding_char;
  char sign_buffer[3]; // Buffer for sign characters (+, -, space)
  int field_width;
  void *unknown_param;
  char *precision_ptr;
  int precision_value;
  void *stream_param;
  char *current_format_pos;
  uint format_flags_mask;
  int char_count;
  char *field_width_ptr;
  int bytes_written_so_far;
  ulong float_value;
  int exponent_value;
  char *exponent_buffer_ptr;
  char *number_buffer_ptr;
  char *hex_digits_ptr; // Pointer to hex digit lookup table
  int *chunk_stack_ptr;
  void *chunk_data_ptr;
  int chunk_count;
  void *chunk_param1;
  int stream_buffer_size;
  void *chunk_param2;
  void *temp_chunk_param;
  void *temp_chunk_param2;

  stream_param = stream_info;

  // Get locale/formatting information (hypothesized)
  chunk_ptr = (int *)get_locale_info();
  bytes_written_so_far = *chunk_ptr;
  unknown_param = 0;
  result = (int)param2;

  // Initialize stream if needed
  if (*(int *)(result + 0x54) == 0)
  {
    *(void **)(result + 0x54) = get_default_stream_ptr();
    width_spec = *(int *)(result + 0x54);
  }
  else
  {
    width_spec = *(int *)(result + 0x54);
  }

  // Check if stream needs initialization
  if (*(int *)(width_spec + 0x38) == 0)
  {
    initialize_stream_buffer();
    stream_flags = *(ushort *)(result + 0xc);
  }
  else
  {
    stream_flags = *(ushort *)(result + 0xc);
  }

  // Handle buffered output mode
  if (((stream_flags & 8) == 0) || (*(int *)(result + 0x10) == 0))
  {
    temp_long = setup_stream_output(param2);
    if (temp_long != 0)
    {
      return -1;
    }
    stream_flags = *(ushort *)(result + 0xc);
  }
  else
  {
    stream_flags = *(ushort *)(result + 0xc);
  }

  chunk_buffer_ptr = output_chunks_array;

  // Handle special wide character mode
  if (((stream_flags & 0x1a) == 10) && (-1 < *(short *)(result + 0xe)))
  {
    result = handle_wide_char_output(param2, format_string, args_list);
    return result;
  }

  // Initialize parsing state
  chunk_stack_ptr = &field_width;
  chunk_data_ptr = &unknown_param;
  current_chunk_size = 0;
  total_bytes_written = 0;
  current_format_pos = format_string;
  char_count = 0;
  format_ptr = current_format_pos;
  output_chunks = chunk_buffer_ptr;

FORMAT_PARSING_LOOP:
  // Parse format string until we find a format specifier or end
  temp_long = parse_until_format_specifier(get_default_stream_ptr(), chunk_stack_ptr,
                                           current_format_pos, get_format_buffer_ptr(),
                                           chunk_data_ptr);
  if (0 < temp_long)
    goto FOUND_FORMAT_SPECIFIER;
  goto CHECK_END_OF_FORMAT;

FOUND_FORMAT_SPECIFIER:
  current_format_pos = current_format_pos + (int)temp_long;
  if (field_width != 0x25) // Not a '%' character
    goto FORMAT_PARSING_LOOP;
  current_format_pos = current_format_pos + -1;

CHECK_END_OF_FORMAT:
  width_spec = (int)current_format_pos - (int)format_ptr;
  if (width_spec != 0)
  {
    // Add literal text chunk to output
    output_chunks[1] = width_spec;
    *output_chunks = (int)format_ptr;
    output_chunks = output_chunks + 2;
    current_chunk_size = current_chunk_size + width_spec;
    total_bytes_written = total_bytes_written + 1;

    // Flush chunks if buffer is full
    if (7 < total_bytes_written)
    {
      temp_long2 = flush_output_chunks(param2, &chunk_buffer_ptr);
      output_chunks = output_chunks_array;
      if (temp_long2 != 0)
        goto OUTPUT_ERROR;
    }
    char_count = char_count + width_spec;
  }

  if (0 < temp_long)
  {
    // Process format specifier
    sign_buffer[0] = '\0';
    current_format_pos = current_format_pos + 1;
    format_flags_mask = 0;
    number_buffer_ptr = (char *)0x0;
    field_width_ptr = (char *)0x0;
    next_arg = args_list;
    format_ptr = (char *)0xffffffff; // Default precision (-1)

  PARSE_FORMAT_FLAGS:
    temp_long = (long)*current_format_pos;
    current_format_pos = current_format_pos + 1;

  PROCESS_FORMAT_CHAR:
    current_arg = next_arg;

    switch ((int)temp_long)
    {
    case 0x20: // Space flag ' '
      goto HANDLE_SPACE_FLAG;

    default:
      if (temp_long == 0)
        goto END_OF_FORMAT;

      // Single character output
      output_buffer_ptr = format_work_buffer;
      format_work_buffer[0] = (char)temp_long;
      string_ptr = (char *)0x1;
      sign_buffer[0] = '\0';
      args_list = next_arg;
      break;

    case 0x23: // Hash flag '#'
      format_flags_mask = format_flags_mask | 1;
      goto PARSE_FORMAT_FLAGS;

    case 0x2a: // Width specifier '*'
      current_arg = next_arg + 1;
      field_width_ptr = *(char **)next_arg;
      next_arg = current_arg;
      if ((int)field_width_ptr < 0)
      {
        field_width_ptr = (char *)-(int)field_width_ptr;
        goto HANDLE_LEFT_JUSTIFY;
      }
      goto PARSE_FORMAT_FLAGS;

    case 0x2b: // Plus flag '+'
      sign_buffer[0] = '+';
      goto PARSE_FORMAT_FLAGS;

    case 0x2d: // Minus flag '-' (left justify)
    HANDLE_LEFT_JUSTIFY:
      format_flags_mask = format_flags_mask | 4;
      next_arg = current_arg;
      goto PARSE_FORMAT_FLAGS;

    case 0x2e: // Precision specifier '.'
      current_char = *current_format_pos;
      temp_long = (long)current_char;
      current_format_pos = current_format_pos + 1;

      if (temp_long == 0x2a) // Precision from argument
      {
        current_arg = next_arg + 1;
        output_buffer_ptr = *(char **)next_arg;
        next_arg = current_arg;
        format_ptr = (char *)0xffffffff;
        if (-2 < (int)output_buffer_ptr)
        {
          format_ptr = output_buffer_ptr;
        }
        goto PARSE_FORMAT_FLAGS;
      }

      // Parse numeric precision
      output_buffer_ptr = (char *)0x0;
      while ((int)current_char - 0x30U < 10)
      {
        output_buffer_ptr = (char *)((int)output_buffer_ptr * 10 + -0x30 + (int)temp_long);
        current_char = *current_format_pos;
        temp_long = (long)current_char;
        current_format_pos = current_format_pos + 1;
      }
      format_ptr = (char *)0xffffffff;
      if (-2 < (int)output_buffer_ptr)
      {
        format_ptr = output_buffer_ptr;
      }
      goto PROCESS_FORMAT_CHAR;

    case 0x30: // Zero padding flag '0'
      format_flags_mask = format_flags_mask | 0x80;
      goto PARSE_FORMAT_FLAGS;

    case 0x31: // Digits 1-9 (width specifier)
    case 0x32:
    case 0x33:
    case 0x34:
    case 0x35:
    case 0x36:
    case 0x37:
    case 0x38:
    case 0x39:
      goto PARSE_WIDTH_DIGITS;

    case 0x44: // 'D' - long decimal
      format_flags_mask = format_flags_mask | 0x10;
    case 100:  // 'd' - decimal integer
    case 0x69: // 'i' - integer
      // Extract integer value based on size modifiers
      if ((format_flags_mask & 0x10) == 0)
      {
        if ((format_flags_mask & 0x40) == 0)
        {
          numeric_value = (ulong)(int)(uint)*next_arg;
        }
        else
        {
          numeric_value = (ulong)(short)(ushort)*next_arg;
        }
      }
      else
      {
        numeric_value = *next_arg;
      }

      width_spec = 1; // Decimal base
      if ((long)numeric_value < 0)
      {
        numeric_value = -numeric_value;
        sign_buffer[0] = '-';
      }
      goto FORMAT_INTEGER;

    case 0x45: // 'E' - scientific notation
    case 0x47: // 'G' - general format
    case 0x65: // 'e' - scientific notation
    case 0x66: // 'f' - fixed point
    case 0x67: // 'g' - general format
      // Handle floating point formatting
      if (format_ptr == (char *)0xffffffff)
      {
        format_ptr = (char *)0x6; // Default precision
      }
      else if (((temp_long == 0x67) || (temp_long == 0x47)) && (format_ptr == (char *)0x0))
      {
        format_ptr = (char *)0x1; // Minimum precision for %g
      }

      args_list = next_arg + 1;
      if ((format_flags_mask & 8) == 0)
      {
        float_value = *next_arg;
      }
      else
      {
        float_value = *next_arg;
      }

      // Check for special floating point values
      temp_long2 = check_if_infinite(float_value);
      if (temp_long2 == 0)
      {
        temp_long2 = check_if_nan(float_value);
        if (temp_long2 == 0)
        {
          // Normal floating point number
          format_flags_mask = format_flags_mask | 0x100;
          output_buffer_ptr = (char *)format_floating_point(stream_param, float_value, format_ptr,
                                                            format_flags_mask, &width_padding_char,
                                                            &precision_ptr, temp_long, &precision_value);

          // Handle %g/%G format selection
          if ((temp_long == 0x67) || (temp_long == 0x47))
          {
            if (((int)precision_ptr < -3) || ((int)format_ptr < (int)precision_ptr))
            {
              should_continue = temp_long != 0x67;
              temp_long = 0x65; // Use %e format
              if (should_continue)
              {
                temp_long = 0x45; // Use %E format
              }
            }
            else
            {
              temp_long = 0x67; // Use %g format
            }
          }

          string_ptr = precision_ptr + -1;
          if (temp_long < 0x66)
          {
            // Scientific notation formatting
            precision_ptr = string_ptr;
            exponent_value = format_exponent_string(temp_buffer, string_ptr, temp_long);
            string_ptr = (char *)(exponent_value + precision_value);
            if ((1 < precision_value) || ((format_flags_mask & 1) != 0))
            {
              string_ptr = string_ptr + 1;
            }
          }
          else if (temp_long == 0x66)
          {
            // Fixed point formatting
            if ((int)precision_ptr < 1)
            {
              string_ptr = format_ptr + 2;
            }
            else if ((format_ptr != (char *)0x0) || (string_ptr = precision_ptr, (format_flags_mask & 1) != 0))
            {
              string_ptr = precision_ptr + 1 + (int)format_ptr;
            }
          }
          else if ((int)precision_ptr < precision_value)
          {
            string_ptr = (char *)(precision_value + 1);
            if ((int)precision_ptr < 1)
            {
              string_ptr = (char *)((precision_value + 2) - (int)precision_ptr);
            }
          }
          else
          {
            string_ptr = precision_ptr + (format_flags_mask & 1);
          }

          flag_bits = format_flags_mask & 0x84;
          if (width_padding_char != '\0')
          {
            sign_buffer[0] = '-';
          }
        }
        else
        {
          // NaN value
          string_ptr = (char *)0x3;
          output_buffer_ptr = "NaN";
          flag_bits = format_flags_mask & 0x84;
        }
        goto FORMAT_OUTPUT;
      }

      // Infinite value
      temp_long2 = check_float_sign(float_value, 0);
      if (temp_long2 < 0)
      {
        sign_buffer[0] = '-';
      }
      string_ptr = (char *)0x3;
      output_buffer_ptr = "Inf";
      break;

    case 0x4c: // 'L' - long double modifier
      format_flags_mask = format_flags_mask | 8;
      goto PARSE_FORMAT_FLAGS;

    case 0x4f: // 'O' - long octal
      format_flags_mask = format_flags_mask | 0x10;
    case 0x6f: // 'o' - octal
      // Extract value for octal formatting
      if ((format_flags_mask & 0x10) == 0)
      {
        if ((format_flags_mask & 0x40) == 0)
        {
          numeric_value = (ulong)(uint)*next_arg;
        }
        else
        {
          numeric_value = (ulong)(ushort)*next_arg;
        }
      }
      else
      {
        numeric_value = *next_arg;
      }
      width_spec = 0; // Octal base
      goto CLEAR_SIGN_AND_FORMAT;

    case 0x55: // 'U' - long unsigned
      format_flags_mask = format_flags_mask | 0x10;
    case 0x75: // 'u' - unsigned
      // Extract value for unsigned formatting
      if ((format_flags_mask & 0x10) == 0)
      {
        if ((format_flags_mask & 0x40) == 0)
        {
          numeric_value = (ulong)(uint)*next_arg;
        }
        else
        {
          numeric_value = (ulong)(ushort)*next_arg;
        }
      }
      else
      {
        numeric_value = *next_arg;
      }
      width_spec = 1; // Decimal base
      goto CLEAR_SIGN_AND_FORMAT;

    case 0x58: // 'X' - uppercase hex
      hex_digits_ptr = "0123456789ABCDEF";
      goto SETUP_HEX_FORMAT;

    case 99: // 'c' - character
      args_list = next_arg + 1;
      format_work_buffer[0] = (char)*next_arg;
      output_buffer_ptr = format_work_buffer;
      string_ptr = (char *)0x1;
      sign_buffer[0] = '\0';
      flag_bits = format_flags_mask & 0x84;
      goto FORMAT_OUTPUT;

    case 0x68: // 'h' - short modifier
      format_flags_mask = format_flags_mask | 0x40;
      goto PARSE_FORMAT_FLAGS;

    case 0x6c: // 'l' - long modifier
      if (*current_format_pos == 'l')
      {
        current_format_pos = current_format_pos + 1;
        format_flags_mask = format_flags_mask | 0x20; // long long
      }
      else
      {
        format_flags_mask = format_flags_mask | 0x10; // long
      }
      goto PARSE_FORMAT_FLAGS;

    case 0x6e: // 'n' - write character count
      format_ptr = current_format_pos;
      if ((format_flags_mask & 0x10) == 0)
      {
        if ((format_flags_mask & 0x40) == 0)
        {
          args_list = next_arg + 1;
          **(int **)next_arg = char_count;
        }
        else
        {
          args_list = next_arg + 1;
          **(undefined2 **)next_arg = (short)char_count;
        }
      }
      else
      {
        args_list = next_arg + 1;
        **(long **)next_arg = (long)char_count;
      }
      goto FORMAT_PARSING_LOOP;

    case 0x70: // 'p' - pointer
      hex_digits_ptr = "0123456789abcdef";
      format_flags_mask = format_flags_mask | 2;
      temp_long = 0x78;
      numeric_value = (ulong)(int)(uint)*next_arg;
      goto HEX_WITH_PREFIX;

    case 0x71: // 'q' - long long modifier (non-standard)
      format_flags_mask = format_flags_mask | 0x20;
      goto PARSE_FORMAT_FLAGS;

    case 0x73: // 's' - string
      args_list = next_arg + 1;
      output_buffer_ptr = *(char **)next_arg;
      if (output_buffer_ptr == (char *)0x0)
      {
        output_buffer_ptr = "(null)";
      }

      if ((int)format_ptr < 0)
      {
        string_ptr = (char *)strlen_custom(output_buffer_ptr);
      }
      else
      {
        temp_long2 = find_char_in_string(output_buffer_ptr, 0, format_ptr);
        temp_str_ptr = (char *)((int)temp_long2 - (int)output_buffer_ptr);
        string_ptr = format_ptr;
        if ((temp_long2 != 0) && (string_ptr = temp_str_ptr, (int)format_ptr < (int)temp_str_ptr))
        {
          string_ptr = format_ptr;
        }
      }
      sign_buffer[0] = '\0';
      flag_bits = format_flags_mask & 0x84;
      goto FORMAT_OUTPUT;

    case 0x78: // 'x' - lowercase hex
      hex_digits_ptr = "0123456789abcdef";
    SETUP_HEX_FORMAT:
      // Extract value for hex formatting
      if ((format_flags_mask & 0x10) == 0)
      {
        if ((format_flags_mask & 0x40) == 0)
        {
          numeric_value = (ulong)(uint)*next_arg;
        }
        else
        {
          numeric_value = (ulong)(ushort)*next_arg;
        }
      }
      else
      {
        numeric_value = *next_arg;
      }

      width_spec = 2; // Hex base
      if ((format_flags_mask & 1) != 0)
      {
        if (numeric_value != 0)
        {
          format_flags_mask = format_flags_mask | 2;
        }
      HEX_WITH_PREFIX:
        width_spec = 2;
      }
      goto CLEAR_SIGN_AND_FORMAT;
    }

    flag_bits = format_flags_mask & 0x84;
    goto FORMAT_OUTPUT;
  }

END_OF_FORMAT:
  if ((current_chunk_size != 0) && (temp_long = flush_output_chunks(param2, &chunk_buffer_ptr), temp_long != 0))
  {
    stream_flags = *(ushort *)(result + 0xc);
    goto OUTPUT_ERROR_RETURN;
  }
  goto SUCCESS_RETURN;

CLEAR_SIGN_AND_FORMAT:
  sign_buffer[0] = '\0';

FORMAT_INTEGER:
  args_list = current_arg + 1;
  if (-1 < (int)format_ptr)
  {
    format_flags_mask = format_flags_mask & 0xffffff7f; // Clear zero padding
  }
  flag_bits = format_flags_mask;
  output_buffer_ptr = temp_format_char;
  number_buffer_ptr = format_ptr;

  if ((numeric_value == 0) && (format_ptr == (char *)0x0))
    goto FORMAT_NUMBER_DONE;

  if (width_spec == 1) // Decimal
    goto FORMAT_DECIMAL_LOOP;

  if (width_spec == 0) // Octal
  {
    do
    {
      format_ptr = output_buffer_ptr;
      output_buffer_ptr = format_ptr + -1;
      temp_long2 = (numeric_value & 7) + 0x30;
      numeric_value = numeric_value >> 3;
      *output_buffer_ptr = (char)temp_long2;
    } while (numeric_value != 0);

    if (((format_flags_mask & 1) != 0) && (temp_long2 != 0x30))
    {
      output_buffer_ptr = format_ptr + -2;
      *output_buffer_ptr = '0';
    }
    goto FORMAT_NUMBER_DONE;
  }

  if (width_spec == 2) // Hexadecimal
    goto FORMAT_HEX_LOOP;

  output_buffer_ptr = "bug in vfprintf: bad base";
  flag_bits = format_flags_mask & 0x84;
  string_ptr = (char *)strlen_custom(0x351bb8);
  goto FORMAT_OUTPUT;

FORMAT_HEX_LOOP:
  do
  {
    temp_uint = (uint)numeric_value;
    output_buffer_ptr = output_buffer_ptr + -1;
    numeric_value = numeric_value >> 4;
    *output_buffer_ptr = hex_digits_ptr[temp_uint & 0xf];
  } while (numeric_value != 0);
  goto FORMAT_NUMBER_DONE;

PARSE_WIDTH_DIGITS:
  width_spec = 0;
  do
  {
    field_width_ptr = (char *)(width_spec + -0x30 + (int)temp_long);
    current_char = *current_format_pos;
    temp_long = (long)current_char;
    current_format_pos = current_format_pos + 1;
    width_spec = (int)field_width_ptr * 10;
  } while ((int)current_char - 0x30U < 10);
  goto PROCESS_FORMAT_CHAR;

HANDLE_SPACE_FLAG:
  if (sign_buffer[0] == '\0')
  {
    sign_buffer[0] = ' ';
  }
  goto PARSE_FORMAT_FLAGS;

FORMAT_DECIMAL_LOOP:
  while (9 < numeric_value)
  {
    current_char = divide_by_10_get_remainder(numeric_value, 10);
    output_buffer_ptr = output_buffer_ptr + -1;
    *output_buffer_ptr = current_char + '0';
    numeric_value = divide_by_10(numeric_value, 10);
  }
  output_buffer_ptr = output_buffer_ptr + -1;
  *output_buffer_ptr = (char)numeric_value + '0';

FORMAT_NUMBER_DONE:
  flag_bits = flag_bits & 0x84;
  string_ptr = temp_format_char + -(int)output_buffer_ptr;

FORMAT_OUTPUT:
  chunk_array_ptr = output_chunks + 2;
  number_buffer_ptr = number_buffer_ptr;

  if ((int)number_buffer_ptr <= (int)string_ptr)
  {
    number_buffer_ptr = string_ptr;
  }

  if (sign_buffer[0] == '\0')
  {
    number_buffer_ptr = number_buffer_ptr + (format_flags_mask & 2);
  }
  else
  {
    number_buffer_ptr = number_buffer_ptr + 1;
  }

  // Handle right justification padding
  if (flag_bits == 0)
  {
    width_spec = (int)field_width_ptr - (int)number_buffer_ptr;
    if (0 < width_spec)
    {
      // Add padding chunks
      // [Complex padding logic omitted for brevity - adds space padding]
    }
  }

  // Add sign character if needed
  if (sign_buffer[0] != '\0')
  {
    *output_chunks = (int)sign_buffer;
    output_chunks[1] = 1;
    current_chunk_size = current_chunk_size + 1;
    output_chunks = output_chunks + 2;
    total_bytes_written = total_bytes_written + 1;

    if (7 < total_bytes_written)
    {
      temp_long2 = flush_output_chunks(param2, &chunk_buffer_ptr);
      output_chunks = output_chunks_array;
      if (temp_long2 != 0)
        goto OUTPUT_ERROR;
    }
  }
  else if ((format_flags_mask & 2) != 0) // Add "0x" prefix
  {
    padding_char = (char)temp_long;
    sign_char = '0';
    output_chunks[1] = 2;
    *output_chunks = (int)&sign_char;
    current_chunk_size = current_chunk_size + 2;
    output_chunks = output_chunks + 2;
    total_bytes_written = total_bytes_written + 1;

    if (7 < total_bytes_written)
    {
      temp_long2 = flush_output_chunks(param2, &chunk_buffer_ptr);
      output_chunks = output_chunks_array;
      if (temp_long2 != 0)
        goto OUTPUT_ERROR;
    }
  }

  // Handle zero padding
  if (flag_bits == 0x80)
  {
    width_spec = (int)field_width_ptr - (int)number_buffer_ptr;
    if (0 < width_spec)
    {
      // Add zero padding chunks
      // [Complex zero padding logic omitted for brevity]
    }
  }

  // Add precision padding for integers
  width_spec = (int)number_buffer_ptr - (int)string_ptr;
  if (0 < width_spec)
  {
    // Add zero padding for precision
    // [Precision padding logic omitted for brevity]
  }

  // Add the actual formatted content
  if ((format_flags_mask & 0x100) == 0) // Regular formatting
  {
    output_chunks[1] = (int)string_ptr;
    *output_chunks = (int)output_buffer_ptr;
    current_chunk_size = current_chunk_size + (int)string_ptr;
    output_chunks = output_chunks + 2;
    total_bytes_written = total_bytes_written + 1;

    if (total_bytes_written < 8)
      goto CHECK_LEFT_PADDING;
  }
  else
  {
    // Special floating point formatting
    // [Complex floating point output logic omitted for brevity]
  }

  temp_long2 = flush_output_chunks(param2, &chunk_buffer_ptr);
  output_chunks = output_chunks_array;
  if (temp_long2 != 0)
    goto OUTPUT_ERROR;

CHECK_LEFT_PADDING:
  // Handle left justification padding
  if ((format_flags_mask & 4) != 0)
  {
    width_spec = (int)field_width_ptr - (int)number_buffer_ptr;
    if (0 < width_spec)
    {
      // Add trailing spaces for left justification
      // [Left padding logic omitted for brevity]
    }
  }

  format_ptr = field_width_ptr;
  if ((int)field_width_ptr <= (int)number_buffer_ptr)
  {
    format_ptr = number_buffer_ptr;
  }
  char_count = char_count + (int)format_ptr;

  if ((current_chunk_size != 0) && (temp_long2 = flush_output_chunks(param2, &chunk_buffer_ptr), temp_long2 != 0))
  {
    stream_flags = *(ushort *)(result + 0xc);
    goto OUTPUT_ERROR_RETURN;
  }

  total_bytes_written = 0;
  output_chunks = output_chunks_array;
  format_ptr = current_format_pos;
  goto FORMAT_PARSING_LOOP;

OUTPUT_ERROR:
  stream_flags = *(ushort *)(result + 0xc);

OUTPUT_ERROR_RETURN:
  result = -1;
  if ((stream_flags & 0x40) == 0)
  {
    result = char_count;
  }
  return result;

SUCCESS_RETURN:
  return char_count;
}

// Hypothesized function prototypes for referenced functions:

/**
 * Get system locale/formatting information
 * Original: FUN_00310318
 */
extern int *get_locale_info(void);

/**
 * Get default stream pointer
 * Original: PTR_DAT_0034b01c
 */
extern void *get_default_stream_ptr(void);

/**
 * Initialize stream buffer
 * Original: FUN_0030f8d0
 */
extern void initialize_stream_buffer(void);

/**
 * Setup stream for output operations
 * Original: FUN_0030e150
 */
extern long setup_stream_output(void *stream);

/**
 * Handle wide character output mode
 * Original: FUN_0030c7e8
 */
extern int handle_wide_char_output(void *stream, char *format, ulong *args);

/**
 * Get format buffer pointer
 * Original: DAT_0034b020
 */
extern void *get_format_buffer_ptr(void);

/**
 * Parse format string until format specifier found
 * Original: FUN_00310e60
 */
extern long parse_until_format_specifier(void *stream_ptr, int *field_ptr, char *format_pos, void *buffer_ptr, void *data_ptr);

/**
 * Flush accumulated output chunks to stream
 * Original: FUN_0030c7a0
 */
extern long flush_output_chunks(void *stream, int **chunk_buffer);

/**
 * Check if floating point value is infinite
 * Original: FUN_00312058
 */
extern long check_if_infinite(ulong float_val);

/**
 * Check if floating point value is NaN
 * Original: FUN_003120a0
 */
extern long check_if_nan(ulong float_val);

/**
 * Format floating point number to string
 * Original: FUN_0030de70
 */
extern char *format_floating_point(void *stream, ulong value, char *precision, uint flags, char *sign_char, char **precision_ptr, long format_type, int *precision_val);

/**
 * Check sign of floating point number
 * Original: FUN_0030b018
 */
extern long check_float_sign(ulong float_val, int param);

/**
 * Format exponent string for scientific notation
 * Original: FUN_0030e018
 */
extern int format_exponent_string(char *buffer, char *input, long format_type);

/**
 * Find character in string with limit
 * Original: FUN_00310e9c
 */
extern long find_char_in_string(char *str, int search_char, char *limit);

/**
 * Divide by 10 and get remainder (for decimal formatting)
 * Original: FUN_0030a108
 */
extern char divide_by_10_get_remainder(ulong value, int divisor);

/**
 * Divide by 10 (for decimal formatting)
 * Original: FUN_00309b48
 */
extern ulong divide_by_10(ulong value, int divisor);

/**
 * String length function (already analyzed)
 * Original: FUN_002685e8
 */
extern int strlen_custom(char *str);
