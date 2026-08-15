"""Pull a voice line out of VOICE.BIN as a WAV.

VOICE.BIN's table of contents is its own first sectors:

    word 0        entry count (3310 on the retail disc)
    word i        (sector << 15) | (sizeBytes >> 4)   -- data at sector * 2048

which is *not* the split the other flat archives use, so do not reach for
scripts/bin_toc.py here. Payloads are raw SPU ADPCM -- 16-byte blocks, no VAG
header -- played back at the rate FUN_00207010's pitch register asks for.

    python tools/voice_extract.py 78 out/voice_78.wav
    python tools/voice_extract.py --list

The ids are the ones the dialogue records name in their 0x16 control code; see
analyzed/text_ops/text_op_16_trigger_voice_or_audio_playback.c.
"""

import argparse
import struct
import sys
import wave

# FUN_00207010: SPU2 plays at the recorded rate when the pitch register reads
# 0x1000 == 48000 Hz.
PITCH = 0x760
RATE = PITCH * 48000 // 4096

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
    parser = argparse.ArgumentParser()
    parser.add_argument("voice_id", nargs="?", type=int)
    parser.add_argument("output", nargs="?")
    parser.add_argument("--voice-bin", default="VOICE.BIN")
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

    if args.voice_id is None or args.output is None:
        parser.error("give a voice id and an output path, or --list")
    if not 0 < args.voice_id <= count:
        parser.error(f"voice id must be 1..{count}")

    offset, size = extent(index, args.voice_id)
    with open(args.voice_bin, "rb") as handle:
        handle.seek(offset)
        blocks = handle.read(size)

    pcm = decode_ps_adpcm(blocks)
    with wave.open(args.output, "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(struct.pack(f"<{len(pcm)}h", *pcm))
    print(f"voice {args.voice_id}: {size} bytes -> {len(pcm)} samples, "
          f"{len(pcm) / RATE:.2f}s at {RATE} Hz -> {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
