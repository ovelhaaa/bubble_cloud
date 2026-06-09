from __future__ import annotations

import math
import struct
from pathlib import Path


def write_core_parity_fixture(path: Path, *, sample_rate: int = 44100, seconds: float = 1.0) -> Path:
    """Write a deterministic mono PCM16 WAV fixture for offline/WASM parity tests."""
    frames = int(sample_rate * seconds)
    pcm = bytearray()
    for i in range(frames):
        t = i / sample_rate
        value = 0.22 * math.sin(2.0 * math.pi * 220.0 * t)
        if i % 4096 == 0:
            value += 0.5
        if 0.35 < t < 0.45:
            value += 0.08 * math.sin(2.0 * math.pi * 880.0 * t)
        value = max(-1.0, min(1.0, value))
        pcm.extend(struct.pack("<h", round(value * 32767.0)))

    fmt_chunk = struct.pack("<HHIIHH", 1, 1, sample_rate, sample_rate * 2, 2, 16)
    body = b"fmt " + struct.pack("<I", len(fmt_chunk)) + fmt_chunk + b"data" + struct.pack("<I", len(pcm)) + bytes(pcm)
    path.write_bytes(b"RIFF" + struct.pack("<I", 4 + len(body)) + b"WAVE" + body)
    return path
