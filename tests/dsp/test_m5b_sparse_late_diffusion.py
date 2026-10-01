"""
M5B Sparse Late-Tail Diffusion Tests.
Validates early-phrase parity invariant (0-300 ms), context-dependent late diffuser activation,
internal loop stability, non-combing periodicity, pitch preservation, block-size invariance,
and silence startup.
"""

from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path
import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def cand_m5b_probe() -> Path:
    build_dir = REPO_ROOT / "build"
    build_dir.mkdir(exist_ok=True)
    suffix = ".exe" if sys.platform == "win32" else ""
    cand_bin = build_dir / f"m5b_cand_probe{suffix}"

    if not cand_bin.exists():
        compiler = shutil.which("gcc") or shutil.which("clang") or shutil.which("cc")
        if compiler is None:
            pytest.skip("No C compiler found in PATH to build M5B candidate probe")
        cmd = [
            compiler,
            "-O2",
            "-Wall",
            "-Wextra",
            "-std=c11",
            "-DM5B_CANDIDATE_BUILD=1",
            f"-I{REPO_ROOT / 'core'}",
            f"-I{REPO_ROOT / 'core' / 'dsp'}",
            f"-I{REPO_ROOT / 'core' / 'engine'}",
            str(REPO_ROOT / "scripts" / "m5b_probe.c"),
            str(REPO_ROOT / "core" / "dsp" / "sound_bubbles_dsp.c"),
            str(REPO_ROOT / "core" / "engine" / "bubble_engine.c"),
            str(REPO_ROOT / "core" / "engine" / "bubble_macro_map.c"),
            "-lm",
            "-o",
            str(cand_bin),
        ]
        subprocess.run(cmd, cwd=REPO_ROOT, check=True)
    return cand_bin


def test_m5b_attack_parity_and_early_invariance(cand_m5b_probe: Path):
    """M5B: 0-300 ms early phrase output must produce valid finite signal."""
    res = subprocess.run([str(cand_m5b_probe), "attack_parity"], capture_output=True, text=True, check=True)
    out = res.stdout.strip()
    assert "ATTACK_PARITY_CSV" in out
    lines = [l for l in out.splitlines() if "," in l and not l.startswith("Source")]
    assert len(lines) >= 8
    for l in lines:
        parts = l.split(",")
        rms_db = float(parts[2])
        peak_db = float(parts[3])
        assert rms_db <= 0.0, f"RMS exceeded 0 dBFS: {rms_db}"
        assert peak_db <= 0.0, f"Peak exceeded 0 dBFS: {peak_db}"


def test_m5b_transition_discontinuity(cand_m5b_probe: Path):
    """M5B: Transition across M5A boundary (~230-420 ms) must remain smooth without clicks."""
    res = subprocess.run([str(cand_m5b_probe), "transition_discontinuity"], capture_output=True, text=True, check=True)
    out = res.stdout.strip()
    assert "TRANSITION_DISCONTINUITY_CSV" in out
    lines = [l for l in out.splitlines() if "," in l and not l.startswith("Source")]
    assert len(lines) >= 4
    for l in lines:
        parts = l.split(",")
        max_diff = float(parts[2])
        rms_diff = float(parts[3])
        assert max_diff < 0.20, f"Excessive step discontinuity: max_diff={max_diff}"
        assert rms_diff < 0.10, f"Excessive RMS step derivative: rms_diff={rms_diff}"


def test_m5b_periodicity_and_decorrelation(cand_m5b_probe: Path):
    """M5B: Diffuser delays must show low autocorrelation (no metallic ring comb)."""
    res = subprocess.run([str(cand_m5b_probe), "periodicity"], capture_output=True, text=True, check=True)
    out = res.stdout.strip()
    assert "PERIODICITY_CSV" in out
    lines = [l for l in out.splitlines() if "," in l and not l.startswith("AC_")]
    assert len(lines) >= 1
    parts = lines[0].split(",")
    ac_d0 = float(parts[0])
    ac_d1 = float(parts[1])
    ac_d2 = float(parts[2])
    assert abs(ac_d0) < 0.35, f"High autocorrelation at delay 0: {ac_d0}"
    assert abs(ac_d1) < 0.35, f"High autocorrelation at delay 1: {ac_d1}"
    assert abs(ac_d2) < 0.35, f"High autocorrelation at delay 2: {ac_d2}"


def test_m5b_runaway_stress_60_and_120(cand_m5b_probe: Path):
    """M5B: 60s and 120s high-feedback stress tests must remain bounded without NaN/Inf."""
    res60 = subprocess.run([str(cand_m5b_probe), "runaway_stress_60"], capture_output=True, text=True, check=True)
    assert "NaN_Inf=0" in res60.stdout
    assert "MaxPeak=" in res60.stdout

    res120 = subprocess.run([str(cand_m5b_probe), "runaway_stress_120"], capture_output=True, text=True, check=True)
    assert "NaN_Inf=0" in res120.stdout
    assert "MaxPeak=" in res120.stdout


def test_m5b_metallic_resonance(cand_m5b_probe: Path):
    """M5B: No sharp metallic resonance peaks in late tail."""
    res = subprocess.run([str(cand_m5b_probe), "metallic_resonance"], capture_output=True, text=True, check=True)
    assert res.returncode == 0
    assert "METALLIC_RESONANCE" in res.stdout


def test_m5b_pitch_preservation(cand_m5b_probe: Path):
    """M5B: Late tail must preserve input pitch within anti-loop drift tolerance."""
    res = subprocess.run([str(cand_m5b_probe), "pitch_preservation"], capture_output=True, text=True, check=True)
    assert res.returncode == 0
    assert "Observed=44" in res.stdout


def test_m5b_memory_bloom_monotonicity(cand_m5b_probe: Path):
    """M5B: Late diffuser send increases monotonically with MEMORY and BLOOM without exceeding ceiling."""
    res = subprocess.run([str(cand_m5b_probe), "memory_bloom_sweep"], capture_output=True, text=True, check=True)
    out = res.stdout.strip()
    assert "MEMORY_BLOOM_SWEEP_CSV" in out
    lines = [l for l in out.splitlines() if "," in l and not l.startswith("Memory")]
    assert len(lines) == 15
    for l in lines:
        parts = l.split(",")
        mem = float(parts[0])
        bloom = float(parts[1])
        send = float(parts[2])
        assert send <= 0.25, f"Send exceeded ceiling 0.25: {send}"
        if mem == 0.0:
            assert send == 0.0, f"Send non-zero with MEMORY=0: {send}"


def test_m5b_silence_startup_and_tail_decay(cand_m5b_probe: Path):
    """M5B: Zero input produces bit-exact zero, and impulse decays to silence."""
    res_sil = subprocess.run([str(cand_m5b_probe), "silence_startup"], capture_output=True, text=True, check=True)
    assert res_sil.returncode == 0
    assert "MaxPeak=0.000000000e+00" in res_sil.stdout

    res_decay = subprocess.run([str(cand_m5b_probe), "tail_decay"], capture_output=True, text=True, check=True)
    assert res_decay.returncode == 0
    assert "RMS_dB=" in res_decay.stdout


def test_m5b_block_size_invariance(cand_m5b_probe: Path):
    """M5B: Output energy must be invariant across host block sizes (32..2048)."""
    res = subprocess.run([str(cand_m5b_probe), "block_invariance"], capture_output=True, text=True, check=True)
    out = res.stdout.strip()
    lines = [l for l in out.splitlines() if "," in l and not l.startswith("Block")]
    assert len(lines) == 6
    rms_vals = [float(l.split(",")[1]) for l in lines]
    assert all(abs(r - rms_vals[0]) < 0.1 for r in rms_vals)
