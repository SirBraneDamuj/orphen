"""Pull a voice line out of VOICE.BIN as a WAV.

VOICE.BIN's table of contents is its own first sectors:

    word 0        entry count (3310 on the retail disc)
    word i        (sector << 15) | (sizeBytes >> 4)   -- data at sector * 2048

which is *not* the split the other flat archives use, so do not reach for
scripts/bin_toc.py here. Payloads are raw SPU ADPCM -- 16-byte blocks, no VAG
header -- played back at the rate FUN_00207010's pitch register asks for.

    python tools/voice_extract.py 190              -> out/voice/voice_0190.wav
    python tools/voice_extract.py 78 190 2540      several at once
    python tools/voice_extract.py 190 -o line.wav  a specific path (one id only)
    python tools/voice_extract.py --list

The dialogue log's "(voice 190, 321f)" is this id; 321f is the clip's length
in 60 Hz frames, which is how long the line holds.

An entry can be a bank of several clips rather than one line -- the spells use
this. FUN_00206f08 reads the entry's first bytes as a directory:

    word 0        clip count, 1..15
    word 1 + i    (offset in 16-byte units << 16) | size in 16-byte units

and FUN_00206d98 only trusts it when the count is 1..15 and the halfword at +6
equals align16(count * 4 + 0x13). A bank is written as one WAV per clip
(voice_0004_clip0.wav, ...); anything else is decoded whole.

The ids are the ones the dialogue records name in their 0x16 control code; see
analyzed/text_ops/text_op_16_trigger_voice_or_audio_playback.c.
"""

import argparse
import os
import struct
import sys
import wave

# FUN_00207010: SPU2 plays at the recorded rate when the pitch register reads
# 0x1000 == 48000 Hz.
PITCH = 0x760
RATE = PITCH * 48000 // 4096

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_VOICE_BIN = os.path.join(REPO, "disc", "VOICE.BIN")
DEFAULT_OUT_DIR = os.path.join(REPO, "out", "voice")

PREDICTORS = [(0.0, 0.0), (60.0, 0.0), (115.0, -52.0), (98.0, -55.0), (122.0, -60.0)]


def read_index(path):
    with open(path, "rb") as handle:
        count = struct.unpack("<I", handle.read(4))[0]
        handle.seek(0)
        words = ((count + 4) >> 2) * 16 // 4
        return list(struct.unpack(f"<{words}I", handle.read(words * 4)))


def extent(index, voice_id):
    packed = index[voice_id]
    return (packed >> 15) * 2048, (packed & 0x7FFF) * 16


def bank_clips(entry):
    """FUN_00206d98's directory test, then FUN_00206f08's read. None when the
    entry is a single line."""
    if len(entry) < 8:
        return None
    count = struct.unpack_from("<I", entry, 0)[0]
    if not 0 < count <= 15:
        return None
    if struct.unpack_from("<H", entry, 6)[0] * 16 != (count * 4 + 0x13) & ~0xF:
        return None
    clips = []
    for i in range(count):
        word = struct.unpack_from("<I", entry, 4 + i * 4)[0]
        start, length = (word >> 16) * 16, (word & 0xFFFF) * 16
        clips.append(entry[start:start + length])
    return clips


def write_wav(path, blocks):
    pcm = decode_ps_adpcm(blocks)
    with wave.open(path, "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(struct.pack(f"<{len(pcm)}h", *pcm))
    seconds = len(pcm) / RATE
    print(f"  {len(blocks):7} bytes  {seconds:5.2f}s ({round(seconds * 60)}f)  -> {path}")


def decode_ps_adpcm(blocks):
    out = []
    previous = 0.0
    before = 0.0
    for at in range(0, len(blocks) - 15, 16):
        shift = blocks[at] & 0x0F
        first, second = PREDICTORS[min((blocks[at] >> 4) & 0x0F, 4)]
        for i in range(28):
            byte = blocks[at + 2 + i // 2]
            nibble = (byte >> 4) if i & 1 else (byte & 0x0F)
            if nibble > 7:
                nibble -= 16
            sample = (nibble << (12 - shift)) + previous * first / 64.0 + before * second / 64.0
            before, previous = previous, sample
            out.append(max(-32768, min(32767, int(sample))))
    return out


def main():
    parser = argparse.ArgumentParser(description="Extract VOICE.BIN clips as WAV.")
    parser.add_argument("voice_ids", nargs="*", type=int, metavar="voice_id")
    parser.add_argument("-o", "--output", help="output path (single id only)")
    parser.add_argument("--out-dir", default=DEFAULT_OUT_DIR)
    parser.add_argument("--voice-bin", default=DEFAULT_VOICE_BIN)
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()

    index = read_index(args.voice_bin)
    count = index[0]
    if args.list:
        print(f"{count} clips, {RATE} Hz")
        for i in range(1, min(count, 40) + 1):
            offset, size = extent(index, i)
            print(f"  [{i:4}] 0x{offset:08x} {size:7} bytes  {size / 16 * 28 / RATE:6.2f}s")
        return 0

    if not args.voice_ids:
        parser.error("give one or more voice ids, or --list")
    if args.output and len(args.voice_ids) != 1:
        parser.error("-o takes a single voice id; use --out-dir for several")
    for voice_id in args.voice_ids:
        if not 0 < voice_id <= count:
            parser.error(f"voice id {voice_id} out of range 1..{count}")

    if not args.output:
        os.makedirs(args.out_dir, exist_ok=True)
    with open(args.voice_bin, "rb") as handle:
        for voice_id in args.voice_ids:
            offset, size = extent(index, voice_id)
            handle.seek(offset)
            entry = handle.read(size)
            base = args.output or os.path.join(args.out_dir, f"voice_{voice_id:04}.wav")
            clips = bank_clips(entry)
            if clips is None:
                print(f"voice {voice_id}:")
                write_wav(base, entry)
            else:
                print(f"voice {voice_id}: bank of {len(clips)} clips")
                stem, ext = os.path.splitext(base)
                for i, clip in enumerate(clips):
                    write_wav(f"{stem}_clip{i}{ext}", clip)
    return 0


if __name__ == "__main__":
    sys.exit(main())
