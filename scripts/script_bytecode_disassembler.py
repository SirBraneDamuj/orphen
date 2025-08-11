#!/usr/bin/env python3
"""
Initial exploratory disassembler for the Orphen script VM bytecode (FUN_0025c258 family).

Goals (phase 1):
  * Parse the two known jump tables (standard opcodes 0x32-0xFE, extended 0xFF nn) from the exported txt listings.
  * Provide a linear disassembler that walks a byte stream starting at a given offset until it reaches a probable terminator (opcode 0x0b) or exceeds a max length.
  * Handle immediate/data-producing low opcodes handled in FUN_0025bf70 (0x0c-0x11, 0x30, 0x31) with correct byte lengths so alignment is preserved.
  * Emit JSON / text describing opcode sequence with original target function addresses (FUN_XXXXXXXX) as placeholders for semantic names.
  * Provide a heuristic scan mode to locate plausible script entry points within a decompressed sector file (e.g. scr2.out) by looking for sequences that successfully parse and end in 0x0b within a bounded size.

Caveats:
  * Many opcode semantics are still unknown; we only record structure and referenced handler function addresses.
  * 0x30 / 0x31 construct a 32-bit composite value via nested interpreter calls; we cannot yet fully expand them without recursive evaluation. For now we just mark them and treat as single-byte opcodes (the nested calls will appear inline as their own opcodes in a real execution context). We may later build an expression tree representation.
  * Branch / call control (opcode 0x32 and mechanism FUN_0025c220) is not yet modeled. We only do linear decode and will stop if we encounter 0x32 to avoid mis-following relative structure until we map its exact semantics.

Usage examples:
    python script_bytecode_disassembler.py --file scr2.out --start 0x0 --count 256
    python script_bytecode_disassembler.py --file scr2.out --scan --max-script-size 1536 --limit 100
    python script_bytecode_disassembler.py --file scr2.out --blocks --min-block 8 --max-block 160 --json
    python script_bytecode_disassembler.py --file scr2.out --frequency

Outputs to stdout.
"""
import argparse, json, re, sys, pathlib
from typing import List, Tuple, Dict, Optional, Any, Set

ROOT = pathlib.Path(__file__).resolve().parent.parent  # project root (.. from analyzed/)
SRC = ROOT / 'src'
JT_STD = SRC / 'PTR_LAB_0031e228.txt'
JT_EXT = SRC / 'PTR_LAB_0031e538.txt'

# Opcode ranges based on analysis:
#  - Low opcodes (< 0x32) handled partly by FUN_0025bf70 and a small table (&PTR_LAB_0031e1f8) for <0x0b excluding 4.
#  - 0x0b appears to be a return/exit in bytecode_interpreter switch.
#  - 0x0c - 0x11 are immediates with variable byte lengths.
#  - 0x30 / 0x31 are composite immediate builders (invoke nested interpreter calls four times) -> treat as standalone for linear pass.
#  - 0x32 - 0xFE (except 0xFF) index standard jump table entry N = opcode - 0x32.
#  - 0xFF <nn> extended => index extended jump table at <nn>, logical opcode value 0x100 + <nn>.

IMMEDIATE_OPCODES = {
    0x0c: ('IMM8', 2),   # opcode + 1 byte
    0x0d: ('IMM16', 3),  # opcode + 2 bytes (little endian 16)
    0x0e: ('IMM32_24?','VAR'), # opcode + 4? Actually 0x0e reads 4 bytes (3 + 1) => total 5
    0x0f: ('IMM_S32x100','5'),
    0x10: ('IMM_S16x1000','3'),
    0x11: ('IMM_S16*0xf570/0x168','3'),
}

# Fixed lengths for known immediate groups (from FUN_0025bf70)
FIXED_LENGTHS = {0x0c:2,0x0d:3,0x0e:5,0x0f:5,0x10:3,0x11:3}

TERMINATOR = 0x0b  # return/exit
CALL_LIKE = 0x32   # special control altering IP (FUN_0025c220)
EXTENDED_PREFIX = 0xff

# We haven't parsed small table (&PTR_LAB_0031e1f8) yet; mark unknown low ops (<0x0b) except 4/terminator.

PTR_LINE_RE = re.compile(r"^[0-9a-fA-F]{8} +([0-9a-fA-F]{2} ){4} +addr +([A-Z0-9_]{9,})")

# Provisional direct character opcode map (printable glyph emit). These bytes appear verbatim in dialogue.
# Reserved opcodes among ASCII ranges (0x0b, 0x0c-0x11, 0x30, 0x31, 0x32, 0xff) are excluded even if printable.
CHAR_OPCODE_MAP = {
    0x20:' ',0x21:'!',0x27:"'",0x2C:',',0x2D:'-',0x2E:'.',0x3C:'<',0x3F:'?',
    0x41:'A',0x42:'B',0x43:'C',0x44:'D',0x46:'F',0x48:'H',0x49:'I',0x4A:'J',
    0x4B:'K',0x4C:'L',0x4D:'M',0x4E:'N',0x4F:'O',0x52:'R',0x53:'S',0x54:'T',
    0x55:'U',0x56:'V',0x57:'W',0x59:'Y',0x5A:'Z',0x5B:'[',0x5D:']',
    0x61:'a',0x62:'b',0x63:'c',0x64:'d',0x65:'e',0x66:'f',0x67:'g',0x68:'h',
    0x69:'i',0x6A:'j',0x6B:'k',0x6C:'l',0x6D:'m',0x6E:'n',0x6F:'o',0x70:'p',
    0x72:'r',0x73:'s',0x74:'t',0x75:'u',0x76:'v',0x77:'w',0x78:'x',0x79:'y',0x7A:'z'
}

RESERVED_OPS: Set[int] = {TERMINATOR, *FIXED_LENGTHS.keys(), 0x30, 0x31, CALL_LIKE, EXTENDED_PREFIX}

def is_char_op(op: int) -> bool:
    return op in CHAR_OPCODE_MAP and op not in RESERVED_OPS

def parse_jump_table(path: pathlib.Path) -> List[str]:
    entries = []
    if not path.exists():
        return entries
    for line in path.read_text(encoding='utf-8', errors='ignore').splitlines():
        m = PTR_LINE_RE.match(line.strip())
        if m:
            func = m.group(2)
            entries.append(func)
    return entries

class Disassembler:
    def __init__(self, data: bytes, coalesce_text: bool=False):
        self.data = data
        self.std = parse_jump_table(JT_STD)
        self.ext = parse_jump_table(JT_EXT)
        self.coalesce_text = coalesce_text

    def decode_one(self, off: int, depth: int=0) -> Tuple[Optional[Dict[str,Any]], int, Optional[str]]:
        if off >= len(self.data):
            return None, off, 'EOF'
        op = self.data[off]
        # Coalesce text run if enabled and this byte appears to be a char opcode.
        if self.coalesce_text and is_char_op(op):
            j = off
            chars = []
            while j < len(self.data):
                b = self.data[j]
                if not is_char_op(b):
                    break
                chars.append(CHAR_OPCODE_MAP.get(b, '?'))
                j += 1
            return ({'offset':off,'opcode':None,'mnemonic':'TEXT','text':'"'+''.join(chars)+'"','length':j-off,'chars':len(chars)} if chars else None, j, None)
        if op == TERMINATOR:
            return {'offset': off, 'opcode': op, 'mnemonic':'RET','length':1}, off+1, None
        if op in FIXED_LENGTHS:
            length = FIXED_LENGTHS[op]
            if off+length>len(self.data):
                return None, off, 'TRUNC'
            imm_bytes = self.data[off+1:off+length]
            val = None
            if op == 0x0c:
                val = imm_bytes[0]
            elif op == 0x0d:
                val = imm_bytes[0] | (imm_bytes[1]<<8)
            elif op == 0x0e:
                # 3 + 1 bytes assembled as uVar3 | (byte4<<24)
                v = imm_bytes[0] | (imm_bytes[1]<<8) | (imm_bytes[2]<<16) | (imm_bytes[3]<<24)
                val = v
            elif op == 0x0f:
                val = int.from_bytes(imm_bytes,'little',signed=False) * 100
            elif op == 0x10:
                val = int.from_bytes(imm_bytes,'little',signed=True) * 1000
            elif op == 0x11:
                s16 = int.from_bytes(imm_bytes,'little',signed=True)
                val = (s16 * 0xF570)//0x168
            return {'offset':off,'opcode':op,'mnemonic':IMMEDIATE_OPCODES.get(op,(f'OP_{op:02X}',))[0],'value':val,'raw':imm_bytes.hex(),'length':length}, off+length, None
        if op == CALL_LIKE:
            # Treat next 4 bytes as parameter payload, NOT as a relative target (real target comes from separate stream).
            if off+5 > len(self.data):
                return None, off, 'TRUNC'
            pbytes = self.data[off+1:off+5]
            inst: Dict[str,Any] = {
                'offset':off,
                'opcode':op,
                'mnemonic':'BLOCK_ENTER',
                'params_hex':pbytes.hex(),
                'p0':pbytes[0], 'p1':pbytes[1], 'p2':pbytes[2], 'p3':pbytes[3],
                'length':5,
                'note':'0x32 param bytes captured; target resolved via separate length-chain (unimplemented)'
            }
            return inst, off+5, None
        if op == EXTENDED_PREFIX:
            if off+2>len(self.data):
                return None, off, 'TRUNC'
            idx = self.data[off+1]
            handler = self.ext[idx] if idx < len(self.ext) else 'EXT_UNKNOWN'
            return {'offset':off,'opcode':0x100+idx,'prefix':0xff,'ext_index':idx,'handler':handler,'mnemonic':handler,'length':2}, off+2, None
        if op >= 0x32:
            idx = op - 0x32
            handler = self.std[idx] if 0 <= idx < len(self.std) else 'STD_UNKNOWN'
            return {'offset':off,'opcode':op,'handler':handler,'mnemonic':handler,'length':1}, off+1, None
        # Low miscellaneous opcodes (0x00-0x0a, 0x12-0x31 except those handled above)
        return {'offset':off,'opcode':op,'mnemonic':f'LOW_{op:02X}','length':1}, off+1, None

    def disassemble(self, start: int, count: Optional[int]=None, max_bytes: int=0x1000, depth: int=0) -> List[Dict[str,Any]]:
        out=[]
        off=start
        end = len(self.data) if count is None else min(len(self.data), start+count)
        while off < len(self.data) and off < start+max_bytes and (count is None or off<end):
            inst, new_off, err = self.decode_one(off, depth)
            if err:
                out.append({'offset':off,'error':err})
                break
            if inst is None:
                out.append({'offset':off,'error':'DECODE_NONE'})
                break
            out.append(inst)
            off = new_off
            if inst.get('mnemonic')=='RET':
                break
        return out

    def scan_scripts(self, max_script_size: int=2048, limit: int=50):
        candidates=[]
        checked=0
        for off in range(0, len(self.data)):
            if checked>=limit:
                break
            seq=self.disassemble(off, max_bytes=max_script_size)
            if seq and seq[-1].get('mnemonic')=='RET':
                # crude heuristic: length >=2, no error entries, and all opcodes recognized
                if all('error' not in i for i in seq) and len(seq)>=2:
                    candidates.append({'start':off,'length':seq[-1]['offset']-off+seq[-1]['length'],'instructions':len(seq)})
                    checked+=1
        return candidates

    def extract_blocks(self, min_block: int, max_block: int) -> List[Dict[str,Any]]:
        """Linear pass splitting on RET (0x0b). Does NOT validate control flow for 0x32 blocks.
        Useful for quickly surfacing candidate script fragments in decompressed sectors (scr2.out).
        """
        blocks: List[Dict[str,Any]] = []
        off = 0
        while off < len(self.data):
            seq = self.disassemble(off, max_bytes=max_block)
            if not seq:
                break
            last = seq[-1]
            # Determine consumed length
            consumed = (last['offset'] + last.get('length',1)) - off if 'offset' in last else 1
            # Basic filters
            if seq and last.get('mnemonic')=='RET' and consumed >= min_block and consumed <= max_block and all('error' not in i for i in seq):
                blocks.append({
                    'start': off,
                    'end': off+consumed,
                    'bytes': consumed,
                    'instructions': len(seq),
                    'text_runs': sum(1 for i in seq if i.get('mnemonic')=='TEXT'),
                })
            off += consumed if consumed>0 else 1
        return blocks

    def opcode_frequencies(self) -> Dict[str,Any]:
        counts: Dict[int,int] = {}
        off=0
        while off < len(self.data):
            inst, new_off, err = self.decode_one(off)
            if err or inst is None:
                # treat undecodable byte as raw
                b = self.data[off]
                counts[b]=counts.get(b,0)+1
                off += 1
                continue
            # If coalesced TEXT, count underlying bytes individually to maintain comparability
            if inst.get('mnemonic')=='TEXT':
                length = inst['length']
                for j in range(off, off+length):
                    b = self.data[j]
                    counts[b]=counts.get(b,0)+1
                off = new_off
                continue
            op = inst.get('opcode')
            if op is None:  # shouldn't happen except TEXT
                op = self.data[off]
            counts[op]=counts.get(op,0)+1
            off = new_off if new_off>off else off+1
        total = sum(counts.values()) or 1
        # Prepare readable dict with hex keys
        freq = {f'0x{k:02X}': {'count':v, 'pct': round(100.0*v/total,3)} for k,v in sorted(counts.items(), key=lambda kv: kv[1], reverse=True)}
        return {'total_bytes': total, 'unique_opcodes': len(counts), 'frequencies': freq}


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--file', required=True)
    ap.add_argument('--start', type=lambda x:int(x,0), default=0)
    ap.add_argument('--count', type=lambda x:int(x,0))
    ap.add_argument('--max-bytes', type=lambda x:int(x,0), default=0x400)
    ap.add_argument('--scan', action='store_true')
    ap.add_argument('--max-script-size', type=int, default=2048)
    ap.add_argument('--limit', type=int, default=40, help='Max candidate scripts to report in scan mode')
    ap.add_argument('--json', action='store_true')
    ap.add_argument('--coalesce-text', action='store_true', help='Group contiguous character opcodes into TEXT pseudo-instructions')
    ap.add_argument('--analyze-32', action='store_true', help='Collect statistics on 0x32 parameter bytes across file')
    ap.add_argument('--blocks', action='store_true', help='Extract linear blocks delimited by RET (0x0b) across entire file')
    ap.add_argument('--min-block', type=int, default=4, help='Minimum block size in bytes (with --blocks)')
    ap.add_argument('--max-block', type=int, default=4096, help='Maximum block size in bytes (with --blocks)')
    ap.add_argument('--frequency', action='store_true', help='Compute opcode byte frequency distribution')
    ap.add_argument('--tag-speakers', action='store_true', help='Heuristically tag speaker name TEXT tokens (names like Cleo, Magnus)')
    ap.add_argument('--speaker-context', action='store_true', help='With --tag-speakers: emit preceding opcode context for each speaker token')
    ap.add_argument('--dialogue-scan', action='store_true', help='Scan entire file for speaker-labelled dialogue segments and summarize opcode context')
    ap.add_argument('--max-dialogues', type=int, default=200, help='Limit number of dialogue segments reported in --dialogue-scan')
    ap.add_argument('--handler-usage', action='store_true', help='Summarize usage counts of standard/extended handler opcodes and common preceding immediate parameter contexts')
    ap.add_argument('--param-groups', action='store_true', help='Extract grouped immediate parameter sequences immediately preceding handler invocations with frequency stats')
    ap.add_argument('--ngrams', action='store_true', help='Compute frequent opcode n-grams (control oriented, excluding text glyph bytes)')
    ap.add_argument('--ng-min', type=int, default=3)
    ap.add_argument('--ng-max', type=int, default=6)
    ap.add_argument('--ng-top', type=int, default=40)
    ap.add_argument('--ng-exclude-text', action='store_true', help='Exclude probable text glyph opcodes from n-gram building')
    ap.add_argument('--param-window', type=int, default=6, help='Max immediates to look back when forming parameter groups')
    ap.add_argument('--segment-1a0100', action='store_true', help='Segment file on raw byte pattern 1A 01 00 (observed cutscene dialog advance trigger)')
    ap.add_argument('--seg-max', type=int, default=500, help='Limit number of segments emitted for --segment-1a0100')
    ap.add_argument('--skip-bytes', type=lambda x:int(x,0), default=0, help='With segmentation modes: manually skip first N bytes (header) before processing')
    ap.add_argument('--auto-skip-header', action='store_true', help='Heuristically skip pre-dialog header/control region before extracting segments')
    ap.add_argument('--auto-header-name-threshold', type=int, default=2, help='Number of distinct capitalized names needed to mark start of dialogue for auto header skip')
    ap.add_argument('--auto-header-scan-limit', type=lambda x:int(x,0), default=0x2000, help='Max bytes to scan when auto detecting header boundary')
    ap.add_argument('--auto-skip-safe-margin', type=int, default=16, help='Minimum bytes before first terminator pattern that must be preserved; if skip would intrude, it is reduced')
    ap.add_argument('--inspect-header', action='store_true', help='Treat initial dwords as offset table and dump target previews')
    ap.add_argument('--header-count', type=int, default=16, help='Number of initial 32-bit little-endian entries to treat as offsets')
    ap.add_argument('--header-span', type=int, default=48, help='Bytes of preview to show per offset')
    ap.add_argument('--classify-header', action='store_true', help='Auto-detect header (initial dwords) and classify each referenced region as code/data/text/pointer-table')
    ap.add_argument('--classify-bytes', type=int, default=512, help='Max bytes to analyze for each region classification')
    args=ap.parse_args()

    data=pathlib.Path(args.file).read_bytes()
    dis=Disassembler(data, coalesce_text=args.coalesce_text)

    if args.analyze_32:
        stats={'count':0,'positions':[{}, {}, {}, {}]}
        i=0
        while i+5 <= len(data):
            if data[i] == CALL_LIKE:
                stats['count'] +=1
                for pos in range(4):
                    b = data[i+1+pos]
                    d = stats['positions'][pos]
                    d[b]=d.get(b,0)+1
                i+=5
            else:
                i+=1
        # Prepare readable summary
        summary=[]
        for pos,dist in enumerate(stats['positions']):
            top = sorted(dist.items(), key=lambda kv: kv[1], reverse=True)[:8]
            summary.append({f'byte{pos}':[(f'0x{b:02X}',c) for b,c in top]})
        if args.json:
            json.dump({'total_block_enters':stats['count'],'top_values_per_position':summary}, sys.stdout, indent=2)
        else:
            print(f"Found {stats['count']} occurrences of opcode 0x32")
            for s in summary:
                [(label, arr)] = s.items()
                print(f"{label}: ", end='')
                print(', '.join(f"{b}:{c}" for b,c in arr))
        return

    if args.scan:
        cands=dis.scan_scripts(args.max_script_size, args.limit)
        if args.json:
            json.dump({'candidates':cands}, sys.stdout, indent=2)
        else:
            for c in cands:
                print(f"script_candidate start=0x{c['start']:06x} bytes={c['length']:4d} insns={c['instructions']}")
        return

    if args.dialogue_scan:
        # Force coalescing to treat names/text as TEXT pseudo-instructions
        dis.coalesce_text = True
        data = dis.data
        name_re = re.compile(r'^[A-Z][a-z]{1,11}$')
        # We'll perform a linear decode; keep sliding window of last few non-text ops
        segments = []
        window_ops: List[int] = []
        off = 0
        current_segment = None
        last_speaker = None
        def flush_segment():
            nonlocal current_segment
            if current_segment and current_segment['lines']:
                # compute opcode frequency inside
                op_freq: Dict[str,int] = {}
                for o in current_segment['intra_ops']:
                    op_freq[f'0x{o:02X}'] = op_freq.get(f'0x{o:02X}',0)+1
                current_segment['opcode_freq'] = op_freq
                segments.append(current_segment)
            current_segment = None
        while off < len(data) and len(segments) < args.max_dialogues:
            inst, new_off, err = dis.decode_one(off)
            if err or inst is None:
                break
            # maintain window of last 5 non-text opcodes
            if inst.get('mnemonic') != 'TEXT':
                op = inst.get('opcode')
                if op is not None:
                    window_ops.append(op & 0xFF)
                    if len(window_ops) > 5:
                        window_ops = window_ops[-5:]
            else:
                # Extract raw text (strip quotes from disassembler formatting)
                raw = inst.get('text','')
                if raw.startswith('"') and raw.endswith('"'):
                    raw_inner = raw[1:-1]
                else:
                    raw_inner = raw
                if name_re.match(raw_inner):
                    # New speaker begins a segment (flush previous if active)
                    if current_segment:
                        flush_segment()
                    current_segment = {
                        'speaker': raw_inner,
                        'start_offset': inst['offset'],
                        'preceding_ops': [f'0x{x:02X}' for x in window_ops],
                        'lines': [],
                        'intra_ops': []
                    }
                    last_speaker = raw_inner
                else:
                    # Dialogue line belonging to current speaker (or attributed via last_speaker)
                    if current_segment is None and last_speaker is not None:
                        # Start implicit segment (speaker line without explicit name header)
                        current_segment = {
                            'speaker': last_speaker,
                            'start_offset': inst['offset'],
                            'preceding_ops': [f'0x{x:02X}' for x in window_ops],
                            'lines': [],
                            'intra_ops': []
                        }
                    if current_segment is not None:
                        current_segment['lines'].append({'offset':inst['offset'],'text':raw_inner})
            # Collect intra ops (non-text) inside an active segment until next speaker name or flush condition
            if current_segment and inst.get('mnemonic') != 'TEXT' and inst.get('opcode') is not None:
                current_segment['intra_ops'].append(inst['opcode'] & 0xFF)
            # Heuristic: terminate segment on RET
            if current_segment and inst.get('mnemonic') == 'RET':
                flush_segment()
            off = new_off
        # Flush if unterminated at end
        if current_segment:
            flush_segment()
        if args.json:
            json.dump({'dialogues':segments}, sys.stdout, indent=2)
        else:
            for seg in segments:
                preview = seg['lines'][0]['text'][:40]+'...' if seg['lines'] else ''
                print(f"speaker={seg['speaker']:>8} start=0x{seg['start_offset']:06x} lines={len(seg['lines']):3d} pre_ops={seg['preceding_ops']} first='{preview}'")
        return

    if args.handler_usage or args.param_groups or args.ngrams:
        # Perform a single linear pass decode without coalescing text to build raw opcode stream & metadata.
        orig_coalesce = dis.coalesce_text
        dis.coalesce_text = False
        stream = []  # list of inst dicts
        off = 0
        while off < len(dis.data):
            inst, new_off, err = dis.decode_one(off)
            if err:
                # treat undecodable byte as raw placeholder
                stream.append({'offset':off,'opcode':dis.data[off],'mnemonic':'RAW'})
                off += 1
                continue
            stream.append(inst)
            off = new_off if new_off>off else off+1
        dis.coalesce_text = orig_coalesce

        if args.handler_usage:
            usage: Dict[str, Dict[str, Any]] = {}
            # track context signatures: last up to 4 opcodes (excluding TEXT-like) & last immediates values
            for idx, inst in enumerate(stream):
                if 'handler' in inst and inst.get('opcode') not in (CALL_LIKE,):
                    handler = inst['handler']
                    u = usage.setdefault(handler, {'count':0,'contexts':{}})
                    u['count'] += 1
                    # Gather preceding small window (exclude char glyphs) of opcode bytes
                    ctx_ops = []
                    j = idx-1
                    while j >=0 and len(ctx_ops)<4:
                        prev = stream[j]
                        opb = prev.get('opcode') if prev.get('opcode') is not None else None
                        if opb is not None and not is_char_op(opb & 0xFF):
                            ctx_ops.append(f"{opb & 0xFF:02X}")
                        j -=1
                    ctx_sig = ' '.join(reversed(ctx_ops))
                    if ctx_sig:
                        u['contexts'][ctx_sig] = u['contexts'].get(ctx_sig,0)+1
            # Prepare output
            usage_out = []
            for handler, info in sorted(usage.items(), key=lambda kv: kv[1]['count'], reverse=True):
                top_ctx = sorted(info['contexts'].items(), key=lambda kv: kv[1], reverse=True)[:6]
                usage_out.append({'handler':handler,'count':info['count'],'top_contexts':top_ctx})
            if args.json:
                json.dump({'handler_usage':usage_out}, sys.stdout, indent=2)
            else:
                for u in usage_out[:120]:
                    ctx_preview = ', '.join(f"[{sig}]x{cnt}" for sig,cnt in u['top_contexts'])
                    print(f"{u['handler']:>20} count={u['count']:5d} contexts: {ctx_preview}")
            if not (args.param_groups or args.ngrams):
                return

        if args.param_groups:
            groups: Dict[str, Dict[str, Any]] = {}
            window = args.param_window
            immediate_set = set(FIXED_LENGTHS.keys())
            for idx, inst in enumerate(stream):
                if 'handler' in inst and inst.get('opcode') not in (CALL_LIKE,):
                    # look backwards collecting immediates until non-immediate or window exceeded
                    imms = []
                    j = idx-1
                    while j >=0 and len(imms) < window:
                        prev = stream[j]
                        opb = prev.get('opcode')
                        if opb in immediate_set:
                            val = prev.get('value')
                            imms.append(val if val is not None else prev.get('raw'))
                        elif prev.get('mnemonic') == 'RET':
                            break
                        else:
                            # stop at first non-immediate (acts like boundary)
                            break
                        j -=1
                    if imms:
                        sig = ' | '.join(str(v) for v in reversed(imms))
                        entry = groups.setdefault(sig, {'count':0,'handlers':{}})
                        entry['count'] +=1
                        h = inst['handler']
                        entry['handlers'][h] = entry['handlers'].get(h,0)+1
            grouped = sorted(groups.items(), key=lambda kv: kv[1]['count'], reverse=True)
            out_list = []
            for sig, meta in grouped[:200]:
                top_handlers = sorted(meta['handlers'].items(), key=lambda kv: kv[1], reverse=True)[:5]
                out_list.append({'immediates':sig,'count':meta['count'],'top_handlers':top_handlers})
            if args.json:
                json.dump({'param_groups':out_list}, sys.stdout, indent=2)
            else:
                for g in out_list[:120]:
                    th = ', '.join(f"{h}:{c}" for h,c in g['top_handlers'])
                    print(f"IMMS[{g['immediates']}] count={g['count']} handlers: {th}")
            if not args.ngrams:
                return

        if args.ngrams:
            # Build raw opcode byte list (use underlying data) excluding text glyphs optionally
            raw_bytes = list(dis.data)
            if args.ng_exclude_text:
                raw_bytes = [b for b in raw_bytes if not is_char_op(b)]
            ng_counts: Dict[Tuple[int,...], int] = {}
            for n in range(args.ng_min, args.ng_max+1):
                for i in range(0, len(raw_bytes)-n+1):
                    tup = tuple(raw_bytes[i:i+n])
                    ng_counts[tup] = ng_counts.get(tup,0)+1
            # Filter to those occurring >1
            rep = [(k,v) for k,v in ng_counts.items() if v>1]
            rep.sort(key=lambda kv: ( -kv[1], -len(kv[0]) ))
            top = rep[:args.ng_top]
            def fmt(t):
                return ' '.join(f"{b:02X}" for b in t)
            if args.json:
                json.dump({'ngrams':[{'pattern':fmt(k),'length':len(k),'count':v} for k,v in top]}, sys.stdout, indent=2)
            else:
                for k,v in top:
                    print(f"NGRAM len={len(k)} count={v} pattern={fmt(k)}")
            return

    if args.blocks:
        blocks = dis.extract_blocks(args.min_block, args.max_block)
        if args.json:
            json.dump({'blocks': blocks}, sys.stdout, indent=2)
        else:
            for b in blocks:
                print(f"block start=0x{b['start']:06x} end=0x{b['end']:06x} bytes={b['bytes']:4d} insns={b['instructions']} text_runs={b['text_runs']}")
        return

    if args.segment_1a0100:
        pattern = b"\x1A\x01\x00"
        full_data = dis.data
        manual_skip = args.skip_bytes if args.skip_bytes>0 else 0
        auto_skip = 0
        if args.auto_skip_header:
            name_re = re.compile(r'^[A-Z][a-z]{1,11}$')
            scan_limit = min(len(full_data), args.auto_header_scan_limit)
            dis_head = Disassembler(full_data, coalesce_text=True)
            off=0
            distinct=set()
            while off < scan_limit and len(distinct) < args.auto_header_name_threshold:
                inst, new_off, err = dis_head.decode_one(off)
                if err:
                    break
                if inst and inst.get('mnemonic')=='TEXT':
                    raw = inst.get('text','')
                    ri = raw[1:-1] if raw.startswith('"') and raw.endswith('"') else raw
                    if name_re.match(ri):
                        distinct.add(ri)
                        if len(distinct) >= args.auto_header_name_threshold:
                            auto_skip = max(manual_skip, max(0, inst['offset'] - 8))
                            break
                off = new_off if new_off and new_off>off else off+1
        effective_skip = manual_skip or auto_skip
        # Protective clamp: do not skip into the first dialogue segment (before first pattern occurrence in full file)
        # Find first pattern in original full_data (not sliced yet)
        first_pat = full_data.find(pattern)
        if first_pat != -1:
            # If our skip lands within (first_pat - safe_margin) .. first_pat, clamp to 0 to keep full first segment
            if effective_skip >= first_pat - args.auto_skip_safe_margin and effective_skip < first_pat:
                effective_skip = 0
        # Also if threshold >1 caused us to skip past earliest single-name segment, allow user override: if threshold>1 and skip>0 and name threshold not met before first_pat, reduce
        if args.auto_skip_header and args.auto_header_name_threshold > 1 and effective_skip and first_pat != -1 and effective_skip >= first_pat:
            # Re-run quick scan for single-name presence before first_pat; if found, don't skip
            dis_temp = Disassembler(full_data, coalesce_text=True)
            off=0; single_found=False
            name_re = re.compile(r'^[A-Z][a-z]{1,11}$')
            while off < first_pat:
                inst, new_off, err = dis_temp.decode_one(off)
                if err: break
                if inst and inst.get('mnemonic')=='TEXT':
                    raw=inst.get('text',''); ri = raw[1:-1] if raw.startswith('"') and raw.endswith('"') else raw
                    if name_re.match(ri):
                        single_found=True; break
                off = new_off if new_off and new_off>off else off+1
            if single_found:
                effective_skip = 0
        data_bytes = full_data[effective_skip:] if effective_skip else full_data
        # Collect terminator pattern occurrences
        occ=[]; start=0
        while True:
            idx=data_bytes.find(pattern,start)
            if idx==-1: break
            occ.append(idx); start=idx+len(pattern)
        segments=[]; prev=0
        dis_text = Disassembler(data_bytes, coalesce_text=True)
        for n, pat_off in enumerate(occ):
            if n >= args.seg_max: break
            seg_start=prev; seg_end=pat_off
            if seg_end <= seg_start:
                prev = pat_off + 3
                continue
            texts=[]; off=seg_start
            while off < seg_end:
                inst, new_off, err = dis_text.decode_one(off)
                if err: break
                if inst and inst.get('mnemonic')=='TEXT':
                    t=inst.get('text',''); t=t[1:-1] if t.startswith('"') and t.endswith('"') else t
                    if t: texts.append({'offset':inst['offset']+effective_skip,'text':t})
                off = new_off if new_off and new_off>off else off+1
            segments.append({
                'index': n,
                'start': seg_start + effective_skip,
                'end': seg_end + effective_skip,
                'length': seg_end - seg_start,
                'terminator_offset': pat_off + effective_skip,
                'text_count': len(texts),
                'texts': texts[:80]
            })
            prev = pat_off + 3
        if prev < len(data_bytes) and len(segments) < args.seg_max:
            seg_start=prev; seg_end=len(data_bytes)
            texts=[]; off=seg_start
            while off < seg_end:
                inst, new_off, err = dis_text.decode_one(off)
                if err: break
                if inst and inst.get('mnemonic')=='TEXT':
                    t=inst.get('text',''); t=t[1:-1] if t.startswith('"') and t.endswith('"') else t
                    if t: texts.append({'offset':inst['offset']+effective_skip,'text':t})
                off = new_off if new_off and new_off>off else off+1
            segments.append({
                'index': len(segments),
                'start': seg_start + effective_skip,
                'end': seg_end + effective_skip,
                'length': seg_end - seg_start,
                'terminator_offset': None,
                'text_count': len(texts),
                'texts': texts[:80]
            })
        if args.json:
            json.dump({'pattern':'1A0100','segments':segments,'total_segments':len(segments),'skipped_bytes':effective_skip,'auto_skip_used': bool(args.auto_skip_header and auto_skip and not args.skip_bytes)}, sys.stdout, indent=2)
        else:
            for s in segments:
                sample = s['texts'][0]['text'][:50] if s['texts'] else ''
                term = f"0x{s['terminator_offset']:06x}" if s['terminator_offset'] is not None else 'EOF'
                print(f"seg#{s['index']:03d} {s['start']:06x}-{s['end']:06x} len={s['length']:5d} term={term} text_entries={s['text_count']:3d} first='{sample}'")
            if effective_skip:
                print(f"[info] Skipped initial {effective_skip} bytes (header region) {'(auto)' if (args.auto_skip_header and auto_skip and not args.skip_bytes) else ''}")
        return
        return

    if args.inspect_header:
        data_bytes = dis.data
        count = args.header_count
        span = args.header_span
        import struct
        entries = []
        for i in range(count):
            off = i*4
            if off+4 > len(data_bytes):
                break
            val = struct.unpack('<I', data_bytes[off:off+4])[0]
            entries.append(val)
        previews = []
        for idx,val in enumerate(entries):
            if val >= len(data_bytes):
                status = 'OUT_OF_FILE'
                preview_hex = ''
                preview_txt = ''
            else:
                chunk = data_bytes[val:val+span]
                preview_hex = ' '.join(f'{b:02X}' for b in chunk)
                # ASCII with '.' for non-printables
                preview_txt = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
                status = 'OK'
            previews.append({'index':idx,'offset_value':val,'in_file': status=='OK','status':status,'preview_hex':preview_hex,'preview_text':preview_txt})
        if args.json:
            json.dump({'header_offsets':previews,'file_size':len(data_bytes)}, sys.stdout, indent=2)
        else:
            print(f"file_size={len(data_bytes)}")
            for p in previews:
                print(f"[{p['index']:02d}] 0x{p['offset_value']:06X} {p['status']:12s} | {p['preview_hex']}")
                if p['status']=='OK':
                    print(f"     TXT: {p['preview_text']}")
        return

    if args.classify_header:
        data_bytes = dis.data
        import struct
        # Step 1: gather dwords until one points outside file or repeating pattern break
        header_offsets = []
        for i in range(args.header_count):
            off = i*4
            if off+4 > len(data_bytes):
                break
            val = struct.unpack('<I', data_bytes[off:off+4])[0]
            # heuristic stop: if value would decode as ASCII fragment of first name (e.g., contains high ASCII pattern) and >= printable region? we'll rely on out-of-file detection mainly
            if val >= len(data_bytes):
                break
            header_offsets.append(val)
        # Step 2: classify each region by reading up to classify-bytes or until next header offset
        header_offsets_sorted = sorted(set(header_offsets))
        classifications = []
        for idx, target in enumerate(header_offsets):
            # define region end as next higher header offset or file end
            next_candidates = [o for o in header_offsets_sorted if o > target]
            region_end = next_candidates[0] if next_candidates else len(data_bytes)
            limit_end = min(region_end, target + args.classify_bytes)
            region = data_bytes[target:limit_end]
            # Feature extraction
            printable = sum(1 for b in region if 32 <= b < 127)
            zero_bytes = region.count(0)
            # pointer-table detection: sequence of little-endian dwords ascending & in-file
            ptr_candidates = []
            asc = True
            for j in range(0, min(len(region), 4*32), 4):
                if j+4 > len(region):
                    break
                val = struct.unpack('<I', region[j:j+4])[0]
                if val == 0:
                    # allow zero terminator
                    ptr_candidates.append(val)
                    continue
                if val < len(data_bytes):
                    if ptr_candidates and val < ptr_candidates[-1] and val != 0:
                        asc = False
                    ptr_candidates.append(val)
                else:
                    break
            pointer_table_score = len(ptr_candidates) if asc and len(ptr_candidates) >= 4 else 0
            # Disassemble to collect opcode metrics
            local_dis = Disassembler(region, coalesce_text=False)
            off_local=0
            handlers=0
            rets=0
            low_ops=0
            textish=0
            steps=0
            while off_local < len(region):
                inst, new_off, err = local_dis.decode_one(off_local)
                if err or inst is None:
                    break
                steps+=1
                op = inst.get('opcode')
                if inst.get('mnemonic')=='RET':
                    rets+=1
                if inst.get('handler'):
                    handlers+=1
                if op is not None and op < 0x32:
                    low_ops+=1
                # Count printable glyph bytes directly when they are not special
                if op is not None and is_char_op(op & 0xFF):
                    textish +=1
                if steps>256: # bound decoding
                    break
                off_local = new_off if new_off>off_local else off_local+1
            # Heuristic classification
            ratio_printable = printable / (len(region) or 1)
            cls = 'unknown'
            if pointer_table_score >= 6 and ratio_printable < 0.25:
                cls='pointer_table'
            elif handlers>0 and rets>0:
                cls='code_like'
            elif handlers==0 and rets>=4 and low_ops>20:
                # many RETs with low ops could be a constant/value table of tiny scripts
                cls='micro_scripts'
            elif ratio_printable > 0.6:
                cls='text_or_mixed'
            elif textish>8 and handlers==0:
                cls='glyph_data'
            classifications.append({
                'header_index': idx,
                'target_offset': target,
                'region_end': region_end,
                'span_analyzed': len(region),
                'printable_ratio': round(ratio_printable,3),
                'handlers': handlers,
                'rets': rets,
                'low_ops': low_ops,
                'textish_glyphs': textish,
                'pointer_table_score': pointer_table_score,
                'zero_bytes': zero_bytes,
                'class': cls
            })
        if args.json:
            json.dump({'header_entry_count':len(header_offsets), 'classifications':classifications}, sys.stdout, indent=2)
        else:
            print(f"Detected header entries: {len(header_offsets)} (stopped at first out-of-range or limit)")
            for c in classifications:
                print(f"[{c['header_index']:02d}] off=0x{c['target_offset']:06X} -> end<=0x{c['region_end']:06X} span={c['span_analyzed']:4d} cls={c['class']:13s} handlers={c['handlers']:3d} rets={c['rets']:3d} low={c['low_ops']:3d} glyphs={c['textish_glyphs']:3d} ptrScore={c['pointer_table_score']:2d} pr={c['printable_ratio']}")
        return

    if args.frequency:
        freq = dis.opcode_frequencies()
        if args.json:
            json.dump(freq, sys.stdout, indent=2)
        else:
            print(f"Total bytes analyzed: {freq['total_bytes']} unique_opcodes={freq['unique_opcodes']}")
            for k,info in list(freq['frequencies'].items())[:64]:  # top 64 by count
                print(f"{k}: {info['count']} ({info['pct']}%)")
        return

    # If tagging speakers but user forgot coalesce, enable it implicitly (names rely on TEXT grouping)
    if args.tag_speakers and not args.coalesce_text:
        dis.coalesce_text = True
    seq=dis.disassemble(args.start, args.count, args.max_bytes)
    if args.tag_speakers:
        name_re = re.compile(r'^"([A-Z][a-z]{1,11})"$')
        speaker_freq: Dict[str,int] = {}
        last_speaker = None
        # Collect contexts if requested
        speaker_contexts = [] if args.speaker_context else None
        for inst in seq:
            if inst.get('mnemonic')=='TEXT':
                txt = inst.get('text','')
                m = name_re.match(txt)
                if m:
                    name = m.group(1)
                    inst['speaker'] = name
                    speaker_freq[name] = speaker_freq.get(name,0)+1
                    if speaker_contexts is not None:
                        # find preceding non-TEXT opcode and up to 5 prior instructions
                        idx = seq.index(inst)
                        window_start = max(0, idx-6)
                        context_slice = seq[window_start:idx]
                        # Filter out any large TEXT blobs except keep immediate prior
                        context_serialized = []
                        for c in context_slice:
                            if c.get('mnemonic')=='TEXT' and c is not context_slice[-1]:
                                continue
                            context_serialized.append({
                                'offset': c.get('offset'),
                                'mnemonic': c.get('mnemonic'),
                                'opcode': c.get('opcode'),
                                'handler': c.get('handler')
                            })
                        speaker_contexts.append({
                            'speaker': name,
                            'offset': inst.get('offset'),
                            'context': context_serialized
                        })
                    last_speaker = name
                else:
                    # Optionally attribute this line to last speaker for downstream analysis
                    if last_speaker and ' ' in txt and len(txt) > 3:
                        inst['attributed_speaker'] = last_speaker
        if args.json:
            # augment json output with speaker frequency summary
            seq.append({'_summary':'speaker_freq','speakers':speaker_freq})
            if speaker_contexts is not None:
                seq.append({'_speaker_contexts': speaker_contexts})
    if args.json:
        json.dump({'start':args.start,'sequence':seq}, sys.stdout, indent=2)
    else:
        for inst in seq:
            if 'error' in inst:
                print(f"0x{inst['offset']:06x}: <ERROR {inst['error']}>")
                break
            op = inst.get('opcode')
            mnem = inst.get('mnemonic')
            extra = ''
            if 'value' in inst:
                extra += f" value=0x{inst['value']:X}"
            if 'handler' in inst:
                extra += f" handler={inst['handler']}"
            if inst.get('text'):
                extra += f" {inst['text']}"
            if inst.get('speaker'):
                extra += f" [SPEAKER:{inst['speaker']}]"
            elif inst.get('attributed_speaker'):
                extra += f" [speaker:{inst['attributed_speaker']}]"
            if 'note' in inst:
                extra += f" // {inst['note']}"
            print(f"0x{inst['offset']:06x}: {op if op is None else f'OP_{op:03X}'} {mnem}{extra}")

if __name__=='__main__':
    main()
