#!/usr/bin/env python3
"""Export Orphen MV3 assets into final archive-friendly audio/video files.

Outputs:
- Axx.MV3 -> Axx.wav, headerless PCM stream with 0x10000-byte chunks
- Mxx.MV3 -> Mxx.mp4, MV30 MPEG video plus reordered 48 kHz stereo audio
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import tempfile
from pathlib import Path

from mv3_demux import SECTOR_SIZE, demux
from mv3_reorder_audio import DEFAULT_STRIPE_SIZE, reorder_channel_stripes


AUDIO_RATE = 48000
AUDIO_CHANNELS = 2
A01_CHUNK_SIZE = 0x10000
MOVIE_PATTERN = "M[0-9][0-9].MV3"
AUDIO_PATTERN = "A[0-9][0-9].MV3"


class ExportError(RuntimeError):
    pass


def run_ffmpeg(ffmpeg: str, args: list[str]) -> None:
    command = [ffmpeg, "-y", "-hide_banner", "-loglevel", "warning", *args]
    subprocess.run(command, check=True)


def require_ffmpeg(ffmpeg: str) -> str:
    resolved = shutil.which(ffmpeg) if not Path(ffmpeg).exists() else ffmpeg
    if resolved is None:
        raise ExportError(f"ffmpeg executable not found: {ffmpeg}")
    return resolved


def clean_outputs(output_dir: Path) -> None:
    for pattern in ("A[0-9][0-9].wav", "M[0-9][0-9].mp4"):
        for path in output_dir.glob(pattern):
            path.unlink()


def export_a_stream(source: Path, output_dir: Path, work_dir: Path, ffmpeg: str) -> Path:
    raw = source.read_bytes()
    reordered = reorder_channel_stripes(raw, A01_CHUNK_SIZE, DEFAULT_STRIPE_SIZE)

    stem = source.stem.upper()
    pcm_path = work_dir / f"{stem}.s16le"
    wav_path = output_dir / f"{stem}.wav"
    pcm_path.write_bytes(reordered)

    run_ffmpeg(
        ffmpeg,
        [
            "-f",
            "s16le",
            "-ar",
            str(AUDIO_RATE),
            "-ac",
            str(AUDIO_CHANNELS),
            "-i",
            str(pcm_path),
            str(wav_path),
        ],
    )
    return wav_path


def export_movie(source: Path, output_dir: Path, work_dir: Path, ffmpeg: str) -> Path:
    header, pcm, mpeg, stats = demux(source.read_bytes(), SECTOR_SIZE)
    if stats["block_count"] == 0:
        raise ExportError(f"{source.name}: no complete MV30 blocks found")

    stem = source.stem.upper()
    mpeg_path = work_dir / f"{stem}.mpg"
    pcm_path = work_dir / f"{stem}.s16le"
    mp4_path = output_dir / f"{stem}.mp4"

    mpeg_path.write_bytes(mpeg)
    pcm_path.write_bytes(reorder_channel_stripes(pcm, int(header["pcm_size"]), DEFAULT_STRIPE_SIZE))

    run_ffmpeg(
        ffmpeg,
        [
            "-fflags",
            "+genpts",
            "-i",
            str(mpeg_path),
            "-f",
            "s16le",
            "-ar",
            str(AUDIO_RATE),
            "-ac",
            str(AUDIO_CHANNELS),
            "-i",
            str(pcm_path),
            "-c:v",
            "libx264",
            "-pix_fmt",
            "yuv420p",
            "-c:a",
            "aac",
            "-b:a",
            "192k",
            "-ar:a",
            str(AUDIO_RATE),
            "-shortest",
            "-movflags",
            "+faststart",
            str(mp4_path),
        ],
    )
    return mp4_path


def discover_sources(source_dir: Path, pattern: str) -> list[Path]:
    return sorted(source_dir.glob(pattern))


def main() -> int:
    parser = argparse.ArgumentParser(description="Export Orphen A/M MV3 files into final WAV/MP4 files.")
    parser.add_argument("source_dir", type=Path, help="Directory containing extracted Axx.MV3 and Mxx.MV3 files")
    parser.add_argument("output_dir", type=Path, help="Directory for final Axx.wav and Mxx.mp4 outputs")
    parser.add_argument("--ffmpeg", default="ffmpeg", help="ffmpeg executable path or name on PATH")
    parser.add_argument("--clean", action="store_true", help="Remove existing Axx.wav and Mxx.mp4 outputs first")
    args = parser.parse_args()

    source_dir = args.source_dir
    output_dir = args.output_dir
    if not source_dir.is_dir():
        raise ExportError(f"source directory not found: {source_dir}")

    ffmpeg = require_ffmpeg(args.ffmpeg)
    output_dir.mkdir(parents=True, exist_ok=True)
    if args.clean:
        clean_outputs(output_dir)

    audio_sources = discover_sources(source_dir, AUDIO_PATTERN)
    movie_sources = discover_sources(source_dir, MOVIE_PATTERN)
    if not audio_sources and not movie_sources:
        raise ExportError(f"no Axx.MV3 or Mxx.MV3 files found in {source_dir}")

    with tempfile.TemporaryDirectory(prefix="mv3_export_", dir=output_dir) as temp_name:
        work_dir = Path(temp_name)
        written: list[Path] = []

        for source in audio_sources:
            print(f"[audio] {source.name} -> {source.stem.upper()}.wav")
            written.append(export_a_stream(source, output_dir, work_dir, ffmpeg))

        for source in movie_sources:
            print(f"[movie] {source.name} -> {source.stem.upper()}.mp4")
            written.append(export_movie(source, output_dir, work_dir, ffmpeg))

    print(f"wrote {len(written)} file(s) to {output_dir}")
    for path in written:
        print(path.name)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())