from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]


def test_m3_2a_tonal_bus_rebalance_invariants(tmp_path: Path) -> None:
    """M3.2A objective bus test: attack HPF keeps 220-440 Hz body, the dynamic
    sustain LPF keeps 2-4 kHz presence, rolls off above it, is modulated by phrase
    state/WARMTH, smoothed without abrupt steps and sample-rate invariant."""
    compiler = shutil.which("gcc") or shutil.which("clang") or shutil.which("cc")
    if compiler is None:
        pytest.skip("No C compiler (gcc, clang, cc) found in PATH")

    binary_suffix = ".exe" if sys.platform == "win32" else ""
    binary = tmp_path / f"m3_2a_tone_harness{binary_suffix}"
    compile_cmd = [
        compiler,
        "-O2",
        "-Wall",
        "-Wextra",
        "-std=c11",
        "-Icore",
        "-Icore/dsp",
        "tests/dsp/m3_2a_tone_harness.c",
        "core/engine/bubble_engine.c",
        "core/engine/bubble_macro_map.c",
        "core/dsp/sound_bubbles_dsp.c",
        "-lm",
        "-o",
        str(binary),
    ]
    subprocess.run(compile_cmd, cwd=REPO_ROOT, check=True)
    subprocess.run([str(binary)], cwd=REPO_ROOT, check=True)
