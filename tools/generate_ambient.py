#!/usr/bin/env python3
"""Generate deterministic, seamless ambient loops for both game levels."""

from __future__ import annotations

from array import array
import argparse
import math
import os
from pathlib import Path
import random
import sys
import wave


SAMPLE_RATE = 11025
LOOP_SECONDS = 8
SAMPLE_COUNT = SAMPLE_RATE * LOOP_SECONDS


def clamp16(value: float) -> int:
    return max(-32768, min(32767, round(value)))


def periodic_noise(seed: int, amplitude: float, low_hz: int, high_hz: int, components: int) -> list[float]:
    """Create noise-like sound from integer-frequency sines, so the loop joins exactly."""
    rng = random.Random(seed)
    frequencies = [rng.randint(low_hz, high_hz) for _ in range(components)]
    phases = [rng.uniform(0.0, math.tau) for _ in range(components)]
    weights = [rng.uniform(0.55, 1.0) for _ in range(components)]
    normalization = amplitude / sum(weights)
    return [
        normalization * sum(
            weight * math.sin(math.tau * frequency * index / SAMPLE_RATE + phase)
            for frequency, phase, weight in zip(frequencies, phases, weights)
        )
        for index in range(SAMPLE_COUNT)
    ]


def level_zero_samples() -> list[int]:
    electrical_texture = periodic_noise(1976, 420.0, 180, 2400, 28)
    result: list[int] = []
    for index, texture in enumerate(electrical_texture):
        time = index / SAMPLE_RATE
        slow_drift = 0.88 + 0.08 * math.sin(math.tau * time / LOOP_SECONDS)
        ballast = (
            760.0 * math.sin(math.tau * 50.0 * time)
            + 300.0 * math.sin(math.tau * 100.0 * time + 0.22)
            + 115.0 * math.sin(math.tau * 150.0 * time + 0.74)
        )
        distant_hum = 260.0 * math.sin(math.tau * 37.0 * time + 1.4)
        result.append(clamp16((ballast + distant_hum + texture) * slow_drift * 3.0))
    return result


def poolrooms_samples() -> list[int]:
    water_texture = periodic_noise(2005, 540.0, 35, 850, 36)
    result: list[int] = []
    for index, texture in enumerate(water_texture):
        time = index / SAMPLE_RATE
        wave = 0.72 + 0.22 * math.sin(math.tau * 2.0 * time / LOOP_SECONDS + 0.5)
        room_tone = (
            660.0 * math.sin(math.tau * 43.0 * time + 0.8)
            + 330.0 * math.sin(math.tau * 86.0 * time + 1.7)
            + 190.0 * math.sin(math.tau * 17.0 * time)
        )
        drip = 0.0
        for center in (1.15, 4.55, 6.80):
            distance = abs(time - center)
            if distance < 0.12:
                envelope = math.exp(-distance * 34.0)
                drip += 720.0 * envelope * math.sin(math.tau * 620.0 * distance)
        result.append(clamp16((room_tone + texture * wave + drip) * 3.0))
    return result


def write_pcm(path: Path, samples: list[int]) -> None:
    encoded = array("h", samples)
    if sys.byteorder != "little":
        encoded.byteswap()
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_bytes(encoded.tobytes())
    os.replace(temporary, path)


def write_wav(path: Path, samples: list[int]) -> None:
    encoded = array("h", samples)
    if sys.byteorder != "little":
        encoded.byteswap()
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    with wave.open(str(temporary), "wb") as destination:
        destination.setnchannels(1)
        destination.setsampwidth(2)
        destination.setframerate(SAMPLE_RATE)
        destination.writeframes(encoded.tobytes())
    os.replace(temporary, path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--asset-dir", type=Path, default=Path("assets"))
    parser.add_argument("--source-dir", type=Path, default=Path("source_assets"))
    args = parser.parse_args()

    loops = {
        "ambient_level0": level_zero_samples(),
        "ambient_poolrooms": poolrooms_samples(),
    }
    for name, samples in loops.items():
        raw_path = args.asset_dir / f"{name}.raw"
        wav_path = args.source_dir / f"{name}.wav"
        write_pcm(raw_path, samples)
        write_wav(wav_path, samples)
        print(f"[OK] {name}: {len(samples)} samples, {SAMPLE_RATE} Hz, {raw_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
