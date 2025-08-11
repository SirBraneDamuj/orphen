/*
 * Reverse engineered formatted output helper (subset of printf) from original function FUN_002f6e60.
 *
 * Original signature (decompiled):
 *   void FUN_002f6e60(byte *param_1, float *param_2);
 *
 * Purpose / Behavior:
 *   Implements a minimal printf-style formatter used by the game's debug / text output system.
 *   It parses a format string pointed to by 'fmt' (param_1) and consumes an argument list
 *   located at 'arg_list' (param_2). Output characters are emitted one-by-one via the function
 *   pointer PTR_FUN_0034a2cc (likely a low-level putc / debug text enqueue routine).
 *
 * Supported conversion specifiers (after '%') mapped by observation of switch decoding:
 *   %c   (case for character 'c')
 *   %d   signed decimal (with optional length modifiers 'h' or 'l')
 *   %u   unsigned decimal (length modifiers honored similarly)
 *   %o   unsigned octal
 *   %x   unsigned hexadecimal (lowercase hex digits; adds 0..9 then 'a'+)
 *   %e / %f  floating point (delegated to FUN_0030beb0 + FUN_002f6cc0); if value == 0 prints '0'
 *   %s   C-string (prints "(null)" if pointer refers to empty string "")
 *
 * Length modifiers:
 *   %h  sets length modifier to 'h' (treat integral argument as 16-bit)
 *   %l  sets length modifier to 'l' (treat integral argument as 64-bit) (only sign handling observed)
 *   These modifiers persist (not reset) until another modifier is encountered, mirroring the original code's behavior.
 *
 * Zero padding / width:
 *   A leading sequence like %0NNd where NN up to two decimal digits (capped at 31 / 0x1F) produces leading zeroes.
 *   Implementation: builds a local stack buffer of '0' characters and chooses the earlier pointer between generated
 *   zero padding start and the generated number digits to enforce minimum width.
 *   Only zero padding supported (no space padding, justification, plus sign, alternate form, etc.).
 *
 * Argument list handling:
 *   The second parameter is decompiled as float* but is used as a pointer into a homogeneous array of 64-bit slots.
 *   Every consumed argument advances arg_list by +2 (i.e., 8 bytes) regardless of the nominal type width. This aligns
 *   with typical PS2 / EE ABI stack / vararg slotting (8-byte alignment). Narrow types (short / int) are read from the
 *   lower portion. Pointer arguments (for %s / %c) are read from the slot reinterpreted as an integer/pointer.
 *
 * Output emission:
 *   All emitted characters go through (*PTR_FUN_0034a2cc)(ch). If a negative integral argument is detected for %d, a
 *   minus sign is first emitted using this callback.
 *
 * Internal helpers (unresolved names retained):
 *   FUN_00308c60(u64 value, int base)      -> divides value by base, returning quotient (loop for decimal %d path)
 *   FUN_003094b8(u64 value, int base)      -> returns current digit (remainder) for decimal loop (signed path)
 *   FUN_0030a108(u64, int) / FUN_00309b48  -> similar pair used in unsigned decimal generation path
 *   FUN_0030beb0() + FUN_002f6cc0(value)   -> formatting / emission of floating point numbers (non-zero case)
 *
 * Distinguishing characteristics vs standard printf:
 *   - Width limited to 31, only zero padding, no precision field except implicit for floats via helper.
 *   - No support observed for '+' ' ' '#' flags, field alignment, precision '.', or length modifiers beyond h/l.
 *   - Minimal safety: expects valid format; unrecognized specifiers fall through and are skipped.
 *   - '(null)' literal only printed when %s argument points to an empty string (length 0) rather than NULL pointer.
 *
 * Side Effects / Globals:
 *   - Calls through PTR_FUN_0034a2cc for each character emitted (global function pointer at address 0x0034a2cc).
 *   - Uses several external helper functions (see externs below) for numeric conversions and float formatting.
 *
 * PS2 / EE considerations:
 *   - 8-byte stepping of arg_list matches alignment/padding on the Emotion Engine for variadic argument retrieval.
 *   - Using integer operations on 'float*' is an artifact of decompilation; real prototype likely resembles
 *       void game_vprintf(const char *fmt, const void *arg_slots);
 *   - Small fixed stack buffer (acStack_62 in original) reused for number assembly + optional zero padding prefix.
 *
 * Retained original name mapping:
 *   FUN_002f6e60 -> game_printf_minimal (this file)
 *
 * Unanalyzed helper and function pointer names kept as extern declarations for clarity and future renaming.
 */

#include <stddef.h>
#include <stdint.h>

/* Original externs (unresolved names kept) */
extern void (*PTR_FUN_0034a2cc)(int ch); /* character output sink */
extern unsigned long FUN_00308c60(unsigned long value, int base);
extern char FUN_003094b8(unsigned long value, int base);
extern char FUN_0030a108(unsigned long value, int base);
extern unsigned long FUN_00309b48(unsigned long value, int base);
extern unsigned long FUN_0030beb0(void);     /* float formatting prep */
extern void FUN_002f6cc0(unsigned long ctx); /* float formatting emit */

/* Wrapper name for analyzed clarity */
void game_printf_minimal(const unsigned char *fmt, const unsigned long *arg_slots)
{
  /* length_modifier: 0 (none), 'h' (0x68), or 'l' (0x6c) per original */
  long length_modifier = 0;
  const unsigned char *cursor = fmt;
  /* Zero padding buffer start pointer (pcVar8 in original) */
  char *zero_pad_start = NULL;
  char number_buf[2]; /* original acStack_62 used [2]; we repurpose differently per branch */

  /* We replicate original control flow structure; a compact reimplementation is safer than micro-editing decompiled code. */
  while (1)
  {
    unsigned char ch = *cursor;
    if (ch == '\0')
    {
      return; /* End format */
    }
    if (ch != '%')
    {
      /* Emit literal and advance */
      PTR_FUN_0034a2cc(ch);
      cursor++;
      continue;
    }
    /* Process a conversion sequence. Step past '%' */
    cursor++;
    zero_pad_start = NULL;
    /* Parse optional zero + width digits (only zero padding supported) */
    if (*cursor == '0')
    {
      /* width parsing: up to two digits, capped at 31 */
      unsigned width = 0;
      const unsigned char *wcur = cursor + 1;
      if (*wcur >= '0' && *wcur <= '9')
      {
        width = (unsigned)(*wcur - '0');
        wcur++;
        if (*wcur >= '0' && *wcur <= '9')
        {
          width = width * 10 + (unsigned)(*wcur - '0');
          wcur++;
        }
        if (width > 31)
          width = 31;
        /* Prepare zero padding buffer inside number_buf logic.
           Original code used acStack_62 as a backing region; here we will allocate a local array of size 32. */
        static char pad_buf[32];
        for (unsigned i = 0; i < width; ++i)
          pad_buf[i] = '0';
        zero_pad_start = pad_buf + (32 - width); /* choose tail region similar to original pointer arithmetic */
        cursor = wcur;                           /* advance past digits */
      }
    }
    unsigned char spec = *cursor;
    if (spec == '\0')
      return; /* malformed trailing '%' */

    switch (spec)
    {
    case 'h':
      length_modifier = 'h';
      cursor++;
      continue; /* persistent like original */
    case 'l':
      length_modifier = 'l';
      cursor++;
      continue; /* persistent */
    case 'd':
    {
      unsigned long raw;
      if (length_modifier == 'l')
        raw = *arg_slots;
      else if (length_modifier == 'h')
        raw = (unsigned long)(short)(*arg_slots & 0xFFFF);
      else
        raw = (unsigned long)(int)*arg_slots;
      arg_slots += 2; /* advance one slot (8 bytes) */
      int negative = (long)raw < 0;
      unsigned long val = negative ? (unsigned long)(-(long)raw) : raw;
      char buf[32];
      char *p = buf + sizeof(buf);
      *--p = '\0';
      if (val == 0)
        *--p = '0';
      else
      {
        while (val)
        {
          unsigned long q = FUN_00308c60(val, 10); /* quotient */
          char digit = FUN_003094b8(val, 10);      /* remainder */
          *--p = (char)('0' + digit);
          val = q;
        }
      }
      if (negative)
        PTR_FUN_0034a2cc('-');
      if (zero_pad_start && zero_pad_start < p)
        p = zero_pad_start; /* enforce width */
      for (; *p; ++p)
        PTR_FUN_0034a2cc(*p);
      cursor++;
      break;
    }
    case 'u':
    {
      unsigned long raw;
      if (length_modifier == 'l')
        raw = *arg_slots;
      else if (length_modifier == 'h')
        raw = (unsigned long)(unsigned short)(*arg_slots & 0xFFFF);
      else
        raw = (unsigned long)(unsigned int)*arg_slots;
      arg_slots += 2;
      char buf[32];
      char *p = buf + sizeof(buf);
      *--p = '\0';
      if (raw == 0)
        *--p = '0';
      else
      {
        while (raw)
        {
          char digit = FUN_0030a108(raw, 10); /* remainder */
          *--p = (char)('0' + digit);
          raw = FUN_00309b48(raw, 10); /* quotient */
        }
      }
      if (zero_pad_start && zero_pad_start < p)
        p = zero_pad_start;
      for (; *p; ++p)
        PTR_FUN_0034a2cc(*p);
      cursor++;
      break;
    }
    case 'o':
    { /* octal */
      unsigned long raw;
      if (length_modifier == 'l')
        raw = *arg_slots;
      else if (length_modifier == 'h')
        raw = (unsigned long)(unsigned short)(*arg_slots & 0xFFFF);
      else
        raw = (unsigned long)(unsigned int)*arg_slots;
      arg_slots += 2;
      char buf[32];
      char *p = buf + sizeof(buf);
      *--p = '\0';
      if (raw == 0)
        *--p = '0';
      else
      {
        while (raw)
        {
          *--p = (char)('0' + (raw & 7));
          raw >>= 3;
        }
      }
      if (zero_pad_start && zero_pad_start < p)
        p = zero_pad_start;
      for (; *p; ++p)
        PTR_FUN_0034a2cc(*p);
      cursor++;
      break;
    }
    case 'x':
    { /* hexadecimal lowercase */
      unsigned long raw;
      if (length_modifier == 'l')
        raw = *arg_slots;
      else if (length_modifier == 'h')
        raw = (unsigned long)(unsigned short)(*arg_slots & 0xFFFF);
      else
        raw = (unsigned long)(unsigned int)*arg_slots;
      arg_slots += 2;
      char buf[34];
      char *p = buf + sizeof(buf);
      *--p = '\0';
      if (raw == 0)
        *--p = '0';
      else
      {
        while (raw)
        {
          unsigned v = (unsigned)(raw & 0xF);
          *--p = (char)(v < 10 ? '0' + v : 'a' + (v - 10));
          raw >>= 4;
        }
      }
      if (zero_pad_start && zero_pad_start < p)
        p = zero_pad_start;
      for (; *p; ++p)
        PTR_FUN_0034a2cc(*p);
      cursor++;
      break;
    }
    case 'c':
    {
      unsigned long v = *arg_slots;
      arg_slots += 2;
      PTR_FUN_0034a2cc((int)(v & 0xFF));
      cursor++;
      break;
    }
    case 's':
    {
      const char *str = (const char *)(uintptr_t)(*arg_slots);
      arg_slots += 2;
      if (!str || *str == '\0')
      {
        const char *nullLit = "(null)";
        while (*nullLit)
          PTR_FUN_0034a2cc(*nullLit++);
      }
      else
      {
        while (*str)
          PTR_FUN_0034a2cc(*str++);
      }
      cursor++;
      break;
    }
    case 'e': /* falls through intentionally */
    case 'f':
    {
      float fv = *(float *)arg_slots; /* low 32 bits of slot */
      if (fv == 0.0f)
      {
        PTR_FUN_0034a2cc('0');
      }
      else
      {
        unsigned long ctx = FUN_0030beb0();
        FUN_002f6cc0(ctx);
      }
      arg_slots += 2;
      cursor++;
      break;
    }
    default: /* Unrecognized specifier: emit it literally like original skip logic */
      PTR_FUN_0034a2cc(spec);
      cursor++;
      break;
    }
  }
}

/* Retain original for traceability (commented, not compiled):
// void FUN_002f6e60(byte *param_1, float *param_2) { ... }
*/
