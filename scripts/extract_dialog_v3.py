#!/usr/bin/env python3
"""
Purpose: Extract structured dialogue from decompressed script binaries (e.g., scr2.out) using
explicit control markers inferred from reverse engineering observations:

Markers:
  - 0x13 0x00 : Start of a speaker name string (speaker name bytes follow until 0x00 terminator).
                The 0x13 byte is preceded by a 0x00 forming the word 0x0013 in little endian
                when viewing as 16-bit units in earlier notes. We'll match the byte sequence 0x13 0x00
                OR 0x00 0x13 to be tolerant; by default we expect 0x13 followed by ASCII and ending 0x00.
  - 0x1A 0x00 : Advance to next speaker (end of current speaker's line group within a scene).
  - 0x1A 0x01 : End of scene (terminates current scene and starts a new one on next speaker).

Simplifying assumptions (adjust as analysis refines):
  * Speaker name bytes are printable ASCII (A-Z a-z space allowed) terminated by 0x00.
  * Dialogue text immediately follows speaker name termination OR follows previous control marker.
  * Dialogue text is a run of printable character opcodes (already present verbatim as ASCII bytes)
    until the next control marker 0x1A?? or next speaker name marker 0x13 00.
  * Single isolated short "lines" of length 1 (or purely punctuation) may represent timing/pause control;
    we record them with a flag control_like=True if they are <=1 char or in a small punctuation set.

Output JSON structure:
{
  "file": "scr2.out",
  "scenes": [
      {
        "index": 0,
        "start_offset": <byte offset where first speaker marker for scene encountered>,
        "speakers": [
            {"name":"Cleo","start_offset":1234,"lines":[{"text":"Hello there","offset":1240,"control_like":false}, ...]},
            ...
        ]
      }, ...
  ],
  "stats": { ... summary counts ... }
}

We deliberately DO NOT skip any header region; extraction is purely marker-driven so we can
inspect any false positives that appear before real dialogue.
"""
import argparse, json, pathlib, sys
from typing import List, Dict, Any, Optional

PRINTABLE_RANGE = set(range(0x20, 0x7F))

PAUSE_PUNCT = set([ord('.'), ord(','), ord('-')])

def is_printable(b: int) -> bool:
    return b in PRINTABLE_RANGE


def load_length_table(json_path: Optional[str]=None, mem_path: Optional[str]=None, addr: int=0x0031c518, count: int=0x80) -> Optional[List[int]]:
    """Load opcode length table either from a dumped JSON (produced by dump_length_table.py)
    or directly from eeMemory.bin. Returns list of byte lengths or None on failure."""
    try:
        if json_path:
            obj = json.loads(pathlib.Path(json_path).read_text())
            entries = obj.get('entries') or []
            table = [0]*len(entries)
            for e in entries:
                idx = e.get('index')
                ln = e.get('len')
                if isinstance(idx,int) and isinstance(ln,int):
                    if idx < len(table):
                        table[idx] = ln
            return table
        if mem_path:
            data = pathlib.Path(mem_path).read_bytes()
            if addr + count <= len(data):
                span = data[addr:addr+count]
            else:
                span = data[addr:]
            return list(span)
    except Exception:
        return None
    return None


def parse_dialog(data: bytes, start: int=0, length_table: Optional[List[int]]=None) -> Dict[str, Any]:
    i = start
    size = len(data)
    scenes: List[Dict[str, Any]] = []
    current_scene: Optional[Dict[str, Any]] = None
    current_speaker: Optional[Dict[str, Any]] = None

    def new_scene(at: int):
        nonlocal current_scene, current_speaker
        current_scene = {"index": len(scenes), "start_offset": at, "speakers": []}
        scenes.append(current_scene)
        current_speaker = None

    def new_speaker(name: str, at: int):
        nonlocal current_scene, current_speaker, stat_speaker_markers
        if current_scene is None:
            new_scene(at)
        speaker = {"name": name, "start_offset": at, "lines": []}
        current_scene["speakers"].append(speaker)  # type: ignore
        current_speaker = speaker
        stat_speaker_markers += 1

    # Stats
    stat_scene_end = 0
    stat_speaker_advance = 0
    stat_speaker_markers = 0
    stat_lines = 0
    stat_control_0c = 0

    using_table = isinstance(length_table, list) and len(length_table) > 0

    # For legacy mode: controls before next line
    pending_controls: List[Dict[str, Any]] = []
    # For table mode: controls collected to attach to upcoming text run
    pending_line_controls: List[Dict[str, Any]] = []

    def finalize_text_run(buf: bytearray, at_offset: int):
        nonlocal stat_lines, pending_line_controls
        if not buf or current_speaker is None:
            buf.clear()
            pending_line_controls.clear()
            return
        # Interpret potential UTF-16LE pairs (char,0x00). We'll collapse where pattern matches.
        out_chars: List[str] = []
        j = 0
        while j < len(buf):
            c = buf[j]
            if j+1 < len(buf) and buf[j+1] == 0x00 and 0x20 <= c < 0x7F:
                out_chars.append(chr(c))
                j += 2
                continue
            if 0x20 <= c < 0x7F:
                out_chars.append(chr(c))
            else:
                out_chars.append(f"\\x{c:02X}")
            j += 1
        text = ''.join(out_chars).rstrip()
        if text:
            control_like = (len(text.strip()) <= 1)
            entry: Dict[str, Any] = {
                'text': text,
                'offset': at_offset,
                'length': len(buf),
                'control_like': control_like
            }
            if pending_line_controls:
                entry['controls'] = list(pending_line_controls)
            current_speaker['lines'].append(entry)  # type: ignore
            stat_lines += 1
        buf.clear()
        pending_line_controls.clear()

    if using_table:
        text_buf = bytearray()
        text_start = 0
        while i < size:
            b = data[i]
            if b == 0:  # hard terminator for stream chunk (assumption)
                finalize_text_run(text_buf, text_start)
                break
            # Control / structural opcodes (<=0x1E)
            if b <= 0x1E:
                # Flush any accumulated text before processing next control
                finalize_text_run(text_buf, text_start)
                if b == 0x13:  # speaker marker
                    i += 1
                    name_bytes = bytearray()
                    while i < size:
                        c = data[i]
                        if c == 0x00:
                            i += 1
                            break
                        # UTF-16LE pair pattern
                        if i+1 < size and data[i+1] == 0x00 and 0x20 <= c < 0x7F:
                            name_bytes.append(c)
                            i += 2
                        else:
                            if 0x20 <= c < 0x7F:
                                name_bytes.append(c)
                            i += 1
                    if name_bytes:
                        try:
                            name = name_bytes.decode('ascii')
                        except Exception:
                            name = ''.join(chr(x) if 32 <= x < 127 else f"\\x{x:02X}" for x in name_bytes)
                        new_speaker(name, i - len(name_bytes) - 1)
                    continue
                if b == 0x1A and i+1 < size:
                    mode = data[i+1]
                    if mode == 0x00:
                        current_speaker = None
                        stat_speaker_advance += 1
                    elif mode == 0x01:
                        current_speaker = None
                        current_scene = None
                        stat_scene_end += 1
                    i += 2
                    continue
                if b == 0x16 and i + 5 <= size:  # voice packet
                    mode = data[i+1]
                    sep = data[i+2]
                    vid = data[i+3] | (data[i+4] << 8)
                    pending_line_controls.append({'offset': i, 'code': 'VOICE', 'mode': mode, 'sep': sep, 'voice_id': vid})
                    adv = 5
                    if length_table and b < len(length_table) and length_table[b]:
                        adv = length_table[b]
                    i += adv
                    continue
                if b == 0x0C and i+2 <= size:  # delay
                    param = data[i+1]
                    pending_line_controls.append({'offset': i, 'code': 'DELAY', 'param': param})
                    stat_control_0c += 1
                    adv = 2
                    if length_table and b < len(length_table) and length_table[b]:
                        adv = length_table[b]
                    i += adv
                    continue
                # Generic skip for known fixed-length controls
                if length_table and b < len(length_table) and length_table[b] > 0:
                    i += length_table[b]
                    continue
                # Zero-length opcode (0x15,0x1E, etc.)
                i += 1
                continue
            # Text byte ( >0x1E )
            if not text_buf:
                text_start = i
            # Collect potential UTF-16LE pair if next is 0x00
            if i+1 < size and data[i+1] == 0x00:
                text_buf.extend(data[i:i+2])
                i += 2
            else:
                text_buf.append(data[i])
                i += 1
        # End of stream flush
        finalize_text_run(text_buf, text_start)
        return {
            'scenes': scenes,
            'stats': {
                'scene_count': len(scenes),
                'speaker_markers': stat_speaker_markers,
                'speaker_advances': stat_speaker_advance,
                'scene_end_markers': stat_scene_end,
                'lines': stat_lines,
                'control_0c': stat_control_0c
            }
        }

    # Legacy heuristic mode (no length table)
    while i < size:
        b = data[i]
        if b == 0x13:  # speaker marker
            j = i + 1
            name_bytes: List[int] = []
            while j < size and data[j] != 0x00:
                c = data[j]
                if 0x20 <= c < 0x7F:
                    name_bytes.append(c)
                else:
                    name_bytes = []
                    break
                j += 1
            if name_bytes and j < size and data[j] == 0x00:
                name = bytes(name_bytes).decode('ascii', errors='ignore')
                new_speaker(name, i)
                i = j + 1
                pending_controls.clear()
                continue
        if b == 0x0C and i+1 < size:  # delay control queued for next line
            param = data[i+1]
            stat_control_0c += 1
            pending_controls.append({'offset': i, 'code': 'DELAY', 'param': param})
            i += 2
            continue
        if b == 0x1A and i+1 < size and data[i+1] == 0x00:  # speaker advance
            current_speaker = None
            stat_speaker_advance += 1
            pending_controls.clear()
            i += 2
            continue
        if b == 0x1A and i+1 < size and data[i+1] == 0x01:  # scene end
            current_speaker = None
            current_scene = None
            stat_scene_end += 1
            pending_controls.clear()
            i += 2
            continue
        if current_speaker is not None and b not in (0x13, 0x1A):
            line_start = i
            line_bytes: List[int] = []
            line_controls: List[Dict[str, Any]] = []
            if pending_controls:
                line_controls.extend(pending_controls)
                pending_controls.clear()
            while i < size:
                c = data[i]
                if c in (0x13, 0x1A):
                    break
                if c == 0x0C and i+1 < size:
                    param_inline = data[i+1]
                    line_controls.append({'offset': i, 'code': 'DELAY', 'param': param_inline})
                    stat_control_0c += 1
                    i += 2
                    continue
                line_bytes.append(c)
                i += 1
            if line_bytes:
                tokens = []
                for by in line_bytes:
                    if 0x20 <= by < 0x7F:
                        tokens.append(chr(by))
                    else:
                        tokens.append(f"\\x{by:02X}")
                while tokens and tokens[-1] == ' ':
                    tokens.pop()
                if tokens:
                    txt = ''.join(tokens)
                    ascii_only = ''.join(ch for ch in txt if ch not in ('\\','x'))
                    control_like = (len(ascii_only.strip()) <= 1)
                    entry: Dict[str, Any] = {
                        'text': txt,
                        'offset': line_start,
                        'length': len(line_bytes),
                        'control_like': control_like
                    }
                    if line_controls:
                        entry['controls'] = line_controls
                    current_speaker['lines'].append(entry)  # type: ignore
                    stat_lines += 1
            continue
        i += 1

    return {
        'scenes': scenes,
        'stats': {
            'scene_count': len(scenes),
            'speaker_markers': stat_speaker_markers,
            'speaker_advances': stat_speaker_advance,
            'scene_end_markers': stat_scene_end,
            'lines': stat_lines,
            'control_0c': stat_control_0c
        }
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--file', required=True)
    ap.add_argument('--start', type=lambda x:int(x,0), default=0)
    ap.add_argument('--json', action='store_true')
    ap.add_argument('--length-table-json', help='Path to JSON dump of length table (from dump_length_table.py) to enable table-driven parsing')
    ap.add_argument('--length-table-mem', help='Optional eeMemory.bin to read length table directly (ignored if JSON provided)')
    ap.add_argument('--length-table-addr', type=lambda x:int(x,0), default=0x0031c518)
    ap.add_argument('--length-table-count', type=lambda x:int(x,0), default=0x80)
    ap.add_argument('--limit-scenes', type=int, default=None)
    args = ap.parse_args()
    data = pathlib.Path(args.file).read_bytes()
    length_table = load_length_table(args.length_table_json, args.length_table_mem, args.length_table_addr, args.length_table_count)
    result = parse_dialog(data, args.start, length_table)
    if args.limit_scenes is not None and len(result['scenes']) > args.limit_scenes:
        result['scenes'] = result['scenes'][:args.limit_scenes]
    if args.json:
        json.dump({'file': args.file, **result}, sys.stdout, indent=2)
    else:
        print(f"File: {args.file} scenes={len(result['scenes'])} stats={result['stats']}")
        for sc in result['scenes'][: (args.limit_scenes or 10) ]:
            print(f"Scene {sc['index']} @0x{sc['start_offset']:06x} speakers={len(sc['speakers'])}")
            for sp in sc['speakers']:
                print(f"  Speaker {sp['name']} lines={len(sp['lines'])}")
                for ln in sp['lines'][:5]:
                    print(f"    {ln['text']!r}{' *' if ln['control_like'] else ''}")
    
if __name__ == '__main__':
    main()
