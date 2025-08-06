#!/usr/bin/env python3
"""
Export Function Pointer Tables from Ghidra

This script exports all function pointer tables found in the program, looking for patterns
like PTR_FUN_* symbols and consecutive function pointer arrays. It helps identify
indirect function calls that are not visible in decompiled code analysis.

Usage: Run this script from Ghidra's Script Manager
Output: function_pointers.json in the current project directory
"""

import json
import os
from ghidra.program.model.data import PointerDataType, FunctionDefinitionDataType
from ghidra.program.model.symbol import SymbolType, SourceType
from ghidra.program.model.listing import Data
from ghidra.program.model.address import AddressSet
from ghidra.program.model.mem import MemoryBlock

def get_function_at_address(addr):
    """Get function information at a given address"""
    func = getFunctionAt(addr)
    if func:
        return {
            'name': func.getName(),
            'address': str(addr),
            'signature': str(func.getSignature())
        }
    return None

def scan_for_function_pointer_symbols():
    """Scan for PTR_FUN_* symbols and consecutive function pointer patterns"""
    function_pointer_tables = {}
    
    symbol_table = currentProgram.getSymbolTable()
    memory = currentProgram.getMemory()
    
    print("Scanning for function pointer symbols and tables...")
    
    # Get all symbols that look like function pointers
    all_symbols = symbol_table.getAllSymbols(True)
    ptr_fun_symbols = []
    
    for symbol in all_symbols:
        symbol_name = symbol.getName()
        if symbol_name.startswith("PTR_FUN_"):
            ptr_fun_symbols.append(symbol)
    
    print("Found {} PTR_FUN_* symbols".format(len(ptr_fun_symbols)))
    
    # For each PTR_FUN_* symbol, scan consecutive addresses to build the full table
    for symbol in ptr_fun_symbols:
        start_addr = symbol.getAddress()
        symbol_name = symbol.getName()
        
        print("\nAnalyzing table starting at {} ({})".format(start_addr, symbol_name))
        
        current_table = []
        current_addr = start_addr
        offset = 0
        
        # Scan consecutive 4-byte addresses starting from this PTR_FUN_*
        while True:
            try:
                # Read 4-byte value at current address
                value = memory.getInt(current_addr)
                
                # Check if this value points to a valid code address
                if 0x00100000 <= value <= 0x00400000:  # Reasonable function address range
                    target_addr = toAddr(value)
                    
                    # Check if it's a function OR has any symbol (including LAB_)
                    func = getFunctionAt(target_addr)
                    symbols_at_target = symbol_table.getSymbols(target_addr)
                    target_symbol_name = None
                    
                    if symbols_at_target:
                        target_symbol_name = symbols_at_target[0].getName()
                    
                    # Include if it's a function OR if it has a symbol (LAB_, FUN_, etc.)
                    if func or target_symbol_name:
                        func_name = func.getName() if func else target_symbol_name
                        
                        current_table.append(func_name)
                        print("  [{}] -> {}".format(offset, func_name))
                        
                        # Move to next 4-byte address
                        current_addr = current_addr.add(4)
                        offset += 1
                    else:
                        # Value doesn't point to a function or symbol, end of table
                        break
                else:
                    # Value not in function range, end of table
                    break
                    
            except Exception as e:
                # Error reading memory or processing, end of table
                print("  Ended table due to error: {}".format(e))
                break
        
        # Save the table if it has entries
        if current_table:
            function_pointer_tables[symbol_name] = current_table
            print("  Table complete: {} entries".format(len(current_table)))
    
    return function_pointer_tables

def main():
    print("Starting function pointer analysis...")
    
    # Find all function pointer tables using symbol analysis
    function_pointer_tables = scan_for_function_pointer_symbols()
    
    # Compile results
    results = {
        'analysis_info': {
            'script_name': 'export_function_pointers.py',
            'description': 'Function pointer tables - PTR_FUN_* symbol analysis',
            'total_function_tables': len(function_pointer_tables)
        },
        'function_pointer_tables': function_pointer_tables
    }
    
    # Write to JSON file in current project directory
    try:
        # Write to the specific decompiled project directory
        output_file = r'C:\Users\zptha\projects\orphen\decompiled\function_pointers.json'
        
        with open(output_file, 'w') as f:
            json.dump(results, f, indent=2)
        
        print("\nFunction pointer analysis complete!")
        print("Results written to: {}".format(output_file))
        print("Found {} function pointer tables".format(len(function_pointer_tables)))
        
        # Print interesting findings
        if function_pointer_tables:
            print("\nFunction pointer tables found:")
            for table_name, functions in function_pointer_tables.items():
                print("  {}: {} entries".format(table_name, len(functions)))
                for i, func in enumerate(functions[:3]):  # Show first 3 entries
                    print("    [{}] {}".format(i, func))
                if len(functions) > 3:
                    print("    ... and {} more entries".format(len(functions) - 3))
                print()
        
    except Exception as e:
        print("Error writing output file: {}".format(e))
        # Fallback: print to console
        print("\n=== FUNCTION POINTER ANALYSIS RESULTS ===")
        print(json.dumps(results, indent=2))

if __name__ == "__main__":
    main()
