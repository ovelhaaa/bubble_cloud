from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]


def test_current_dsp_feature_controls_and_scheduler_behaviors(tmp_path: Path) -> None:
    compiler = shutil.which("gcc") or shutil.which("clang") or shutil.which("cc")
    if compiler is None:
        pytest.skip("No C compiler (gcc, clang, cc) found in PATH")

    binary_suffix = ".exe" if sys.platform == "win32" else ""
    binary = tmp_path / f"dsp_feature_harness{binary_suffix}"
    compile_cmd = [
        compiler,
        "-O2",
        "-Wall",
        "-Wextra",
        "-std=c11",
        "-Icore",
        "-Icore/dsp",
        "tests/dsp/dsp_feature_harness.c",
        "core/engine/bubble_engine.c",
        "core/engine/bubble_macro_map.c",
        "core/dsp/sound_bubbles_dsp.c",
        "-lm",
        "-o",
        str(binary),
    ]
    subprocess.run(compile_cmd, cwd=REPO_ROOT, check=True)
    subprocess.run([str(binary)], cwd=REPO_ROOT, check=True)
