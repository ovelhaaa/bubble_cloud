from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]

C_SOURCES = [
    "core/dsp/sound_bubbles_dsp.c",
    "core/engine/bubble_engine.c",
    "core/engine/bubble_macro_map.c",
]
WRAPPER_SOURCE = "platform/juce/Source/BubbleCloudEngineWrapper.cpp"
PROBE_SOURCE = "tests/juce/wrapper_stereo_probe.cpp"


def test_engine_wrapper_preserves_dry_locality_and_spatial_wet(tmp_path: Path) -> None:
    cc = shutil.which("gcc") or shutil.which("clang") or shutil.which("cc")
    cxx = shutil.which("g++") or shutil.which("clang++") or shutil.which("c++")
    if cc is None or cxx is None:
        pytest.skip("A C and C++ compiler are required for the wrapper stereo probe")

    exe_suffix = ".exe" if sys.platform == "win32" else ""

    objects: list[str] = []
    for source in C_SOURCES:
        obj = tmp_path / (Path(source).stem + ".o")
        subprocess.run(
            [cc, "-O2", "-Wall", "-Wextra", "-std=c11", "-Icore", "-Icore/dsp",
             "-c", source, "-o", str(obj)],
            cwd=REPO_ROOT,
            check=True,
        )
        objects.append(str(obj))

    wrapper_obj = tmp_path / "BubbleCloudEngineWrapper.o"
    subprocess.run(
        [cxx, "-std=c++17", "-O2", "-Iplatform/juce/Source", "-Icore", "-Icore/dsp",
         "-c", WRAPPER_SOURCE, "-o", str(wrapper_obj)],
        cwd=REPO_ROOT,
        check=True,
    )

    binary = tmp_path / f"wrapper_stereo_probe{exe_suffix}"
    subprocess.run(
        [cxx, "-std=c++17", "-O2", "-Iplatform/juce/Source", "-Icore", "-Icore/dsp",
         PROBE_SOURCE, str(wrapper_obj), *objects, "-lm", "-o", str(binary)],
        cwd=REPO_ROOT,
        check=True,
    )
    subprocess.run([str(binary)], cwd=REPO_ROOT, check=True)
