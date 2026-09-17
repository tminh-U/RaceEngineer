"""Resample a mono 16-bit PCM WAV using a windowed-sinc filter.

This helper intentionally uses only Python's standard library so setup_runtime.ps1
does not need FFmpeg just to prepare the Gwen-TTS reference voice.
"""

from __future__ import annotations

import argparse
import math
import struct
import wave
from pathlib import Path


def resample(source: Path, destination: Path, target_rate: int) -> None:
    with wave.open(str(source), "rb") as input_wave:
        channels = input_wave.getnchannels()
        sample_width = input_wave.getsampwidth()
        source_rate = input_wave.getframerate()
        frame_count = input_wave.getnframes()
        frames = input_wave.readframes(frame_count)

    if channels != 1 or sample_width != 2:
        raise ValueError("Only mono 16-bit PCM WAV files are supported")
    if source_rate <= 0 or target_rate <= 0 or frame_count == 0:
        raise ValueError("Invalid WAV sample rate or frame count")

    samples = struct.unpack(f"<{frame_count}h", frames)
    output_count = round(frame_count * target_rate / source_rate)
    output = bytearray(output_count * 2)
    ratio = source_rate / target_rate
    radius = 16
    cutoff = min(1.0, target_rate / source_rate)

    def sinc(value: float) -> float:
        if abs(value) < 1e-12:
            return 1.0
        angle = math.pi * value
        return math.sin(angle) / angle

    for output_index in range(output_count):
        position = output_index * ratio
        center = math.floor(position)
        total = 0.0
        weight_sum = 0.0
        for source_index in range(center - radius + 1, center + radius + 1):
            if source_index < 0 or source_index >= frame_count:
                continue
            distance = position - source_index
            weight = cutoff * sinc(distance * cutoff) * sinc(distance / radius)
            total += samples[source_index] * weight
            weight_sum += weight
        value = round(total / weight_sum) if abs(weight_sum) > 1e-12 else samples[min(center, frame_count - 1)]
        struct.pack_into("<h", output, output_index * 2, max(-32768, min(32767, value)))

    destination.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(destination), "wb") as output_wave:
        output_wave.setnchannels(1)
        output_wave.setsampwidth(2)
        output_wave.setframerate(target_rate)
        output_wave.writeframes(output)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--rate", type=int, default=24_000)
    arguments = parser.parse_args()
    resample(arguments.source, arguments.destination, arguments.rate)


if __name__ == "__main__":
    main()
