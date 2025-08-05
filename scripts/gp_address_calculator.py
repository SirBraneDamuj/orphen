#!/usr/bin/env python3
"""
GP Address Calculator for Orphen: Scion of Sorcery

This tool converts GP-relative addresses (like cGpffffb663) to real memory addresses
using the GP base address.

Usage:
    python gp_address_calculator.py <gp_address> [--gp-base <base>]
    python gp_address_calculator.py --interactive

Examples:
    python gp_address_calculator.py cGpffffb663
    python gp_address_calculator.py uGpffffb0ec --gp-base 0x00359F70
    python gp_address_calculator.py --interactive
"""

import argparse
import re
import sys

# Default GP base address for Orphen
DEFAULT_GP_BASE = 0x00359F70

def parse_gp_address(gp_addr_str):
    """
    Parse a GP-relative address string and extract the offset.
    
    Examples:
        cGpffffb663 -> -0x490D (GP-0x490D)
        uGpffffb0ec -> -0x4E84 (GP-0x4E84)
        bGpffffb66d -> -0x4993 (GP-0x4993)
    
    Args:
        gp_addr_str: String like "cGpffffb663" or "0xffffb663"
    
    Returns:
        int: The offset from GP (negative value)
    """
    # Remove variable type prefix (c, u, b, i, s, etc.)
    clean_addr = re.sub(r'^[a-zA-Z]*Gp', '', gp_addr_str)
    
    # Handle direct hex addresses
    if clean_addr.startswith('0x'):
        hex_value = int(clean_addr, 16)
    else:
        # Handle ffff addresses (negative offsets)
        if clean_addr.startswith('ffff'):
            hex_value = int(clean_addr, 16)
        else:
            # Try to parse as hex without 0x prefix
            try:
                hex_value = int(clean_addr, 16)
            except ValueError:
                raise ValueError(f"Cannot parse GP address: {gp_addr_str}")
    
    # Convert to signed 32-bit offset
    if hex_value > 0x7FFFFFFF:
        # This is a negative offset (two's complement)
        offset = hex_value - 0x100000000
    else:
        offset = hex_value
    
    return offset

def calculate_real_address(gp_addr_str, gp_base=DEFAULT_GP_BASE):
    """
    Calculate the real memory address from a GP-relative address.
    
    Args:
        gp_addr_str: GP-relative address string (e.g., "cGpffffb663")
        gp_base: GP base address (default: 0x00359F70)
    
    Returns:
        tuple: (real_address, offset, formatted_info)
    """
    offset = parse_gp_address(gp_addr_str)
    real_address = gp_base + offset
    
    # Format the information
    info = {
        'gp_variable': gp_addr_str,
        'gp_base': f"0x{gp_base:08X}",
        'offset': f"{offset:+#X}" if offset >= 0 else f"-0x{abs(offset):X}",
        'real_address': f"0x{real_address:08X}",
        'calculation': f"0x{gp_base:08X} + ({offset:+#X})" if offset >= 0 else f"0x{gp_base:08X} - 0x{abs(offset):X}"
    }
    
    return real_address, offset, info

def format_output(info):
    """Format the calculation results for display."""
    return f"""GP Address Calculation:
  Variable:     {info['gp_variable']}
  GP Base:      {info['gp_base']}
  Offset:       {info['offset']}
  Calculation:  {info['calculation']}
  Real Address: {info['real_address']}"""

def interactive_mode():
    """Interactive mode for multiple calculations."""
    print("GP Address Calculator - Interactive Mode")
    print(f"Default GP Base: 0x{DEFAULT_GP_BASE:08X}")
    print("Type 'quit' or 'exit' to leave, 'help' for usage examples\n")
    
    current_gp_base = DEFAULT_GP_BASE
    
    while True:
        try:
            user_input = input("Enter GP address (or command): ").strip()
            
            if user_input.lower() in ['quit', 'exit', 'q']:
                break
            elif user_input.lower() in ['help', 'h']:
                print("""
Examples:
  cGpffffb663          - Calculate address for debug flag
  uGpffffb0ec          - Calculate address for frame state
  bGpffffb66d          - Calculate address for debug control
  set-gp 0x00359F70    - Change GP base address
  gp                   - Show current GP base
""")
                continue
            elif user_input.lower() == 'gp':
                print(f"Current GP Base: 0x{current_gp_base:08X}")
                continue
            elif user_input.lower().startswith('set-gp '):
                try:
                    new_gp = user_input[7:].strip()
                    if new_gp.startswith('0x'):
                        current_gp_base = int(new_gp, 16)
                    else:
                        current_gp_base = int(new_gp, 16)
                    print(f"GP Base set to: 0x{current_gp_base:08X}")
                except ValueError:
                    print("Invalid GP base address format")
                continue
            elif not user_input:
                continue
            
            # Calculate the address
            real_addr, offset, info = calculate_real_address(user_input, current_gp_base)
            print(format_output(info))
            print()
            
        except KeyboardInterrupt:
            print("\nGoodbye!")
            break
        except Exception as e:
            print(f"Error: {e}")
            print()

def main():
    parser = argparse.ArgumentParser(
        description="Convert GP-relative addresses to real memory addresses",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s cGpffffb663
  %(prog)s uGpffffb0ec --gp-base 0x00359F70
  %(prog)s --interactive

Common debug flags from main_game_loop.c:
  bGpffffb66d  - Multi-bit debug control flags
  cGpffffb128  - Debug output master enable  
  cGpffffb663  - Debug mode state flag
  cGpffffb66a  - Master enable/disable for debug systems
  cGpffffadc0  - Stepping mode state flag
        """
    )
    
    parser.add_argument('gp_address', nargs='?', 
                       help='GP-relative address (e.g., cGpffffb663)')
    parser.add_argument('--gp-base', type=lambda x: int(x, 0), 
                       default=DEFAULT_GP_BASE,
                       help=f'GP base address (default: 0x{DEFAULT_GP_BASE:08X})')
    parser.add_argument('--interactive', '-i', action='store_true',
                       help='Enter interactive mode')
    
    args = parser.parse_args()
    
    if args.interactive:
        interactive_mode()
        return
    
    if not args.gp_address:
        parser.print_help()
        return
    
    try:
        real_addr, offset, info = calculate_real_address(args.gp_address, args.gp_base)
        print(format_output(info))
        
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
