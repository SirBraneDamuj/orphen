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
When analyzing a new file, extern the un-analyzed functions as-is. Do not change the function name unless we actually have analyzed it and have a meaningful name to replace it with.

Key Technical Context:

PS2 game engine with sophisticated graphics pipeline using DMA packets and GPU command buffers
Custom implementations of standard C library functions optimized for PS2 hardware
Flag-based state machine with ~18,424 flags controlling game behavior
Dual controller support with 64-entry input history and analog processing
Fixed-point graphics using 4096.0 scaling for coordinate transformation

The workflow we have been using so far is:

1. I identify a function to analyze from the `src` directory (all 2922 decompiled functions exported via Ghidra script).
2. I ask you to help analyze the function - creating a meaningful name, and putting the legible code into the `analyzed` directory.
3. You find references to the analyzed function in the existing `analyzed` code and update the callsites to use the new name/signature.
4. We repeat this process as needed.

All decompiled functions have been exported to the `src` directory using a Ghidra automation script. The `src` directory contains the raw decompiled code for reference during analysis.

Additionally, we have exported global variables metadata to `globals.json` using the `export_globals.py` script. This file contains:

- All global variables (DAT\_\* and named globals) with their addresses, types, and sizes
- Cross-references showing which functions access each global variable
- Pointer relationships showing what globals point to other addresses
- Initial values for primitive data types

When analyzing a new function, reference `globals.json` to:

- Quickly identify what DAT\_\* variables represent based on their usage patterns
- Understand data flow between functions through shared global access
- Determine appropriate variable names based on which functions use them
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

**Important Note on File Access:**

The `src/` directory, `globals.json`, and `strings.json` are gitignored to prevent committing large amounts of generated code. However, they are essential for analysis work. The built-in search tools (grep_search, file_search) respect gitignore and cannot find files in these directories.

**When searching gitignored files, use `run_in_terminal` with direct grep commands:**

- `grep -r "pattern" src/` to search source files
- `grep "pattern" globals.json` to search global metadata
- `grep "pattern" strings.json` to search string data
- This bypasses gitignore restrictions and allows full text search

Please help me continue this reverse engineering workflow with the same systematic approach we've been using.
