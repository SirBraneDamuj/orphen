#!/usr/bin/env python3
"""
SCR.BIN Timing Patch Script
"""

import shutil
from pathlib import Path

def patch_scr_bin():
    scr_path = Path("SCR.BIN")
    backup_path = Path("SCR.BIN.backup")
    
    # Create backup
    shutil.copy(scr_path, backup_path)
    
    with open(scr_path, "rb+") as f:
        data = f.read()
        
        # Patch 300 -> 30 frames
        data = data.replace(bytes.fromhex("2C01"), bytes.fromhex("1E00"))
        
        # Patch 360 -> 30 frames
        data = data.replace(bytes.fromhex("6801"), bytes.fromhex("1E00"))
        
        # Patch 480 -> 60 frames
        data = data.replace(bytes.fromhex("E001"), bytes.fromhex("3C00"))
        
        # Patch 600 -> 60 frames
        data = data.replace(bytes.fromhex("5802"), bytes.fromhex("3C00"))
        
        # Patch 720 -> 60 frames
        data = data.replace(bytes.fromhex("D002"), bytes.fromhex("3C00"))
        
        # Patch 900 -> 90 frames
        data = data.replace(bytes.fromhex("8403"), bytes.fromhex("5A00"))
        
        # Patch 1200 -> 120 frames
        data = data.replace(bytes.fromhex("B004"), bytes.fromhex("7800"))
        
        # Patch 1800 -> 180 frames
        data = data.replace(bytes.fromhex("0807"), bytes.fromhex("B400"))
        
        f.seek(0)
        f.write(data)
        f.truncate()
    
    print("SCR.BIN patched for faster cutscenes!")

if __name__ == "__main__":
    patch_scr_bin()
