# Cutscene Skip Development - Progress Report

## Objective

Develop a method to skip or accelerate cutscenes in Orphen: Scion of Sorcery (PS2) by modifying timing values in the game's script files.

## Initial Analysis

### File System Discovery

- **Target File**: SCR.BIN - Contains 227 script files with cutscene logic
- **Structure**: 4-byte lookup table entries (offset/size pairs) followed by script data
- **Total Size**: ~2.7MB of script content
- **Loading Functions**:
  - `FUN_00223268` - Main file loader
  - `FUN_00221c90` - SCR.BIN lookup handler

### Timing Pattern Analysis

Successfully identified consistent timing patterns across script files:

| Value (frames) | Duration (60fps) | Occurrences | Percentage |
| -------------- | ---------------- | ----------- | ---------- |
| 300            | 5.0 seconds      | 539         | 18.2%      |
| 360            | 6.0 seconds      | 1,238       | 41.8%      |
| 480            | 8.0 seconds      | 208         | 7.0%       |
| 600            | 10.0 seconds     | 299         | 10.1%      |
| 720            | 12.0 seconds     | 105         | 3.5%       |
| 900            | 15.0 seconds     | 258         | 8.7%       |
| 1200           | 20.0 seconds     | 81          | 2.7%       |
| 1800           | 30.0 seconds     | 236         | 8.0%       |

**Total**: 2,964 timing locations identified

### Script Opcode Analysis

Identified potential timing instruction opcodes with 0xFF prefix:

- **0xFF25**: 36 timing instructions
- **0xFF29**: 91 timing instructions (common)
- **0xFFA0**: 87 timing instructions (very common)
- **0xFFE0**: 41 timing instructions
- **0xFFFF**: 111 timing instructions (most common)

## Development Attempts

### Attempt 1: Broad Binary Replacement

**Approach**: Global search-and-replace of timing byte sequences throughout SCR.BIN
**Implementation**: `patch_scr_timing.py`

**Strategy**:

- Replace all occurrences of timing values (e.g., 300 frames → 30 frames)
- Applied 8 different timing patches simultaneously
- Acceleration factor: 8-12x faster

**Results**:

- ❌ **FAILED** - Caused file corruption
- Generated TLB miss errors in PCSX2
- File size discrepancies detected
- ISO became unstable

**Root Cause**:
Binary replacement was too aggressive, corrupting:

- File lookup tables (4-byte offset/size entries)
- Non-timing data that coincidentally matched timing patterns
- Critical game state variables
- File structure integrity

### Attempt 2: Context-Aware Patching

**Approach**: Only patch timing values that appear after specific opcodes
**Implementation**: `patch_scr_timing_precise.py` (incomplete)

**Strategy**:

- Identify timing values following 0xFF extended opcodes
- Validate context before applying patches
- More conservative replacement ratios

**Status**:

- ⚠️ **ABANDONED** - Design flaw identified
- Cannot verify which specific cutscenes are affected
- No way to test effectiveness of changes
- Impossible to correlate patches with gameplay

## Technical Challenges

### 1. Opaque Script Format

- No documentation of script bytecode format
- Unknown instruction set architecture
- Timing values could be parameters, not standalone instructions
- Context validation extremely difficult without format specification

### 2. Testing Methodology

- 227 script files with unknown purposes
- No mapping between script numbers and game scenes
- Cannot target specific cutscenes for testing
- Verification requires playing through entire game sections

### 3. File Integrity

- SCR.BIN contains critical lookup tables
- Binary modifications risk corrupting file structure
- PS2 file system very sensitive to size/offset changes
- Backup/restore mechanisms essential

### 4. Timing Value Ambiguity

- Same byte patterns used for timing and non-timing data
- 16-bit values could represent multiple data types
- Little-endian format complicates pattern matching
- False positives caused file corruption

## Tools Developed

### 1. `analyze_scr_bin.py`

- **Purpose**: Initial SCR.BIN structure analysis
- **Capabilities**:
  - Header parsing and file enumeration
  - Timing pattern frequency analysis
  - Opcode distribution statistics
- **Status**: ✅ Complete and functional

### 2. `extract_and_patch_scripts.py`

- **Purpose**: Extract individual scripts and generate patches
- **Capabilities**:
  - Extract 227 script files to separate directory
  - Analyze timing patterns per script
  - Generate patch candidates with statistics
- **Status**: ✅ Complete and functional

### 3. `patch_scr_timing.py`

- **Purpose**: Apply broad timing acceleration patches
- **Status**: ❌ Functional but causes corruption

### 4. `patch_scr_timing_precise.py`

- **Purpose**: Context-aware timing patches
- **Status**: ⚠️ Incomplete - design flawed

### 5. `rebuild_iso.py`

- **Purpose**: Repackage modified files into ISO format
- **Status**: ✅ Created with ImgBurn integration guidance

## Lessons Learned

### What Worked

1. **File Structure Analysis**: Successfully reverse-engineered SCR.BIN format
2. **Pattern Recognition**: Identified consistent timing patterns across scripts
3. **Tool Development**: Created robust analysis and extraction tools
4. **ISO Rebuilding**: Successfully repackaged modified files

### What Failed

1. **Blind Binary Patching**: Global replacements cause unintended corruption
2. **Context-Free Modifications**: Cannot safely identify timing vs. non-timing data
3. **Broad-Spectrum Approach**: Simultaneous patches make debugging impossible

### Critical Insights

1. **Need Script-Level Understanding**: Must identify specific script purposes
2. **Require Testable Changes**: Cannot verify effectiveness without targeted modifications
3. **File Format Documentation**: Need complete script bytecode specification
4. **Conservative Approach**: Single-script modifications safer than bulk changes

## Current Status

### Completed

- ✅ SCR.BIN structure fully analyzed
- ✅ Timing patterns comprehensively catalogued
- ✅ Extraction tools functional
- ✅ ISO rebuilding process established

### Blocked

- ❌ Safe patching methodology
- ❌ Script-to-cutscene mapping
- ❌ Bytecode format specification
- ❌ Testable modification approach

## Next Steps Recommendations

### Immediate Actions

1. **Restore from backup**: Ensure clean SCR.BIN for future attempts
2. **Script mapping**: Identify which scripts correspond to specific game scenes
3. **Single-script testing**: Modify one script file and test specific cutscene

### Research Needed

1. **Bytecode analysis**: Reverse-engineer script instruction format
2. **Game flow mapping**: Document when each script file is loaded
3. **Timing instruction identification**: Determine exact opcode meanings
4. **Alternative approaches**: Investigate memory patching vs. file modification

### Tool Development

1. **Script debugger**: Tool to trace script execution in real-time
2. **Cutscene mapper**: Associate script files with specific game scenes
3. **Surgical patcher**: Modify individual script files with verification
4. **Test automation**: Automate cutscene timing verification

## Risk Assessment

### High Risk

- File corruption leading to unplayable game
- Cascading effects from timing changes
- Save game compatibility issues

### Medium Risk

- Gameplay balance disruption
- Audio/video synchronization problems
- Unintended sequence skipping

### Low Risk

- Minor visual glitches
- Temporary performance issues
- Reversible modifications

## Conclusion

While significant progress was made in understanding SCR.BIN structure and timing patterns, the current approach of bulk binary patching proved unsuitable due to file corruption risks. Future development requires a more surgical approach with script-level understanding and targeted testing methodology.

The foundation tools and analysis are solid, but the patching strategy needs fundamental redesign to ensure file integrity and verifiable results.
