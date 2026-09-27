from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]


def test_m2_stereo_coherence_lockstep_and_microdetune_guard(tmp_path: Path) -> None:
    """M2.1 regression for the shared coherence RNG lockstep under temporary L/R
    state divergence and for the microdetune-before-guard ordering.

    The harness includes the DSP implementation directly (white box) so it can
    drive the static spawn/guard helpers; the DSP source is therefore not passed
    to the compiler as a separate translation unit."""
    compiler = shutil.which("gcc") or shutil.which("clang") or shutil.which("cc")
    if compiler is None:
        pytest.skip("No C compiler (gcc, clang, cc) found in PATH")

    binary_suffix = ".exe" if sys.platform == "win32" else ""
    binary = tmp_path / f"m2_coherence_guard_harness{binary_suffix}"
    compile_cmd = [
        compiler,
        "-O2",
        "-Wall",
        "-Wextra",
        "-std=c11",
        "-Icore",
        "-Icore/dsp",
        "tests/dsp/m2_coherence_guard_harness.c",
        "core/engine/bubble_engine.c",
        "core/engine/bubble_macro_map.c",
        "-lm",
        "-o",
        str(binary),
    ]
    subprocess.run(compile_cmd, cwd=REPO_ROOT, check=True)
    subprocess.run([str(binary)], cwd=REPO_ROOT, check=True)
