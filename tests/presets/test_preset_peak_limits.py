from __future__ import annotations

import csv
import json
import math
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
TESTS_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS_ROOT))
from support_audio import write_core_parity_fixture
LIMITS_FILE = REPO_ROOT / "tests" / "presets" / "preset_peak_limits.json"


def _build_renderer(tmp_path: Path) -> Path:
    binary = tmp_path / "sound_bubbles_render"
    subprocess.run(
        [
            "gcc",
            "platform/offline/sound_bubbles_render.c",
            "core/engine/bubble_engine.c",
            "core/dsp/sound_bubbles_dsp.c",
            "-O2",
            "-Wall",
            "-Wextra",
            "-std=c11",
            "-Icore",
            "-Icore/dsp",
            "-lm",
            "-o",
            str(binary),
        ],
        cwd=REPO_ROOT,
        check=True,
    )
    return binary


def _metrics_summary(metrics_csv: Path) -> tuple[float, int]:
    max_peak = 0.0
    max_clip_count = 0
    with metrics_csv.open(newline="") as fh:
        reader = csv.DictReader(fh)
        for row in reader:
            peaks = [
                abs(float(row["out_peak_l"])),
                abs(float(row["out_peak_r"])),
                abs(float(row["peak_l"])),
                abs(float(row["peak_r"])),
            ]
            if any(not math.isfinite(v) for v in peaks):
                raise AssertionError(f"Non-finite peak in {metrics_csv}: {row}")
            max_peak = max(max_peak, *peaks)
            max_clip_count = max(max_clip_count, int(float(row["clip_count"])))
    return max_peak, max_clip_count


def test_each_factory_preset_stays_within_declared_peak_and_clipping_limits(tmp_path: Path) -> None:
    renderer = _build_renderer(tmp_path)
    fixture = write_core_parity_fixture(tmp_path / "core_parity_fixture.wav")
    limits = json.loads(LIMITS_FILE.read_text(encoding="utf-8"))
    failures: list[str] = []

    for preset_rel, limit in sorted(limits.items()):
        preset_path = REPO_ROOT / preset_rel
        metrics_csv = tmp_path / (preset_path.stem + ".metrics.csv")
        output_wav = tmp_path / (preset_path.stem + ".wav")
        subprocess.run(
            [str(renderer), str(fixture), str(preset_path), str(output_wav), "--metrics-out", str(metrics_csv), "--repro-check"],
            cwd=REPO_ROOT,
            check=True,
        )
        max_peak, max_clip_count = _metrics_summary(metrics_csv)
        if max_peak > float(limit["max_peak"]):
            failures.append(f"{preset_rel}: peak {max_peak:.9g} > max_peak {limit['max_peak']}")
        if max_clip_count > int(limit["max_clip_count"]):
            failures.append(f"{preset_rel}: clip_count {max_clip_count} > max_clip_count {limit['max_clip_count']}")

    assert not failures, "Preset peak/clipping limits exceeded:\n" + "\n".join(failures)
