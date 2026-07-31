from __future__ import annotations

import re
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
PLUGIN_EDITOR = REPO_ROOT / "platform" / "juce" / "Source" / "PluginEditor.cpp"
PLUGIN_PROCESSOR = REPO_ROOT / "platform" / "juce" / "Source" / "PluginProcessor.cpp"

EXPECTED_NEW_PRESETS = [
    "Pick Halo",
    "Bass Shadow",
    "Vocal Veil",
    "Small Cloud",
    "Firefly Arp",
    "Reverse Undercurrent",
    "Wide Clean Doubler",
    "Capture Ready",
]

EXPECTED_MACRO_IDS = [
    "DENSITY",
    "BLOOM",
    "MOTION",
    "TEXTURE",
    "SPACE",
    "GRAVITY",
    "MEMORY",
    "CLARITY",
    "FREEZE",
    "SPARKLE",
    "WARMTH",
    "MIX",
]


def _parse_factory_presets() -> list[tuple[str, int, list[tuple[str, float]]]]:
    source = PLUGIN_EDITOR.read_text(encoding="utf-8")
    preset_pattern = re.compile(
        r'\{\s*"(?P<name>[^"]+)",\s*(?P<quality>[0-3]),\s*\{\{(?P<macros>.*?)\}\}\s*\}',
        re.DOTALL,
    )
    macro_pattern = re.compile(r'\{\s*"([A-Z_]+)",\s*([0-9]+(?:\.[0-9]+)?)f\s*\}')

    presets = []
    for match in preset_pattern.finditer(source):
        macros = [(name, float(value)) for name, value in macro_pattern.findall(match.group("macros"))]
        presets.append((match.group("name"), int(match.group("quality")), macros))
    return presets


def test_vst_factory_catalog_contains_new_macro_presets() -> None:
    source = PLUGIN_EDITOR.read_text(encoding="utf-8")
    presets = _parse_factory_presets()

    declared_count_match = re.search(r"std::array<FactoryPreset,\s*(\d+)>", source)
    assert declared_count_match is not None
    assert int(declared_count_match.group(1)) == len(presets) == 12

    names = [name for name, _, _ in presets]
    for expected_name in EXPECTED_NEW_PRESETS:
        assert expected_name in names

    for name, quality, macros in presets:
        assert 0 <= quality <= 3, f"{name}: invalid quality profile {quality}"
        assert [macro_name for macro_name, _ in macros] == EXPECTED_MACRO_IDS
        for macro_name, value in macros:
            assert 0.0 <= value <= 1.0, f"{name}: {macro_name}={value} outside 0..1"


def test_quality_choice_is_forwarded_as_denormalised_index() -> None:
    source = PLUGIN_PROCESSOR.read_text(encoding="utf-8")
    quality_block_match = re.search(
        r'else if \(parameterID == "QUALITY_PROFILE"\) \{(?P<body>.*?)\n\s*\}',
        source,
        re.DOTALL,
    )
    assert quality_block_match is not None
    quality_block = quality_block_match.group("body")

    assert "jlimit(0.0f, 3.0f, newValue)" in quality_block
    assert "* 3.0f" not in quality_block
