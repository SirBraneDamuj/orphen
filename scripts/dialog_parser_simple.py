#!/usr/bin/env python3
"""Dialog parser (experimental) for Orphen script binaries using simplified control grammar.

Current working assumptions (per latest investigation):
    00 13 <name bytes> 00  => Speaker name token (0x0013 sentinel; name ASCII; 0x00 terminator).
    1A 00                 => Advance to next speaker (end of current speaker's last line set).
    1A 01                 => End of scene (close current scene; next speaker token opens new scene).
    Text                  => Printable ASCII (0x20-0x7E) between control tokens belongs to current speaker.

Parsing model:
    - Scan for speaker tokens; open a scene on first encountered speaker.
    - Accumulate printable runs; non-printable bytes end a run. On control token boundary flush any pending text into a line.
    - 1A00 ends the speaker; require a following 0013 to start next speaker.
    - 1A01 ends the scene; subsequent 0013 begins next scene index.

Output JSON structure (high-level):
{
    "scenes": [ { "index": <n>, "start_offset": <file offset>, "end_offset": <inclusive end>,
                                 "speakers": [ {"name": "Orphen", "lines": ["line1", "line2", ...]}, ...],
                                 "events": [ ordered event log for debugging ] }, ... ]
}

This intentionally does NOT attempt to remove or skip any leading file header; raw parsing from offset 0.
All heuristics are provisional and subject to revision as more control byte semantics are discovered.
"""
import argparse, json, pathlib, sys
from typing import List, Dict, Any, Optional

SPEAKER_SENTINEL_LOW = 0x13  # expected after 0x00
CONTROL_MARK = 0x1A          # followed by subtype (00 next speaker, 01 end scene)

PRINTABLE_MIN = 0x20
PRINTABLE_MAX = 0x7E

def is_printable(b: int) -> bool:
    return PRINTABLE_MIN <= b <= PRINTABLE_MAX

def parse_dialog(data: bytes) -> Dict[str,Any]:
    scenes: List[Dict[str,Any]] = []
    cur_scene: Optional[Dict[str,Any]] = None
    cur_speaker: Optional[str] = None
    speakers_order: List[str] = []  # maintain order as encountered in scene
    speaker_lines: Dict[str,List[str]] = {}
    events: List[Dict[str,Any]] = []
    i = 0
    scene_index = 0
    scene_start_offset = None
    pending_text: List[int] = []
    pending_text_off: Optional[int] = None

    def flush_scene(final_offset: int):
        nonlocal cur_scene, speakers_order, speaker_lines, events, scene_index
        if cur_scene is None:
            return
        scenes.append({
            'index': cur_scene['index'],
            'start_offset': cur_scene['start_offset'],
            'end_offset': final_offset,
            'speakers': [{'name': sp, 'lines': speaker_lines.get(sp, [])} for sp in speakers_order],
            'events': events
        })
        cur_scene = None

    def ensure_scene(start_off: int):
        nonlocal cur_scene, scene_index, scene_start_offset, speakers_order, speaker_lines, events
        if cur_scene is None:
            cur_scene = {'index': scene_index, 'start_offset': start_off}
            scene_start_offset = start_off
            speakers_order = []
            speaker_lines = {}
            events = []

    data_len = len(data)
    while i < data_len:
        b = data[i]
        # Speaker token: 00 13 name... 00
        if b == 0x00 and i + 1 < data_len and data[i+1] == SPEAKER_SENTINEL_LOW:
            # flush any pending text for previous speaker
            if pending_text and cur_speaker:
                text = bytes(pending_text).decode('ascii','replace').strip()
                if text:
                    if cur_speaker not in speaker_lines:
                        speaker_lines[cur_speaker] = []
                        speakers_order.append(cur_speaker)
                    speaker_lines[cur_speaker].append(text)
                    events.append({'type':'line','offset':pending_text_off,'speaker':cur_speaker,'text':text})
            pending_text = []
            pending_text_off = None
            name_off = i
            i += 2
            name_bytes = []
            while i < data_len and data[i] != 0x00:
                name_bytes.append(data[i])
                i += 1
            if i < data_len and data[i] == 0x00:
                i += 1
            name = bytes(name_bytes).decode('ascii','ignore').strip() or 'UNKNOWN'
            ensure_scene(name_off)
            cur_speaker = name
            if cur_speaker not in speaker_lines:
                speaker_lines[cur_speaker] = []
                speakers_order.append(cur_speaker)
            events.append({'type':'speaker','offset':name_off,'name':cur_speaker})
            continue
        # Control marker 1A subtype
        if b == CONTROL_MARK and i + 1 < data_len:
            subtype = data[i+1]
            mark_off = i
            i += 2
            # flush pending line
            if pending_text and cur_speaker:
                text = bytes(pending_text).decode('ascii','replace').strip()
                if text:
                    if cur_speaker not in speaker_lines:
                        speaker_lines[cur_speaker] = []
                        speakers_order.append(cur_speaker)
                    speaker_lines[cur_speaker].append(text)
                    events.append({'type':'line','offset':pending_text_off,'speaker':cur_speaker,'text':text})
            pending_text = []
            pending_text_off = None
            if subtype == 0x00:  # next speaker
                events.append({'type':'speaker_advance','offset':mark_off})
                cur_speaker = None
            elif subtype == 0x01:  # scene end
                events.append({'type':'scene_end','offset':mark_off})
                flush_scene(mark_off+1)
                scene_index += 1
                cur_speaker = None
            else:
                events.append({'type':'control','offset':mark_off,'subtype':subtype})
            continue
        # Accumulate printable text for current speaker
        if is_printable(b) and cur_speaker:
            if not pending_text:
                pending_text_off = i
            pending_text.append(b)
            i += 1
            continue
        # Non printable; break line if we are in middle of accumulating
        if pending_text and cur_speaker:
            text = bytes(pending_text).decode('ascii','replace').strip()
            if text:
                if cur_speaker not in speaker_lines:
                    speaker_lines[cur_speaker] = []
                    speakers_order.append(cur_speaker)
                speaker_lines[cur_speaker].append(text)
                events.append({'type':'line','offset':pending_text_off,'speaker':cur_speaker,'text':text})
            pending_text = []
            pending_text_off = None
        i += 1

    # Flush trailing text & scene
    if pending_text and cur_speaker:
        text = bytes(pending_text).decode('ascii','replace').strip()
        if text:
            if cur_speaker not in speaker_lines:
                speaker_lines[cur_speaker] = []
                speakers_order.append(cur_speaker)
            speaker_lines[cur_speaker].append(text)
            events.append({'type':'line','offset':pending_text_off,'speaker':cur_speaker,'text':text})
    if cur_scene is not None:
        flush_scene(len(data)-1)
    return {'scenes': scenes}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--file', required=True)
    ap.add_argument('--json', action='store_true')
    # No extra options for now; keep interface minimal.
    args = ap.parse_args()

    data = pathlib.Path(args.file).read_bytes()
    result = parse_dialog(data)
    if args.json:
        json.dump(result, sys.stdout, indent=2)
    else:
        for sc in result['scenes']:
            print(f"Scene {sc['index']} @0x{sc['start_offset']:06X} - 0x{sc['end_offset']:06X}")
            for sp in sc['speakers']:
                print(f"  [{sp['name']}] lines={len(sp['lines'])}")
                for line in sp['lines'][:8]:
                    print(f"    {line}")
            print()

if __name__ == '__main__':
    main()
