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


def _compile_probe(compiler: str, float_ring: int, binary_path: Path) -> None:
    probe_src = REPO_ROOT / "scripts" / "m4c_1_freeze_probe.c"
    cmd = [
        compiler,
        "-O2",
        "-Wall",
        "-Wextra",
        "-std=c11",
        f"-DBUBBLES_RING_FLOAT={float_ring}",
        f"-I{REPO_ROOT / 'core'}",
        f"-I{REPO_ROOT / 'core' / 'dsp'}",
        f"-I{REPO_ROOT / 'core' / 'engine'}",
        str(probe_src),
        str(REPO_ROOT / "core" / "engine" / "bubble_engine.c"),
        str(REPO_ROOT / "core" / "engine" / "bubble_macro_map.c"),
        "-lm",
        "-o",
        str(binary_path),
    ]
    subprocess.run(cmd, cwd=REPO_ROOT, check=True)


def test_m4c_freeze_qualification_core(tmp_path: Path) -> None:
    compiler = _compiler()
    suffix = ".exe" if sys.platform == "win32" else ""
    bin_float = tmp_path / f"probe_float{suffix}"
    bin_int16 = tmp_path / f"probe_int16{suffix}"

    _compile_probe(compiler, 1, bin_float)
    _compile_probe(compiler, 0, bin_int16)

    # 1. Determinism
    res_det_fl = subprocess.run([str(bin_float), "--determinism"], capture_output=True, text=True, check=True).stdout
    assert "output_match=1,ring_match=1" in res_det_fl

    res_det_i16 = subprocess.run([str(bin_int16), "--determinism", "--dither"], capture_output=True, text=True, check=True).stdout
    assert "output_match=1,ring_match=1,dither_rng_match=1" in res_det_i16

    # 2. Freeze Write Lock
    res_frz_fl = subprocess.run([str(bin_float), "--freeze-lock"], capture_output=True, text=True, check=True).stdout
    assert "ptr_match=1,hash_match=1" in res_frz_fl

    res_frz_i16 = subprocess.run([str(bin_int16), "--freeze-lock", "--dither"], capture_output=True, text=True, check=True).stdout
    assert "ptr_match=1,hash_match=1,dither_rng_match=1" in res_frz_i16

    # 3. Dither RNG Isolation
    res_iso = subprocess.run([str(bin_int16), "--dither-isolation"], capture_output=True, text=True, check=True).stdout
    assert "mismatches=0" in res_iso

    # 4. Hermite Math Parity
    res_herm_fl = subprocess.run([str(bin_float), "--hermite-math-parity"], capture_output=True, text=True, check=True).stdout
    for line in res_herm_fl.strip().splitlines()[1:]:
        assert line.endswith(",1"), f"Hermite float test failed: {line}"

    res_herm_i16 = subprocess.run([str(bin_int16), "--hermite-math-parity"], capture_output=True, text=True, check=True).stdout
    for line in res_herm_i16.strip().splitlines()[1:]:
        assert line.endswith(",1"), f"Hermite int16 test failed: {line}"

    # 5. Block Invariance
    res_bi_fl = subprocess.run([str(bin_float), "--block-invariance"], capture_output=True, text=True, check=True).stdout
    for line in res_bi_fl.strip().splitlines()[1:]:
        parts = line.split(",")
        assert float(parts[1]) == 0.0, f"Float block variance detected: {line}"
