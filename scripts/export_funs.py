# ExportOrphenFunctions.py - Jython script for Ghidra
# Exports all FUN_* functions to individual .c files

import os
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor

# Configuration
OUTPUT_DIR = "C:/Users/zptha/projects/orphen/decompiled/src/"
FUNCTION_PREFIX = "FUN_"

# Get current program and listing
program = getCurrentProgram()
listing = program.getListing()

# Create output directory if it doesn't exist
if not os.path.exists(OUTPUT_DIR):
    os.makedirs(OUTPUT_DIR)

# Initialize decompiler
decompiler = DecompInterface()
decompiler.openProgram(program)

# Create task monitor for progress
monitor = ConsoleTaskMonitor()

# Get all functions
functions = listing.getFunctions(True)
exported_count = 0

print("Starting function export...")

for func in functions:
    # Get function name and address
    func_name = func.getName()
    func_addr = func.getEntryPoint().toString()
    
    # Skip if not a FUN_ function
    if not func_name.startswith(FUNCTION_PREFIX):
        continue
    
    # Skip if file already exists (optional - remove this if you want to overwrite)
    filename = os.path.join(OUTPUT_DIR, func_name + ".c")
    if os.path.exists(filename):
        print("Skipping existing file: " + func_name)
        continue
    
    print("Exporting: " + func_name + " at " + func_addr)
    
    # Decompile the function
    try:
        results = decompiler.decompileFunction(func, 30, monitor)
        
        if results.decompileCompleted():
            # Get the decompiled C code
            decomp_func = results.getDecompiledFunction()
            if decomp_func is not None:
                c_code = decomp_func.getC()
                
                # Write to file
                with open(filename, 'w') as f:
                    f.write(c_code)
                
                exported_count += 1
                print("  -> Exported successfully")
            else:
                print("  -> Decompilation returned null")
        else:
            print("  -> Decompilation failed: " + str(results.getErrorMessage()))
            
    except Exception as e:
        print("  -> Error: " + str(e))

# Cleanup
decompiler.dispose()

print("\nExport complete! Exported " + str(exported_count) + " functions to " + OUTPUT_DIR)
