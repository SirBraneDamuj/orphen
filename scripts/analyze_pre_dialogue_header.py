#!/usr/bin/env python3
"""Parse and compare pre-dialogue header region of scr*.out files.

Heuristic:
  - First dialogue pointer table's first entry = 0x2C offset.
  - Bytes [0,0x2C) hold a sequence of uint32 LE values (aligned every 4 bytes).
  - Identify which of these look like in-file offsets (< file_size) vs possible memory/virtual addresses (> file_size or suspicious gaps).
  - Try to classify which one equals start of pointer table region (we know for scr2 it's 0x1680) and which equals post-table region start (0x17D4 etc.).
Outputs:
  JSON summary per file and a side-by-side table.
"""
import os, struct, json, argparse

HEADER_BOUNDARY = 0x2C

def read_u32(buf, off):
    if off+4>len(buf): return None
    return struct.unpack_from('<I', buf, off)[0]

def analyze(path):
    data=open(path,'rb').read()
    size=len(data)
    header=data[:HEADER_BOUNDARY]
    values=[read_u32(header,i) for i in range(0,HEADER_BOUNDARY,4)]
    classified=[]
    for idx,v in enumerate(values):
        if v is None:
            cls='truncated'
            tag=''
            hexv=None
        else:
            cls='in_file' if v < size else '>=file_size'
            # heuristic tags (conservative)
            if cls=='in_file' and v==0x1680:
                tag='known_pointer_table_start (scr2)'
            else:
                tag=''
            hexv=hex(v)
        classified.append({'index':idx,'offset':idx*4,'value':v,'hex':hexv,'class':cls,'tag':tag})
    # Derived semantics (assumes layout established from comparative analysis)
    derived={}
    try:
        v = [c['value'] for c in classified]
        # indices mapping
        idx_pointer_table_start=5
        idx_pointer_table_end=0
        idx_struct_before_arrays=6
        idx_array_a=8
        idx_array_b=9
        idx_array_c=10
        idx_footer=7
        start=v[idx_pointer_table_start]
        end=v[idx_pointer_table_end]
        if isinstance(start,int) and isinstance(end,int) and end>start:
            table_bytes=end-start
            entries_u32=table_bytes//4
            derived['pointer_table_bytes']=table_bytes
            derived['pointer_table_entries_u32']=entries_u32
            if entries_u32>0:
                derived['pointer_table_pointer_count']=entries_u32-1  # assume last sentinel
        # struct + arrays
        struct_start=v[idx_struct_before_arrays]
        array_a=v[idx_array_a]
        array_b=v[idx_array_b]
        array_c=v[idx_array_c]
        if all(isinstance(x,int) for x in (struct_start,array_a)) and array_a>struct_start:
            derived['descriptor_block_size']=array_a-struct_start
        if all(isinstance(x,int) for x in (array_a,array_b,array_c)):
            derived['triple_array_delta_ab']=array_b-array_a
            derived['triple_array_delta_bc']=array_c-array_b
            derived['triple_arrays_uniform_4byte']= (array_b-array_a==4 and array_c-array_b==4)
        footer=v[idx_footer]
        if isinstance(footer,int) and footer<size:
            derived['footer_length']=size-footer
    except Exception as e:
        derived['error']=str(e)
    derived['section_index_map']={
        'pointer_table_end_first_block':0,
        'block1':1,
        'block2':2,
        'block3':3,
        'block4':4,
        'pointer_table_start':5,
        'struct_before_arrays':6,
        'footer_start':7,
        'array_a_start':8,
        'array_b_start':9,
        'array_c_start':10
    }
    return {'file':os.path.basename(path),'size':size,'header_values':classified,'derived':derived}

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('files', nargs='+')
    ap.add_argument('--json-out')
    args=ap.parse_args()
    reports=[analyze(f) for f in args.files]
    # Cross-file value occurrence map
    cross={}  
    for r in reports:
        for hv in r['header_values']:
            cross.setdefault(hv['value'],[]).append(r['file'])
    for r in reports:
        for hv in r['header_values']:
            hv['appears_in']=cross[hv['value']]
    out={'reports':reports}
    js=json.dumps(out,indent=2)
    if args.json_out:
        open(args.json_out,'w').write(js)
    else:
        print(js)

if __name__=='__main__':
    main()
