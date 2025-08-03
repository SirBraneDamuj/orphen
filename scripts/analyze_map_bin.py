import os
import struct

# Path to the MAP.BIN file
MAP_BIN_PATH = "MAP.BIN"
# Number of lookup entries to inspect
NUM_ENTRIES_TO_INSPECT = 20 # Increased to see more entries
# Standard sector size for PS2 discs
SECTOR_SIZE = 2048 # 0x800 bytes

def analyze_map_bin_structure():
    """
    Analyzes the structure of MAP.BIN based on the logic from FUN_00223268.
    - First 4 bytes: Number of entries in the lookup table.
    - Table starts at offset 0x4.
    - Each entry is a 32-bit packed value:
      - Bits 31-17 (15 bits): Sector offset.
      - Bits 16-0 (17 bits): Size in 4-byte words.
    """
    if not os.path.exists(MAP_BIN_PATH):
        print(f"Error: {MAP_BIN_PATH} not found.")
        return

    file_size = os.path.getsize(MAP_BIN_PATH)
    print(f"Analyzing {MAP_BIN_PATH} (size: 0x{file_size:08x})...")

    with open(MAP_BIN_PATH, "rb") as f:
        # Hypothesis: The first 4 bytes are the number of entries.
        entry_count_bytes = f.read(4)
        entry_count = struct.unpack('<I', entry_count_bytes)[0]
        
        print(f"Reading first 4 bytes as entry count: {entry_count} (0x{entry_count:x})")

        # The table should start immediately after the count.
        table_offset = 0x4
        f.seek(table_offset)
        
        print(f"\\nReading lookup table from file offset 0x{table_offset:x}...")
        print("Decoding based on FUN_00223268: 15-bit sector offset, 17-bit size.")

        last_end_offset = 0
        for i in range(min(NUM_ENTRIES_TO_INSPECT, entry_count)):
            entry_bytes = f.read(4)
            if not entry_bytes:
                print("End of file reached unexpectedly.")
                break
            
            raw_value = struct.unpack('<I', entry_bytes)[0]
            
            # --- CORRECT DECODING LOGIC FROM C CODE ---
            sector_offset = raw_value >> 17
            size_in_bytes = (raw_value & 0x1ffff) * 4
            # --- END CORRECT LOGIC ---

            # Calculate the final byte offset and end offset
            final_offset = sector_offset * SECTOR_SIZE
            end_offset = final_offset + size_in_bytes

            print(f"Entry {i:03d}: Raw=0x{raw_value:08x} -> Sector=0x{sector_offset:04x}, Size=0x{size_in_bytes:08x}")
            print(f"         Offset=0x{final_offset:08x}, End=0x{end_offset:08x}")

            if final_offset < last_end_offset and final_offset != 0:
                print("  -> WARNING: This entry's offset is before the previous entry's end.")
            
            if end_offset > file_size and final_offset != 0:
                 print(f"  -> WARNING: Map data (up to 0x{end_offset:x}) exceeds file size (0x{file_size:x}).")
            
            last_end_offset = end_offset

if __name__ == "__main__":
    analyze_map_bin_structure()
