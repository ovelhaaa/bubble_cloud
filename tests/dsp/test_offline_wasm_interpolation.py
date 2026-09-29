from __future__ import annotations

import json
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]
TESTS_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS_ROOT))
from support_audio import write_core_parity_fixture  # noqa: E402

SHIM_DIR = REPO_ROOT / "tests" / "dsp" / "emscripten_shim"
PRESET = REPO_ROOT / "core" / "presets" / "factory" / "neutral.json"


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


def _build_wasm_probe(compiler: str, tmp_path: Path) -> Path:
    binary = tmp_path / "wasm_interpolation_probe"
    subprocess.run(
        [
            compiler,
            "tests/dsp/wasm_interpolation_probe.c",
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


def test_wasm_module_selects_same_interpolator_as_core(tmp_path: Path) -> None:
    """The real WASM translation unit must select WEB_* -> Hermite and
    MCU_* -> linear, exactly like the shared core."""
    compiler = _compiler()
    probe = _build_wasm_probe(compiler, tmp_path)
    subprocess.run([str(probe)], cwd=REPO_ROOT, check=True)


def test_offline_profile_maps_to_the_same_interpolator(tmp_path: Path) -> None:
    """Offline/WASM parity is evaluated within the same quality profile. Using
    presets that differ only in quality_profile (same voice limit) isolates the
    read path: the MCU render must use linear and the WEB render Hermite, so the
    two outputs differ, while re-rendering the same profile is deterministic."""
    compiler = _compiler()
    renderer = _build_offline_renderer(compiler, tmp_path)
    fixture = write_core_parity_fixture(tmp_path / "fixture.wav")

    base = json.loads(PRESET.read_text(encoding="utf-8"))
    presets: dict[int, Path] = {}
    for profile, quality in ((0, "MCU_SAFE"), (2, "WEB_STANDARD")):
        data = dict(base)
        data["quality_profile"] = profile
        data["active_voice_limit"] = 24  # identical pool: only the interpolator differs
        path = tmp_path / f"preset_{quality}.json"
        path.write_text(json.dumps(data), encoding="utf-8")
        presets[profile] = path

    def render(preset: Path, tag: str) -> bytes:
        out_wav = tmp_path / f"{tag}.wav"
        subprocess.run(
            [str(renderer), str(fixture), str(preset), str(out_wav)],
            cwd=REPO_ROOT,
            check=True,
            capture_output=True,
        )
        return out_wav.read_bytes()

    mcu = render(presets[0], "mcu")
    web = render(presets[2], "web")
    mcu_again = render(presets[0], "mcu_again")

    assert mcu == mcu_again, "the same quality profile must render deterministically"
    assert mcu != web, "MCU (linear) and WEB (Hermite) must render differently offline"

