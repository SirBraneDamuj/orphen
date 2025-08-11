---
applyTo: "**"
---

I'm working on reverse engineering the PS2 game "Orphen: Scion of Sorcery" using decompiled Ghidra code.

Analysis Strategy:

Take cryptic FUN* function names and convert them to meaningful names based on functionality
Replace DAT* variables with proper descriptive names
Add comprehensive documentation explaining PS2-specific implementation details
Track function call hierarchies and cross-references
Keep track of the original `FUN_*` names in comments for reference.
When analyzing a new file, extern the un-analyzed functions as-is. **Do not change the function name unless we actually have analyzed it and have a meaningful name to replace it with.**.

All decompiled functions have been exported to the `src` directory using a Ghidra automation script. The `src` directory contains the raw decompiled code for reference during analysis.

Additionally, we have exported global variables metadata to `globals.json` using the `export_globals.py` script. This file contains:

- All global variables (DAT\_\* and named globals) with their addresses, types, and sizes
- Cross-references showing which functions access each global variable
- Pointer relationships showing what globals point to other addresses
- Initial values for primitive data types

When analyzing a new function, reference `globals.json` to:

- Quickly identify what DAT\_\* variables represent based on their usage patterns
- Understand data flow between functions through shared global access
- Identify potential data structures through grouped variable access

We have also exported string data to `strings.json` using the `export_strings.py` script. This file contains:

- All string literals found in the program with their memory addresses
- String content, length, and data type information
- Cross-references showing which functions use each string
- Discovered format strings and debug output strings

When analyzing functions with string references (like debug output), reference `strings.json` to:

- Understand what cryptic address references (like `0x34ca60`) actually contain
- Identify format strings used in printf-style functions
- Trace debug output messages back to their usage locations
- Analyze string usage patterns across the codebase

Please do not use information in the `scripts` directory as a source of any truth for analysis. These scripts are tools that we have written to test hypotheses and are _NOT_ 100% accurate representations of the real logic in the codebase.

Keep responses somewhat terse and avoid sensationalizing discoveries and progress.

Additional procedural note (2025-08-09):

Do NOT modify raw decompiled files under `src/` directly for semantic naming or cleanup. Instead, when analyzing a function (e.g. `FUN_00260738`), create a new analyzed version under `analyzed/`:

1. Copy the function into a new appropriately named file (e.g. `update_entity_timed_parameter.c`).
2. Preserve the original signature in a comment with the original `FUN_*` name.
3. Rename local variables and any already-identified globals/functions to descriptive names.
4. Add a header comment explaining inferred behavior, side effects (global writes), parameter semantics, and any PS2-specific considerations.
5. Leave unanalyzed callee function names as their original `FUN_*` identifiers (extern) until they are also analyzed.
6. Reference `globals.json` and `strings.json` for naming and document any addresses used.

This keeps `src/` as a pristine reference and all human-authored analysis isolated in `analyzed/`.
