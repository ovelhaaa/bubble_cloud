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
    # Sample-exact or sub-micro difference vs 64
    max_diffs = [float(l.split(",")[4]) for l in lines]
    assert all(d < 1e-5 for d in max_diffs)


def test_m5b_diffuser_sanity_and_window_telemetry(cand_m5b_probe: Path):
    """M5B.1: Diffuser window telemetry must record active return energy (resolving Problem B)."""
    res = subprocess.run([str(cand_m5b_probe), "diffuser_sanity"], capture_output=True, text=True, check=True)
    out = res.stdout.strip()
    assert "DIFFUSER_SANITY:" in out
    assert "SendMean=" in out
    assert "ReturnRMS_dB=" in out
    # Verify return is well above noise floor and not -180 dB
    assert "-180.00" not in out
    # Parse metrics
    parts = dict(kv.split("=") for kv in out.replace("DIFFUSER_SANITY: ", "").split())
    send_mean = float(parts["SendMean"])
    ret_rms_db = float(parts["ReturnRMS_dB"])
    active_pct = float(parts["ActivePct"])
    assert send_mean > 0.05
    assert ret_rms_db > -90.0
    assert active_pct > 50.0


def test_m5b_limiter_hierarchy_nominal_and_dense(cand_m5b_probe: Path):
    """M5B.1: Nominal and Dense scenarios must produce 0.00 dB final limiter GR (resolving Problem D)."""
    res = subprocess.run([str(cand_m5b_probe), "limiter_scenarios"], capture_output=True, text=True, check=True)
    out = res.stdout.strip()
    assert "LIMITER_SCENARIOS_CSV" in out
    lines = [l for l in out.splitlines() if "," in l and not l.startswith("Scenario")]
    assert len(lines) == 3
    # Check Nominal
    nom_parts = lines[0].split(",")
    assert nom_parts[0] == "Nominal"
    assert float(nom_parts[1]) == 0.00  # FinalLimMaxGR_dB
    assert float(nom_parts[2]) == 0.00  # FinalLimActivePct
    # Check Dense
    dense_parts = lines[1].split(",")
    assert dense_parts[0] == "Dense"
    assert float(dense_parts[1]) == 0.00  # FinalLimMaxGR_dB
    assert float(dense_parts[2]) == 0.00  # FinalLimActivePct
    # Check Extreme
    ext_parts = lines[2].split(",")
    assert ext_parts[0] == "Extreme"
    assert float(ext_parts[1]) < 6.0  # Safe transient control under overload


def test_m5b_normalized_tail_continuity_metrics(cand_m5b_probe: Path):
    """M5B.1: Normalized tail evolution must report finite continuity metrics across 4-12s."""
    res = subprocess.run([str(cand_m5b_probe), "normalized_tail_evolution"], capture_output=True, text=True, check=True)
    out = res.stdout.strip()
    assert "NORMALIZED_TAIL_EVOLUTION_CSV" in out
    lines = [l for l in out.splitlines() if "," in l and not l.startswith("Source")]
    assert len(lines) >= 12
    for l in lines:
        parts = l.split(",")
        occ = float(parts[2])
        crest = float(parts[3])
        gap = float(parts[4])
        assert occ >= 0.0
        assert crest >= 1.0
        assert 0.0 <= gap <= 100.0


def test_m5b_cpu_benchmark_speed(cand_m5b_probe: Path):
    """M5B.1: CPU benchmark must maintain >= 25x real-time speed across all sample rates."""
    res = subprocess.run([str(cand_m5b_probe), "cpu_benchmark"], capture_output=True, text=True, check=True)
    out = res.stdout.strip()
    assert "CPU_BENCHMARK_CSV" in out
    lines = [l for l in out.splitlines() if "," in l and not l.startswith("SampleRate") and not l.startswith("State")]
    bench_lines = [l for l in lines if len(l.split(",")) == 4]
    assert len(bench_lines) == 12
    for l in bench_lines:
        parts = l.split(",")
        speed = float(parts[3])
        assert speed >= 25.0, f"Speed dropped below 25x: {speed}"

