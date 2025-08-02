#!/usr/bin/env python3
"""
Orphen SCR.BIN Script Extractor and Timing Analyzer

This script extracts individual script files from SCR.BIN and analyzes
timing patterns that could be modified for cutscene skip functionality.
"""

import os
import sys
import struct
from pathlib import Path

# Disc extraction path
DISC_PATH = r"C:\Users\zptha\projects\orphen\orphen_ghidra\Orphen - Scion of Sorcery (USA)\Orphen - Scion of Sorcery (USA)"

def extract_script_files():
    """Extract individual script files from SCR.BIN."""
    scr_path = Path(DISC_PATH) / "SCR.BIN"
    output_dir = Path("extracted_scripts")
    output_dir.mkdir(exist_ok=True)
    
    if not scr_path.exists():
        print(f"ERROR: SCR.BIN not found at {scr_path}")
        return
    
    with open(scr_path, 'rb') as f:
        # Read file count
        file_count = struct.unpack('<I', f.read(4))[0]
        print(f"Extracting {file_count} script files...")
        
        # Read lookup table (4-byte entries)
        lookup_table = []
        for i in range(file_count):
            entry = struct.unpack('<I', f.read(4))[0]
            lookup_table.append(entry)
        
        # Calculate file sizes and offsets
        # The lookup table appears to contain packed size+offset information
        # Based on the analysis, let's try to decode the structure
        
        file_info = []
        for i, entry in enumerate(lookup_table):
            # Try different interpretations of the entry
            # Pattern 1: offset in lower 17 bits, size in upper 15 bits
            offset = (entry & 0x1FFFF) << 2  # Lower 17 bits * 4
            size = entry >> 17  # Upper 15 bits
            
            if i < 10:  # Show first 10 for debugging
                print(f"  File {i:3d}: Raw=0x{entry:08X}, Offset=0x{offset:08X}, Size={size}")
            
            file_info.append((offset, size))
        
        # Extract files
        total_extracted = 0
        for i, (offset, size) in enumerate(file_info[:50]):  # Extract first 50 files for analysis
            if offset > 0 and size > 0 and offset < scr_path.stat().st_size:
                try:
                    f.seek(offset)
                    data = f.read(size)
                    
                    if len(data) == size:
                        output_file = output_dir / f"script_{i:03d}.bin"
                        with open(output_file, 'wb') as out_f:
                            out_f.write(data)
                        
                        total_extracted += 1
                        
                        # Analyze timing patterns in this script
                        analyze_script_timing(data, i)
                        
                    else:
                        print(f"  WARNING: Could not read full file {i} (got {len(data)}, expected {size})")
                        
                except Exception as e:
                    print(f"  ERROR extracting file {i}: {e}")
                    
        print(f"\nExtracted {total_extracted} script files to {output_dir}")

def analyze_script_timing(data, script_id):
    """Analyze timing patterns in a script file."""
    timing_values = [30, 60, 90, 120, 150, 180, 240, 300, 360, 480, 600]  # Common frame counts
    
    found_timings = []
    
    for timing_val in timing_values:
        # Look for 16-bit and 32-bit little-endian values
        timing_16 = struct.pack('<H', timing_val)
        timing_32 = struct.pack('<I', timing_val)
        
        count_16 = data.count(timing_16)
        count_32 = data.count(timing_32)
        
        if count_16 > 0 or count_32 > 0:
            found_timings.append((timing_val, count_16, count_32))
    
    if found_timings:
        print(f"    Script {script_id:3d} timing patterns:")
        for timing_val, count_16, count_32 in found_timings:
            if count_16 > 0:
                print(f"      {timing_val:3d} frames (16-bit): {count_16} occurrences")
            if count_32 > 0:
                print(f"      {timing_val:3d} frames (32-bit): {count_32} occurrences")

def analyze_opcode_patterns():
    """Analyze opcode patterns to understand script structure."""
    scr_path = Path(DISC_PATH) / "SCR.BIN"
    
    with open(scr_path, 'rb') as f:
        data = f.read()
    
    print("\nAnalyzing opcode patterns for timing instructions...")
    
    # Look for sequences that might be timing instructions
    # Pattern: 0xFF followed by opcode, then timing value
    timing_opcodes = {}
    
    for i in range(len(data) - 5):
        if data[i] == 0xFF:
            opcode = data[i + 1]
            
            # Check if next 2-4 bytes look like timing values
            next_2_bytes = struct.unpack('<H', data[i+2:i+4])[0]
            if i + 6 <= len(data):
                next_4_bytes = struct.unpack('<I', data[i+2:i+6])[0]
            else:
                next_4_bytes = 0
            
            # Common timing values (30-600 frames = 0.5-10 seconds at 60 FPS)
            if 30 <= next_2_bytes <= 600:
                key = f"0xFF{opcode:02X}"
                if key not in timing_opcodes:
                    timing_opcodes[key] = []
                timing_opcodes[key].append((i, next_2_bytes, "16-bit"))
            
            if 30 <= next_4_bytes <= 600:
                key = f"0xFF{opcode:02X}"
                if key not in timing_opcodes:
                    timing_opcodes[key] = []
                timing_opcodes[key].append((i, next_4_bytes, "32-bit"))
    
    print("Potential timing instruction opcodes:")
    for opcode, occurrences in sorted(timing_opcodes.items()):
        if len(occurrences) > 5:  # Show opcodes with significant usage
            print(f"  {opcode}: {len(occurrences)} potential timing instructions")
            
            # Show distribution of timing values
            values_16 = [val for _, val, typ in occurrences if typ == "16-bit"]
            values_32 = [val for _, val, typ in occurrences if typ == "32-bit"]
            
            if values_16:
                print(f"    16-bit values: {sorted(set(values_16))[:10]}")
            if values_32:
                print(f"    32-bit values: {sorted(set(values_32))[:10]}")

def create_timing_patch_candidates():
    """Identify specific byte sequences that could be patched for faster cutscenes."""
    scr_path = Path(DISC_PATH) / "SCR.BIN"
    
    with open(scr_path, 'rb') as f:
        data = f.read()
    
    print("\nIdentifying patch candidates for timing acceleration...")
    
    # Common problematic timing values (long delays)
    long_delays = [300, 360, 480, 600, 720, 900, 1200, 1800]  # 5-30 seconds
    fast_replacements = [30, 30, 60, 60, 60, 90, 120, 180]   # 0.5-3 seconds
    
    patch_candidates = []
    
    for i, (long_delay, fast_delay) in enumerate(zip(long_delays, fast_replacements)):
        # Look for 16-bit values
        long_bytes = struct.pack('<H', long_delay)
        fast_bytes = struct.pack('<H', fast_delay)
        
        positions = []
        start = 0
        while True:
            pos = data.find(long_bytes, start)
            if pos == -1:
                break
            positions.append(pos)
            start = pos + 1
        
        if positions:
            patch_candidates.append({
                'original_value': long_delay,
                'replacement_value': fast_delay,
                'original_bytes': long_bytes.hex().upper(),
                'replacement_bytes': fast_bytes.hex().upper(),
                'positions': positions[:10],  # Show first 10 positions
                'total_count': len(positions)
            })
    
    # Output patch information
    if patch_candidates:
        print("Potential timing patches for SCR.BIN:")
        print("=" * 60)
        
        for patch in patch_candidates:
            if patch['total_count'] > 0:
                print(f"Patch: {patch['original_value']} frames → {patch['replacement_value']} frames")
                print(f"  Original bytes: {patch['original_bytes']}")
                print(f"  Replace with:   {patch['replacement_bytes']}")
                print(f"  Occurrences:    {patch['total_count']}")
                print(f"  Positions:      {', '.join(f'0x{pos:X}' for pos in patch['positions'])}")
                print()
        
        # Create a simple patch script
        patch_script_path = Path("patch_scr_timing.py")
        with open(patch_script_path, 'w', encoding='utf-8') as f:
            f.write('#!/usr/bin/env python3\n')
            f.write('"""\nSCR.BIN Timing Patch Script\n"""\n\n')
            f.write('import shutil\nfrom pathlib import Path\n\n')
            f.write('def patch_scr_bin():\n')
            f.write('    scr_path = Path("SCR.BIN")\n')
            f.write('    backup_path = Path("SCR.BIN.backup")\n')
            f.write('    \n')
            f.write('    # Create backup\n')
            f.write('    shutil.copy(scr_path, backup_path)\n')
            f.write('    \n')
            f.write('    with open(scr_path, "rb+") as f:\n')
            f.write('        data = f.read()\n')
            f.write('        \n')
            
            for patch in patch_candidates:
                if patch['total_count'] > 0:
                    f.write(f'        # Patch {patch["original_value"]} -> {patch["replacement_value"]} frames\n')
                    f.write(f'        data = data.replace(bytes.fromhex("{patch["original_bytes"]}"), ')
                    f.write(f'bytes.fromhex("{patch["replacement_bytes"]}"))\n')
                    f.write('        \n')
            
            f.write('        f.seek(0)\n')
            f.write('        f.write(data)\n')
            f.write('        f.truncate()\n')
            f.write('    \n')
            f.write('    print("SCR.BIN patched for faster cutscenes!")\n')
            f.write('\nif __name__ == "__main__":\n')
            f.write('    patch_scr_bin()\n')
        
        print(f"Generated patch script: {patch_script_path}")

def main():
    print("Orphen SCR.BIN Script Extractor and Timing Analyzer")
    print("=" * 60)
    
    # Extract and analyze script files
    extract_script_files()
    
    # Analyze opcode patterns
    analyze_opcode_patterns()
    
    # Create patch candidates
    create_timing_patch_candidates()

if __name__ == "__main__":
    main()
