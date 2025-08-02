#!/usr/bin/env python3
"""
SCR.BIN Precise Timing Patch Script
Only patches timing values that appear in specific opcode contexts
"""

import shutil
from pathlib import Path

def find_timing_contexts(data):
    """Find timing values that appear after specific opcodes"""
    contexts = []
    
    # Look for timing patterns after 0xFF opcodes
    timing_opcodes = [0xFF25, 0xFF29, 0xFFA0, 0xFFE0, 0xFFFF]
    
    for i in range(len(data) - 4):
        # Check if we're at an extended opcode
        if data[i] == 0xFF and i + 3 < len(data):
            opcode = (data[i] << 8) | data[i + 1]
            
            if opcode in timing_opcodes:
                # Check if the next 2 bytes could be a timing value
                timing_val = data[i + 2] | (data[i + 3] << 8)
                
                # Focus on the specific values we want to patch
                target_timings = {300: 30, 360: 30, 480: 60, 600: 60, 
                                720: 60, 900: 90, 1200: 120, 1800: 180}
                
                if timing_val in target_timings:
                    contexts.append({
                        'offset': i + 2,
                        'original': timing_val,
                        'replacement': target_timings[timing_val],
                        'opcode': opcode
                    })
    
    return contexts

def patch_scr_bin_precise():
    scr_path = Path("SCR.BIN")
    backup_path = Path("SCR.BIN.backup")
    
    if not scr_path.exists():
        print("SCR.BIN not found!")
        return False
    
    # Create backup
    print("Creating backup...")
    shutil.copy(scr_path, backup_path)
    
    with open(scr_path, "rb") as f:
        data = bytearray(f.read())
    
    print(f"Original file size: {len(data)} bytes")
    
    # Find timing contexts
    contexts = find_timing_contexts(data)
    print(f"Found {len(contexts)} precise timing locations to patch")
    
    if len(contexts) == 0:
        print("No timing contexts found - patch may not be applicable")
        return False
    
    # Apply patches
    patches_applied = 0
    for ctx in contexts:
        offset = ctx['offset']
        original = ctx['original']
        replacement = ctx['replacement']
        
        # Verify the value is still what we expect
        current_val = data[offset] | (data[offset + 1] << 8)
        if current_val == original:
            # Apply patch (little-endian)
            data[offset] = replacement & 0xFF
            data[offset + 1] = (replacement >> 8) & 0xFF
            patches_applied += 1
            print(f"Patched {original} -> {replacement} frames at offset 0x{offset:X}")
    
    # Write patched data
    with open(scr_path, "wb") as f:
        f.write(data)
    
    print(f"Applied {patches_applied} patches")
    print(f"Final file size: {len(data)} bytes")
    print("SCR.BIN patched with precise timing changes!")
    return True

def restore_backup():
    """Restore from backup if something went wrong"""
    scr_path = Path("SCR.BIN")
    backup_path = Path("SCR.BIN.backup")
    
    if backup_path.exists():
        shutil.copy(backup_path, scr_path)
        print("Restored SCR.BIN from backup")
        return True
    else:
        print("No backup found!")
        return False

if __name__ == "__main__":
    import sys
    
    if len(sys.argv) > 1 and sys.argv[1] == "restore":
        restore_backup()
    else:
        patch_scr_bin_precise()
