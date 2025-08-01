# export_globals.py - Jython script for Ghidra
# Exports all global variables (DAT_* and others) with their metadata

import os
import json
from ghidra.program.model.data import DataType
from ghidra.program.model.symbol import SymbolType

# Configuration
OUTPUT_FILE = "C:/Users/zptha/projects/orphen/decompiled/globals.json"

# Get current program and listing
program = getCurrentProgram()
listing = program.getListing()
symbol_table = program.getSymbolTable()
reference_manager = program.getReferenceManager()

print("Starting global variables export...")

globals_data = {}
export_count = 0

# Get all defined data in the program
data_iterator = listing.getDefinedData(True)

for data in data_iterator:
    address = data.getAddress()
    address_str = address.toString()
    
    # Get symbol name (DAT_* or other names)
    symbols = symbol_table.getSymbols(address)
    symbol_name = None
    for symbol in symbols:
        if symbol.getSymbolType() == SymbolType.LABEL:
            symbol_name = symbol.getName()
            break
    
    if symbol_name is None:
        continue
    
    # Get data type information
    data_type = data.getDataType()
    data_type_name = data_type.getDisplayName() if data_type else "unknown"
    data_size = data.getLength()
    
    # Get initial value if it's a primitive
    initial_value = None
    try:
        if data_type and (data_type.getName() in ["int", "uint", "short", "ushort", "byte", "undefined"]):
            initial_value = str(data.getValue())
    except:
        pass
    
    # Get cross-references (what functions access this variable)
    references_to = []
    refs = reference_manager.getReferencesTo(address)
    for ref in refs:
        from_addr = ref.getFromAddress()
        from_func = listing.getFunctionContaining(from_addr)
        if from_func:
            ref_info = {
                "function": from_func.getName(),
                "address": from_addr.toString(),
                "type": ref.getReferenceType().toString()
            }
            references_to.append(ref_info)
    
    # Get references from this address (if it's a pointer)
    references_from = []
    refs_from = reference_manager.getReferencesFrom(address)
    for ref in refs_from:
        to_addr = ref.getToAddress()
        to_func = listing.getFunctionContaining(to_addr)
        ref_info = {
            "to_address": to_addr.toString(),
            "type": ref.getReferenceType().toString()
        }
        if to_func:
            ref_info["to_function"] = to_func.getName()
        references_from.append(ref_info)
    
    # Build the global variable entry
    global_entry = {
        "address": address_str,
        "name": symbol_name,
        "data_type": data_type_name,
        "size": data_size,
        "initial_value": initial_value,
        "accessed_by": references_to,
        "points_to": references_from
    }
    
    globals_data[symbol_name] = global_entry
    export_count += 1
    
    if export_count % 100 == 0:
        print("Exported " + str(export_count) + " globals...")

# Write to JSON file
try:
    with open(OUTPUT_FILE, 'w') as f:
        json.dump(globals_data, f, indent=2)
    print("\nExport complete! Exported " + str(export_count) + " global variables to " + OUTPUT_FILE)
except Exception as e:
    print("Error writing file: " + str(e))

print("\nSample entries:")
count = 0
for name, data in globals_data.items():
    if count < 3:
        print("- " + name + " (" + data["data_type"] + ") at " + data["address"])
        if data["accessed_by"]:
            print("  Used by: " + str(len(data["accessed_by"])) + " functions")
        count += 1
    else:
        break
