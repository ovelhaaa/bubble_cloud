from __future__ import annotations

import subprocess
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]


def test_core_numerical_stability_vectors_and_extreme_presets(tmp_path: Path) -> None:
    binary = tmp_path / "numerical_stability_harness"
    compile_cmd = [
        "gcc",
        "-O2",
        "-Wall",
        "-Wextra",
        "-std=c11",
        "-Icore",
        "-Icore/dsp",
        "tests/dsp/numerical_stability_harness.c",
        "core/engine/bubble_engine.c",
        "core/dsp/sound_bubbles_dsp.c",
        "-lm",
        "-o",
        str(binary),
    ]
    subprocess.run(compile_cmd, cwd=REPO_ROOT, check=True)
    subprocess.run([str(binary)], cwd=REPO_ROOT, check=True)
