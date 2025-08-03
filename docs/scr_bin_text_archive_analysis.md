# SCR.BIN Text Archive Analysis

## Discovery Summary

SCR.BIN is **not** a cutscene script archive as initially assumed. It is the game's **text database** containing all UI text, system messages, item names, location names, and other string content.

## Key Findings

### Archive Structure
- **227 text entries** total
- **21,269 characters** of text content
- **4,491 individual strings**
- Archive size: 1,290,240 bytes
- Uses packed 32-bit lookup table (17-bit offset + 15-bit size)

### Content Categories

1. **UI Text (26 entries)**
   - Menu options: "Load", "Save", "Options"
   - Dialog prompts: "Overwrite?", "YesNo"
   - Interface text: "to view the Diary, Movie or Monster Picture Book"

2. **System Messages (3 entries)**
   - Memory card operations: "Please insert into MEMORY CARD slot 1"
   - Error messages: "Not enough free space on"
   - Status messages: "created?", "fail"

3. **Game Content (49 entries)**
   - Item names: "Sword", "Armor", "Emerald Incense"
   - Game mechanics: "HP was added", "recover 10HP"
   - Magic/skills: spell names and effects

4. **Location Names (23 entries)**
   - Areas: "Watercourse Labyrinth", "Limestone Cavern"
   - Character names: "Zeus", "Keith", "Coggie", "Cleo", "Magnus"
   - Story locations: "Mountain Summit", "Spell Hill"

5. **Numeric Data (4 entries)**
   - Level/area numbers: "614", "531", "712"
   - Sequential data: "01234567"

6. **Binary Data (57 entries)**
   - Non-text data mixed within the archive
   - May include formatting codes or other metadata

## Analysis Tools

### `text_archive_extractor.py`
The only relevant tool for SCR.BIN analysis. Properly extracts and categorizes the text content.

**Features:**
- Accurate text extraction using correct archive format
- Content categorization by type
- String analysis with positioning and termination info
- Statistics on text density and distribution

**Usage:**
```bash
python text_archive_extractor.py
```

**Output:** `text_archive_analysis.json` with complete text catalog

## Implications for Reverse Engineering

### What This Means for Cutscene Skip Development
- **SCR.BIN is irrelevant** for cutscene control
- Cutscene scripts are located elsewhere in the game files
- Previous timing analysis was completely incorrect

### Where to Look for Actual Cutscene Scripts
1. **Other .BIN archives**: Check STR.BIN, MOV.BIN, or similar files
2. **Executable code**: Timing might be hardcoded in the main executable
3. **Different file formats**: Scripts might use proprietary bytecode formats
4. **Audio/Video files**: Cutscene timing might be tied to media playback

### Lessons Learned
- **File naming can be misleading**: "SCR" suggested "Script" but meant something else
- **Always verify assumptions**: Extremely long timing values were a red flag
- **Look for readable content**: Text archives often contain obvious string patterns
- **Consider file purpose**: Games separate data types (text vs. code vs. media)

## Next Steps for Cutscene Research

1. **Examine other .BIN files** in the disc image
2. **Analyze the main executable** for hardcoded timing values
3. **Study audio/video files** for cutscene duration information
4. **Look for debug information** that might reference cutscene control
5. **Research PS2 game development patterns** for common cutscene implementation methods

## File Cleanup

The following files were removed as they were based on incorrect assumptions:
- All script analysis tools (`scr_bin_extractor.py`, `script_disassembler.py`, etc.)
- Timing patch scripts (`patch_scr_timing*.py`)
- Generated analysis data (extracted_scripts/, disassembled_scripts/, etc.)
- Outdated documentation (`cutscene_skip_development.md`, etc.)

Only `text_archive_extractor.py` remains as the correct tool for SCR.BIN analysis.
