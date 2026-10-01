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


def _compile_probe(compiler: str, binary_path: Path) -> None:
    probe_src = REPO_ROOT / "scripts" / "m4d_probe.c"
    cmd = [
        compiler,
        "-O2",
        "-Wall",
        "-Wextra",
        "-std=c11",
        f"-I{REPO_ROOT / 'core'}",
        f"-I{REPO_ROOT / 'core' / 'dsp'}",
        f"-I{REPO_ROOT / 'core' / 'engine'}",
        str(probe_src),
        str(REPO_ROOT / "core" / "dsp" / "sound_bubbles_dsp.c"),
        str(REPO_ROOT / "core" / "engine" / "bubble_engine.c"),
        str(REPO_ROOT / "core" / "engine" / "bubble_macro_map.c"),
        "-lm",
        "-o",
        str(binary_path),
    ]
    subprocess.run(cmd, cwd=REPO_ROOT, check=True)


@pytest.fixture(scope="module")
def m4d_probe_bin(tmp_path_factory: pytest.TempPathFactory) -> Path:
    compiler = _compiler()
    temp_dir = tmp_path_factory.mktemp("m4d_probe")
    suffix = ".exe" if sys.platform == "win32" else ""
    bin_path = temp_dir / f"m4d_probe{suffix}"
    _compile_probe(compiler, bin_path)
    return bin_path


def test_m4d_mix_zero_invariant(m4d_probe_bin: Path) -> None:
    res = subprocess.run([str(m4d_probe_bin), "--mix-zero"], capture_output=True, text=True, check=True)
    assert "MIX=0 Invariant" in res.stdout
    assert "final_limiter_gain=1.0000" in res.stdout


def test_m4d_dry_pumping_mitigation(m4d_probe_bin: Path) -> None:
    res = subprocess.run([str(m4d_probe_bin), "--dry-pumping"], capture_output=True, text=True, check=True)
    assert "final_limiter_active_rate=0.00%" in res.stdout
    assert "min_final_lim_gain=1.0000" in res.stdout


def test_m4d_stereo_preservation(m4d_probe_bin: Path) -> None:
    res = subprocess.run([str(m4d_probe_bin), "--stereo"], capture_output=True, text=True, check=True)
    assert "Stereo Preservation" in res.stdout


def test_m4d_silence_noise_pumping_invariant(m4d_probe_bin: Path) -> None:
    res = subprocess.run([str(m4d_probe_bin), "--silence-noise"], capture_output=True, text=True, check=True)
    assert "Silence Noise Pumping Invariant" in res.stdout


def test_m4d_sample_rate_and_quality_profiles(m4d_probe_bin: Path) -> None:
    res = subprocess.run([str(m4d_probe_bin), "--sr-profiles"], capture_output=True, text=True, check=True)
    assert "MCU_SAFE" in res.stdout
    assert "WEB_ULTRA" in res.stdout


def test_m4d_extreme_stress_resilience(m4d_probe_bin: Path) -> None:
    res = subprocess.run([str(m4d_probe_bin), "--extreme"], capture_output=True, text=True, check=True)
    assert "Extreme Stress" in res.stdout


def test_m4d_freeze_qualification_direct_dry_pumping_under_threshold(tmp_path_factory: pytest.TempPathFactory) -> None:
    compiler = _compiler()
    temp_dir = tmp_path_factory.mktemp("m4d_freeze")
    suffix = ".exe" if sys.platform == "win32" else ""
    bin_path = temp_dir / f"m4d_freeze_probe{suffix}"
    probe_src = REPO_ROOT / "scripts" / "m4d_freeze_probe.c"
    cmd = [
        compiler,
        "-O2",
        "-Wall",
        "-Wextra",
        "-std=c11",
        "-DM4D_CANDIDATE_BUILD=1",
        f"-I{REPO_ROOT / 'core'}",
        f"-I{REPO_ROOT / 'core' / 'dsp'}",
        f"-I{REPO_ROOT / 'core' / 'engine'}",
        str(probe_src),
        str(REPO_ROOT / "core" / "dsp" / "sound_bubbles_dsp.c"),
        str(REPO_ROOT / "core" / "engine" / "bubble_engine.c"),
        str(REPO_ROOT / "core" / "engine" / "bubble_macro_map.c"),
        "-lm",
        "-o",
        str(bin_path),
    ]
    subprocess.run(cmd, cwd=REPO_ROOT, check=True)

    res = subprocess.run([str(bin_path), "--dry-pumping"], capture_output=True, text=True, check=True)
    # Parse dry_modulation_depth_db
    mod_depth = None
    for line in res.stdout.splitlines():
        if "dry_modulation_depth_db:" in line:
            mod_depth = float(line.split(":")[1].strip())
            break
    assert mod_depth is not None, "dry_modulation_depth_db not found in output"
    assert mod_depth < 0.25, f"Expected dry modulation depth < 0.25 dB, got {mod_depth} dB"

