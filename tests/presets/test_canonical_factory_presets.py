from __future__ import annotations

import json
import re
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
FACTORY_ROOT = REPO_ROOT / "core" / "presets" / "factory"
UI_FACTORY = REPO_ROOT / "ui" / "web" / "src" / "presets" / "factoryPresets.js"
SCHEMA_JS = REPO_ROOT / "ui" / "web" / "src" / "presets" / "presetSchema.js"

EXPECTED_PRESETS = [
    ("Ambient Bloom", "ambient-bloom"),
    ("Frozen Cathedral", "frozen-cathedral"),
    ("Glass Rain", "glass-rain"),
    ("Shoegaze Cloud", "shoegaze-cloud"),
    ("Dream Pad", "dream-pad"),
    ("Ocean Mist", "ocean-mist"),
    ("Submerged Piano", "submerged-piano"),
    ("Reverse Horizon", "reverse-horizon"),
    ("Infinite Guitar", "infinite-guitar"),
    ("Dust Particles", "dust-particles"),
    ("Ghost Chorus", "ghost-chorus"),
    ("Crystal Bloom", "crystal-bloom"),
    ("Warm Nebula", "warm-nebula"),
    ("Slow Motion", "slow-motion"),
    ("Tape Cloud", "tape-cloud"),
    ("Cosmic Swell", "cosmic-swell"),
]

EXPECTED_MACROS = [
    "density",
    "bloom",
    "motion",
    "texture",
    "space",
    "gravity",
    "memory",
    "clarity",
    "freeze",
    "sparkle",
    "warmth",
    "mix",
]

PROFILE_LIMITS = {
    0: ("MCU_SAFE", 8),
    1: ("MCU_PLUS", 16),
    2: ("WEB_STANDARD", 24),
    3: ("WEB_ULTRA", 32),
}


def _schema_ranges() -> dict[str, tuple[float, float, str]]:
    source = SCHEMA_JS.read_text(encoding="utf-8")
    ranges: dict[str, tuple[float, float, str]] = {}
    pattern = re.compile(
        r"name: '([^']+)', min: ([^,]+), max: ([^,]+), defaultValue: [^,]+, type: '([^']+)'"
    )
    for name, min_value, max_value, param_type in pattern.findall(source):
        ranges[name] = (float(min_value), float(max_value), param_type)
    assert ranges, "Preset schema ranges were not parsed from UI schema"
    return ranges


def _load_canonical_presets() -> list[dict]:
    presets = []
    for expected_name, slug in EXPECTED_PRESETS:
        preset = json.loads((FACTORY_ROOT / f"{slug}.json").read_text(encoding="utf-8"))
        assert preset["preset_name"] == expected_name
        assert preset["preset_slug"] == slug
        presets.append(preset)
    return presets


def test_canonical_factory_catalog_contains_requested_presets_and_ui_mirror() -> None:
    presets = _load_canonical_presets()
    assert [(preset["preset_name"], preset["preset_slug"]) for preset in presets] == EXPECTED_PRESETS

    ui_source = UI_FACTORY.read_text(encoding="utf-8")
    for preset in presets:
        assert f'preset_slug: "{preset["preset_slug"]}"' in ui_source
        assert f'preset_name: "{preset["preset_name"]}"' in ui_source


def test_canonical_factory_presets_stay_in_param_ranges_and_voice_profile_limits() -> None:
    ranges = _schema_ranges()
    failures: list[str] = []

    for preset in _load_canonical_presets():
        name = preset["preset_name"]
        macros = preset.get("macro_values", {})
        if list(macros.keys()) != EXPECTED_MACROS:
            failures.append(f"{name}: macro_values must provide exactly the canonical 12 macros")
        for macro_name, macro_value in macros.items():
            if not isinstance(macro_value, (int, float)) or not 0.0 <= float(macro_value) <= 1.0:
                failures.append(f"{name}: macro {macro_name}={macro_value!r} outside 0..1")

        params = preset.get("params", {})
        for param_name, raw_value in params.items():
            if param_name not in ranges:
                failures.append(f"{name}: unknown param {param_name}")
                continue
            min_value, max_value, param_type = ranges[param_name]
            value = float(raw_value)
            if not min_value <= value <= max_value:
                failures.append(f"{name}: {param_name}={raw_value} outside {min_value}..{max_value}")
            if param_type in {"int", "bool", "enum"} and int(value) != value:
                failures.append(f"{name}: {param_name}={raw_value} must be an integer-like {param_type}")

        quality_profile = int(params.get("quality_profile", 2))
        active_voice_limit = int(params.get("active_voice_limit", 24))
        profile_name, profile_voice_limit = PROFILE_LIMITS[quality_profile]
        if active_voice_limit > profile_voice_limit:
            failures.append(
                f"{name}: active_voice_limit {active_voice_limit} exceeds {profile_name} limit {profile_voice_limit}"
            )
        metadata = preset.get("metadata", {})
        if metadata.get("recommended_min_profile") != profile_name:
            failures.append(f"{name}: recommended_min_profile does not match quality_profile {profile_name}")
        mcu = metadata.get("mcu_compatibility", {})
        if mcu.get("profile") != profile_name or int(mcu.get("active_voice_limit", -1)) != active_voice_limit:
            failures.append(f"{name}: MCU compatibility metadata is not synchronized with params")
        if bool(preset.get("esp32_safe")) != (quality_profile <= 1 and active_voice_limit <= 16):
            failures.append(f"{name}: esp32_safe must reflect MCU_SAFE/MCU_PLUS voice budgets")

    assert not failures, "Canonical preset validation failures:\n" + "\n".join(failures)
