import os
import struct
import sys

def parse_map_data(map_filepath):
    """
    Parses an extracted map file, including the main header and the
    model chunk descriptor table. Based on analysis of game code.
    """
    if not os.path.exists(map_filepath):
        print(f"Error: File not found at '{map_filepath}'")
        return

    print(f"--- Analyzing Map File: {os.path.basename(map_filepath)} ---")

    with open(map_filepath, "rb") as f:
        map_data = f.read()

    # --- Main Header Analysis ---
    header_size = 32
    if len(map_data) < header_size:
        print("Error: Map file is too small for a valid header.")
        return
    header_fields = struct.unpack('<8I', map_data[:header_size])
    
    print("\n--- Main Header Analysis ---")
    magic_number_raw = header_fields[0]
    magic_number_ascii = struct.pack('<I', magic_number_raw).decode('ascii', errors='replace')
    print(f"  -> Magic Number: 0x{magic_number_raw:08x} ('{magic_number_ascii}')")

    packed_field_1 = header_fields[1]
    model_chunk_count = (packed_field_1 & 0x000000FF)
    model_block_offset = (packed_field_1 & 0xFFFF0000) >> 16
    
    print(f"  -> Model Chunk Count: {model_chunk_count}")
    
    model_data_start = header_size + model_block_offset
    print(f"  -> Model Block Start Offset: 0x{model_data_start:x}")

    # --- Model Chunk Descriptor Table Analysis ---
    print("\n--- Model Chunk Descriptor Table ---")
    print(f"Reading {model_chunk_count} model descriptors from offset 0x{model_data_start:x}...")

    num_descriptors_to_show = 5 # Show the first few descriptors
    for i in range(min(num_descriptors_to_show, model_chunk_count)):
        descriptor_offset = model_data_start + (i * 16)
        if len(map_data) < descriptor_offset + 16:
            print("Warning: Reached end of file while reading model descriptors.")
            break
        
        desc_data = struct.unpack('<4I', map_data[descriptor_offset : descriptor_offset + 16])
        
        vertex_ptr = desc_data[0]
        texture_ptr = desc_data[1]
        packed_counts = desc_data[2]
        chunk_type = desc_data[3]

        # Unpack the counts from the third field
        vertex_count = (packed_counts & 0x000000FF)
        poly_count = (packed_counts & 0x0000FF00) >> 8
        
        print(f"\n  -> Descriptor {i:02d} (at 0x{descriptor_offset:x}):")
        print(f"     - Vertex Data Ptr:   0x{vertex_ptr:08x}")
        print(f"     - Texture Data Ptr:  0x{texture_ptr:08x}")
        print(f"     - Vertex Count:      {vertex_count}")
        print(f"     - Polygon Count:     {poly_count}")
        print(f"     - Chunk Type:        0x{chunk_type:08x}")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"Usage: python {sys.argv[0]} <path_to_map_file>")
        sys.exit(1)

    map_file = sys.argv[1]
    parse_map_data(map_file)
