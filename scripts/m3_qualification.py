#!/usr/bin/env python3
"""M3 Expressive Freeze & Temporal Morphing Qualification Script.

Compiles the M3 qualification probe, runs it against
tests/fixtures/audio/farran_ez-soft-indie-guitar-sample-456142.wav for
FREEZE in [0.0, 0.25, 0.50, 0.75, 1.0], generates output WAV files,
computes objective perceptual metrics (RMS, Peak, Correlation, Side/Mid,
Active Voices, Retention, Renewal, Limiter GR, CPU block budget),
and produces a summary report.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_WAV = REPO_ROOT / "tests" / "fixtures" / "audio" / "farran_ez-soft-indie-guitar-sample-456142.wav"
DEFAULT_OUTPUT_DIR = REPO_ROOT / "build_check" / "m3_qualification"

C_SOURCES = [
    "core/dsp/sound_bubbles_dsp.c",
    "core/engine/bubble_engine.c",
    "core/engine/bubble_macro_map.c",
]


def _get_compiler() -> str:
    compiler = shutil.which("gcc") or shutil.which("clang") or shutil.which("cc")
    if compiler is None:
        raise SystemExit("A C compiler (gcc, clang, cc) is required for M3 qualification")
    return compiler


def build_probe(output_binary: Path) -> Path:
    cc = _get_compiler()
    probe_source = REPO_ROOT / "scripts" / "m3_qualification_probe.c"
    sources = [str(probe_source)] + [str(REPO_ROOT / s) for s in C_SOURCES]

    cmd = [
        cc,
        "-O2",
        "-Wall",
        "-Wextra",
        "-std=c11",
        "-Icore",
        "-Icore/dsp",
        *sources,
        "-lm",
        "-o",
        str(output_binary),
    ]
    subprocess.run(cmd, cwd=REPO_ROOT, check=True)
    return output_binary


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wav", type=Path, default=DEFAULT_WAV, help="Input WAV file")
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR, help="Output directory")
    args = parser.parse_args()

    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    exe_suffix = ".exe" if sys.platform == "win32" else ""
    binary_path = output_dir / f"m3_qualification_probe{exe_suffix}"

    print(f"[M3 Qualification] Building probe at {binary_path}...")
    build_probe(binary_path)

    print(f"[M3 Qualification] Running probe on {args.wav}...")
    result = subprocess.run(
        [str(binary_path), str(args.wav.resolve()), str(output_dir)],
        cwd=REPO_ROOT,
        check=True,
        text=True,
        capture_output=True,
    )

    print(result.stdout)

    # Save output log
    report_path = output_dir / "m3_qualification_report.txt"
    report_path.write_text(result.stdout, encoding="utf-8")
    print(f"[M3 Qualification] Report written to {report_path}")
    print(f"[M3 Qualification] Generated audio renders in {output_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
