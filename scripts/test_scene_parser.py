#!/usr/bin/env python3
"""
Test script to run the scene data parser on Orphen disc image files.

This script assumes the disc image files are available and tests the parser
on the most likely scene data files identified from the disc listing.
"""

import os
import sys
from scene_data_parser import SceneDataParser

def test_file(file_path: str, file_name: str):
    """Test a single BIN file"""
    print(f"\n{'='*60}")
    print(f"TESTING: {file_name}")
    print(f"{'='*60}")
    
    if not os.path.exists(file_path):
        print(f"File not found: {file_path}")
        return
    
    parser = SceneDataParser(file_path)
    if not parser.load_file():
        return
    
    analysis = parser.analyze_file()
    
    # Save analysis to JSON
    output_file = f"analysis_{file_name.lower().replace('.bin', '')}.json"
    import json
    with open(output_file, 'w') as f:
        json.dump(analysis, f, indent=2)
    
    print(f"Analysis saved to {output_file}")

def main():
    # Expected disc image file paths - adjust these based on actual disc image location
    disc_files = [
        ("MCB1.BIN", "Primary scene data archive (295 MB)"),
        ("MAP.BIN", "Map/level data (17.7 MB)"), 
        ("SCR.BIN", "Script data (1.26 MB)"),
        ("MCB0.BIN", "Secondary scene data (12 KB)"),
        ("TEX.BIN", "Texture data (25.4 MB)")
    ]
    
    # Try to find disc image files in common locations
    disc_paths = [
        r"C:\Users\zptha\projects\orphen\orphen_ghidra\Orphen - Scion of Sorcery (USA)\Orphen - Scion of Sorcery (USA)",  # Extracted disc location
        ".",  # Current directory
        "../disc",  # Disc subdirectory
        "../iso",   # ISO subdirectory
        "disc",     # Local disc directory
        "iso"       # Local iso directory
    ]
    
    found_files = []
    
    for base_path in disc_paths:
        for file_name, description in disc_files:
            file_path = os.path.join(base_path, file_name)
            if os.path.exists(file_path):
                found_files.append((file_path, file_name, description))
                break  # Found this file, move to next
    
    if not found_files:
        print("No disc image files found!")
        print("\nExpected files:")
        for file_name, description in disc_files:
            print(f"  {file_name} - {description}")
        print("\nPlace disc image files in one of these locations:")
        for path in disc_paths:
            print(f"  {os.path.abspath(path)}")
        return 1
    
    print(f"Found {len(found_files)} disc image files to analyze:")
    for file_path, file_name, description in found_files:
        print(f"  {file_name} - {description}")
        print(f"    Path: {file_path}")
    
    # Test each file
    for file_path, file_name, description in found_files:
        try:
            test_file(file_path, file_name)
        except Exception as e:
            print(f"Error analyzing {file_name}: {e}")
    
    print(f"\n{'='*60}")
    print("ANALYSIS COMPLETE")
    print(f"{'='*60}")
    print(f"Analyzed {len(found_files)} files")
    print("Check the generated analysis_*.json files for detailed results")
    
    return 0

if __name__ == '__main__':
    sys.exit(main())
