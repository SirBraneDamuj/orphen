# export_strings.py - Export all strings with their addresses from Ghidra
# This script extracts string data to help with reverse engineering analysis
# Place this in your Ghidra scripts directory and run it from the Script Manager

import json
import os
from ghidra.program.model.data import StringDataType, TerminatedStringDataType, UnicodeDataType
from ghidra.program.model.listing import Data
from ghidra.program.model.symbol import SourceType

def export_strings():
    """Export all string data from the current Ghidra program"""
    
    print("Starting string export...")
    
    strings_data = {}
    memory = currentProgram.getMemory()
    listing = currentProgram.getListing()
    symbol_table = currentProgram.getSymbolTable()
    
    # Get all defined data in the program
    data_iterator = listing.getDefinedData(True)
    string_count = 0
    
    for data in data_iterator:
        data_type = data.getDataType()
        address = data.getAddress()
        
        # Check if this is a string data type
        if (isinstance(data_type, StringDataType) or 
            isinstance(data_type, TerminatedStringDataType) or
            isinstance(data_type, UnicodeDataType) or
            data_type.getName().lower().find("string") != -1):
            
            try:
                address_str = address.toString()
                
                # Get the string value
                string_value = ""
                try:
                    if data.hasStringValue():
                        string_value = str(data.getValue())
                    else:
                        # Try to read as bytes and convert
                        byte_data = data.getBytes()
                        string_value = ''.join(chr(b & 0xFF) for b in byte_data if (b & 0xFF) != 0)
                except:
                    # Fallback: read memory directly
                    try:
                        byte_length = data_type.getLength()
                        if byte_length <= 0:
                            byte_length = 256  # Max reasonable string length
                        
                        byte_array = [0] * byte_length
                        memory.getBytes(address, byte_array)
                        
                        # Convert to string, stopping at null terminator
                        chars = []
                        for b in byte_array:
                            if b == 0:
                                break
                            if 32 <= b <= 126:  # Printable ASCII
                                chars.append(chr(b))
                            else:
                                chars.append('\\x{:02x}'.format(b))
                        string_value = ''.join(chars)
                    except:
                        string_value = "[Could not read string]"
                
                # Get references to this string
                references = []
                refs = getReferencesTo(address)
                for ref in refs:
                    ref_addr = ref.getFromAddress()
                    ref_function = getFunctionContaining(ref_addr)
                    ref_info = {
                        "from_address": ref_addr.toString(),
                        "function": ref_function.getName() if ref_function else "Unknown",
                        "reference_type": str(ref.getReferenceType())
                    }
                    references.append(ref_info)
                
                # Get symbol name if it exists
                symbol_name = None
                symbols = symbol_table.getSymbols(address)
                for symbol in symbols:
                    if symbol.getSource() != SourceType.DEFAULT:
                        symbol_name = symbol.getName()
                        break
                
                strings_data[address_str] = {
                    "address": address_str,
                    "address_hex": "0x{:08x}".format(address.getOffset()),
                    "value": string_value,
                    "length": len(string_value),
                    "data_type": data_type.getName(),
                    "symbol_name": symbol_name,
                    "references": references,
                    "reference_count": len(references)
                }
                
                string_count += 1
                
                # Progress indicator
                if string_count % 100 == 0:
                    print("Processed {} strings...".format(string_count))
                    
            except Exception as e:
                print("Error processing string at {}: {}".format(address, str(e)))
                continue
    
    # Also look for potential strings that might not be explicitly typed
    print("Scanning for additional string patterns...")
    
    # Look for common format string patterns
    format_patterns = [
        "%d", "%s", "%x", "%02d", "%3d", "\\n", "\\t"
    ]
    
    # Search memory for these patterns
    for block in memory.getBlocks():
        if block.isInitialized() and block.isRead():
            try:
                block_start = block.getStart()
                block_size = block.getSize()
                
                # Read block in chunks to avoid memory issues
                chunk_size = min(block_size, 0x10000)  # 64KB chunks
                offset = 0
                
                while offset < block_size:
                    current_size = min(chunk_size, block_size - offset)
                    current_addr = block_start.add(offset)
                    
                    try:
                        byte_array = [0] * current_size
                        memory.getBytes(current_addr, byte_array)
                        
                        # Look for null-terminated strings
                        i = 0
                        while i < len(byte_array) - 4:  # Need at least 4 chars for a meaningful string
                            if 32 <= byte_array[i] <= 126:  # Start of potential string
                                string_start = i
                                chars = []
                                
                                # Collect printable characters until null or non-printable
                                while (i < len(byte_array) and 
                                       byte_array[i] != 0 and 
                                       (32 <= byte_array[i] <= 126 or byte_array[i] in [9, 10, 13])):  # Include tab, newline, CR
                                    chars.append(chr(byte_array[i]))
                                    i += 1
                                
                                # If we found a reasonable string and it ends with null
                                if (len(chars) >= 4 and 
                                    i < len(byte_array) and 
                                    byte_array[i] == 0 and
                                    any(pattern in ''.join(chars) for pattern in format_patterns)):
                                    
                                    string_addr = current_addr.add(string_start)
                                    addr_str = string_addr.toString()
                                    
                                    # Only add if not already found
                                    if addr_str not in strings_data:
                                        string_value = ''.join(chars)
                                        
                                        # Get references
                                        references = []
                                        refs = getReferencesTo(string_addr)
                                        for ref in refs:
                                            ref_addr = ref.getFromAddress()
                                            ref_function = getFunctionContaining(ref_addr)
                                            ref_info = {
                                                "from_address": ref_addr.toString(),
                                                "function": ref_function.getName() if ref_function else "Unknown",
                                                "reference_type": str(ref.getReferenceType())
                                            }
                                            references.append(ref_info)
                                        
                                        strings_data[addr_str] = {
                                            "address": addr_str,
                                            "address_hex": "0x{:08x}".format(string_addr.getOffset()),
                                            "value": string_value,
                                            "length": len(string_value),
                                            "data_type": "Discovered String",
                                            "symbol_name": None,
                                            "references": references,
                                            "reference_count": len(references)
                                        }
                                        string_count += 1
                            else:
                                i += 1
                                
                    except Exception as e:
                        print("Error scanning block at {}: {}".format(current_addr, str(e)))
                        break
                    
                    offset += chunk_size
                    
            except Exception as e:
                print("Error processing memory block {}: {}".format(block.getName(), str(e)))
                continue
    
    # Sort by address for easier browsing
    sorted_strings = dict(sorted(strings_data.items(), key=lambda x: int(x[1]["address_hex"], 16)))
    
    # Create summary statistics
    summary = {
        "total_strings": len(sorted_strings),
        "strings_with_references": sum(1 for s in sorted_strings.values() if s["reference_count"] > 0),
        "format_strings": sum(1 for s in sorted_strings.values() if any(pattern in s["value"] for pattern in ["%", "\\n", "\\t"])),
        "export_timestamp": "Generated by export_strings.py",
        "program_name": currentProgram.getName()
    }
    
    # Prepare final output
    output_data = {
        "summary": summary,
        "strings": sorted_strings
    }
    
    # Write to JSON file in the project directory (same as other export scripts)
    output_file = r"C:\Users\zptha\projects\orphen\decompiled\strings.json"
    
    try:
        with open(output_file, 'w') as f:
            json.dump(output_data, f, indent=2, ensure_ascii=False)
        
        print("\n" + "="*60)
        print("STRING EXPORT COMPLETE")
        print("="*60)
        print("Total strings found: {}".format(summary["total_strings"]))
        print("Strings with references: {}".format(summary["strings_with_references"]))
        print("Potential format strings: {}".format(summary["format_strings"]))
        print("Output file: {}".format(output_file))
        print("="*60)
        
        # Show some examples of the most referenced strings
        referenced_strings = [s for s in sorted_strings.values() if s["reference_count"] > 0]
        referenced_strings.sort(key=lambda x: x["reference_count"], reverse=True)
        
        print("\nMost referenced strings:")
        for i, string_info in enumerate(referenced_strings[:10]):
            print("  {} - {} refs: '{}'".format(
                string_info["address_hex"], 
                string_info["reference_count"],
                string_info["value"][:50] + ("..." if len(string_info["value"]) > 50 else "")
            ))
        
        # Look for the specific addresses mentioned in the debug function
        debug_addresses = ["0x0034ca60", "0x0034ca78"]
        print("\nDebug format strings from process_scene_with_work_flags:")
        for addr in debug_addresses:
            found = False
            for string_info in sorted_strings.values():
                if string_info["address_hex"].lower() == addr.lower():
                    print("  {} - '{}' ({} refs)".format(
                        addr, string_info["value"], string_info["reference_count"]
                    ))
                    found = True
                    break
            if not found:
                print("  {} - [Not found]".format(addr))
        
    except Exception as e:
        print("Error writing output file: {}".format(str(e)))
        print("Dumping first 10 strings to console:")
        count = 0
        for addr, info in sorted_strings.items():
            if count >= 10:
                break
            print("  {} - '{}'".format(info["address_hex"], info["value"][:100]))
            count += 1

# Run the export
export_strings()
