#!/usr/bin/env python3
"""
M4D.1 Wet Dynamics Qualification Freeze Harness.
Executes reproducible historical A/B qualification between:
Baseline:  M4C.1 @ 865842c01f4cb46854f9f0a80af364ddf024df78
Candidate: M4D   @ 28677f681626b1bddd56904a9aab5bf9c38a0e86
"""

from __future__ import annotations

import csv
import io
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
BASELINE_ROOT = REPO_ROOT.parent / "bubble_cloud_m4c_baseline"
BASELINE_SHA = "865842c01f4cb46854f9f0a80af364ddf024df78"
CANDIDATE_SHA = "28677f681626b1bddd56904a9aab5bf9c38a0e86"


def _check_environment() -> str:
    compiler = shutil.which("gcc") or shutil.which("clang") or shutil.which("cc")
    if compiler is None:
        raise RuntimeError("C compiler (gcc/clang/cc) not found in PATH")
    if not BASELINE_ROOT.exists():
        raise RuntimeError(f"Baseline worktree not found at {BASELINE_ROOT}")
    return compiler


def _compile_probes(compiler: str) -> tuple[Path, Path]:
    build_dir = REPO_ROOT / "build"
    build_dir.mkdir(exist_ok=True)
    suffix = ".exe" if sys.platform == "win32" else ""
    cand_bin = build_dir / f"m4d_candidate_probe{suffix}"
    base_bin = BASELINE_ROOT / f"m4d_baseline_probe{suffix}"

    # 1. Compile Candidate Probe
    subprocess.run(
        [
            compiler,
            "-O2",
            "-Wall",
            "-Wextra",
            "-std=c11",
            "-DM4D_CANDIDATE_BUILD=1",
            f"-I{REPO_ROOT / 'core'}",
            f"-I{REPO_ROOT / 'core' / 'dsp'}",
            f"-I{REPO_ROOT / 'core' / 'engine'}",
            str(REPO_ROOT / "scripts" / "m4d_freeze_probe.c"),
            str(REPO_ROOT / "core" / "dsp" / "sound_bubbles_dsp.c"),
            str(REPO_ROOT / "core" / "engine" / "bubble_engine.c"),
            str(REPO_ROOT / "core" / "engine" / "bubble_macro_map.c"),
            "-lm",
            "-o",
            str(cand_bin),
        ],
        cwd=REPO_ROOT,
        check=True,
    )

    # 2. Compile Baseline Probe
    subprocess.run(
        [
            compiler,
            "-O2",
            "-Wall",
            "-Wextra",
            "-std=c11",
            f"-I{BASELINE_ROOT / 'core'}",
            f"-I{BASELINE_ROOT / 'core' / 'dsp'}",
            f"-I{BASELINE_ROOT / 'core' / 'engine'}",
            str(BASELINE_ROOT / "scripts" / "m4d_freeze_probe.c"),
            str(BASELINE_ROOT / "core" / "dsp" / "sound_bubbles_dsp.c"),
            str(BASELINE_ROOT / "core" / "engine" / "bubble_engine.c"),
            str(BASELINE_ROOT / "core" / "engine" / "bubble_macro_map.c"),
            "-lm",
            "-o",
            str(base_bin),
        ],
        cwd=BASELINE_ROOT,
        check=True,
    )

    return base_bin, cand_bin


def _run(bin_path: Path, mode: str) -> str:
    res = subprocess.run([str(bin_path), mode], capture_output=True, text=True, check=True)
    return res.stdout.strip()


def run_all_qualification() -> dict:
    compiler = _check_environment()
    base_bin, cand_bin = _compile_probes(compiler)
    print("=" * 70)
    print("M4D.1 QUALIFICATION FREEZE HARNESS")
    print(f"Compiler:       {compiler}")
    print(f"Baseline SHA:   {BASELINE_SHA}")
    print(f"Candidate SHA:  {CANDIDATE_SHA}")
    print("=" * 70)

    # 1. Direct Dry Pumping Measurement (Sections 3-8)
    print("\n[1/10] Measuring Direct Dry Pumping (Mix=0.25, bursty wet + steady dry)...")
    base_pump_out = _run(base_bin, "--dry-pumping")
    cand_pump_out = _run(cand_bin, "--dry-pumping")

    def parse_kv(text: str) -> dict[str, float]:
        d = {}
        for line in text.splitlines():
            if ":" in line:
                k, v = line.split(":", 1)
                d[k.strip()] = float(v.strip())
        return d

    pump_base = parse_kv(base_pump_out)
    pump_cand = parse_kv(cand_pump_out)

    depth_base = pump_base["dry_modulation_depth_db"]
    depth_cand = pump_cand["dry_modulation_depth_db"]
    depth_reduction_pct = (1.0 - (depth_cand / max(1e-6, depth_base))) * 100.0 if depth_base > 0 else 0.0

    print(f"  Baseline dry modulation depth:  {depth_base:.4f} dB")
    print(f"  Candidate dry modulation depth: {depth_cand:.4f} dB")
    print(f"  Modulation reduction:           {depth_reduction_pct:.1f}%")
    print(f"  Baseline wet-burst correlation: {pump_base['wet_burst_correlation']:.4f}")
    print(f"  Candidate wet-burst correlation:{pump_cand['wet_burst_correlation']:.4f}")

    # 2. Baseline Limiter Metrics (Section 2)
    print("\n[2/10] Running Limiter Historical Matrix (3 sources x 3 dens x 2 bloom x 2 mix)...")
    base_lim_csv = _run(base_bin, "--limiter-matrix")
    cand_lim_csv = _run(cand_bin, "--limiter-matrix")

    base_rows = list(csv.DictReader(io.StringIO(base_lim_csv)))
    cand_rows = list(csv.DictReader(io.StringIO(cand_lim_csv)))

    # Compute aggregate active % and max GR
    base_act_rates = [float(r["FIN_LIM_ACT_PCT"]) for r in base_rows]
    cand_act_rates = [float(r["FIN_LIM_ACT_PCT"]) for r in cand_rows]
    base_max_grs = [float(r["FIN_LIM_MAX_GR_DB"]) for r in base_rows]
    cand_max_grs = [float(r["FIN_LIM_MAX_GR_DB"]) for r in cand_rows]

    base_mean_act = sum(base_act_rates) / len(base_act_rates)
    cand_mean_act = sum(cand_act_rates) / len(cand_act_rates)
    base_peak_act = max(base_act_rates)
    cand_peak_act = max(cand_act_rates)
    base_peak_gr = max(base_max_grs)
    cand_peak_gr = max(cand_max_grs)

    print(f"  Baseline final limiter:  mean act={base_mean_act:.1f}%, peak act={base_peak_act:.1f}%, max GR={base_peak_gr:.2f} dB")
    print(f"  Candidate final limiter: mean act={cand_mean_act:.1f}%, peak act={cand_peak_act:.1f}%, max GR={cand_peak_gr:.2f} dB")

    # 3. Normalization Telemetry Distribution (Sections 9-11)
    print("\n[3/10] Evaluating Normalization Telemetry Distribution (Normal, Dense, Extreme)...")
    cand_norm_csv = _run(cand_bin, "--normalization-stats")
    norm_rows = list(csv.DictReader(io.StringIO(cand_norm_csv)))
    for r in norm_rows:
        print(f"  [{r['TIER']}] min={r['MIN_NORM']} med={r['MEDIAN_NORM']} p95_GR={r['P95_GR_DB']}dB <0.95={r['TIME_LT_095_PCT']}% <0.80={r['TIME_LT_080_PCT']}% floor={r['TIME_NEAR_045_PCT']}% wetLimGR={r['WET_LIM_MAX_GR_DB']}dB finLimGR={r['FIN_LIM_MAX_GR_DB']}dB finAct={r['FIN_LIM_ACT_PCT']}%")

    # 4. Bloom Historical A/B (Section 13)
    print("\n[4/10] Comparing BLOOM A/B Historical Output & Wet Deltas...")
    base_bloom_csv = _run(base_bin, "--bloom-ab")
    cand_bloom_csv = _run(cand_bin, "--bloom-ab")
    base_bloom_rows = list(csv.DictReader(io.StringIO(base_bloom_csv)))
    cand_bloom_rows = list(csv.DictReader(io.StringIO(cand_bloom_csv)))

    bloom_comparisons = []
    for b_row, c_row in zip(base_bloom_rows, cand_bloom_rows):
        dens = b_row["DENSITY"]
        b_delta = float(b_row["DELTA_OUT_RMS_DB"])
        c_delta = float(c_row["DELTA_OUT_RMS_DB"])
        bloom_comparisons.append((dens, b_delta, c_delta))
        print(f"  Density {dens}: Baseline Bloom0->1 delta = {b_delta:+.2f} dB | Candidate delta = {c_delta:+.2f} dB")

    # 5. Tail Preservation (Section 14)
    print("\n[5/10] Evaluating Tail Preservation (500ms pluck -> 10s silence)...")
    base_tail_csv = _run(base_bin, "--tail")
    cand_tail_csv = _run(cand_bin, "--tail")
    base_tail_rows = list(csv.DictReader(io.StringIO(base_tail_csv)))
    cand_tail_rows = list(csv.DictReader(io.StringIO(cand_tail_csv)))
    for b_t, c_t in zip(base_tail_rows, cand_tail_rows):
        print(f"  Window {b_t['WINDOW']}: Baseline RMS={b_t['RMS_DB']} dB (cent={b_t['CENTROID_HZ']} Hz) | Candidate RMS={c_t['RMS_DB']} dB (cent={c_t['CENTROID_HZ']} Hz)")

    # 6. Stereo Preservation (Section 15)
    print("\n[6/10] Checking Stereo Image Preservation...")
    base_stereo_out = _run(base_bin, "--stereo")
    cand_stereo_out = _run(cand_bin, "--stereo")
    base_stereo = parse_kv(base_stereo_out)
    cand_stereo = parse_kv(cand_stereo_out)
    print(f"  Baseline:  corr={base_stereo['CORRELATION']:.4f}, side/mid={base_stereo['SIDE_MID_RATIO']:.4f}, ILD={base_stereo['ILD_DB']:.2f} dB")
    print(f"  Candidate: corr={cand_stereo['CORRELATION']:.4f}, side/mid={cand_stereo['SIDE_MID_RATIO']:.4f}, ILD={cand_stereo['ILD_DB']:.2f} dB")

    # 7. MIX = 0 Proof (Section 16)
    print("\n[7/10] Testing MIX = 0 Invariant (high wet cloud, pure dry)...")
    cand_m0 = parse_kv(_run(cand_bin, "--mix-zero"))
    print(f"  Candidate Mix=0 max delta: {cand_m0['MIX_ZERO_MAX_DELTA']:.6f} (final limiter = {cand_m0['MIX_ZERO_FINAL_LIMITER']:.4f})")

    # 8. Silence Invariant (Section 17)
    print("\n[8/10] Testing Silence Invariants (30s startup, 15s decay)...")
    cand_silence = parse_kv(_run(cand_bin, "--silence"))
    print(f"  Startup 30s peak: {cand_silence['STARTUP_SILENCE_PEAK']:.6f}")
    print(f"  Post-signal max norm gain: {cand_silence['POST_SILENCE_MAX_NORM_GAIN']:.4f} (must be <= 1.00)")

    # 9. Parameter Automation Slew (Section 18)
    print("\n[9/10] Testing Parameter Automation Slew & Discontinuity...")
    cand_auto = parse_kv(_run(cand_bin, "--automation"))
    print(f"  Max sample discontinuity: {cand_auto['MAX_SAMPLE_DISCONTINUITY']:.4f}")
    print(f"  Max norm slew per block:  {cand_auto['MAX_NORM_SLEW_PER_BLOCK']:.6f}")

    # 10. CPU Benchmark (Section 19)
    print("\n[10/10] Running CPU Overhead Benchmark...")
    base_cpu_csv = _run(base_bin, "--cpu")
    cand_cpu_csv = _run(cand_bin, "--cpu")
    base_cpu_rows = list(csv.DictReader(io.StringIO(base_cpu_csv)))
    cand_cpu_rows = list(csv.DictReader(io.StringIO(cand_cpu_csv)))

    cpu_diffs = []
    for b_c, c_c in zip(base_cpu_rows, cand_cpu_rows):
        b_time = float(b_c["PROCESS_TIME_MS"])
        c_time = float(c_c["PROCESS_TIME_MS"])
        overhead = ((c_time - b_time) / max(0.1, b_time)) * 100.0
        cpu_diffs.append((c_c["CONFIG"], c_c["SR_HZ"], c_c["VOICES"], b_time, c_time, overhead))
        print(f"  {c_c['CONFIG']} ({c_c['SR_HZ']} Hz, {c_c['VOICES']}v): Baseline={b_time:.1f}ms | Candidate={c_time:.1f}ms (overhead: {overhead:+.1f}%)")

    # Evaluation against Freeze Criteria (Section 23)
    freeze_pass = True
    reasons = []

    if depth_cand > 0.25:
        freeze_pass = False
        reasons.append(f"Dry modulation depth {depth_cand:.2f} dB > 0.25 dB")

    if cand_peak_act > 0.05:
        freeze_pass = False
        reasons.append(f"Candidate final limiter active rate {cand_peak_act:.1f}% > 0.0%")

    if cand_silence["POST_SILENCE_MAX_NORM_GAIN"] > 1.0001:
        freeze_pass = False
        reasons.append("Upward normalization detected in silence")

    if cand_m0["MIX_ZERO_MAX_DELTA"] > 0.001:
        freeze_pass = False
        reasons.append(f"Mix=0 dry deviation {cand_m0['MIX_ZERO_MAX_DELTA']:.6f} > 0.001")

    print("\n" + "=" * 70)
    if freeze_pass:
        print("VERDICT: M4D READY TO FREEZE")
    else:
        print("VERDICT: M4D QUALIFICATION PARTIAL")
        for r in reasons:
            print(f" - {r}")
    print("=" * 70)

    return {
        "depth_base": depth_base,
        "depth_cand": depth_cand,
        "depth_reduction_pct": depth_reduction_pct,
        "pump_base": pump_base,
        "pump_cand": pump_cand,
        "base_rows": base_rows,
        "cand_rows": cand_rows,
        "base_mean_act": base_mean_act,
        "cand_mean_act": cand_mean_act,
        "base_peak_act": base_peak_act,
        "cand_peak_act": cand_peak_act,
        "base_peak_gr": base_peak_gr,
        "cand_peak_gr": cand_peak_gr,
        "norm_rows": norm_rows,
        "bloom_comparisons": bloom_comparisons,
        "base_tail_rows": base_tail_rows,
        "cand_tail_rows": cand_tail_rows,
        "base_stereo": base_stereo,
        "cand_stereo": cand_stereo,
        "cand_m0": cand_m0,
        "cand_silence": cand_silence,
        "cand_auto": cand_auto,
        "cpu_diffs": cpu_diffs,
        "freeze_pass": freeze_pass,
    }


if __name__ == "__main__":
    run_all_qualification()
