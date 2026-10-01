"""
M5A Multi-Scale Granular Memory & Anti-Loop Decorrelation Tests.
Validates multi-tier distribution, phrase-anchor and cross-phrase bleed isolation,
periodicity collapse via anti-loop decorrelation, pitch stress resilience under Hermite,
and block-size invariance.
"""

from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path
import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module")
def cand_probe() -> Path:
    build_dir = REPO_ROOT / "build"
    build_dir.mkdir(exist_ok=True)
    suffix = ".exe" if sys.platform == "win32" else ""
    cand_bin = build_dir / f"m5a_cand_probe{suffix}"

    if not cand_bin.exists():
        compiler = shutil.which("gcc") or shutil.which("clang") or shutil.which("cc")
        if compiler is None:
            pytest.skip("No C compiler found in PATH to build M5A candidate probe")
        cmd = [
            compiler,
            "-O2",
            "-Wall",
            "-Wextra",
            "-std=c11",
            "-DM5A_CANDIDATE_BUILD=1",
            f"-I{REPO_ROOT / 'core'}",
            f"-I{REPO_ROOT / 'core' / 'dsp'}",
            f"-I{REPO_ROOT / 'core' / 'engine'}",
            str(REPO_ROOT / "scripts" / "m5a_probe.c"),
            str(REPO_ROOT / "core" / "dsp" / "sound_bubbles_dsp.c"),
            str(REPO_ROOT / "core" / "engine" / "bubble_engine.c"),
            str(REPO_ROOT / "core" / "engine" / "bubble_macro_map.c"),
            "-lm",
            "-o",
            str(cand_bin),
        ]
        subprocess.run(cmd, cwd=REPO_ROOT, check=True)
    return cand_bin


def test_m5a_tier_distribution_and_phrase_states(cand_probe: Path):
    """M5A: Attack must be source-connected (Recent >= 70%, Deep <= 2%)."""
    res = subprocess.run([str(cand_probe), "distribution"], capture_output=True, text=True, check=True)
    out = res.stdout.strip()

    lines = [l for l in out.splitlines() if "," in l]
    data = {}
    for line in lines[1:]:  # skip header
        parts = line.split(",")
        state = parts[0]
        data[state] = {
            "Recent": int(parts[1]),
            "Mid": int(parts[2]),
            "Deep": int(parts[3]),
            "RecentPct": float(parts[4]),
            "MidPct": float(parts[5]),
            "DeepPct": float(parts[6]),
        }

    assert "ATTACK" in data
    # Section 7: Attack is strictly source-connected
    assert data["ATTACK"]["RecentPct"] >= 70.0, f"Attack Recent % too low: {data['ATTACK']}"
    assert data["ATTACK"]["DeepPct"] <= 2.0, f"Attack Deep % must be ~zero: {data['ATTACK']}"

    # Telemetry inspection
    assert "TELEMETRY:" in out
    assert "anchor_fraction=" in out


def test_m5a_cross_phrase_bleed_isolation(cand_probe: Path):
    """M5A: First 300ms of a new phrase must not read deep historical memory from prior phrase."""
    res = subprocess.run([str(cand_probe), "cross_phrase"], capture_output=True, text=True, check=True)
    out = res.stdout.strip()
    assert "DeepPct=" in out
    # Format: CROSS_PHRASE: Spawns=9 Recent=9 Mid=0 Deep=0 DeepPct=0.00%
    deep_pct = float(out.split("DeepPct=")[1].split("%")[0])
    assert deep_pct < 5.0, f"Cross-phrase Deep bleed {deep_pct:.2f}% exceeds 5% threshold"


def test_m5a_anti_loop_periodicity_collapse(cand_probe: Path):
    """M5A: Anti-loop decorrelation collapses 2.0s buffer wrap periodicity peak."""
    res = subprocess.run([str(cand_probe), "periodicity"], capture_output=True, text=True, check=True)
    out = res.stdout.strip()
    lines = [l for l in out.splitlines() if "," in l]
    row = [float(x) for x in lines[1].split(",")]

    ac_2_0s = row[2]  # Lag 2.0s autocorrelation
    # Ring is 2.0s: without decorrelation, periodic peaks occur. With M5A drift, ac is strictly low/negative.
    assert ac_2_0s < 0.25, f"2.0s lag autocorrelation {ac_2_0s:.4f} too high (indicates periodic loop)"


def test_m5a_pitch_stress_hermite_resilience(cand_probe: Path):
    """M5A: Deep reads under extreme pitch offsets (+12, +19, reverse) with Hermite must not NaN or clamp."""
    res = subprocess.run([str(cand_probe), "pitch_stress"], capture_output=True, text=True, check=True)
    out = res.stdout.strip()
    assert "NaN_Inf=0" in out, f"NaN or Inf detected: {out}"
    assert "ClampCount=0" in out, f"Guard clamp violation detected: {out}"


def test_m5a_block_size_invariance(cand_probe: Path):
    """M5A: Read region tiering and drift must be host block-size invariant (32..2048)."""
    res = subprocess.run([str(cand_probe), "block_invariance"], capture_output=True, text=True, check=True)
    out = res.stdout.strip()
    lines = [l for l in out.splitlines() if "," in l and not l.startswith("BlockSize")]
    rms_vals = [float(l.split(",")[1]) for l in lines]
    delta = max(rms_vals) - min(rms_vals)
    assert delta < 0.10, f"Block invariance RMS delta {delta:.3f} dB exceeds 0.10 dB threshold"
