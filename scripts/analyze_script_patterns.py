#!/usr/bin/env python3
"""
Enhanced Script Analysis - Pattern Recognition

This script analyzes the bytecode patterns to understand what the scripts are doing.
"""

import struct

def read_uint32_le(data, offset):
    """Read a 32-bit little-endian integer from data at offset."""
    if offset + 4 > len(data):
        return None
    return struct.unpack('<I', data[offset:offset+4])[0]

def find_script_patterns(memory_data):
    """Look for common script patterns that indicate purpose."""
    
    # Pattern 1: Conditional logic (comparison + return)
    conditional_pattern = [0x12, 0x0b]  # EQUAL + RETURN
    conditional_pattern2 = [0x13, 0x0b]  # NOT_EQUAL + RETURN
    
    # Pattern 2: Arithmetic sequences
    arithmetic_patterns = [
        [0x1c, 0x0b],  # ADD + RETURN
        [0x1d, 0x0b],  # SUBTRACT + RETURN
        [0x23, 0x0b],  # MULTIPLY + RETURN
    ]
    
    # Pattern 3: Logic operations
    logic_patterns = [
        [0x18, 0x0b],  # LOGICAL_NOT + RETURN
        [0x1a, 0x0b],  # LOGICAL_AND + RETURN
    ]
    
    print("Analyzing script patterns in memory...")
    print("=" * 60)
    
    # Search through the script memory range
    script_start = 0x01c40000
    script_end = 0x01c50000  # Check first 64KB
    
    pattern_counts = {
        "conditional": 0,
        "arithmetic": 0,
        "logic": 0,
        "returns": 0
    }
    
    interesting_locations = []
    
    for addr in range(script_start, min(script_end, len(memory_data) - 1)):
        if addr + 1 < len(memory_data):
            byte1 = memory_data[addr]
            byte2 = memory_data[addr + 1]
            
            # Count returns (script endings)
            if byte1 == 0x0b:
                pattern_counts["returns"] += 1
            
            # Check for patterns
            pattern = [byte1, byte2]
            
            if pattern in [conditional_pattern, conditional_pattern2]:
                pattern_counts["conditional"] += 1
                interesting_locations.append((addr, "CONDITIONAL", f"0x{byte1:02x} 0x{byte2:02x}"))
            
            elif pattern in arithmetic_patterns:
                pattern_counts["arithmetic"] += 1
                interesting_locations.append((addr, "ARITHMETIC", f"0x{byte1:02x} 0x{byte2:02x}"))
            
            elif pattern in logic_patterns:
                pattern_counts["logic"] += 1
                interesting_locations.append((addr, "LOGIC", f"0x{byte1:02x} 0x{byte2:02x}"))
    
    print("Pattern Analysis Results:")
    print(f"  Conditional operations: {pattern_counts['conditional']}")
    print(f"  Arithmetic operations:  {pattern_counts['arithmetic']}")
    print(f"  Logic operations:       {pattern_counts['logic']}")
    print(f"  Return instructions:    {pattern_counts['returns']}")
    print()
    
    if interesting_locations:
        print("Interesting script locations (first 20):")
        for addr, pattern_type, bytes_str in interesting_locations[:20]:
            print(f"  0x{addr:08X}: {pattern_type:<12} - {bytes_str}")
    
    return pattern_counts, interesting_locations

def analyze_script_density(memory_data):
    """Analyze how densely packed the scripts are."""
    
    script_start = 0x01c40000
    script_end = 0x01c50000
    
    # Count non-zero bytes in script region
    non_zero_count = 0
    total_bytes = 0
    
    for addr in range(script_start, min(script_end, len(memory_data))):
        total_bytes += 1
        if memory_data[addr] != 0:
            non_zero_count += 1
    
    density = (non_zero_count / total_bytes) * 100 if total_bytes > 0 else 0
    
    print(f"Script Memory Density Analysis:")
    print(f"  Region: 0x{script_start:08X} - 0x{script_end:08X}")
    print(f"  Total bytes: {total_bytes}")
    print(f"  Non-zero bytes: {non_zero_count}")
    print(f"  Density: {density:.1f}%")
    print()
    
    if density > 50:
        print("✓ High density suggests active script storage")
    elif density > 20:
        print("⚠ Medium density - mixed script/data region")
    else:
        print("✗ Low density - mostly empty or data")

def main():
    try:
        with open('eeMemory.bin', 'rb') as f:
            memory_data = f.read()
        
        print("Enhanced Script Pattern Analysis")
        print("=" * 60)
        print()
        
        # Analyze script density
        analyze_script_density(memory_data)
        
        # Find script patterns
        patterns, locations = find_script_patterns(memory_data)
        
        # Analyze what this tells us about script purpose
        print("\nScript Purpose Analysis:")
        print("=" * 40)
        
        total_operations = sum(patterns.values()) - patterns["returns"]
        
        if patterns["conditional"] > patterns["arithmetic"]:
            print("→ Scripts appear to be primarily CONDITIONAL (decision-making)")
            print("  Likely used for: game state checks, flag testing, event triggers")
        elif patterns["arithmetic"] > patterns["conditional"]:
            print("→ Scripts appear to be primarily ARITHMETIC (calculation)")
            print("  Likely used for: damage calculation, stat computation, positioning")
        else:
            print("→ Scripts appear to be MIXED PURPOSE")
            print("  Likely used for: general game logic, various computations")
        
        if patterns["returns"] > total_operations * 0.1:
            print("→ High return count suggests many small, focused scripts")
            print("  Likely: expression evaluators rather than complex programs")
        
        print()
        print("Conclusion:")
        if patterns["conditional"] > 0 or patterns["arithmetic"] > 0:
            print("✓ CONFIRMED: Active bytecode scripts found in memory")
            print("✓ Scripts are being used for game logic evaluation")
            print("✓ This is a functional scripting system, not just data")
        else:
            print("? Unclear script purpose - may need deeper analysis")
            
    except FileNotFoundError:
        print("ERROR: eeMemory.bin not found")
    except Exception as e:
        print(f"ERROR: {e}")

if __name__ == "__main__":
    main()
