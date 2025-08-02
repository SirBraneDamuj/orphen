#!/usr/bin/env python3
"""
Orphen SCR.BIN Analysis Tool

This script analyzes the SCR.BIN archive file to understand the script/cutscene
data structure and identify timing-related patterns that could be modified
for cutscene skip functionality.
"""

import os
import sys
import struct
from pathlib import Path

# Disc extraction path
DISC_PATH = r"C:\Users\zptha\projects\orphen\orphen_ghidra\Orphen - Scion of Sorcery (USA)\Orphen - Scion of Sorcery (USA)"

def find_bin_files():
    """Find all .BIN files in the disc directory."""
    disc_dir = Path(DISC_PATH)
    if not disc_dir.exists():
        print(f"ERROR: Disc directory not found: {DISC_PATH}")
        return []
    
    bin_files = list(disc_dir.glob("*.BIN"))
    print(f"Found {len(bin_files)} .BIN files:")
    for bin_file in bin_files:
        size_mb = bin_file.stat().st_size / (1024 * 1024)
        print(f"  {bin_file.name}: {size_mb:.2f} MB")
    
    return bin_files

def analyze_scr_bin():
    """Analyze SCR.BIN structure."""
    scr_path = Path(DISC_PATH) / "SCR.BIN"
    
    if not scr_path.exists():
        print(f"ERROR: SCR.BIN not found at {scr_path}")
        return
    
    file_size = scr_path.stat().st_size
    print(f"\nAnalyzing SCR.BIN ({file_size:,} bytes)...")
    
    with open(scr_path, 'rb') as f:
        # Read first 1KB to analyze header structure
        header = f.read(1024)
        
        print("\nHeader Analysis (first 64 bytes):")
        for i in range(0, min(64, len(header)), 16):
            hex_data = ' '.join(f'{b:02X}' for b in header[i:i+16])
            ascii_data = ''.join(chr(b) if 32 <= b <= 126 else '.' for b in header[i:i+16])
            print(f"  {i:04X}: {hex_data:<48} {ascii_data}")
        
        # Look for potential file table
        f.seek(0)
        
        # Try to identify file count and table structure
        # The first value (227) looks like a file count, let's examine the structure differently
        f.seek(0)
        potential_file_count = struct.unpack('<I', f.read(4))[0]
        print(f"\nPotential file count: {potential_file_count}")
        
        if potential_file_count > 0 and potential_file_count < 10000:
            print(f"Examining file table structure...")
            
            # SCR.BIN appears to use a different structure
            # Let's look for patterns in the next few bytes
            table_data = f.read(potential_file_count * 4)  # Try 4-byte entries first
            
            print("First 10 table entries (as 4-byte values):")
            for i in range(min(10, potential_file_count)):
                if i * 4 + 4 <= len(table_data):
                    value = struct.unpack('<I', table_data[i*4:(i+1)*4])[0]
                    print(f"  Entry {i:3d}: 0x{value:08X} ({value:8d})")
            
            # Maybe it's a lookup table like we saw in the code: FUN_00221c90
            # That function does: return *(undefined4 *)(param_1 * 4 + iGpffffbc38);
            # So it's an array of 4-byte values (offsets or sizes)
        
        # Look for script instruction patterns
        print("\nSearching for script patterns...")
        f.seek(0)
        data = f.read()
        
        # Look for common script instruction opcodes
        opcodes = {}
        for i in range(len(data) - 1):
            if data[i] == 0x04:  # Opcode 4 (yield instruction identified in analysis)
                opcodes.setdefault(0x04, []).append(i)
            elif data[i] == 0xFF and i < len(data) - 2:  # Extended instructions (0xFF xx)
                ext_opcode = data[i+1]
                opcodes.setdefault(f'0xFF{ext_opcode:02X}', []).append(i)
        
        print("Common instruction patterns found:")
        for opcode in sorted(opcodes.keys(), key=lambda x: x if isinstance(x, int) else 0):
            positions = opcodes[opcode]
            if len(positions) > 5:  # Only show opcodes that appear frequently
                print(f"  {opcode}: {len(positions)} occurrences")
                if len(positions) <= 20:
                    print(f"    Positions: {', '.join(f'0x{pos:X}' for pos in positions[:10])}")
        
        # Look for timing patterns (60 frames = 1 second at 60 FPS)
        print("\nSearching for timing patterns...")
        timing_values = [60, 120, 180, 240, 300]  # 1-5 seconds at 60 FPS
        
        for timing_val in timing_values:
            # Search for little-endian 16-bit and 32-bit values
            timing_16 = struct.pack('<H', timing_val)
            timing_32 = struct.pack('<I', timing_val)
            
            pos_16 = data.find(timing_16)
            pos_32 = data.find(timing_32)
            
            if pos_16 != -1:
                count_16 = data.count(timing_16)
                print(f"  {timing_val} frames (16-bit): {count_16} occurrences, first at 0x{pos_16:X}")
            
            if pos_32 != -1:
                count_32 = data.count(timing_32)
                print(f"  {timing_val} frames (32-bit): {count_32} occurrences, first at 0x{pos_32:X}")

def analyze_archive_structure(bin_path):
    """Generic archive structure analysis."""
    print(f"\nAnalyzing {bin_path.name}...")
    
    with open(bin_path, 'rb') as f:
        # Try different potential header structures
        f.seek(0)
        
        # Pattern 1: File count + table of 8-byte entries (offset, size)
        file_count = struct.unpack('<I', f.read(4))[0]
        
        if 1 <= file_count <= 1000:  # Reasonable file count
            print(f"  Potential archive with {file_count} files")
            
            entries = []
            for i in range(min(file_count, 10)):
                entry_data = f.read(8)
                if len(entry_data) == 8:
                    offset, size = struct.unpack('<II', entry_data)
                    entries.append((offset, size))
                    print(f"    File {i}: offset=0x{offset:X}, size={size}")
                    
                    # Basic validation
                    if offset > bin_path.stat().st_size or size > bin_path.stat().st_size:
                        print(f"      WARNING: Invalid entry")
                        break
            
            # If we found valid entries, try to extract first file for analysis
            if entries and entries[0][0] > 0 and entries[0][1] > 0:
                first_offset, first_size = entries[0]
                f.seek(first_offset)
                first_file_data = f.read(min(first_size, 256))
                
                print(f"    First file preview (offset 0x{first_offset:X}):")
                for i in range(0, len(first_file_data), 16):
                    hex_data = ' '.join(f'{b:02X}' for b in first_file_data[i:i+16])
                    ascii_data = ''.join(chr(b) if 32 <= b <= 126 else '.' for b in first_file_data[i:i+16])
                    print(f"      {first_offset + i:04X}: {hex_data:<48} {ascii_data}")

def main():
    print("Orphen SCR.BIN Analysis Tool")
    print("=" * 50)
    
    # Find all BIN files
    bin_files = find_bin_files()
    
    if not bin_files:
        return
    
    # Focus on SCR.BIN for cutscene analysis
    scr_bin = None
    for bin_file in bin_files:
        if bin_file.name.upper() == "SCR.BIN":
            scr_bin = bin_file
            break
    
    if scr_bin:
        analyze_scr_bin()
    else:
        print("SCR.BIN not found!")
    
    # Also analyze other archives for context
    print("\n" + "=" * 50)
    print("Analyzing other archive structures...")
    
    for bin_file in bin_files:
        if bin_file.name.upper() != "SCR.BIN":
            analyze_archive_structure(bin_file)

if __name__ == "__main__":
    main()
