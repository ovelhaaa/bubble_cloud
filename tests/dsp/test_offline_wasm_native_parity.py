from __future__ import annotations

import csv
import json
import shutil
import struct
import subprocess
import sys
import wave
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]
TESTS_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS_ROOT))
from support_audio import write_core_parity_fixture  # noqa: E402

PRESET = REPO_ROOT / "core" / "presets" / "factory" / "neutral.json"
SHIM_DIR = REPO_ROOT / "tests" / "dsp" / "emscripten_shim"

# Mirrors tests/dsp/wasm_metrics_runner.mjs so both harnesses apply the exact
# same raw DSP parameter mapping when loading a preset.
PARAM_IDS = {
    "noise_floor": 0,
    "tracking_thresh": 1,
    "sustain_thresh": 2,
    "transient_delta": 3,
    "duck_burst_level": 4,
    "duck_attack_coef": 5,
    "duck_release_coef": 6,
    "burst_duration_ticks": 7,
    "burst_immediate_count": 8,
    "density_burst": 9,
    "density_sustain": 10,
    "density_decay": 11,
    "attack_region_min_offset_samples": 12,
    "attack_region_max_offset_samples": 13,
    "body_region_min_offset_samples": 14,
    "body_region_max_offset_samples": 15,
    "memory_region_min_offset_samples": 16,
    "memory_region_max_offset_samples": 17,
    "micro_duration_ms_min": 18,
    "micro_duration_ms_max": 19,
    "short_duration_ms_min": 20,
    "short_duration_ms_max": 21,
    "body_duration_ms_min": 22,
    "body_duration_ms_max": 23,
    "rng_seed": 24,
    "mix_dry_gain": 25,
    "mix_wet_gain": 26,
    "stereo_width": 27,
    "attack_pan_spread": 28,
    "sustain_pan_spread": 29,
    "smart_start_enable": 30,
    "smart_start_range": 31,
    "envelope_variation": 32,
    "envelope_family": 33,
    "wet_drive": 34,
    "wet_clip_amount": 35,
    "wet_output_trim": 36,
    "sustain_diffusion_enable": 37,
    "sustain_diffusion_amount": 38,
    "sustain_diffusion_stages": 39,
    "sustain_diffusion_delay": 40,
    "sustain_diffusion_feedback": 41,
    "droplet_enable": 42,
    "droplet_probability": 43,
    "droplet_gain": 44,
    "droplet_length_scale": 45,
    "memory_mix": 46,
    "memory_pull": 47,
    "memory_darkening": 48,
    "tone_variation": 49,
    "attack_brightness": 50,
    "sustain_darkness": 51,
    "attack_rate_jitter": 52,
    "attack_rate_jitter_depth": 53,
    "quality_profile": 54,
    "active_voice_limit": 55,
    "freeze_amount": 56,
    "freeze_enabled": 57,
    "reverse_probability": 58,
    "pitch_mode": 59,
    "shimmer_amount": 60,
    "final_limiter_ceiling_db": 61,
    "final_limiter_release_ms": 62,
}

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


def _compiler() -> str:
    compiler = shutil.which("gcc") or shutil.which("clang") or shutil.which("cc")
    if compiler is None:
        pytest.skip("No C compiler (gcc, clang, cc) found in PATH")
    return compiler


def _build_offline_renderer(compiler: str, tmp_path: Path) -> Path:
    binary = tmp_path / "sound_bubbles_render"
    subprocess.run(
        [
            compiler,
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


def _build_native_wasm_runner(compiler: str, tmp_path: Path) -> Path:
    binaries = {
        "win32": ".exe",
        "cygwin": ".exe",
    }
    binary = tmp_path / ("wasm_native_runner" + binaries.get(sys.platform, ""))
    subprocess.run(
        [
            compiler,
            "tests/dsp/wasm_native_runner.c",
            "core/engine/bubble_engine.c",
            "core/engine/bubble_macro_map.c",
            "core/dsp/sound_bubbles_dsp.c",
            "-O2",
            "-Wall",
            "-Wextra",
            "-std=c11",
            "-Icore",
            "-Icore/dsp",
            f"-I{SHIM_DIR}",
            "-lm",
            "-o",
            str(binary),
        ],
        cwd=REPO_ROOT,
        check=True,
    )
    return binary


def _write_params_file(preset: dict, path: Path) -> None:
    lines: list[str] = []
    for key, param_id in PARAM_IDS.items():
        value = preset.get(key)
        if value is None:
            if key == "mix_dry_gain":
                value = preset.get("master_dry_gain")
            elif key == "mix_wet_gain":
                value = preset.get("master_wet_gain")
            elif key == "attack_region_min_offset_samples":
                value = preset.get("micro_offset_samples")
            elif key == "attack_region_max_offset_samples":
                base = preset.get("micro_offset_samples", 441)
                value = base + preset.get("micro_jitter_samples", 3087)
            elif key == "body_region_min_offset_samples":
                value = preset.get("short_offset_samples")
            elif key == "body_region_max_offset_samples":
                base = preset.get("short_offset_samples", 3528)
                value = base + preset.get("short_jitter_samples", 7497)
            elif key == "memory_region_min_offset_samples":
                value = preset.get("body_offset_samples")
            elif key == "memory_region_max_offset_samples":
                base = preset.get("body_offset_samples", 11025)
                value = base + preset.get("body_jitter_samples", 28665)
        if value is None:
            continue
        lines.append(f"{param_id} {float(value):.9g}")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def _wav_to_raw_f32(path: Path, destination: Path) -> None:
    with wave.open(str(path), "rb") as handle:
        channels = handle.getnchannels()
        width = handle.getsampwidth()
        frames = handle.getnframes()
        raw = handle.readframes(frames)
    if width != 2:
        raise ValueError("Parity fixture must be 16-bit PCM")
    samples = struct.unpack("<" + "h" * (len(raw) // 2), raw)
    if channels == 1:
        mono = samples
    else:
        mono = tuple(
            sum(samples[i + ch] for ch in range(channels)) / channels
            for i in range(0, len(samples), channels)
        )
    with destination.open("wb") as out:
        out.write(struct.pack("<" + "f" * len(mono), *[s / 32768.0 for s in mono]))


def _read_metrics(path: Path) -> list[dict[str, float]]:
    with path.open(newline="") as fh:
        return [{key: float(value) for key, value in row.items()} for row in csv.DictReader(fh)]


def test_offline_c_and_native_wasm_module_metrics_match(tmp_path: Path) -> None:
    """Compile the real platform/wasm/bubble_cloud_wasm.c natively and require it
    to reproduce the Offline C renderer metrics. This runs without Emscripten and
    protects the WASM parameter plumbing (notably developer-mode enablement)."""
    compiler = _compiler()
    renderer = _build_offline_renderer(compiler, tmp_path)
    native_runner = _build_native_wasm_runner(compiler, tmp_path)

    fixture = write_core_parity_fixture(tmp_path / "core_parity_fixture.wav")
    raw = tmp_path / "core_parity_fixture.f32"
    _wav_to_raw_f32(fixture, raw)

    params_path = tmp_path / "neutral.params.txt"
    _write_params_file(json.loads(PRESET.read_text(encoding="utf-8")), params_path)

    offline_metrics = tmp_path / "offline.metrics.csv"
    wasm_metrics = tmp_path / "wasm.metrics.csv"
    subprocess.run(
        [str(renderer), str(fixture), str(PRESET), str(tmp_path / "offline.wav"),
         "--metrics-out", str(offline_metrics), "--repro-check"],
        cwd=REPO_ROOT,
        check=True,
    )
    subprocess.run([str(native_runner), str(raw), str(params_path), str(wasm_metrics)],
                   cwd=REPO_ROOT, check=True)

    offline_rows = _read_metrics(offline_metrics)
    wasm_rows = _read_metrics(wasm_metrics)
    assert len(offline_rows) == len(wasm_rows)
    assert len(offline_rows) > 0

    failures: list[str] = []
    for column in COMMON_COLUMNS:
        deltas = [abs(off[column] - wasm[column]) for off, wasm in zip(offline_rows, wasm_rows)]
        max_delta = max(deltas)
        mean_delta = sum(deltas) / len(deltas)
        if max_delta > MAX_ABS_DELTA or mean_delta > MEAN_ABS_DELTA:
            failures.append(
                f"{column}: max_delta={max_delta:.9g} mean_delta={mean_delta:.9g}"
            )

    assert not failures, "Native WASM module/Offline C parity exceeded tolerance:\n" + "\n".join(failures)
