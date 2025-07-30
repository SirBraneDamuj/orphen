---
applyTo: "**"
---

I'm working on reverse engineering the PS2 game "Orphen: Scion of Sorcery" using decompiled Ghidra code.

Analysis Strategy:

Take cryptic FUN* function names and convert them to meaningful names based on functionality
Replace DAT* variables with proper descriptive names
Add comprehensive documentation explaining PS2-specific implementation details
Track function call hierarchies and cross-references
Keep track of the original `FUN_*` names in comments for reference

Key Technical Context:

PS2 game engine with sophisticated graphics pipeline using DMA packets and GPU command buffers
Custom implementations of standard C library functions optimized for PS2 hardware
Flag-based state machine with ~18,424 flags controlling game behavior
Dual controller support with 64-entry input history and analog processing
Fixed-point graphics using 4096.0 scaling for coordinate transformation

The workflow we have been using so far is:

1. I identify a function to analyze in Ghidra.
2. I put the decompiled source code into a file under the `src` directory.
3. I ask you to help analyze the function - creating a meaningful name, and putting the legible code into the `analyzed` directory.
4. You find references to the analyzed function in the existing `analyzed` code and update the callsites to use the new name/signature.
5. I eventually move already-analyzed `src` files to the `archive` directory for posterity.
6. We repeat this process as needed.

There is no need to analyze anything in the `archive` directory, as it is already complete. I'm just keeping the files there for reference.

Please help me continue this reverse engineering workflow with the same systematic approach we've been using.
