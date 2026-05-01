# MV3 Movie Playback Analysis

Source functions: `src/FUN_002f1808.c`, `src/FUN_002f1f38.c`, `src/FUN_002f1a70.c`, `src/FUN_002f1c98.c`, `src/FUN_002f2198.c`, `src/FUN_002f2ca0.c`, `src/FUN_002f2ef0.c`.

This note started from code analysis and was later validated against the extracted disc files under:

```text
C:\Users\zptha\projects\orphen\orphen_ghidra\Orphen - Scion of Sorcery (USA)\Orphen - Scion of Sorcery (USA)\MV3
```

## Disc filenames

The executable contains one standalone audio-like MV3 path plus nineteen movie paths:

```text
0x0034d4d8  \MV3\A01.MV3;1
0x0034fcb0  \MV3\M01.MV3;1
0x0034fcc0  \MV3\M02.MV3;1
0x0034fcd0  \MV3\M03.MV3;1
0x0034fce0  \MV3\M04.MV3;1
0x0034fcf0  \MV3\M05.MV3;1
0x0034fd00  \MV3\M06.MV3;1
0x0034fd10  \MV3\M07.MV3;1
0x0034fd20  \MV3\M08.MV3;1
0x0034fd30  \MV3\M09.MV3;1
0x0034fd40  \MV3\M10.MV3;1
0x0034fd50  \MV3\M11.MV3;1
0x0034fd60  \MV3\M12.MV3;1
0x0034fd70  \MV3\M13.MV3;1
0x0034fd80  \MV3\M14.MV3;1
0x0034fd90  \MV3\M15.MV3;1
0x0034fda0  \MV3\M16.MV3;1
0x0034fdb0  \MV3\M17.MV3;1
0x0034fdc0  \MV3\M18.MV3;1
0x0034fdd0  \MV3\M19.MV3;1
```

`FUN_002f1808(movie_id)` uses the `M01`..`M19` table at `0x00326f80` and selects `table[movie_id - 1]`. `FUN_00267650` opens `A01.MV3` separately through the PCM stream state machine.

Real-file validation: `M01.MV3` through `M19.MV3` all start with `MV30`. `A01.MV3` does not; it starts with zero-filled data and should be handled as a separate audio/PCM stream, not as the movie container described below.

## Trigger path

- Script opcode `0x13A` (`FUN_00265378`, already analyzed as `analyzed/ops/0x13A_set_global_byte_3555d2.c`) evaluates one byte and stores it to `DAT_003555d2`.
- Scene transition code `FUN_0022a418` checks `DAT_003555d2`; when it is positive, it clears some state, calls `FUN_002f1808(DAT_003555d2)`, then resets the movie request byte.
- There is also a debug menu entry `FUN_00269a98` labelled `MOVIE No`. It writes the selected movie number to `cGpffffb662`, then sets scene globals to map `0x0c`, area `99`, flags `0x2001`.
- `FUN_002f1808` allocates aligned movie work buffers, loads per-movie metadata from tables near `0x00326fd0` and `0x00326ff8`, then calls `FUN_002f1f38` with the selected MV3 path.

## Header format

`FUN_002f1a70` reads the first CD sector and validates a 24-byte little-endian header. The magic compare is against integer `0x3033564d`, bytes `4d 56 33 30`, ASCII `MV30`.

The debug format string at `0x0034fe30` gives the field names:

```text
blk_byte=%d,pcm_ofs=%d,pcm_1sz=%d,mpg_ofs=%d,mpg_1sz=%d
```

Header layout:

| Offset | Type | Meaning |
| --- | --- | --- |
| `0x00` | char[4] | `MV30` magic |
| `0x04` | u32 | `blk_byte`, size of each interleaved block |
| `0x08` | u32 | `pcm_ofs`, offset of the PCM chunk inside each block |
| `0x0c` | u32 | `pcm_1sz`, bytes of PCM copied from each block |
| `0x10` | u32 | `mpg_ofs`, offset of the MPEG chunk inside each block |
| `0x14` | u32 | `mpg_1sz`, bytes of MPEG copied from each block |

The player reads sector 0 for the header and then starts block reads from the next sector. That means the interleaved block area should begin at file offset `0x800` for a raw ISO file extract. Any bytes after the 24-byte header inside sector 0 are ignored by the game.

The loader bounds-checks the chunk sizes against its fixed ring buffers:

- `mpg_1sz * 5 <= 0x600000`
- `pcm_1sz * 8 <= 0x200000`

Both failure paths log `[%s] mpg_block size over.`, so the string name is broader than the actual checks.

All nineteen extracted `Mxx.MV3` files share the same layout:

| Field | Value |
| --- | --- |
| `blk_byte` | `0x000aa800` / 698,368 bytes |
| `pcm_ofs` | `0x00000000` |
| `pcm_1sz` | `0x00030000` / 196,608 bytes |
| `mpg_ofs` | `0x00030000` |
| `mpg_1sz` | `0x0007a120` / 500,000 bytes |

Each block therefore contains 196,608 bytes of PCM data, 500,000 bytes of MPEG data, and 1,760 bytes of unused/padding space at the end of the block.

## Interleaving model

`FUN_002f1c98` repeatedly reads one full `blk_byte` block from the CD stream into `0x005c9a00`. Once a block is complete, it copies two fixed-size regions out of that block:

```text
block + pcm_ofs, length pcm_1sz  -> PCM ring at 0x01949a00
block + mpg_ofs, length mpg_1sz  -> MPEG ring at 0x007c9a00
```

The copy loop moves 16-byte units but advances the write cursors by the exact header sizes. For offline demuxing, concatenate exactly `pcm_1sz` and `mpg_1sz` bytes from each complete block.

The PCM side is an 8-chunk ring. The MPEG side is a 5-chunk sliding buffer. When the MPEG write offset reaches `mpg_1sz * 5`, the code copies the chunk at offset `mpg_1sz` back to the start and resumes writing at offset `mpg_1sz`. That looks like decoder history/window management, not a different on-disc layout.

## Playback model

`FUN_002f1f38` is the top-level movie session:

1. Flushes caches and resets display/audio state.
2. Calls `FUN_002f1a70(path)` to open the MV3 stream and parse the header.
3. Initializes the MPEG/IPU work area with `FUN_002fc7b0`.
4. Installs an INTC handler at `FUN_002f27b0`.
5. Runs `FUN_002f2198`, the main decode/render loop.
6. Removes the handler, closes the CD stream, restores display state.

`FUN_002f2198` uses Sony/SCE-style MPEG/IPU code rather than a hand-written video codec. The important evidence:

- It logs `sceMpegGetPicture err` if `FUN_002fc9e8` fails.
- It feeds the PS2 IPU through `REG_DMAC_4_IPU_TO_*`.
- It renders decoded pictures through GIF DMA chains (`REG_DMAC_2_GIF_*`).
- The executable includes SCE library strings `PsIIlibmpeg 1510` and `PsIIlibipu  15`.

The decoded picture path appears to be:

```text
MV3 block MPEG chunks -> MPEG ring -> sceMpeg/libipu decode -> decoded image buffer -> GIF DMA display chain
```

The audio path uses the `pcm_*` chunk stream. `FUN_002f2110` copies slices of the PCM ring to IOP/SPU memory through `FUN_002073d0`, and `FUN_00207408` starts the stream with a per-movie value from `0x00326fd0`.

Real-file validation shows the payload is signed 16-bit little-endian stereo at 48,000 Hz, but not stored as ordinary LRLR samples. Each 0x30000-byte audio chunk is arranged as alternating 0x200-byte channel stripes:

```text
0x000-0x1ff  left samples
0x200-0x3ff  right samples
0x400-0x5ff  left samples
0x600-0x7ff  right samples
...
```

The offline conversion step must reorder those stripes into conventional interleaved stereo samples. This was validated by listening to `M02.MV3`, which has prominent dialogue and effects; the `0x200` stripe reorder fixes the high-pitched/tinny and syllable-shuffled audio artifacts produced by treating the raw payload as LRLR PCM.

Each 0x30000-byte raw audio chunk contains 49,152 stereo sample pairs. At the correct 48 kHz playback rate that is 1.024 seconds of audio:

```text
0x30000 bytes / (2 channels * 2 bytes/sample) = 49,152 stereo sample pairs
49,152 / 48,000 Hz = 1.024 seconds
```

Interpreting the same samples as 49,152 Hz makes the audio run 2.4% too fast; on `M04.MV3` that shows up as roughly one second of audio lead by 0:50. The last chunk of `M13.MV3` is all zeroes, and the preceding chunk is mostly near-silence. This suggests the fixed-size PCM stream carries end padding; use video duration or `ffmpeg -shortest` when muxing.

## Conversion plan

The container wrapper is simple enough to demux without emulating the game:

1. Extract `\MV3\*.MV3;1` from the disc image.
2. Parse the `MV30` header.
3. Starting at file offset `0x800`, iterate complete `blk_byte` blocks.
4. Concatenate each block's MPEG slice into one output file.
5. Concatenate each block's PCM slice into a second output file.
6. Feed the MPEG payload directly to `ffmpeg` as raw MPEG video.
7. Reorder the PCM payload's 0x200-byte channel stripes.
8. Feed the reordered PCM payload to `ffmpeg` as `s16le`, stereo, 48,000 Hz.

`tools/mv3_demux.py` implements steps 2-5.
`tools/mv3_reorder_audio.py` implements step 7.

Example:

```bash
python tools/mv3_demux.py path/to/M01.MV3 -o out/M01
python tools/mv3_reorder_audio.py out/M01.pcm out/M01_audio_s16le_stereo_48000.pcm
ffprobe out/M01.mpg
ffmpeg -i out/M01.mpg -c:v libx264 -pix_fmt yuv420p out/M01_video_only.mp4
```

Working audio/video MP4 command, validated on `M02.MV3` and `M13.MV3`:

```bash
python tools/mv3_demux.py "C:/path/to/M13.MV3" -o out/mv3_test/M13
python tools/mv3_reorder_audio.py \
  out/mv3_test/M13.pcm \
  out/mv3_test/M13_audio_s16le_stereo_48000.pcm
ffmpeg -fflags +genpts -i out/mv3_test/M13.mpg \
	-f s16le -ar 48000 -ac 2 -i out/mv3_test/M13_audio_s16le_stereo_48000.pcm \
	-c:v libx264 -pix_fmt yuv420p \
	-c:a aac -b:a 192k -ar:a 48000 \
	-shortest -movflags +faststart \
	out/mv3_test/M13_av_stripe0200_ar48000.mp4
```

For a less destructive archival remux, use Matroska and copy the MPEG-2 video:

```bash
ffmpeg -fflags +genpts -i out/mv3_test/M13.mpg \
	-f s16le -ar 48000 -ac 2 -i out/mv3_test/M13_audio_s16le_stereo_48000.pcm \
	-c:v copy -c:a flac -shortest out/mv3_test/M13_preserve_video.mkv
```

## FFmpeg Validation

`M13.MV3` was demuxed as a test case because it is the smallest regular movie. Results:

- Demuxed video: `out/mv3_test/M13.mpg`, 10,500,000 bytes.
- Demuxed audio: `out/mv3_test/M13.pcm`, 4,128,768 bytes.
- `ffprobe` recognizes the video as raw MPEG video: MPEG-2, 640x480, 4:3, 29.97 fps, 4 Mbps.
- The MPEG stream contains 600 decoded pictures. At 30000/1001 fps, that is 20.02 seconds.
- The MPEG sequence end marker is followed by zero padding (`M13`: 535,865 bytes after `00 00 01 b7`).
- Reordering the PCM with 0x200-byte channel stripes and then treating it as `s16le`, stereo, 48,000 Hz yields 21.504 seconds before trimming, with trailing silence; muxing with `-shortest` produces a 20.02-second MP4.
- A preview frame extracted around 5 seconds shows a prerendered ship scene, confirming the video payload is rendering correctly.

`M02.MV3` was used as the audio validation case because it has clear dialogue and effects. The raw LRLR interpretation has correct timing but sounds high-pitched/tinny and syllable-shuffled. The 0x200-byte stripe reorder fixes those artifacts.

## Open questions

- Decode the `0x00326fd0` per-movie table used as the third argument to `FUN_00207408`.
- Analyze `A01.MV3` separately; it does not use the `MV30` header.
