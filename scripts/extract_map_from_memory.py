#!/usr/bin/env python3
"""
Extract PSM2 map data from PCSX2 memory dump
Based on runtime debugging of load_cached_data function
"""

def extract_map_from_memory():
    # Memory dump parameters from PCSX2 debugging
    memory_dump_file = "eeMemory.bin"
    map_start_address = 0x00F752D0
    map_size_bytes = 174008  # 0x2A7B8 in decimal
    
    # Output file for extracted map data
    output_file = "extracted_map_runtime.psm2"
    
    try:
        print(f"Reading memory dump from {memory_dump_file}")
        with open(memory_dump_file, 'rb') as f:
            memory_data = f.read()
        
        print(f"Memory dump size: {len(memory_data)} bytes")
        print(f"Extracting from address: 0x{map_start_address:08X}")
        print(f"Extracting {map_size_bytes} bytes")
        
        # Extract the map data
        if map_start_address + map_size_bytes > len(memory_data):
            print(f"ERROR: Address range extends beyond memory dump!")
            print(f"Requested end: 0x{map_start_address + map_size_bytes:08X}")
            print(f"Memory dump end: 0x{len(memory_data):08X}")
            return False
        
        map_data = memory_data[map_start_address:map_start_address + map_size_bytes]
        
        # Verify .PSM2 magic header (starts with 0x1F)
        magic = map_data[:5]  # Check first 5 bytes for \x1fPSM2
        if magic == b'\x1fPSM2':
            print("✓ .PSM2 magic header confirmed!")
        else:
            print(f"WARNING: Expected .PSM2 magic, got: {magic}")
            print(f"First 16 bytes: {map_data[:16].hex()}")
        
        # Write extracted data
        with open(output_file, 'wb') as f:
            f.write(map_data)
        
        print(f"Map data extracted to: {output_file}")
        print(f"File size: {len(map_data)} bytes")
        
        # Show some header info
        if len(map_data) >= 16:
            print("\nHeader analysis:")
            print(f"Magic: {map_data[0:4]}")
            print(f"Next 4 bytes: {map_data[4:8].hex()}")
            print(f"Next 4 bytes: {map_data[8:12].hex()}")
            print(f"Next 4 bytes: {map_data[12:16].hex()}")
        
        return True
        
    except FileNotFoundError:
        print(f"ERROR: Could not find {memory_dump_file}")
        return False
    except Exception as e:
        print(f"ERROR: {e}")
        return False

if __name__ == "__main__":
    success = extract_map_from_memory()
    if success:
        print("\n✓ Map extraction completed successfully!")
    else:
        print("\n✗ Map extraction failed!")
