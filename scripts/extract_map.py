import os
import struct
import sys

# Path to the MAP.BIN file
MAP_BIN_PATH = "MAP.BIN"
# Standard sector size for PS2 discs
SECTOR_SIZE = 2048 # 0x800 bytes
# Directory to save extracted maps
OUTPUT_DIR = "extracted_maps"

def extract_map_by_id(map_id):
    """
    Extracts a single map from MAP.BIN given its ID.
    """
    if not os.path.exists(MAP_BIN_PATH):
        print(f"Error: {MAP_BIN_PATH} not found.")
        return

    with open(MAP_BIN_PATH, "rb") as f:
        # Read the header to get the total number of entries
        entry_count_bytes = f.read(4)
        entry_count = struct.unpack('<I', entry_count_bytes)[0]

        if map_id >= entry_count:
            print(f"Error: Map ID {map_id} is out of bounds. File contains {entry_count} maps (IDs 0 to {entry_count - 1}).")
            return

        # Seek to the correct entry in the lookup table
        # Table starts at 0x4, and each entry is 4 bytes
        table_entry_offset = 4 + (map_id * 4)
        f.seek(table_entry_offset)

        # Read the packed entry
        entry_bytes = f.read(4)
        raw_value = struct.unpack('<I', entry_bytes)[0]

        # Decode the entry using the logic from the game's code
        sector_offset = raw_value >> 17
        size_in_bytes = (raw_value & 0x1ffff) * 4

        # Calculate the final offset of the map data
        final_offset = sector_offset * SECTOR_SIZE

        print(f"Extracting Map ID {map_id}:")
        print(f"  -> Raw Entry: 0x{raw_value:08x}")
        print(f"  -> Sector Offset: 0x{sector_offset:04x}")
        print(f"  -> Data Size: {size_in_bytes} bytes (0x{size_in_bytes:x})")
        print(f"  -> Data Offset: 0x{final_offset:08x}")

        # Seek to the map data and read it
        f.seek(final_offset)
        map_data = f.read(size_in_bytes)

        # Create output directory if it doesn't exist
        if not os.path.exists(OUTPUT_DIR):
            os.makedirs(OUTPUT_DIR)

        # Write the extracted data to a new file
        output_filename = os.path.join(OUTPUT_DIR, f"map_{map_id:04d}.dat")
        with open(output_filename, "wb") as out_f:
            out_f.write(map_data)

        print(f"\\nSuccessfully extracted map data to '{output_filename}'")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python extract_map.py <map_id>")
        sys.exit(1)

    try:
        map_id_to_extract = int(sys.argv[1])
        extract_map_by_id(map_id_to_extract)
    except ValueError:
        print("Error: Please provide a valid integer for the map ID.")
        sys.exit(1)
