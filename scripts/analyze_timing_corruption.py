#!/usr/bin/env python3
"""
Analyze SCR.BIN timing patterns to understand corruption
"""

def analyze_scr_timing():
    with open('SCR.BIN', 'rb') as f:
        data = f.read()
    
    print(f"SCR.BIN size: {len(data)} bytes")
    
    # Check if this looks like a corrupted file by examining the header
    print("\nFirst 32 bytes (should be lookup table):")
    header = data[:32]
    print(" ".join(f"{b:02X}" for b in header))
    
    # Look for our target timing values
    target_values = [300, 360, 480, 600, 720, 900, 1200, 1800]
    
    for val in target_values:
        # Little-endian representation
        bytes_le = val.to_bytes(2, 'little')
        positions = []
        start = 0
        while True:
            pos = data.find(bytes_le, start)
            if pos == -1:
                break
            positions.append(pos)
            start = pos + 1
        
        print(f"\n{val} frames (0x{bytes_le.hex().upper()}): {len(positions)} occurrences")
        
        # Show context for first few occurrences
        for i, pos in enumerate(positions[:3]):
            ctx_start = max(0, pos - 8)
            ctx_end = min(len(data), pos + 8)
            context = data[ctx_start:ctx_end]
            
            hex_str = " ".join(f"{b:02X}" for b in context)
            # Mark our target bytes
            target_start = pos - ctx_start
            marked = hex_str.replace(
                f"{bytes_le[0]:02X} {bytes_le[1]:02X}",
                f"[{bytes_le[0]:02X} {bytes_le[1]:02X}]"
            )
            print(f"  @0x{pos:06X}: {marked}")

if __name__ == "__main__":
    analyze_scr_timing()
