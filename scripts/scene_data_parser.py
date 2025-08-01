#!/usr/bin/env python3
"""
Orphen Scene Data Parser

Analyzes BIN files from the Orphen PS2 disc image to detect and parse
scene data structures based on the reverse-engineered format.

Expected Scene Structure (from process_scene_with_work_flags analysis):
- Scene Header with main command sequence offset at +0x08
- Scene Objects Array: up to 62 entries, each 4-byte pointer
- Scene object metadata stored 4 bytes before each object
- Scene Work Data: 128 entries * 4 bytes = 512 bytes
- Scene Work Flags: 4 * 32-bit values = 128 bits

Usage:
    python scene_data_parser.py <bin_file> [options]
"""

import struct
import sys
import os
import argparse
from typing import List, Dict, Tuple, Optional
from dataclasses import dataclass


@dataclass
class SceneObject:
    """Represents a scene object with metadata and command data"""
    index: int
    metadata: int
    data_offset: int
    data_size: int
    commands: bytes


@dataclass
class SceneHeader:
    """Represents a scene file header"""
    main_command_offset: int
    objects_array_offset: int
    work_data_offset: int
    work_flags_offset: int


class SceneDataParser:
    """Parser for Orphen scene data files"""
    
    # Scene command opcodes based on scene_command_interpreter analysis
    BASIC_COMMANDS = list(range(0x00, 0x0B))
    EXTENDED_CMD_PREFIX = 0xFF
    SUBROUTINE_CALL = 0x32
    RETURN_CMD = 0x04
    HIGH_COMMANDS_START = 0x33
    
    def __init__(self, file_path: str):
        self.file_path = file_path
        self.data = None
        self.file_size = 0
        
    def load_file(self) -> bool:
        """Load the BIN file into memory"""
        try:
            with open(self.file_path, 'rb') as f:
                self.data = f.read()
                self.file_size = len(self.data)
                print(f"Loaded {self.file_path}: {self.file_size:,} bytes")
                return True
        except Exception as e:
            print(f"Error loading file: {e}")
            return False
    
    def read_uint32(self, offset: int) -> int:
        """Read a 32-bit unsigned integer at offset"""
        if offset + 4 > self.file_size:
            return 0
        return struct.unpack('<I', self.data[offset:offset+4])[0]
    
    def read_uint16(self, offset: int) -> int:
        """Read a 16-bit unsigned integer at offset"""
        if offset + 2 > self.file_size:
            return 0
        return struct.unpack('<H', self.data[offset:offset+2])[0]
    
    def read_uint8(self, offset: int) -> int:
        """Read an 8-bit unsigned integer at offset"""
        if offset >= self.file_size:
            return 0
        return self.data[offset]
    
    def is_valid_command_sequence(self, offset: int, max_length: int = 1024) -> Tuple[bool, int]:
        """
        Check if data at offset looks like a valid scene command sequence
        Uses rigorous validation based on scene_command_interpreter analysis
        Returns (is_valid, sequence_length)
        """
        if offset >= self.file_size:
            return False, 0
        
        sequence_length = 0
        current_offset = offset
        command_count = 0
        invalid_commands = 0
        
        # Track command distribution for validation
        basic_cmds = 0
        extended_cmds = 0
        subroutine_calls = 0
        high_cmds = 0
        
        while current_offset < self.file_size and sequence_length < max_length:
            cmd = self.read_uint8(current_offset)
            
            # Check for sequence termination (null byte or end of valid commands)
            if cmd == 0x00:
                # Valid termination if we have enough commands
                if command_count >= 3:
                    return True, sequence_length + 1
                else:
                    return False, 0
            
            # Validate against actual scene command interpreter logic
            if cmd <= 0x0A:  # Basic commands (0x00-0x0A)
                current_offset += 1
                sequence_length += 1
                command_count += 1
                basic_cmds += 1
            elif cmd == self.EXTENDED_CMD_PREFIX:  # 0xFF - Extended commands
                if current_offset + 1 >= self.file_size:
                    break
                ext_cmd = self.read_uint8(current_offset + 1)
                # Extended commands are dispatched as (ext_cmd + 0x100)
                # This should be a reasonable byte value
                if ext_cmd > 0xFE:  # Arbitrary upper limit for extended commands
                    invalid_commands += 1
                current_offset += 2
                sequence_length += 2
                command_count += 1
                extended_cmds += 1
            elif cmd == self.SUBROUTINE_CALL:  # 0x32 - Subroutine call
                if current_offset + 4 >= self.file_size:
                    break
                # Read 4-byte offset and validate it's reasonable
                target_offset = self.read_uint32(current_offset + 1)
                if target_offset > self.file_size or target_offset == 0:
                    invalid_commands += 1
                current_offset += 5  # 1 byte cmd + 4 byte offset
                sequence_length += 5
                command_count += 1
                subroutine_calls += 1
            elif cmd >= self.HIGH_COMMANDS_START:  # 0x33+ - High commands
                # These are dispatched via PTR_LAB_0031e228[command_byte - 0x32]
                # So valid range is roughly 0x33 to some reasonable upper limit
                if cmd > 0xFE:  # Reasonable upper limit
                    invalid_commands += 1
                current_offset += 1
                sequence_length += 1
                command_count += 1
                high_cmds += 1
            else:
                # Commands 0x0B-0x31 (except 0x32) are not handled by interpreter
                # These indicate invalid sequences
                invalid_commands += 1
                if invalid_commands > 2 or command_count < 3:
                    return False, 0
                current_offset += 1
                sequence_length += 1
        
        # Additional validation: check command distribution
        if command_count < 3:
            return False, 0
        
        # Reject if too many invalid commands
        if invalid_commands > command_count * 0.2:  # More than 20% invalid
            return False, 0
        
        # Good sequences should have a reasonable mix of command types
        # Pure high commands (0x33+) might be data, not commands
        if high_cmds == command_count and command_count > 10:
            return False, 0
        
        return True, sequence_length
    
    def validate_scene_header_structure(self, offset: int) -> bool:
        """
        Validate that this looks like a real scene header based on 
        process_scene_with_work_flags analysis
        """
        if offset + 16 > self.file_size:
            return False
        
        # Read the header fields
        field0 = self.read_uint32(offset)      # Unknown field
        field4 = self.read_uint32(offset + 4)  # Unknown field  
        main_cmd_offset = self.read_uint32(offset + 8)   # Main command sequence offset
        field12 = self.read_uint32(offset + 12)  # Unknown field
        
        # Validate main command offset points to valid commands
        if main_cmd_offset == 0 or main_cmd_offset > self.file_size:
            return False
        
        # The main command sequence should be a valid command sequence
        abs_command_offset = offset + main_cmd_offset
        if abs_command_offset >= self.file_size:
            return False
        
        is_valid, seq_len = self.is_valid_command_sequence(abs_command_offset, 1024)
        if not is_valid or seq_len < 10:  # Should be substantial command sequence
            return False
        
        # Look for scene objects array near the header
        # Based on process_scene_with_work_flags, the objects array should be accessible
        objects_array_found = False
        search_start = offset + 16
        search_end = min(offset + 1024, self.file_size - (62 * 4))
        
        for obj_array_offset in range(search_start, search_end, 4):
            if self.validate_scene_objects_array(obj_array_offset):
                objects_array_found = True
                break
        
        return objects_array_found
    
    def validate_scene_objects_array(self, offset: int) -> bool:
        """
        Validate that this looks like a real scene objects array
        Based on process_scene_with_work_flags: array of up to 62 pointers
        """
        if offset + (62 * 4) > self.file_size:
            return False
        
        valid_objects = 0
        null_objects = 0
        
        for i in range(62):
            ptr_offset = offset + (i * 4)
            obj_ptr = self.read_uint32(ptr_offset)
            
            if obj_ptr == 0:
                null_objects += 1
                continue
            
            # Pointer should be within file bounds
            if obj_ptr >= self.file_size:
                continue
            
            # Should have metadata 4 bytes before (scene_object_ptr + -4)
            if obj_ptr < 4:
                continue
            
            metadata = self.read_uint32(obj_ptr - 4)
            
            # Validate the command sequence at obj_ptr
            is_valid, cmd_length = self.is_valid_command_sequence(obj_ptr, 512)
            if is_valid and cmd_length > 0:
                valid_objects += 1
        
        # A valid scene array should have:
        # - At least 5 valid scene objects
        # - Not more than 57 null objects (allowing some valid objects)
        # - Some null objects are normal (sparse arrays)
        return valid_objects >= 5 and null_objects <= 57

    def find_scene_headers(self) -> List[Tuple[int, SceneHeader]]:
        """
        Scan file for potential scene headers using rigorous validation
        Look for patterns that match expected scene structure from reverse engineering
        """
        headers = []
        
        # Scan through file looking for potential headers
        # Use wider scan for better coverage but with rigorous validation
        scan_size = min(0x50000, self.file_size)  # Scan first 320KB
        
        print(f"Scanning {scan_size:,} bytes for scene headers...")
        
        for offset in range(0, scan_size - 32, 16):  # 16-byte aligned headers
            # Use rigorous structure validation
            if self.validate_scene_header_structure(offset):
                # Read the validated header
                main_cmd_offset = self.read_uint32(offset + 8)
                
                header = SceneHeader(
                    main_command_offset=main_cmd_offset,
                    objects_array_offset=0,  # Will be found during object array search
                    work_data_offset=0,      # Not yet located
                    work_flags_offset=0      # Not yet located
                )
                headers.append((offset, header))
                
                abs_command_offset = offset + main_cmd_offset
                is_valid, seq_len = self.is_valid_command_sequence(abs_command_offset, 512)
                print(f"Validated scene header at 0x{offset:08X}, command sequence at 0x{abs_command_offset:08X} ({seq_len} bytes)")
                
                # Limit results to prevent excessive output
                if len(headers) >= 20:
                    print(f"Found {len(headers)} validated headers, stopping search for performance...")
                    break
        
        return headers
    
    def find_object_arrays(self, header_offset: int) -> List[int]:
        """
        Look for arrays of 62 pointers (scene objects array) using rigorous validation
        """
        arrays = []
        
        # Look within reasonable range after header
        search_start = header_offset + 32
        search_end = min(header_offset + 4096, self.file_size - (62 * 4))
        
        for offset in range(search_start, search_end, 4):
            if self.validate_scene_objects_array(offset):
                arrays.append(offset)
                
                # Count valid objects for reporting
                valid_count = 0
                for i in range(62):
                    ptr_offset = offset + (i * 4)
                    obj_ptr = self.read_uint32(ptr_offset)
                    if obj_ptr != 0 and obj_ptr < self.file_size:
                        is_valid, _ = self.is_valid_command_sequence(obj_ptr)
                        if is_valid:
                            valid_count += 1
                
                print(f"Validated scene objects array at 0x{offset:08X} ({valid_count} valid objects)")
        
        return arrays
    
    def parse_scene_objects(self, array_offset: int) -> List[SceneObject]:
        """Parse scene objects from the objects array"""
        objects = []
        
        for i in range(62):
            ptr_offset = array_offset + (i * 4)
            if ptr_offset + 4 > self.file_size:
                break
            
            obj_ptr = self.read_uint32(ptr_offset)
            if obj_ptr == 0:
                continue
            
            if obj_ptr >= self.file_size:
                continue
            
            # Read metadata (4 bytes before object data)
            metadata = self.read_uint32(obj_ptr - 4) if obj_ptr >= 4 else 0
            
            # Try to determine object size by finding next valid pointer or command end
            is_valid, cmd_length = self.is_valid_command_sequence(obj_ptr, 2048)
            
            if is_valid and self.data is not None:
                obj_data = self.data[obj_ptr:obj_ptr + cmd_length]
                
                scene_obj = SceneObject(
                    index=i,
                    metadata=metadata,
                    data_offset=obj_ptr,
                    data_size=cmd_length,
                    commands=obj_data
                )
                objects.append(scene_obj)
        
        return objects
    
    def analyze_commands(self, commands: bytes) -> Dict:
        """Analyze command sequence and extract statistics"""
        stats = {
            'total_commands': 0,
            'basic_commands': 0,
            'extended_commands': 0,
            'subroutine_calls': 0,
            'returns': 0,
            'high_commands': 0,
            'unknown_commands': 0,
            'command_breakdown': {}
        }
        
        offset = 0
        while offset < len(commands):
            if offset >= len(commands):
                break
                
            cmd = commands[offset]
            stats['total_commands'] += 1
            
            if cmd in stats['command_breakdown']:
                stats['command_breakdown'][cmd] += 1
            else:
                stats['command_breakdown'][cmd] = 1
            
            if cmd in self.BASIC_COMMANDS:
                stats['basic_commands'] += 1
                offset += 1
            elif cmd == self.EXTENDED_CMD_PREFIX:
                stats['extended_commands'] += 1
                offset += 2
            elif cmd == self.SUBROUTINE_CALL:
                stats['subroutine_calls'] += 1
                offset += 5
            elif cmd == self.RETURN_CMD:
                stats['returns'] += 1
                offset += 1
            elif cmd >= self.HIGH_COMMANDS_START:
                stats['high_commands'] += 1
                offset += 1
            else:
                stats['unknown_commands'] += 1
                offset += 1
        
        return stats
    
    def analyze_file(self) -> Dict:
        """Perform complete analysis of the file"""
        if not self.data:
            return {}
        
        print(f"\n=== Analyzing {os.path.basename(self.file_path)} ===")
        
        # Find potential scene headers
        headers = self.find_scene_headers()
        
        analysis = {
            'file_size': self.file_size,
            'potential_headers': len(headers),
            'scenes': []
        }
        
        for header_offset, header in headers[:3]:  # Analyze first 3 potential scenes only
            print(f"\n--- Analyzing scene at 0x{header_offset:08X} ---")
            
            # Find object arrays for this scene
            object_arrays = self.find_object_arrays(header_offset)
            
            scene_analysis = {
                'header_offset': header_offset,
                'header': {
                    'main_command_offset': header.main_command_offset,
                    'objects_array_offset': header.objects_array_offset,
                    'work_data_offset': header.work_data_offset,
                    'work_flags_offset': header.work_flags_offset
                },
                'object_arrays': object_arrays,
                'objects': []
            }
            
            # Analyze first object array found
            if object_arrays:
                array_offset = object_arrays[0]
                objects = self.parse_scene_objects(array_offset)
                
                print(f"Found {len(objects)} scene objects")
                
                for obj in objects:
                    cmd_stats = self.analyze_commands(obj.commands)
                    obj_analysis = {
                        'index': obj.index,
                        'metadata': obj.metadata,
                        'data_offset': obj.data_offset,
                        'data_size': obj.data_size,
                        'command_stats': cmd_stats
                    }
                    scene_analysis['objects'].append(obj_analysis)
                    
                    print(f"  Object {obj.index}: metadata=0x{obj.metadata:08X}, "
                          f"size={obj.data_size}, commands={cmd_stats['total_commands']}")
            
            analysis['scenes'].append(scene_analysis)
        
        return analysis


def main():
    parser = argparse.ArgumentParser(description='Parse Orphen scene data from BIN files')
    parser.add_argument('bin_file', help='Path to BIN file to analyze')
    parser.add_argument('--output', '-o', help='Output analysis to JSON file')
    parser.add_argument('--verbose', '-v', action='store_true', help='Verbose output')
    
    args = parser.parse_args()
    
    if not os.path.exists(args.bin_file):
        print(f"Error: File {args.bin_file} not found")
        return 1
    
    # Create parser and analyze file
    scene_parser = SceneDataParser(args.bin_file)
    
    if not scene_parser.load_file():
        return 1
    
    analysis = scene_parser.analyze_file()
    
    if args.output:
        import json
        with open(args.output, 'w') as f:
            json.dump(analysis, f, indent=2)
        print(f"\nAnalysis saved to {args.output}")
    
    print(f"\n=== Summary ===")
    print(f"File size: {analysis['file_size']:,} bytes")
    print(f"Potential scenes found: {analysis['potential_headers']}")
    print(f"Scenes analyzed: {len(analysis['scenes'])}")
    
    return 0


if __name__ == '__main__':
    sys.exit(main())
