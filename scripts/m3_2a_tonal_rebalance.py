#!/usr/bin/env python3
"""M3.2A tonal bus rebalance A/B qualification.

Renders the same test material through a baseline tree (default: HEAD, i.e. the
pre-M3.2A cutoffs) and the current working tree with the offline renderer, using
the same preset, seed and input, then reports objective perceptual metrics:

    RMS, peak, spectral centroid, limiter gain reduction,
    band energies (80-250, 250-800, 800-2k, 2-5k, 5-10k Hz),
    and a transient-to-sustain spectral continuity score.

The baseline is materialised with a throw-away ``git worktree`` so the current
working tree is never disturbed. Everything is pure standard library (no numpy):
the analysis uses a small in-script radix-2 FFT.

Test material:
    pluck      -> tests/fixtures/audio/farran_ez-soft-indie-guitar-sample-456142.wav
    pad        -> deterministic synthesized sustained pad
    transient  -> deterministic synthesized short transient train
"""
from __future__ import annotations

import argparse
import cmath
import csv
import math
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT_DIR = REPO_ROOT / "build_check" / "m3_2a_tonal"
DEFAULT_GUITAR = REPO_ROOT / "tests" / "fixtures" / "audio" / "farran_ez-soft-indie-guitar-sample-456142.wav"
PRESET = "core/presets/factory/neutral.json"

C_SOURCES = [
    "platform/offline/sound_bubbles_render.c",
    "core/presets/bubble_preset.c",
    "core/engine/bubble_engine.c",
    "core/engine/bubble_macro_map.c",
    "core/dsp/sound_bubbles_dsp.c",
]

BANDS = [
    ("80-250 Hz", 80.0, 250.0),
    ("250-800 Hz", 250.0, 800.0),
    ("800-2k Hz", 800.0, 2000.0),
    ("2-5k Hz", 2000.0, 5000.0),
    ("5-10k Hz", 5000.0, 10000.0),
]

FFT_SIZE = 2048
HOP = 1024


# ---------------------------------------------------------------------------
# Minimal WAV I/O (stdlib only)
# ---------------------------------------------------------------------------
def read_wav_mono(path: Path) -> tuple[list[float], int]:
    data = path.read_bytes()
    if data[0:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError(f"Not a RIFF/WAVE file: {path}")
    pos = 12
    fmt = None
    audio = None
    while pos + 8 <= len(data):
        chunk_id = data[pos:pos + 4]
        size = struct.unpack_from("<I", data, pos + 4)[0]
        body = data[pos + 8:pos + 8 + size]
        if chunk_id == b"fmt ":
            fmt = struct.unpack_from("<HHIIHH", body, 0)
        elif chunk_id == b"data":
            audio = body
        pos += 8 + size + (size & 1)
    if fmt is None or audio is None:
        raise ValueError(f"Missing fmt/data chunk: {path}")

    audio_format, channels, sample_rate, _, _, bits = fmt
    samples: list[float] = []
    if audio_format == 3 and bits == 32:
        count = len(audio) // 4
        values = struct.unpack_from(f"<{count}f", audio, 0)
        for frame in range(0, count - channels + 1, channels):
            samples.append(sum(values[frame:frame + channels]) / channels)
    elif audio_format == 1 and bits == 16:
        count = len(audio) // 2
        values = struct.unpack_from(f"<{count}h", audio, 0)
        for frame in range(0, count - channels + 1, channels):
            samples.append(sum(values[frame:frame + channels]) / (channels * 32768.0))
    else:
        raise ValueError(f"Unsupported WAV format {audio_format}/{bits} in {path}")
    return samples, sample_rate


def write_wav_pcm16(path: Path, samples: list[float], sample_rate: int) -> None:
    pcm = bytearray()
    for value in samples:
        clipped = max(-1.0, min(1.0, value))
        pcm.extend(struct.pack("<h", round(clipped * 32767.0)))
    fmt_chunk = struct.pack("<HHIIHH", 1, 1, sample_rate, sample_rate * 2, 2, 16)
    body = b"fmt " + struct.pack("<I", len(fmt_chunk)) + fmt_chunk + b"data" + struct.pack("<I", len(pcm)) + bytes(pcm)
    path.write_bytes(b"RIFF" + struct.pack("<I", 4 + len(body)) + b"WAVE" + body)


# ---------------------------------------------------------------------------
# Deterministic test material
# ---------------------------------------------------------------------------
def synth_pad(sample_rate: int = 44100, seconds: float = 4.0) -> list[float]:
    frames = int(sample_rate * seconds)
    partials = [(110.0, 0.55), (164.81, 0.28), (220.0, 0.22), (329.63, 0.14), (440.0, 0.08)]
    out = []
    for i in range(frames):
        t = i / sample_rate
        env = min(1.0, t / 0.8) * min(1.0, (seconds - t) / 0.8)
        value = 0.0
        for freq, amp in partials:
            value += amp * math.sin(2.0 * math.pi * freq * t + 0.3 * math.sin(2.0 * math.pi * 0.7 * t))
        out.append(0.28 * env * value)
    return out


def synth_transient(sample_rate: int = 44100, seconds: float = 3.0) -> list[float]:
    frames = int(sample_rate * seconds)
    out = [0.0] * frames
    for onset in range(0, frames, int(sample_rate * 0.5)):
        for i in range(onset, min(frames, onset + int(sample_rate * 0.02))):
            t = (i - onset) / sample_rate
            out[i] += 0.7 * math.sin(2.0 * math.pi * 1400.0 * t) * math.exp(-t * 220.0)
        for i in range(onset, min(frames, onset + int(sample_rate * 0.25))):
            t = (i - onset) / sample_rate
            out[i] += 0.25 * math.sin(2.0 * math.pi * 196.0 * t) * math.exp(-t * 7.0)
    return out


# ---------------------------------------------------------------------------
# FFT analysis
# ---------------------------------------------------------------------------
def _fft(values: list[complex]) -> list[complex]:
    n = len(values)
    if n <= 1:
        return values
    even = _fft(values[0::2])
    odd = _fft(values[1::2])
    result = [0j] * n
    for k in range(n // 2):
        twiddle = cmath.exp(-2j * math.pi * k / n) * odd[k]
        result[k] = even[k] + twiddle
        result[k + n // 2] = even[k] - twiddle
    return result


def _window() -> list[float]:
    return [0.5 - 0.5 * math.cos(2.0 * math.pi * i / (FFT_SIZE - 1)) for i in range(FFT_SIZE)]


def analyze(samples: list[float], sample_rate: int) -> dict[str, float]:
    hann = _window()
    frames = max(1, (len(samples) - FFT_SIZE) // HOP + 1)
    band_power = [0.0] * len(BANDS)
    centroid_weighted = 0.0
    centroid_total = 0.0
    frame_rms: list[float] = []

    for f in range(frames):
        start = f * HOP
        windowed = [samples[start + i] * hann[i] for i in range(FFT_SIZE)]
        spectrum = _fft([complex(v, 0.0) for v in windowed])
        half = FFT_SIZE // 2
        rms = math.sqrt(sum(abs(spectrum[k]) ** 2 for k in range(half)) / half)
        frame_rms.append(rms)
        for k in range(1, half):
            freq = k * sample_rate / FFT_SIZE
            mag = abs(spectrum[k])
            power = mag * mag
            for bi, (_, lo, hi) in enumerate(BANDS):
                if lo <= freq < hi:
                    band_power[bi] += power
                    break
            centroid_weighted += freq * mag
            centroid_total += mag

    # Time-domain metrics.
    sum_sq = sum(v * v for v in samples)
    rms = math.sqrt(sum_sq / max(1, len(samples)))
    peak = max((abs(v) for v in samples), default=0.0)
    centroid = (centroid_weighted / centroid_total) if centroid_total > 0 else 0.0

    band_db = []
    for power in band_power:
        band_db.append(10.0 * math.log10(power / frames + 1e-20))

    # Transient-to-sustain continuity: compare the averaged band distribution of
    # attack frames (short-term energy above the local trend) with sustain frames.
    if len(frame_rms) >= 8:
        window_len = 8
        attack_vec = [0.0] * len(BANDS)
        sustain_vec = [0.0] * len(BANDS)
        attack_count = 0
        sustain_count = 0
        for f in range(frames):
            lo = max(0, f - window_len)
            hi = min(frames, f + window_len + 1)
            local = sum(frame_rms[lo:hi]) / (hi - lo)
            if frame_rms[f] > 1.6 * local and frame_rms[f] > 1e-4:
                group = attack_vec
                attack_count += 1
            elif frame_rms[f] > 1e-4:
                group = sustain_vec
                sustain_count += 1
            else:
                continue
            start = f * HOP
            windowed = [samples[start + i] * hann[i] for i in range(FFT_SIZE)]
            spectrum = _fft([complex(v, 0.0) for v in windowed])
            for k in range(1, FFT_SIZE // 2, 4):
                freq = k * sample_rate / FFT_SIZE
                power = abs(spectrum[k]) ** 2
                for bi, (_, blo, bhi) in enumerate(BANDS):
                    if blo <= freq < bhi:
                        group[bi] += power
                        break
        continuity = 0.0
        if attack_count > 0 and sustain_count > 0:
            dot = sum(a * s for a, s in zip(attack_vec, sustain_vec))
            na = math.sqrt(sum(a * a for a in attack_vec))
            ns = math.sqrt(sum(s * s for s in sustain_vec))
            if na > 0 and ns > 0:
                continuity = dot / (na * ns)
        else:
            continuity = float("nan")
    else:
        continuity = float("nan")

    result = {
        "rms": rms,
        "peak": peak,
        "centroid_hz": centroid,
        "continuity": continuity,
    }
    for (name, _, _), value in zip(BANDS, band_db):
        result[f"band_{name}"] = value
    return result


def min_limiter_gain(metrics_csv: Path) -> float:
    minimum = 1.0
    with metrics_csv.open(newline="") as fh:
        for row in csv.DictReader(fh):
            try:
                value = float(row["limiter_gain"])
            except (KeyError, ValueError):
                continue
            if value < minimum:
                minimum = value
    return minimum


# ---------------------------------------------------------------------------
# Build + render
# ---------------------------------------------------------------------------
def build_renderer(tree: Path, output: Path) -> Path:
    cc = shutil.which("gcc") or shutil.which("clang") or shutil.which("cc")
    if cc is None:
        raise SystemExit("A C compiler (gcc, clang, cc) is required for M3.2A A/B validation")
    exe_suffix = ".exe" if sys.platform == "win32" else ""
    binary = output.with_suffix(exe_suffix)
    subprocess.run(
        [cc, "-O2", "-Wall", "-Wextra", "-std=c11", "-Icore", "-Icore/dsp",
         *[str(tree / s) for s in C_SOURCES], "-lm", "-o", str(binary)],
        cwd=tree, check=True,
    )
    return binary


def render(binary: Path, tree: Path, input_wav: Path, out_wav: Path, metrics_csv: Path) -> None:
    subprocess.run(
        [str(binary), str(input_wav), str(tree / PRESET), str(out_wav), "--metrics-out", str(metrics_csv)],
        cwd=tree, check=True, capture_output=True, text=True,
    )


def _fmt(value: float, decimals: int = 4) -> str:
    if value != value:  # NaN
        return "n/a"
    return f"{value:.{decimals}f}"


def delta(base: float, cand: float) -> str:
    if base != base or cand != cand:
        return "n/a"
    return f"{cand - base:+.4f}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-ref", default="HEAD", help="git ref for the pre-M3.2A baseline")
    parser.add_argument("--output-dir", default=str(DEFAULT_OUTPUT_DIR), help="where reports and renders are written")
    parser.add_argument("--guitar", default=str(DEFAULT_GUITAR), help="plucked guitar fixture")
    args = parser.parse_args()

    output_dir = Path(args.output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    materials: list[tuple[str, Path]] = []
    guitar_src = Path(args.guitar).resolve()
    if guitar_src.exists():
        (samples, sr) = read_wav_mono(guitar_src)
        guitar_copy = output_dir / "input_pluck.wav"
        write_wav_pcm16(guitar_copy, samples, sr)
        materials.append(("pluck", guitar_copy))
    pad_path = output_dir / "input_pad.wav"
    write_wav_pcm16(pad_path, synth_pad(), 44100)
    materials.append(("pad", pad_path))
    transient_path = output_dir / "input_transient.wav"
    write_wav_pcm16(transient_path, synth_transient(), 44100)
    materials.append(("transient", transient_path))

    candidate_binary = build_renderer(REPO_ROOT, output_dir / "render_candidate")

    results: dict[str, dict[str, dict[str, float]]] = {"baseline": {}, "candidate": {}}
    limiter: dict[str, dict[str, float]] = {"baseline": {}, "candidate": {}}

    with tempfile.TemporaryDirectory() as tmp:
        worktree = Path(tmp) / "baseline"
        subprocess.run(["git", "worktree", "add", "--detach", str(worktree), args.baseline_ref],
                       cwd=REPO_ROOT, check=True, capture_output=True)
        try:
            baseline_binary = build_renderer(worktree, output_dir / "render_baseline")
            for name, input_wav in materials:
                for label, binary, tree in (
                    ("baseline", baseline_binary, worktree),
                    ("candidate", candidate_binary, REPO_ROOT),
                ):
                    out_wav = output_dir / f"{label}_{name}.wav"
                    metrics_csv = output_dir / f"{label}_{name}_metrics.csv"
                    render(binary, tree, input_wav, out_wav, metrics_csv)
                    samples, sr = read_wav_mono(out_wav)
                    results[label][name] = analyze(samples, sr)
                    limiter[label][name] = min_limiter_gain(metrics_csv)
        finally:
            subprocess.run(["git", "worktree", "remove", "--force", str(worktree)],
                           cwd=REPO_ROOT, check=False, capture_output=True)

    metric_keys = ["rms", "peak", "centroid_hz", "continuity", "limiter_gr_db"] + [
        f"band_{name}" for name, _, _ in BANDS
    ]

    rows: list[dict[str, str]] = []
    for name, _ in materials:
        for key in metric_keys:
            if key == "limiter_gr_db":
                base = limiter["baseline"][name]
                cand = limiter["candidate"][name]
                base = -20.0 * math.log10(base) if base > 0 else 0.0
                cand = -20.0 * math.log10(cand) if cand > 0 else 0.0
            else:
                base = results["baseline"][name][key]
                cand = results["candidate"][name][key]
            rows.append({
                "material": name,
                "metric": key,
                "baseline": _fmt(base),
                "candidate": _fmt(cand),
                "delta": delta(base, cand),
            })

    report = output_dir / "m3_2a_tonal_rebalance.csv"
    with report.open("w", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=["material", "metric", "baseline", "candidate", "delta"])
        writer.writeheader()
        writer.writerows(rows)

    print(f"baseline ref: {args.baseline_ref}")
    print(f"report: {report}")
    print(f"{'material':10} {'metric':14} {'baseline':>12} {'candidate':>12} {'delta':>12}")
    for row in rows:
        print(f"{row['material']:10} {row['metric']:14} {row['baseline']:>12} {row['candidate']:>12} {row['delta']:>12}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
