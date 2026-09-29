from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]


def test_m3_2c_intra_tick_spawn_jitter(tmp_path: Path) -> None:
    """M3.2C regression: deterministic intra-tick spawn onset jitter.

    The harness includes the DSP implementation directly (white box) so it can
    drive the static onset helper and the scheduler; the DSP source is therefore
    not passed to the compiler as a separate translation unit. It checks onset
    determinism/bounds, sample-exact RHYTHM/STRUM, stereo onset coherence,
    droplet identity, burst spread, bit determinism, block-size independence and
    delayed-onset guard safety."""
    compiler = shutil.which("gcc") or shutil.which("clang") or shutil.which("cc")
    if compiler is None:
        pytest.skip("No C compiler (gcc, clang, cc) found in PATH")

    binary_suffix = ".exe" if sys.platform == "win32" else ""
    binary = tmp_path / f"m3_2c_spawn_jitter_harness{binary_suffix}"
    compile_cmd = [
        compiler,
        "-O2",
        "-Wall",
        "-Wextra",
        "-std=c11",
        "-Icore",
        "-Icore/dsp",
        "tests/dsp/m3_2c_spawn_jitter_harness.c",
        "core/engine/bubble_engine.c",
        "core/engine/bubble_macro_map.c",
        "-lm",
        "-o",
        str(binary),
    ]
    subprocess.run(compile_cmd, cwd=REPO_ROOT, check=True)
    subprocess.run([str(binary)], cwd=REPO_ROOT, check=True)
