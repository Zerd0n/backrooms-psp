#!/usr/bin/env python3
"""Validate and convert chase WAV to mono signed 16-bit PCM at 11025 Hz."""

from __future__ import annotations

import argparse
from array import array
import datetime as dt
import os
from pathlib import Path
import sys
import wave


TARGET_RATE = 11025


def log(message: str, log_path: Path) -> None:
    line = f"[{dt.datetime.now().isoformat(timespec='seconds')}] {message}"
    print(line, flush=True)
    with log_path.open("a", encoding="utf-8") as handle:
        handle.write(line + "\n")


def clamp16(value: int) -> int:
    return max(-32768, min(32767, value))


def load_samples(path: Path) -> tuple[list[int], int]:
    with wave.open(str(path), "rb") as source:
        channels = source.getnchannels()
        sample_width = source.getsampwidth()
        rate = source.getframerate()
        frames = source.getnframes()
        compression = source.getcomptype()
        if compression != "NONE":
            raise ValueError(f"compressed WAV is unsupported: {compression}")
        if channels < 1 or channels > 2:
            raise ValueError(f"expected mono/stereo WAV, got {channels} channels")
        if sample_width not in (1, 2):
            raise ValueError(f"expected 8/16-bit PCM WAV, got {sample_width * 8}-bit")
        raw = source.readframes(frames)

    if sample_width == 2:
        values = array("h")
        values.frombytes(raw)
        if sys.byteorder != "little":
            values.byteswap()
        unpacked = list(values)
    else:
        unpacked = [(value - 128) << 8 for value in raw]
    if channels == 2:
        unpacked = [clamp16((unpacked[i] + unpacked[i + 1]) // 2) for i in range(0, len(unpacked), 2)]
    return unpacked, rate


def resample(samples: list[int], source_rate: int, target_rate: int) -> list[int]:
    if not samples:
        raise ValueError("source audio contains no samples")
    if source_rate == target_rate:
        return samples
    output_count = max(1, round(len(samples) * target_rate / source_rate))
    output: list[int] = []
    for index in range(output_count):
        source_position = index * source_rate / target_rate
        left = min(len(samples) - 1, int(source_position))
        right = min(len(samples) - 1, left + 1)
        fraction = source_position - left
        output.append(clamp16(round(samples[left] * (1.0 - fraction) + samples[right] * fraction)))
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path(os.environ.get("BACKROOMS_CHASE_WAV", "source_assets/GAZ.wav")))
    parser.add_argument("--output", type=Path, default=Path("assets/chase.raw"))
    args = parser.parse_args()
    if not args.source.is_file():
        raise FileNotFoundError(f"chase audio not found: {args.source}")
    log_dir = Path("logs")
    log_dir.mkdir(parents=True, exist_ok=True)
    log_path = log_dir / f"audio_{dt.datetime.now().strftime('%Y%m%d_%H%M%S')}.log"
    samples, source_rate = load_samples(args.source)
    log(f"Input: {args.source} mono PCM samples={len(samples)} rate={source_rate}", log_path)
    samples = resample(samples, source_rate, TARGET_RATE)
    encoded = array("h", samples)
    if sys.byteorder != "little":
        encoded.byteswap()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix(args.output.suffix + ".tmp")
    temporary.write_bytes(encoded.tobytes())
    os.replace(temporary, args.output)
    log(f"Output: {args.output} mono signed 16-bit PCM rate={TARGET_RATE} bytes={args.output.stat().st_size}", log_path)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
