from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]


def _compiler() -> str:
    compiler = shutil.which("gcc") or shutil.which("clang") or shutil.which("cc")
    if compiler is None:
        pytest.skip("No C compiler (gcc, clang, cc) found in PATH")
    return compiler


def _run(compile_cmd: list[str], binary: Path) -> None:
    subprocess.run(compile_cmd, cwd=REPO_ROOT, check=True)
    subprocess.run([str(binary)], cwd=REPO_ROOT, check=True)


def test_hermite_interpolation_math_selection_and_guard(tmp_path: Path) -> None:
    """M3.2B core: constant/ramp/sine/wrap/reverse math, profile selection,
    overshoot quantification, guard footprint, scheduler determinism and cost.

    The harness includes the real core translation unit with the interpolation
    telemetry enabled so the selected path is observable."""
    compiler = _compiler()
    suffix = ".exe" if sys.platform == "win32" else ""
    binary = tmp_path / f"hermite_interpolation_harness{suffix}"
    _run(
        [
            compiler,
            "-O2",
            "-Wall",
            "-Wextra",
            "-std=c11",
            "-DBUBBLES_INTERPOLATION_TELEMETRY=1",
            "-Icore",
            "-Icore/dsp",
            "tests/dsp/hermite_interpolation_harness.c",
            "core/engine/bubble_engine.c",
            "core/engine/bubble_macro_map.c",
            "-lm",
            "-o",
            str(binary),
        ],
        binary,
    )


def test_hermite_musical_ab_and_profile_paths(tmp_path: Path) -> None:
    """M3.2B musical A/B through the real engine: linear (MCU) vs Hermite (WEB)
    with identical seed/input, plus evidence that the engine actually selected
    and executed the expected interpolation path."""
    compiler = _compiler()
    suffix = ".exe" if sys.platform == "win32" else ""
    binary = tmp_path / f"hermite_musical_ab_harness{suffix}"
    _run(
        [
            compiler,
            "-O2",
            "-Wall",
            "-Wextra",
            "-std=c11",
            "-DBUBBLES_INTERPOLATION_TELEMETRY=1",
            "-Icore",
            "-Icore/dsp",
            "tests/dsp/hermite_musical_ab_harness.c",
            "core/engine/bubble_engine.c",
            "core/engine/bubble_macro_map.c",
            "core/dsp/sound_bubbles_dsp.c",
            "-lm",
            "-o",
            str(binary),
        ],
        binary,
    )
