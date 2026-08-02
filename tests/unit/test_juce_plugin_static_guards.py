from __future__ import annotations

import re
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
PLUGIN_EDITOR = REPO_ROOT / "platform" / "juce" / "Source" / "PluginEditor.cpp"
PLUGIN_PROCESSOR = REPO_ROOT / "platform" / "juce" / "Source" / "PluginProcessor.cpp"
PLUGIN_PROCESSOR_HEADER = REPO_ROOT / "platform" / "juce" / "Source" / "PluginProcessor.h"
ENGINE_WRAPPER = REPO_ROOT / "platform" / "juce" / "Source" / "BubbleCloudEngineWrapper.cpp"
ENGINE_WRAPPER_HEADER = REPO_ROOT / "platform" / "juce" / "Source" / "BubbleCloudEngineWrapper.h"
JUCE_CMAKE = REPO_ROOT / "platform" / "juce" / "CMakeLists.txt"
VST_WORKFLOW = REPO_ROOT / ".github" / "workflows" / "build-vst.yml"
PROCESSOR_SMOKE = REPO_ROOT / "tests" / "juce" / "processor_smoke.cpp"
CORE_ENGINE = REPO_ROOT / "core" / "engine" / "bubble_engine.c"

EXPECTED_NEW_PRESETS = [
    "Pick Halo",
    "Bass Shadow",
    "Vocal Veil",
    "Small Cloud",
    "Firefly Arp",
    "Reverse Undercurrent",
    "Wide Clean Doubler",
    "Capture Ready",
    "Quarter Strum",
    "Tresillo Spray",
    "Last-16th Swarm",
    "Reverse Pulse",
    "Fifth Choir",
    "Undertow Octave",
    "Morse Dust",
    "Broken Constellation",
]

EXPECTED_ADVANCED_PARAMETER_IDS = [
    "TEMPO_SYNC",
    "RHYTHM_DIVISION",
    "BURST_MODE",
    "RHYTHM_PATTERN",
    "PITCH_MODE_OVERRIDE",
    "MOTION_SHAPE",
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
        r'\{\s*"(?P<name>[^"]+)",\s*(?P<quality>[0-3]),\s*\{\{(?P<macros>.*?)\}\}'
        r'(?:\s*,\s*\{[^}]*\})?\s*\}',
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
    assert int(declared_count_match.group(1)) == len(presets) == 20

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


def test_advanced_parameters_are_persistent_and_host_tempo_is_forwarded() -> None:
    processor = PLUGIN_PROCESSOR.read_text(encoding="utf-8")
    editor = PLUGIN_EDITOR.read_text(encoding="utf-8")

    for parameter_id in EXPECTED_ADVANCED_PARAMETER_IDS:
        assert f'"{parameter_id}"' in processor
        assert f'setParameterValue("{parameter_id}"' in editor

    assert "getPlayHead()" in processor
    assert "getPosition()" in processor
    assert "getBpm()" in processor
    assert "getPpqPosition()" in processor
    assert "engineWrapper.setHostTempo" in processor
    assert "engineWrapper.syncRhythmPhase" in processor


def test_juce_wrapper_preserves_stereo_input_without_audio_thread_allocations() -> None:
    wrapper = ENGINE_WRAPPER.read_text(encoding="utf-8")
    wrapper_header = ENGINE_WRAPPER_HEADER.read_text(encoding="utf-8")

    assert "monoMixBuffer" not in wrapper
    assert "monoMixBuffer" not in wrapper_header
    assert "inLeft + processed" in wrapper
    assert "inRight + processed" in wrapper
    assert "scratchCapacity" in wrapper

    process_body = re.search(
        r"void BubbleCloudEngineWrapper::process\(.*?\n\}",
        wrapper,
        re.DOTALL,
    )
    assert process_body is not None
    assert ".resize(" not in process_body.group(0)
    assert "std::map" not in wrapper
    assert "std::map" not in wrapper_header


def test_performance_controls_support_midi_capture_scene_morph_and_rhythm_ui() -> None:
    processor = PLUGIN_PROCESSOR.read_text(encoding="utf-8")
    processor_header = PLUGIN_PROCESSOR_HEADER.read_text(encoding="utf-8")
    editor = PLUGIN_EDITOR.read_text(encoding="utf-8")
    cmake = JUCE_CMAKE.read_text(encoding="utf-8")
    workflow = VST_WORKFLOW.read_text(encoding="utf-8")

    for parameter_id in ["MORPH", "FREEZE_MIDI_MODE", "FREEZE_MIDI_NOTE"]:
        assert f'"{parameter_id}"' in processor

    assert "NEEDS_MIDI_INPUT TRUE" in cmake
    assert "acceptsMidi() const override { return true; }" in processor_header
    assert "handlePerformanceMidi(midiMessages)" in processor
    assert "message.isNoteOn()" in processor
    assert "message.isNoteOff()" in processor
    assert "PERFORMANCE_SCENES" in processor
    assert "captureScene(0)" in editor
    assert "captureScene(1)" in editor
    assert 'SliderAttachment>(audioProcessor.treeState, "MORPH"' in editor
    assert "setCaptureHeld(captureButton.isDown())" in editor
    assert "std::array<juce::TextButton, 16> rhythmStepButtons" in (
        REPO_ROOT / "platform" / "juce" / "Source" / "PluginEditor.h"
    ).read_text(encoding="utf-8")
    assert 'ComboBoxAttachment>(audioProcessor.treeState, "RHYTHM_DIVISION"' in editor
    assert 'ComboBoxAttachment>(audioProcessor.treeState, "BURST_MODE"' in editor
    assert "pattern |= (1u << i)" in editor
    assert '"ENGINE READY"' not in editor
    assert "BUBBLES_BUILD_PROCESSOR_TESTS=ON" in workflow
    assert "ctest --test-dir build_juce" in workflow


def test_cloud_visualizer_uses_lock_free_engine_telemetry_and_rhythm_playhead() -> None:
    wrapper = ENGINE_WRAPPER.read_text(encoding="utf-8")
    wrapper_header = ENGINE_WRAPPER_HEADER.read_text(encoding="utf-8")
    processor = PLUGIN_PROCESSOR.read_text(encoding="utf-8")
    editor = PLUGIN_EDITOR.read_text(encoding="utf-8")
    smoke = PROCESSOR_SMOKE.read_text(encoding="utf-8")

    assert "BubbleCloudVoiceTelemetry" in wrapper_header
    assert "std::atomic" in wrapper_header
    assert "bubble_engine_set_metrics_callback" in wrapper
    assert "publishVoiceTelemetry()" in wrapper
    assert "telemetryPeakL.exchange" in wrapper
    assert "telemetrySpawnCount.exchange" in wrapper
    assert "getTelemetrySnapshot()" in processor
    assert "setTelemetry(const BubbleCloudTelemetry&" in editor
    assert "telemetry.voices" in editor
    assert 'getProperties().set("rhythmPlayhead"' in editor
    assert "stereoTelemetry.peakLeft" in smoke
    assert "stereoTelemetry.activeVoices" in smoke
    assert "rhythmTelemetry.rhythmStep" in smoke
    assert "std::mutex" not in wrapper
    assert "std::lock_guard" not in wrapper


def test_release_candidate_has_perceptual_morph_calibration_and_pluginval() -> None:
    processor = PLUGIN_PROCESSOR.read_text(encoding="utf-8")
    smoke = PROCESSOR_SMOKE.read_text(encoding="utf-8")
    workflow = VST_WORKFLOW.read_text(encoding="utf-8")
    core_engine = CORE_ENGINE.read_text(encoding="utf-8")

    assert "morphedContinuousValue" in processor
    assert "BUBBLE_MACRO_SMOOTH_TIME_SECONDS" in core_engine
    assert "expf(" in core_engine
    assert "engine->config.sample_rate" in core_engine
    for token in [
        "44100.0",
        "48000.0",
        "88200.0",
        "96000.0",
        "correlation",
        "rmsMono",
        "calibration spread",
        "getMorphedParameterValue",
    ]:
        assert token in smoke

    parameter_callback = re.search(
        r"void BubbleCloudAudioProcessor::parameterChanged\(.*?\n\}",
        processor,
        re.DOTALL,
    )
    assert parameter_callback is not None
    assert "forwardParameterToEngine" not in parameter_callback.group(0)

    assert "pluginval_Windows.zip" in workflow
    assert "--strictness-level 5" in workflow
    assert "Get-FileHash" in workflow
    assert 'COMPANY_NAME "Bubbles Audio"' in JUCE_CMAKE.read_text(encoding="utf-8")
    assert 'BUNDLE_ID "audio.bubbles.Bubbles"' in JUCE_CMAKE.read_text(encoding="utf-8")
