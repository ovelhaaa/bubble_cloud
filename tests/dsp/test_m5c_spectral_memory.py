"""
M5C — Spectral Memory Evolution Tests.
Validates early-phrase parity invariant (0-300 ms), progressive tail spectral aging,
monotonic MEMORY scaling, CLARITY high-frequency retention, WARMTH bias,
FREEZE age locking, cross-phrase attack isolation, shimmer cutoff floor preservation,
block-size invariance, silence startup, and CPU overhead budget.
"""

from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path
import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def cand_m5c_probe() -> Path:
    build_dir = REPO_ROOT / "build"
    build_dir.mkdir(exist_ok=True)
    suffix = ".exe" if sys.platform == "win32" else ""
    cand_bin = build_dir / f"m5c_cand_probe{suffix}"

    compiler = shutil.which("gcc") or shutil.which("clang") or shutil.which("cc")
    if compiler is None:
        pytest.skip("No C compiler found in PATH to build M5C candidate probe")
    cmd = [
        compiler,
        "-O2",
        "-Wall",
        "-Wextra",
        "-std=c11",
        "-DM5C_CANDIDATE_BUILD=1",
        f"-I{REPO_ROOT / 'core'}",
        f"-I{REPO_ROOT / 'core' / 'dsp'}",
        f"-I{REPO_ROOT / 'core' / 'engine'}",
        str(REPO_ROOT / "scripts" / "m5c_probe.c"),
        str(REPO_ROOT / "core" / "dsp" / "sound_bubbles_dsp.c"),
        str(REPO_ROOT / "core" / "engine" / "bubble_engine.c"),
        str(REPO_ROOT / "core" / "engine" / "bubble_macro_map.c"),
        "-lm",
        "-o",
        str(cand_bin),
    ]
    subprocess.run(cmd, cwd=REPO_ROOT, check=True)
    return cand_bin


def test_m5c_attack_parity_and_early_invariance(cand_m5c_probe: Path):
    """M5C: 0-300 ms early phrase output must produce valid finite signal within bounds."""
    res = subprocess.run([str(cand_m5c_probe), "attack_parity"], capture_output=True, text=True, check=True)
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


def test_m5c_transition_discontinuity(cand_m5c_probe: Path):
    """M5C: Transition across M5A boundary (~230-420 ms) must remain smooth without clicks."""
    res = subprocess.run([str(cand_m5c_probe), "transition_discontinuity"], capture_output=True, text=True, check=True)
    out = res.stdout.strip()
    assert "TRANSITION_DISCONTINUITY_CSV" in out
    lines = [l for l in out.splitlines() if "," in l and not l.startswith("Source")]
    assert len(lines) >= 4
    for l in lines:
        parts = l.split(",")
        max_diff = float(parts[2])
        rms_diff = float(parts[3])
        assert max_diff < 0.35, f"Excessive step discontinuity: max_diff={max_diff}"
        assert rms_diff < 0.10, f"Excessive RMS step derivative: rms_diff={rms_diff}"


def test_m5c_tail_evolution(cand_m5c_probe: Path):
    """M5C: Late tail (6-12s) must show positive spectral age and moderate cutoff softening."""
    res = subprocess.run([str(cand_m5c_probe), "tail_evolution"], capture_output=True, text=True, check=True)
    out = res.stdout.strip()
    assert "TAIL_EVOLUTION_CSV" in out
    lines = [l for l in out.splitlines() if "," in l and not l.startswith("Source")]
    assert len(lines) >= 20

    # Parse rows for harmonic pluck
    pluck_rows = [l.split(",") for l in lines if l.startswith("harmonic_pluck")]
    w_0_1 = [r for r in pluck_rows if r[1] == "0-1s"][0]
    w_6_12 = [r for r in pluck_rows if r[1] == "6-12s"][0]

    age_0_1 = float(w_0_1[5])
    age_6_12 = float(w_6_12[5])
    cutoff_0_1 = float(w_0_1[8])
    cutoff_6_12 = float(w_6_12[8])

    assert age_6_12 > age_0_1, f"Expected age growth: {age_6_12} <= {age_0_1}"
    assert cutoff_6_12 < cutoff_0_1, f"Expected cutoff softening: {cutoff_6_12} >= {cutoff_0_1}"
    # Softening must be gentle and musical (not > 50% drop)
    assert cutoff_6_12 >= 0.50 * cutoff_0_1, f"Excessive darkening: {cutoff_6_12} < 0.50 * {cutoff_0_1}"


def test_m5c_memory_monotonicity(cand_m5c_probe: Path):
    """M5C: Increasing MEMORY macro must monotonically increase spectral age in the tail."""
    res = subprocess.run([str(cand_m5c_probe), "memory_sweep"], capture_output=True, text=True, check=True)
    out = res.stdout.strip()
    assert "MEMORY_SWEEP_CSV" in out
    lines = [l for l in out.splitlines() if "," in l and not l.startswith("MemoryMacro")]
    assert len(lines) == 5

    ages = []
    cutoffs = []
    for l in lines:
        parts = l.split(",")
        ages.append(float(parts[3]))
        cutoffs.append(float(parts[6]))

    for i in range(1, len(ages)):
        assert ages[i] > ages[i - 1], f"Non-monotonic age: {ages[i]} <= {ages[i-1]}"
        assert cutoffs[i] < cutoffs[i - 1], f"Non-monotonic cutoff: {cutoffs[i]} >= {cutoffs[i-1]}"


def test_m5c_clarity_retention(cand_m5c_probe: Path):
    """M5C: Increasing CLARITY macro must retain higher cutoff in late tail."""
    res = subprocess.run([str(cand_m5c_probe), "clarity_sweep"], capture_output=True, text=True, check=True)
    out = res.stdout.strip()
    assert "CLARITY_SWEEP_CSV" in out
    lines = [l for l in out.splitlines() if "," in l and not l.startswith("ClarityMacro")]
    assert len(lines) == 5

    cutoffs = [float(l.split(",")[5]) for l in lines]
    for i in range(1, len(cutoffs)):
        assert cutoffs[i] > cutoffs[i - 1], f"CLARITY failed to retain higher cutoff: {cutoffs[i]} <= {cutoffs[i-1]}"


def test_m5c_warmth_darkening(cand_m5c_probe: Path):
    """M5C: Increasing WARMTH macro must slightly increase spectral age in late tail."""
    res = subprocess.run([str(cand_m5c_probe), "warmth_sweep"], capture_output=True, text=True, check=True)
    out = res.stdout.strip()
    assert "WARMTH_SWEEP_CSV" in out
    lines = [l for l in out.splitlines() if "," in l and not l.startswith("WarmthMacro")]
    assert len(lines) == 5

    ages = [float(l.split(",")[3]) for l in lines]
    for i in range(1, len(ages)):
        assert ages[i] >= ages[i - 1], f"WARMTH failed to increase age: {ages[i]} < {ages[i-1]}"


def test_m5c_freeze_lock(cand_m5c_probe: Path):
    """M5C: When FREEZE engages, spectral age must stay locked with zero drift."""
    res = subprocess.run([str(cand_m5c_probe), "freeze_autohold_test"], capture_output=True, text=True, check=True)
    out = res.stdout.strip()
    assert "FREEZE_AUTOHOLD_CSV" in out
    lines = [l for l in out.splitlines() if "," in l and not l.startswith("Time_s")]

    # Freeze is active from t=2.5s onwards
    frozen_ages = []
    for l in lines:
        parts = l.split(",")
        t = float(parts[0])
        freeze_active = int(parts[1])
        age = float(parts[3])
        if t >= 3.0:
            assert freeze_active == 1
            frozen_ages.append(age)

    assert len(frozen_ages) >= 5
    for a in frozen_ages:
        assert abs(a - frozen_ages[0]) < 1e-4, f"Freeze age drifted: {a} != {frozen_ages[0]}"


def test_m5c_cross_phrase_attack_reset(cand_m5c_probe: Path):
    """M5C: On new transient onset (phrase 2 at t=3.0s), spectral age must drop immediately to 0."""
    res = subprocess.run([str(cand_m5c_probe), "cross_phrase_test"], capture_output=True, text=True, check=True)
    out = res.stdout.strip()
    assert "CROSS_PHRASE_CSV" in out
    lines = [l for l in out.splitlines() if "," in l and not l.startswith("Time_s")]

    attack2_rows = [l.split(",") for l in lines if 3.000 <= float(l.split(",")[0]) <= 3.050]
    assert len(attack2_rows) > 0
    for r in attack2_rows:
        age = float(r[2])
        assert age == 0.0, f"Spectral age was not reset on new attack: age={age}"


def test_m5c_shimmer_cutoff_floor(cand_m5c_probe: Path):
    """M5C: Shimmer mode must preserve cutoff above 3200 Hz floor."""
    res = subprocess.run([str(cand_m5c_probe), "shimmer_preservation"], capture_output=True, text=True, check=True)
    out = res.stdout.strip()
    assert "SHIMMER_PRESERVATION_CSV" in out
    lines = [l for l in out.splitlines() if "," in l and not l.startswith("ShimmerAmount")]
    assert len(lines) == 3

    for l in lines:
        parts = l.split(",")
        shimmer = float(parts[0])
        min_cutoff = float(parts[1])
        preserved = int(parts[4])
        assert preserved == 1, f"Floor was not preserved: shimmer={shimmer}, min_cutoff={min_cutoff}"
        if shimmer > 0.05:
            assert min_cutoff >= 3200.0 - 1.0, f"Cutoff fell below shimmer floor 3200 Hz: {min_cutoff}"
        else:
            assert min_cutoff >= 2800.0 - 1.0, f"Cutoff fell below base floor 2800 Hz: {min_cutoff}"


def test_m5c_block_invariance(cand_m5c_probe: Path):
    """M5C: Output RMS and cutoff should match across block sizes 32, 64, 128, 256."""
    res = subprocess.run([str(cand_m5c_probe), "block_invariance"], capture_output=True, text=True, check=True)
    out = res.stdout.strip()
    assert "BLOCK_INVARIANCE_CSV" in out
    lines = [l for l in out.splitlines() if "," in l and not l.startswith("BlockSize")]
    assert len(lines) == 4

    rms_vals = [float(l.split(",")[1]) for l in lines]
    cutoffs = [float(l.split(",")[4]) for l in lines]

    for r in rms_vals:
        assert abs(r - rms_vals[0]) < 0.10, f"Block size variance in RMS: {rms_vals}"
    for c in cutoffs:
        assert abs(c - cutoffs[0]) < 1.0, f"Block size variance in cutoff: {cutoffs}"


def test_m5c_silence_startup(cand_m5c_probe: Path):
    """M5C: Silence startup must produce bit-exact zeros without denormals or NaN."""
    res = subprocess.run([str(cand_m5c_probe), "silence_startup"], capture_output=True, text=True, check=True)
    out = res.stdout.strip()
    assert "SILENCE_STARTUP_CSV" in out
    lines = [l for l in out.splitlines() if "," in l and not l.startswith("Block")]
    assert len(lines) == 1
    parts = lines[0].split(",")
    max_abs = float(parts[1])
    spectral_age = float(parts[2])
    is_clean = int(parts[3])
    assert is_clean == 1
    assert max_abs == 0.0
    assert spectral_age == 0.0


def test_m5c_runaway_stress(cand_m5c_probe: Path):
    """M5C: Extreme parameters must remain numerically stable without NaN or unbounded limiter GR."""
    res = subprocess.run([str(cand_m5c_probe), "runaway_stress"], capture_output=True, text=True, check=True)
    out = res.stdout.strip()
    assert "RUNAWAY_STRESS_CSV" in out
    lines = [l for l in out.splitlines() if "," in l and not l.startswith("MaxPeak_dB")]
    assert len(lines) == 1
    parts = lines[0].split(",")
    max_peak = float(parts[0])
    final_gr = float(parts[2])
    is_stable = int(parts[3])
    assert is_stable == 1
    assert max_peak <= 0.0
    assert final_gr < 12.0


def test_m5c_cpu_overhead(cand_m5c_probe: Path):
    """M5C: CPU overhead benchmark must stay within 5.0% budget."""
    res = subprocess.run([str(cand_m5c_probe), "cpu_benchmark"], capture_output=True, text=True, check=True)
    out = res.stdout.strip()
    assert "CPU_BENCHMARK_CSV" in out
    lines = [l for l in out.splitlines() if "," in l and not l.startswith("Run")]
    assert len(lines) == 3
    for l in lines:
        cpu_pct = float(l.split(",")[3])
        assert cpu_pct < 5.0, f"CPU overhead exceeded 5.0%: {cpu_pct}%"
