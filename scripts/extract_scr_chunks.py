#!/usr/bin/env python3
"""
Extract individual script chunks from SCR.BIN

The structure is:
- Chunk 0: starts at 0x00000000 (begins with 0xE3, not 0x20)
- Chunks 1-220: begin with 0x20 after null byte padding sequences
Total: 221 chunks (0-220)
"""

import os
import sys

def find_chunk_boundaries(data):
    """Find all chunk boundaries in the data"""
    # Find 0x20 bytes preceded by significant null sequences (10+ nulls)
    boundaries = []
    i = 0
    while i < len(data):
        if data[i] == 0x20:
            # Count null bytes before this position
            j = i - 1
            null_count = 0
            while j >= 0 and data[j] == 0x00:
                null_count += 1
                j -= 1
            
            if null_count >= 10:  # Significant null sequence
                boundaries.append(i)
        i += 1
    
    return boundaries

def extract_chunks_corrected(input_file, output_dir):
    """Extract all chunks to individual files with correct structure"""
    print(f"Reading {input_file}...")
    
    with open(input_file, 'rb') as f:
        data = f.read()
    
    print(f"File size: {len(data)} bytes")
    
    # Find chunk boundaries (0x20 after null sequences)
    chunk_boundaries = find_chunk_boundaries(data)
    print(f"Found {len(chunk_boundaries)} chunk boundaries with 0x20 separators")
    
    # Create output directory
    os.makedirs(output_dir, exist_ok=True)
    
    # Extract chunk 0 (from start to first boundary)
    if chunk_boundaries:
        first_boundary = chunk_boundaries[0]
        # Find where the null bytes start before first boundary
        end_pos = first_boundary
        while end_pos > 0 and data[end_pos - 1] == 0x00:
            end_pos -= 1
        
        chunk0_data = data[0:end_pos]
        output_file = os.path.join(output_dir, "scr0.bin")
        with open(output_file, 'wb') as f:
            f.write(chunk0_data)
        print(f"Extracted chunk   0: {len(chunk0_data):6d} bytes -> {output_file} (starts with 0x{chunk0_data[0]:02x})")
    
    # Extract chunks 1-N (each starting with 0x20)
    for i, start_pos in enumerate(chunk_boundaries):
        chunk_num = i + 1  # Chunk 0 was already extracted
        
        # Determine end position
        if i + 1 < len(chunk_boundaries):
            # End at the beginning of null bytes before next chunk
            end_pos = chunk_boundaries[i + 1]
            while end_pos > start_pos and data[end_pos - 1] == 0x00:
                end_pos -= 1
        else:
            # Last chunk goes to end of file, but trim trailing nulls
            end_pos = len(data)
            while end_pos > start_pos and data[end_pos - 1] == 0x00:
                end_pos -= 1
        
        # Extract chunk data
        chunk_data = data[start_pos:end_pos]
        
        # Write to output file
        output_file = os.path.join(output_dir, f"scr{chunk_num}.bin")
        with open(output_file, 'wb') as f:
            f.write(chunk_data)
        
        print(f"Extracted chunk {chunk_num:3d}: {len(chunk_data):6d} bytes -> {output_file} (starts with 0x{chunk_data[0]:02x})")
    
    total_chunks = len(chunk_boundaries) + 1  # +1 for chunk 0
    print(f"\\nExtraction complete. {total_chunks} chunks extracted to {output_dir}/")
    print(f"  - Chunk 0: starts at file beginning (0xE3...)")
    print(f"  - Chunks 1-{len(chunk_boundaries)}: start with 0x20 after null padding")

def main():
    if len(sys.argv) < 2:
        print("Usage: python extract_scr_chunks.py <input_file> [output_dir]")
        print("Example: python extract_scr_chunks.py SCR.BIN")
        print("         python extract_scr_chunks.py SCR.BIN custom_dir/")
        sys.exit(1)
    
    input_file = sys.argv[1]
    output_dir = sys.argv[2] if len(sys.argv) > 2 else "scr"
    
    if not os.path.exists(input_file):
        print(f"Error: Input file '{input_file}' not found")
        sys.exit(1)
    
    extract_chunks_corrected(input_file, output_dir)

if __name__ == "__main__":
    main()
