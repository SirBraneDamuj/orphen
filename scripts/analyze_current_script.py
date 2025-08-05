#!/usr/bin/env python3
"""
Script Analysis Tool for Orphen Memory Dump

This script reads the current instruction pointer from DAT_00355cd0 and analyzes
the bytecode being executed in the eeMemory.bin dump.
"""

import struct
import sys

# Known bytecode opcodes from the interpreter
BASIC_OPCODES = {
    0x0b: "RETURN",
    0x12: "EQUAL", 
    0x13: "NOT_EQUAL",
    0x14: "LESS_THAN_1", 
    0x15: "LESS_THAN_2",
    0x16: "GREATER_EQUAL_1",
    0x17: "GREATER_EQUAL_2", 
    0x18: "LOGICAL_NOT",
    0x19: "BITWISE_NOT",
    0x1a: "LOGICAL_AND",
    0x1b: "BITWISE_OR",
    0x1c: "ADD",
    0x1d: "SUBTRACT", 
    0x1e: "NEGATE",
    0x1f: "BITWISE_XOR",
    0x20: "BITWISE_AND",
    0x21: "BITWISE_OR_ALT",
    0x22: "DIVIDE",
    0x23: "MULTIPLY",
    0x24: "MODULO"
}

def read_uint32_le(data, offset):
    """Read a 32-bit little-endian integer from data at offset."""
    if offset + 4 > len(data):
        return None
    return struct.unpack('<I', data[offset:offset+4])[0]

def read_uint8(data, offset):
    """Read a single byte from data at offset."""
    if offset >= len(data):
        return None
    return data[offset]

def analyze_bytecode(memory_data, start_addr, length=64):
    """Analyze bytecode starting at the given address."""
    print(f"Analyzing bytecode at 0x{start_addr:08X}:")
    print("=" * 50)
    
    offset = 0
    while offset < length:
        addr = start_addr + offset
        byte_val = read_uint8(memory_data, addr)
        
        if byte_val is None:
            print(f"0x{addr:08X}: [END OF DATA]")
            break
            
        # Check if it's a known opcode
        if byte_val in BASIC_OPCODES:
            print(f"0x{addr:08X}: 0x{byte_val:02X} - {BASIC_OPCODES[byte_val]}")
            offset += 1
        elif byte_val == 0xff:
            # Extended instruction
            if addr + 1 < len(memory_data):
                param = read_uint8(memory_data, addr + 1)
                extended_opcode = 0x100 + param if param is not None else 0x100
                print(f"0x{addr:08X}: 0x{byte_val:02X} 0x{param:02X} - EXTENDED_INSTRUCTION (0x{extended_opcode:03X})")
                offset += 2
            else:
                print(f"0x{addr:08X}: 0x{byte_val:02X} - EXTENDED_INSTRUCTION (incomplete)")
                offset += 1
        elif 0x32 <= byte_val <= 0xfe:
            # Standard instruction range
            instruction_id = byte_val - 0x32
            print(f"0x{addr:08X}: 0x{byte_val:02X} - STANDARD_INSTRUCTION_{instruction_id}")
            offset += 1
        elif 0x00 <= byte_val <= 0x31:
            # Basic instruction range (handled by FUN_0025bf70)
            print(f"0x{addr:08X}: 0x{byte_val:02X} - BASIC_INSTRUCTION_{byte_val}")
            offset += 1
        else:
            # Unknown/data
            print(f"0x{addr:08X}: 0x{byte_val:02X} - DATA/UNKNOWN")
            offset += 1
    
    print("=" * 50)

def main():
    # Memory addresses (PS2 addresses need to be converted to file offsets)
    DAT_00355cd0_ADDR = 0x355cd0  # Instruction pointer location
    
    try:
        # Load the memory dump
        with open('eeMemory.bin', 'rb') as f:
            memory_data = f.read()
        
        print(f"Loaded memory dump: {len(memory_data)} bytes")
        print()
        
        # Read the current instruction pointer value
        instruction_ptr_value = read_uint32_le(memory_data, DAT_00355cd0_ADDR)
        
        if instruction_ptr_value is None:
            print(f"ERROR: Could not read instruction pointer at 0x{DAT_00355cd0_ADDR:08X}")
            return
        
        print(f"Current instruction pointer (DAT_00355cd0): 0x{instruction_ptr_value:08X}")
        
        # Check if the instruction pointer is in the expected range
        if 0x01c40000 <= instruction_ptr_value <= 0x01c8ffff:
            print("✓ Instruction pointer is in the 0x01C4xxxx script execution range!")
        else:
            print("⚠ Instruction pointer is outside expected script range")
        
        print()
        
        # Analyze the bytecode at the current instruction pointer
        if instruction_ptr_value < len(memory_data):
            analyze_bytecode(memory_data, instruction_ptr_value)
            
            # Also check if there are other interesting patterns nearby
            print("\nNearby memory context (32 bytes before current position):")
            context_start = max(0, instruction_ptr_value - 32)
            context_data = memory_data[context_start:instruction_ptr_value + 64]
            
            print("Hex dump:")
            for i in range(0, len(context_data), 16):
                addr = context_start + i
                line_data = context_data[i:i+16]
                hex_str = ' '.join(f'{b:02x}' for b in line_data)
                ascii_str = ''.join(chr(b) if 32 <= b <= 126 else '.' for b in line_data)
                
                # Mark the current instruction pointer
                marker = " <-- IP" if addr == instruction_ptr_value else ""
                print(f"0x{addr:08X}: {hex_str:<48} |{ascii_str}|{marker}")
        else:
            print(f"ERROR: Instruction pointer 0x{instruction_ptr_value:08X} is beyond memory dump size")
            
        # Look for other script-related data
        print("\nSearching for additional script pointers in memory...")
        
        # Look for other addresses in the 0x01C4xxxx range
        script_pointers = []
        for addr in range(0, len(memory_data) - 4, 4):
            value = read_uint32_le(memory_data, addr)
            if value and 0x01c40000 <= value <= 0x01c8ffff:
                script_pointers.append((addr, value))
        
        if script_pointers:
            print(f"Found {len(script_pointers)} pointers to script memory range:")
            for ptr_addr, ptr_value in script_pointers[:10]:  # Show first 10
                print(f"  0x{ptr_addr:08X} -> 0x{ptr_value:08X}")
            if len(script_pointers) > 10:
                print(f"  ... and {len(script_pointers) - 10} more")
        else:
            print("No other pointers to script range found.")
            
    except FileNotFoundError:
        print("ERROR: eeMemory.bin not found. Make sure you're in the correct directory.")
    except Exception as e:
        print(f"ERROR: {e}")

if __name__ == "__main__":
    main()
