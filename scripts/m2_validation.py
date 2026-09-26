#!/usr/bin/env python3
"""M2 perceptual validation renders.

Builds the comparative wrapper probe (tests/juce/m2_validation_probe.cpp) against
two source trees and reports the basic musical metrics for each short scenario:

    --baseline-ref <git ref>   baseline sources (default: HEAD, the M1 tree)

The baseline is materialised with a throw-away ``git worktree`` so the current
working tree (M2) is never disturbed. Each probe prints CSV; the script merges
the two runs into a before/after table and writes ``m2_validation.csv``.

Requirements: gcc/g++ and an audio-free build (the probe synthesises its own
deterministic plucked-phrase stimulus, so no WAV fixtures are needed).
"""
from __future__ import annotations

import argparse
import csv
import io
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
PROBE_SOURCE = "tests/juce/m2_validation_probe.cpp"
C_SOURCES = [
    "core/dsp/sound_bubbles_dsp.c",
    "core/engine/bubble_engine.c",
    "core/engine/bubble_macro_map.c",
]
WRAPPER_SOURCE = "platform/juce/Source/BubbleCloudEngineWrapper.cpp"

METRICS = ["rms", "peak", "correlation", "side_mid", "active_voices", "limiter_gain"]


def _tools() -> tuple[str, str]:
    cc = shutil.which("gcc") or shutil.which("clang") or shutil.which("cc")
    cxx = shutil.which("g++") or shutil.which("clang++") or shutil.which("c++")
    if cc is None or cxx is None:
        raise SystemExit("A C and C++ compiler are required for M2 validation")
    return cc, cxx


def _build_probe(tree: Path, output: Path, cc: str, cxx: str) -> Path:
    exe_suffix = ".exe" if sys.platform == "win32" else ""
    objects: list[str] = []
    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        for source in C_SOURCES:
            obj = tmp_path / (Path(source).stem + ".o")
            subprocess.run(
                [cc, "-O2", "-Wall", "-Wextra", "-std=c11", "-Icore", "-Icore/dsp",
                 "-c", str(tree / source), "-o", str(obj)],
                cwd=tree, check=True,
            )
            objects.append(str(obj))
        wrapper_obj = tmp_path / "BubbleCloudEngineWrapper.o"
        subprocess.run(
            [cxx, "-std=c++17", "-O2", "-Iplatform/juce/Source", "-Icore", "-Icore/dsp",
             "-c", str(tree / WRAPPER_SOURCE), "-o", str(wrapper_obj)],
            cwd=tree, check=True,
        )
        binary = output.with_suffix(exe_suffix)
        # The probe only uses the stable wrapper API, so compile the working-tree
        # probe source against either tree's engine objects.
        subprocess.run(
            [cxx, "-std=c++17", "-O2", "-Iplatform/juce/Source", "-Icore", "-Icore/dsp",
             str(REPO_ROOT / PROBE_SOURCE), str(wrapper_obj), *objects, "-lm", "-o", str(binary)],
            cwd=tree, check=True,
        )
    return binary


def _run(binary: Path, tree: Path, mode: str) -> list[dict[str, str]]:
    result = subprocess.run(
        [str(binary), mode], cwd=tree, check=True, text=True, capture_output=True
    )
    return list(csv.DictReader(io.StringIO(result.stdout)))


def _delta(base: float, cand: float) -> str:
    if base == 0.0:
        return "n/a"
    return f"{(cand - base):+.4f}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-ref", default="HEAD", help="git ref for the M1 baseline sources")
    parser.add_argument("--output-dir", default="build_check/m2_validation", help="where reports are written")
    args = parser.parse_args()

    cc, cxx = _tools()
    output_dir = (REPO_ROOT / args.output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    candidate_binary = _build_probe(REPO_ROOT, output_dir / "probe_candidate", cc, cxx)

    with tempfile.TemporaryDirectory() as worktree_tmp:
        worktree = Path(worktree_tmp) / "baseline"
        subprocess.run(
            ["git", "worktree", "add", "--detach", str(worktree), args.baseline_ref],
            cwd=REPO_ROOT, check=True, capture_output=True,
        )
        try:
            baseline_binary = _build_probe(worktree, output_dir / "probe_baseline", cc, cxx)
            baseline = {
                mode: {row["scenario"]: row for row in _run(baseline_binary, worktree, mode)}
                for mode in ("mono", "stereo")
            }
            candidate = {
                mode: {row["scenario"]: row for row in _run(candidate_binary, REPO_ROOT, mode)}
                for mode in ("mono", "stereo")
            }
        finally:
            subprocess.run(["git", "worktree", "remove", "--force", str(worktree)],
                           cwd=REPO_ROOT, check=False, capture_output=True)

    rows: list[dict[str, str]] = []
    for mode in ("mono", "stereo"):
        for scenario in candidate[mode]:
            base = baseline[mode][scenario]
            cand = candidate[mode][scenario]
            row = {"input": mode, "scenario": scenario}
            for metric in METRICS:
                row[f"{metric}_m1"] = base[metric]
                row[f"{metric}_m2"] = cand[metric]
                row[f"{metric}_delta"] = _delta(float(base[metric]), float(cand[metric]))
            rows.append(row)

    report = output_dir / "m2_validation.csv"
    fieldnames = ["input", "scenario"] + [
        f"{metric}_{suffix}" for metric in METRICS for suffix in ("m1", "m2", "delta")
    ]
    with report.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    print(f"M1 baseline ref: {args.baseline_ref}")
    print(f"report: {report}")
    print(f"{'input':7} {'scenario':14} {'rms m1/m2':>18} {'corr m1/m2':>18} {'side/mid m1/m2':>20} {'voices m1/m2':>12} {'lim m1/m2':>16}")
    for row in rows:
        print(f"{row['input']:7} {row['scenario']:14} "
              f"{row['rms_m1']:>8}/{row['rms_m2']:>8} "
              f"{row['correlation_m1']:>8}/{row['correlation_m2']:>8} "
              f"{row['side_mid_m1']:>9}/{row['side_mid_m2']:>9} "
              f"{row['active_voices_m1']:>5}/{row['active_voices_m2']:>5} "
              f"{row['limiter_gain_m1']:>7}/{row['limiter_gain_m2']:>7}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
