#!/usr/bin/env python3
"""
Conservative SCR.BIN Timing Patch
Only patches timing values that appear to be standalone timing parameters
"""

import shutil
from pathlib import Path

def find_safe_timing_patches(data):
    """Find timing values that are likely safe to patch based on context"""
    patches = []
    
    # Define our target timing replacements
    timing_map = {
        300: 60,   # 5s -> 1s (more conservative)
        360: 60,   # 6s -> 1s  
        480: 60,   # 8s -> 1s
        600: 60,   # 10s -> 1s
        720: 60,   # 12s -> 1s
        900: 90,   # 15s -> 1.5s
        1200: 120, # 20s -> 2s
        1800: 180  # 30s -> 3s
    }
    
    for target_val, replacement in timing_map.items():
        target_bytes = target_val.to_bytes(2, 'little')
        replacement_bytes = replacement.to_bytes(2, 'little')
        
        # Find all occurrences
        pos = 0
        count = 0
        while pos < len(data) - 1:
            pos = data.find(target_bytes, pos)
            if pos == -1:
                break
            
            # Basic safety checks
            safe_to_patch = True
            
            # Don't patch if we're in the first 1KB (likely headers/lookup table)
            if pos < 1024:
                safe_to_patch = False
            
            # Don't patch if this looks like it might be an offset/address
            # (values that are too close to file boundaries or common patterns)
            if pos + target_val < len(data) and pos + target_val > len(data) - 1000:
                safe_to_patch = False
            
            # Look for patterns that suggest this is actually timing
            # Many timing values appear after certain byte patterns
            if pos >= 2:
                prev_bytes = data[pos-2:pos]
                # Common patterns before timing values in scripts
                timing_prefixes = [
                    b'\\x02\\x11',  # Common script pattern
                    b'\\x01\\x02',  # Another common pattern
                    b'\\x03\\x11',  # Another timing context
                ]
                
                # This is more likely a timing value if it follows these patterns
                if any(prev_bytes == prefix for prefix in timing_prefixes):
                    safe_to_patch = True
                # Be more conservative otherwise
                elif target_val > 600:  # Only patch longer delays by default
                    safe_to_patch = True
                else:
                    safe_to_patch = False
            
            if safe_to_patch:
                patches.append({
                    'offset': pos,
                    'original_val': target_val,
                    'replacement_val': replacement,
                    'original_bytes': target_bytes,
                    'replacement_bytes': replacement_bytes
                })
                count += 1
            
            pos += 1
        
        print(f"Found {count} safe locations to patch {target_val} -> {replacement} frames")
    
    return patches

def apply_conservative_patch():
    scr_path = Path("SCR.BIN")
    backup_path = Path("SCR.BIN.backup")
    
    if not scr_path.exists():
        print("SCR.BIN not found!")
        return False
    
    # Create backup
    print("Creating backup...")
    if backup_path.exists():
        backup_path.unlink()
    shutil.copy(scr_path, backup_path)
    
    with open(scr_path, "rb") as f:
        data = bytearray(f.read())
    
    original_size = len(data)
    print(f"Original file size: {original_size} bytes")
    
    # Find safe patches
    patches = find_safe_timing_patches(data)
    
    if not patches:
        print("No safe timing locations found to patch")
        return False
    
    # Group patches by timing value for reporting
    patch_counts = {}
    for patch in patches:
        key = f"{patch['original_val']} -> {patch['replacement_val']}"
        patch_counts[key] = patch_counts.get(key, 0) + 1
    
    print("\\nPatch summary:")
    for change, count in patch_counts.items():
        print(f"  {change} frames: {count} locations")
    
    # Apply patches (in reverse order to avoid offset issues)
    patches.sort(key=lambda x: x['offset'], reverse=True)
    applied = 0
    
    for patch in patches:
        offset = patch['offset']
        original_bytes = patch['original_bytes']
        replacement_bytes = patch['replacement_bytes']
        
        # Verify the bytes are still what we expect
        if data[offset:offset+2] == original_bytes:
            data[offset:offset+2] = replacement_bytes
            applied += 1
    
    # Write the patched file
    with open(scr_path, "wb") as f:
        f.write(data)
    
    final_size = len(data)
    print(f"\\nApplied {applied} patches")
    print(f"Final file size: {final_size} bytes (change: {final_size - original_size})")
    
    if final_size != original_size:
        print("WARNING: File size changed! This shouldn't happen with this patch method.")
        return False
    
    print("Conservative timing patch applied successfully!")
    return True

if __name__ == "__main__":
    apply_conservative_patch()
