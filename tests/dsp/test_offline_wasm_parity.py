from __future__ import annotations

import csv
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]
TESTS_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS_ROOT))
from support_audio import write_core_parity_fixture
PRESET = REPO_ROOT / "core" / "presets" / "factory" / "neutral.json"
WASM_MODULE = REPO_ROOT / "ui" / "web" / "bubble_cloud_wasm.js"

COMMON_COLUMNS = [
    "active_voices",
    "engine_state",
    "envelope",
    "out_rms_l",
    "out_rms_r",
    "out_peak_l",
    "out_peak_r",
    "peak_l",
    "peak_r",
    "clip_count",
    "limiter_gain",
]
MAX_ABS_DELTA = 2.5e-4
MEAN_ABS_DELTA = 2.5e-5


def _build_renderer(tmp_path: Path) -> Path:
    binary = tmp_path / "sound_bubbles_render"
    subprocess.run(
        [
            "gcc",
            "platform/offline/sound_bubbles_render.c",
            "core/engine/bubble_engine.c",
            "core/engine/bubble_macro_map.c",
            "core/dsp/sound_bubbles_dsp.c",
            "core/presets/bubble_preset.c",
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


def _read_metrics(path: Path) -> list[dict[str, float]]:
    with path.open(newline="") as fh:
        return [{key: float(value) for key, value in row.items()} for row in csv.DictReader(fh)]


def test_offline_c_and_wasm_metrics_match_with_defined_tolerance(tmp_path: Path) -> None:
    if shutil.which("node") is None:
        pytest.skip("Node.js is required to execute the generated WASM module")
    if not WASM_MODULE.exists():
        pytest.fail("WASM module is not built; run `make wasm` before running parity tests")

    renderer = _build_renderer(tmp_path)
    fixture = write_core_parity_fixture(tmp_path / "core_parity_fixture.wav")
    offline_metrics = tmp_path / "offline.metrics.csv"
    wasm_metrics = tmp_path / "wasm.metrics.csv"
    subprocess.run(
        [str(renderer), str(fixture), str(PRESET), str(tmp_path / "offline.wav"), "--metrics-out", str(offline_metrics), "--repro-check"],
        cwd=REPO_ROOT,
        check=True,
    )
    wasm_run = subprocess.run(
        ["node", "tests/dsp/wasm_metrics_runner.mjs", str(fixture), str(PRESET), str(wasm_metrics)],
        cwd=REPO_ROOT,
        text=True,
        capture_output=True,
    )
    if wasm_run.returncode != 0:
        pytest.fail(
            "WASM parity runner failed; rebuild ui/web/bubble_cloud_wasm.js with `make wasm` "
            "when exports are stale or missing.\n"
            f"stdout:\n{wasm_run.stdout}\n"
            f"stderr:\n{wasm_run.stderr}"
        )

    offline_rows = _read_metrics(offline_metrics)
    wasm_rows = _read_metrics(wasm_metrics)
    assert len(offline_rows) == len(wasm_rows)
    assert len(offline_rows) > 0, "No metrics rows were parsed from the C or WASM runs."

    failures: list[str] = []
    for column in COMMON_COLUMNS:
        deltas = [abs(off[column] - wasm[column]) for off, wasm in zip(offline_rows, wasm_rows)]
        max_delta = max(deltas)
        mean_delta = sum(deltas) / len(deltas)
        if max_delta > MAX_ABS_DELTA or mean_delta > MEAN_ABS_DELTA:
            failures.append(
                f"{column}: max_delta={max_delta:.9g} mean_delta={mean_delta:.9g} "
                f"limits=max<={MAX_ABS_DELTA} mean<={MEAN_ABS_DELTA}"
            )

    assert not failures, "Offline C/WASM parity exceeded tolerance:\n" + "\n".join(failures)
