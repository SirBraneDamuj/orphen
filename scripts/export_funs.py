# Ghidra script to export all functions to individual files
import os
from ghidra.program.model.listing import Function

# Get current program
program = getCurrentProgram()
listing = program.getListing()

# Output directory
output_dir = "C:/Users/zptha/projects/orphen/decompiled/src/"

# Get all functions
functions = listing.getFunctions(True)

for func in functions:
    # Get function name and address
    func_name = func.getName()
    func_addr = func.getEntryPoint().toString()
    
    # Skip if not a FUN_ function
    if not func_name.startswith("FUN_"):
        continue
        
    # Get decompiled code
    decompiler = DecompInterface()
    decompiler.openProgram(program)
    results = decompiler.decompileFunction(func, 30, monitor)
    
    if results.decompileCompleted():
        # Write to file
        filename = os.path.join(output_dir, func_name + ".c")
        with open(filename, 'w') as f:
            f.write(results.getDecompiledFunction().getC())
        print("Exported: " + func_name)
    
    decompiler.dispose()
