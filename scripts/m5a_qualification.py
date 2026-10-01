#!/usr/bin/env python3
"""
M5A Multi-Scale Granular Memory & Anti-Loop Decorrelation Qualification Harness.
Compares Candidate against frozen baseline M4D.1 @ ad691aecdfa4b92124943f53c43a31b684d5ced8.
"""

from __future__ import annotations

import csv
import io
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
BASELINE_SHA = "ad691aecdfa4b92124943f53c43a31b684d5ced8"
OUTPUT_REPORT = REPO_ROOT / "docs" / "m5a_multiscale_memory_qualification.md"


def _check_compiler() -> str:
    compiler = shutil.which("gcc") or shutil.which("clang") or shutil.which("cc")
    if compiler is None:
        raise RuntimeError("No C compiler (gcc/clang/cc) found in PATH")
    return compiler


def _compile_candidate(compiler: str, build_dir: Path) -> Path:
    suffix = ".exe" if sys.platform == "win32" else ""
    cand_bin = build_dir / f"m5a_cand_probe{suffix}"
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


def _compile_baseline(compiler: str, base_dir: Path, build_dir: Path) -> Path:
    suffix = ".exe" if sys.platform == "win32" else ""
    base_bin = build_dir / f"m5a_base_probe{suffix}"
    cmd = [
        compiler,
        "-O2",
        "-Wall",
        "-Wextra",
        "-std=c11",
        f"-I{base_dir / 'core'}",
        f"-I{base_dir / 'core' / 'dsp'}",
        f"-I{base_dir / 'core' / 'engine'}",
        str(REPO_ROOT / "scripts" / "m5a_probe.c"),
        str(base_dir / "core" / "dsp" / "sound_bubbles_dsp.c"),
        str(base_dir / "core" / "engine" / "bubble_engine.c"),
        str(base_dir / "core" / "engine" / "bubble_macro_map.c"),
        "-lm",
        "-o",
        str(base_bin),
    ]
    subprocess.run(cmd, cwd=base_dir, check=True)
    return base_bin


def _run(bin_path: Path, mode: str) -> str:
    res = subprocess.run([str(bin_path), mode], capture_output=True, text=True, check=True)
    return res.stdout.strip()


def run_qualification() -> dict:
    compiler = _check_compiler()
    build_dir = REPO_ROOT / "build"
    build_dir.mkdir(exist_ok=True)

    cand_bin = _compile_candidate(compiler, build_dir)

    temp_wt_dir = REPO_ROOT / "build" / "wt_m4d_base"
    if temp_wt_dir.exists():
        subprocess.run(["git", "worktree", "remove", "--force", str(temp_wt_dir)], cwd=REPO_ROOT, capture_output=True)

    print("=" * 70)
    print("M5A MULTI-SCALE GRANULAR MEMORY QUALIFICATION HARNESS")
    print(f"Compiler:       {compiler}")
    print(f"Baseline SHA:   {BASELINE_SHA}")
    print("=" * 70)

    print(f"\n[0/8] Creating detached baseline worktree at {temp_wt_dir}...")
    subprocess.run(["git", "worktree", "add", "--detach", str(temp_wt_dir), BASELINE_SHA], cwd=REPO_ROOT, check=True, capture_output=True)

    results = {}

    try:
        base_bin = _compile_baseline(compiler, temp_wt_dir, build_dir)

        # 1. Tier Distribution
        print("\n[1/8] Measuring Multi-Tier Distribution Across Phrase States...")
        cand_dist_raw = _run(cand_bin, "distribution")
        results["dist_raw"] = cand_dist_raw
        print(cand_dist_raw)

        # 2. Memory Sweep
        print("\n[2/8] Running MEMORY Sweep & Loudness Invariance...")
        cand_mem_raw = _run(cand_bin, "memory_sweep")
        base_mem_raw = _run(base_bin, "memory_sweep")
        results["cand_mem"] = cand_mem_raw
        results["base_mem"] = base_mem_raw
        print("Candidate MEMORY Sweep:")
        print(cand_mem_raw)

        # 3. Periodicity & Anti-Loop Decorrelation
        print("\n[3/8] Measuring Anti-Loop Periodicity & Decorrelation vs Baseline...")
        cand_per_raw = _run(cand_bin, "periodicity")
        base_per_raw = _run(base_bin, "periodicity")
        results["cand_periodicity"] = cand_per_raw
        results["base_periodicity"] = base_per_raw
        print(f"Baseline Periodicity:\n  {base_per_raw}")
        print(f"Candidate Periodicity:\n  {cand_per_raw}")

        # 4. Attack Integrity
        print("\n[4/8] Evaluating Attack Integrity (0-100ms and 100-300ms)...")
        cand_att_raw = _run(cand_bin, "attack_integrity")
        base_att_raw = _run(base_bin, "attack_integrity")
        results["cand_attack"] = cand_att_raw
        results["base_attack"] = base_att_raw
        print(f"Baseline Attack:\n  {base_att_raw}")
        print(f"Candidate Attack:\n  {cand_att_raw}")

        # 5. Cross-Phrase Contamination
        print("\n[5/8] Checking Cross-Phrase Contamination...")
        cand_cross_raw = _run(cand_bin, "cross_phrase")
        base_cross_raw = _run(base_bin, "cross_phrase")
        results["cand_cross"] = cand_cross_raw
        results["base_cross"] = base_cross_raw
        print(f"Candidate Cross-Phrase:\n  {cand_cross_raw}")

        # 6. Pitch Stress Resilience under Hermite
        print("\n[6/8] Testing Pitch Stress Resilience (Unison, +12, +19, Reverse with Hermite)...")
        cand_pitch_raw = _run(cand_bin, "pitch_stress")
        results["cand_pitch"] = cand_pitch_raw
        print(f"Candidate Pitch Stress:\n  {cand_pitch_raw}")

        # 7. Block Invariance
        print("\n[7/8] Testing Block Size Invariance (32, 64, 127, 256, 512, 2048)...")
        cand_block_raw = _run(cand_bin, "block_invariance")
        results["cand_block"] = cand_block_raw
        print(f"Candidate Block Invariance:\n{cand_block_raw}")

        # 8. CPU Benchmark
        print("\n[8/8] Benchmarking CPU Overhead...")
        cand_cpu_raw = _run(cand_bin, "cpu_benchmark")
        base_cpu_raw = _run(base_bin, "cpu_benchmark")
        results["cand_cpu"] = cand_cpu_raw
        results["base_cpu"] = base_cpu_raw
        print(f"Candidate CPU Benchmark:\n{cand_cpu_raw}")

    finally:
        print(f"\nCleaning up baseline worktree at {temp_wt_dir}...")
        subprocess.run(["git", "worktree", "remove", "--force", str(temp_wt_dir)], cwd=REPO_ROOT, capture_output=True)

    # Validate Qualification Criteria
    print("\n" + "=" * 70)
    print("M5A QUALIFICATION VERIFICATION")
    print("=" * 70)

    pass_all = True
    checks = []

    # Check 1: Attack Deep reads < 2%
    # Parse candidate distribution
    dist_lines = results["dist_raw"].splitlines()
    for line in dist_lines:
        if line.startswith("ATTACK,"):
            parts = line.split(",")
            deep_pct = float(parts[6])
            rec_pct = float(parts[4])
            passed = (deep_pct <= 2.0) and (rec_pct >= 70.0)
            checks.append(("Attack Tier Distribution (Recent >= 70%, Deep <= 2%)", passed, f"Recent={rec_pct:.1f}%, Deep={deep_pct:.1f}%"))
            if not passed: pass_all = False

    # Check 2: Cross phrase Deep reads in first 300ms < 5%
    cross_lines = results["cand_cross"].splitlines()
    for line in cross_lines:
        if "DeepPct=" in line:
            # e.g. "CROSS_PHRASE: Phrase B first 300ms ... DeepPct=0.0%"
            part = line.split("DeepPct=")[1].split("%")[0]
            deep_b_pct = float(part)
            passed = deep_b_pct < 5.0
            checks.append(("Cross-Phrase Bleed (Phrase B 300ms Deep < 5%)", passed, f"Deep={deep_b_pct:.1f}%"))
            if not passed: pass_all = False

    # Check 3: Periodicity at 2.0s lag autocorrelation
    cand_per_line = [l for l in results["cand_periodicity"].splitlines() if not l.startswith("Lag_") and not l.startswith("PERIODICITY")][0]
    base_per_line = [l for l in results["base_periodicity"].splitlines() if not l.startswith("Lag_") and not l.startswith("PERIODICITY")][0]
    cand_per_vals = [float(x) for x in cand_per_line.split(",")]
    base_per_vals = [float(x) for x in base_per_line.split(",")]

    ac2_base = base_per_vals[2]
    ac2_cand = cand_per_vals[2]
    # Lag 2.0s autocorrelation should be low / negative (decorrelated)
    passed_ac2 = ac2_cand < 0.30
    checks.append(("Autocorrelation @ 2.0s Lag (< 0.30)", passed_ac2, f"Baseline={ac2_base:+.4f}, Candidate={ac2_cand:+.4f}"))
    if not passed_ac2: pass_all = False

    # Check 4: Pitch stress 0 NaN/Inf and 0 Clamp
    pitch_line = results["cand_pitch"]
    passed_pitch = ("NaN_Inf=0" in pitch_line) and ("ClampCount=0" in pitch_line)
    checks.append(("Pitch Stress Hermite Resilience (0 NaN, 0 Clamp)", passed_pitch, pitch_line))
    if not passed_pitch: pass_all = False

    # Check 5: Block invariance RMS delta < 0.1 dB across all blocks
    block_lines = [l for l in results["cand_block"].splitlines() if not l.startswith("BlockSize") and not l.startswith("BLOCK_INVARIANCE")]
    rms_vals = [float(l.split(",")[1]) for l in block_lines]
    delta_rms = max(rms_vals) - min(rms_vals)
    passed_block = delta_rms < 0.10
    checks.append(("Block Invariance Across 32..2048 (Delta RMS < 0.10 dB)", passed_block, f"Delta={delta_rms:.3f} dB (Min={min(rms_vals):.2f}, Max={max(rms_vals):.2f})"))
    if not passed_block: pass_all = False

    # Check 6: CPU realtime speedup > 30x across all configurations
    cand_cpu_lines = [l for l in results["cand_cpu"].splitlines() if not l.startswith("SampleRate") and not l.startswith("CPU_BENCHMARK")]
    speeds = [float(l.split(",")[3]) for l in cand_cpu_lines]
    min_speed = min(speeds)
    passed_cpu = min_speed > 30.0
    checks.append(("CPU Performance (> 30x realtime on all configurations)", passed_cpu, f"Min Speedup={min_speed:.1f}x"))
    if not passed_cpu: pass_all = False

    for name, ok, details in checks:
        status = "PASS" if ok else "FAIL"
        print(f"[{status}] {name}: {details}")

    print("=" * 70)
    if pass_all:
        print("VERDICT: M5A QUALIFICATION PASSED — READY FOR FREEZE")
    else:
        print("VERDICT: M5A QUALIFICATION FAILED")
    print("=" * 70)

    results["checks"] = checks
    results["pass_all"] = pass_all
    return results


if __name__ == "__main__":
    run_qualification()
