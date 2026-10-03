#include "PluginEditor.h"
#include "BubblesLookAndFeel.h"
#include "CloudVisualizer.h"

#include <array>
#include <cmath>

namespace
{
    using namespace BubblesTheme;
    struct MacroSetting
    {
        const char* parameterId;
        float value;
    };

    struct AdvancedPresetSettings
    {
        int tempoSyncEnabled = 0;
        int rhythmDivision = 2;
        int burstMode = 0;
        int rhythmPattern = 4369;
        int pitchModeOverride = 0;
        int motionShape = 0;
    };

    struct FactoryPreset
    {
        const char* name;
        int qualityProfile;
        std::array<MacroSetting, 12> macros;
        AdvancedPresetSettings advanced;
    };

    const std::array<FactoryPreset, 20> factoryPresets {{
        {
            "Neutral",
            3,
            {{
                { "DENSITY", 0.50f },
                { "BLOOM", 0.50f },
                { "MOTION", 0.50f },
                { "TEXTURE", 0.50f },
                { "SPACE", 0.50f },
                { "GRAVITY", 0.50f },
                { "MEMORY", 0.50f },
                { "CLARITY", 0.50f },
                { "FREEZE", 0.00f },
                { "SPARKLE", 0.00f },
                { "WARMTH", 0.50f },
                { "MIX", 0.50f },
            }}
        },
        {
            "Ambient Bloom",
            3,
            {{
                { "DENSITY", 0.44f },
                { "BLOOM", 0.86f },
                { "MOTION", 0.34f },
                { "TEXTURE", 0.48f },
                { "SPACE", 0.78f },
                { "GRAVITY", 0.58f },
                { "MEMORY", 0.70f },
                { "CLARITY", 0.36f },
                { "FREEZE", 0.52f },
                { "SPARKLE", 0.28f },
                { "WARMTH", 0.74f },
                { "MIX", 0.58f },
            }}
        },
        {
            "Glass Rain",
            3,
            {{
                { "DENSITY", 0.62f },
                { "BLOOM", 0.54f },
                { "MOTION", 0.68f },
                { "TEXTURE", 0.72f },
                { "SPACE", 0.74f },
                { "GRAVITY", 0.36f },
                { "MEMORY", 0.42f },
                { "CLARITY", 0.74f },
                { "FREEZE", 0.18f },
                { "SPARKLE", 0.82f },
                { "WARMTH", 0.28f },
                { "MIX", 0.60f },
            }}
        },
        {
            "Frozen Cathedral",
            3,
            {{
                { "DENSITY", 0.50f },
                { "BLOOM", 0.92f },
                { "MOTION", 0.22f },
                { "TEXTURE", 0.42f },
                { "SPACE", 0.92f },
                { "GRAVITY", 0.72f },
                { "MEMORY", 0.86f },
                { "CLARITY", 0.24f },
                { "FREEZE", 0.88f },
                { "SPARKLE", 0.22f },
                { "WARMTH", 0.62f },
                { "MIX", 0.64f },
            }}
        },
        {
            "Pick Halo",
            3,
            {{
                { "DENSITY", 0.26f },
                { "BLOOM", 0.22f },
                { "MOTION", 0.18f },
                { "TEXTURE", 0.28f },
                { "SPACE", 0.58f },
                { "GRAVITY", 0.82f },
                { "MEMORY", 0.16f },
                { "CLARITY", 0.90f },
                { "FREEZE", 0.00f },
                { "SPARKLE", 0.08f },
                { "WARMTH", 0.34f },
                { "MIX", 0.24f },
            }}
        },
        {
            "Bass Shadow",
            3,
            {{
                { "DENSITY", 0.32f },
                { "BLOOM", 0.36f },
                { "MOTION", 0.20f },
                { "TEXTURE", 0.24f },
                { "SPACE", 0.32f },
                { "GRAVITY", 0.78f },
                { "MEMORY", 0.30f },
                { "CLARITY", 0.58f },
                { "FREEZE", 0.00f },
                { "SPARKLE", 0.00f },
                { "WARMTH", 0.88f },
                { "MIX", 0.28f },
            }}
        },
        {
            "Vocal Veil",
            3,
            {{
                { "DENSITY", 0.34f },
                { "BLOOM", 0.52f },
                { "MOTION", 0.26f },
                { "TEXTURE", 0.34f },
                { "SPACE", 0.78f },
                { "GRAVITY", 0.64f },
                { "MEMORY", 0.42f },
                { "CLARITY", 0.70f },
                { "FREEZE", 0.00f },
                { "SPARKLE", 0.12f },
                { "WARMTH", 0.48f },
                { "MIX", 0.34f },
            }}
        },
        {
            "Small Cloud",
            3,
            {{
                { "DENSITY", 0.42f },
                { "BLOOM", 0.30f },
                { "MOTION", 0.24f },
                { "TEXTURE", 0.30f },
                { "SPACE", 0.08f },
                { "GRAVITY", 0.68f },
                { "MEMORY", 0.20f },
                { "CLARITY", 0.72f },
                { "FREEZE", 0.00f },
                { "SPARKLE", 0.00f },
                { "WARMTH", 0.58f },
                { "MIX", 0.30f },
            }}
        },
        {
            "Firefly Arp",
            3,
            {{
                { "DENSITY", 0.48f },
                { "BLOOM", 0.38f },
                { "MOTION", 0.54f },
                { "TEXTURE", 0.82f },
                { "SPACE", 0.86f },
                { "GRAVITY", 0.46f },
                { "MEMORY", 0.22f },
                { "CLARITY", 0.84f },
                { "FREEZE", 0.00f },
                { "SPARKLE", 0.36f },
                { "WARMTH", 0.24f },
                { "MIX", 0.46f },
            }}
        },
        {
            "Reverse Undercurrent",
            3,
            {{
                { "DENSITY", 0.44f },
                { "BLOOM", 0.56f },
                { "MOTION", 0.86f },
                { "TEXTURE", 0.58f },
                { "SPACE", 0.82f },
                { "GRAVITY", 0.44f },
                { "MEMORY", 0.58f },
                { "CLARITY", 0.48f },
                { "FREEZE", 0.00f },
                { "SPARKLE", 0.10f },
                { "WARMTH", 0.52f },
                { "MIX", 0.56f },
            }}
        },
        {
            "Wide Clean Doubler",
            3,
            {{
                { "DENSITY", 0.38f },
                { "BLOOM", 0.18f },
                { "MOTION", 0.34f },
                { "TEXTURE", 0.22f },
                { "SPACE", 1.00f },
                { "GRAVITY", 0.72f },
                { "MEMORY", 0.12f },
                { "CLARITY", 0.78f },
                { "FREEZE", 0.00f },
                { "SPARKLE", 0.00f },
                { "WARMTH", 0.42f },
                { "MIX", 0.30f },
            }}
        },
        {
            "Capture Ready",
            3,
            {{
                { "DENSITY", 0.46f },
                { "BLOOM", 0.86f },
                { "MOTION", 0.20f },
                { "TEXTURE", 0.36f },
                { "SPACE", 0.90f },
                { "GRAVITY", 0.70f },
                { "MEMORY", 0.88f },
                { "CLARITY", 0.32f },
                { "FREEZE", 0.00f },
                { "SPARKLE", 0.14f },
                { "WARMTH", 0.72f },
                { "MIX", 0.62f },
            }}
        },
        {
            "Quarter Strum",
            3,
            {{
                { "DENSITY", 0.44f },
                { "BLOOM", 0.36f },
                { "MOTION", 0.28f },
                { "TEXTURE", 0.30f },
                { "SPACE", 0.76f },
                { "GRAVITY", 0.80f },
                { "MEMORY", 0.22f },
                { "CLARITY", 0.74f },
                { "FREEZE", 0.00f },
                { "SPARKLE", 0.00f },
                { "WARMTH", 0.52f },
                { "MIX", 0.46f },
            }},
            { 1, 2, 2, 4369, 0, 0 }
        },
        {
            "Tresillo Spray",
            3,
            {{
                { "DENSITY", 0.52f },
                { "BLOOM", 0.28f },
                { "MOTION", 0.36f },
                { "TEXTURE", 0.62f },
                { "SPACE", 0.84f },
                { "GRAVITY", 0.66f },
                { "MEMORY", 0.24f },
                { "CLARITY", 0.78f },
                { "FREEZE", 0.00f },
                { "SPARKLE", 0.10f },
                { "WARMTH", 0.30f },
                { "MIX", 0.48f },
            }},
            { 1, 2, 1, 18761, 0, 0 }
        },
        {
            "Last-16th Swarm",
            3,
            {{
                { "DENSITY", 0.34f },
                { "BLOOM", 0.22f },
                { "MOTION", 0.58f },
                { "TEXTURE", 0.78f },
                { "SPACE", 0.92f },
                { "GRAVITY", 0.62f },
                { "MEMORY", 0.16f },
                { "CLARITY", 0.84f },
                { "FREEZE", 0.00f },
                { "SPARKLE", 0.06f },
                { "WARMTH", 0.24f },
                { "MIX", 0.42f },
            }},
            { 1, 2, 3, 34952, 0, 1 }
        },
        {
            "Reverse Pulse",
            3,
            {{
                { "DENSITY", 0.40f },
                { "BLOOM", 0.56f },
                { "MOTION", 0.34f },
                { "TEXTURE", 0.40f },
                { "SPACE", 0.88f },
                { "GRAVITY", 0.68f },
                { "MEMORY", 0.52f },
                { "CLARITY", 0.52f },
                { "FREEZE", 0.00f },
                { "SPARKLE", 0.00f },
                { "WARMTH", 0.56f },
                { "MIX", 0.56f },
            }},
            { 1, 1, 4, 21845, 0, 1 }
        },
        {
            "Fifth Choir",
            3,
            {{
                { "DENSITY", 0.46f },
                { "BLOOM", 0.82f },
                { "MOTION", 0.20f },
                { "TEXTURE", 0.30f },
                { "SPACE", 0.90f },
                { "GRAVITY", 0.70f },
                { "MEMORY", 0.76f },
                { "CLARITY", 0.40f },
                { "FREEZE", 0.00f },
                { "SPARKLE", 0.00f },
                { "WARMTH", 0.62f },
                { "MIX", 0.60f },
            }},
            { 0, 2, 0, 4369, 3, 1 }
        },
        {
            "Undertow Octave",
            3,
            {{
                { "DENSITY", 0.38f },
                { "BLOOM", 0.58f },
                { "MOTION", 0.18f },
                { "TEXTURE", 0.24f },
                { "SPACE", 0.68f },
                { "GRAVITY", 0.78f },
                { "MEMORY", 0.70f },
                { "CLARITY", 0.28f },
                { "FREEZE", 0.00f },
                { "SPARKLE", 0.00f },
                { "WARMTH", 0.90f },
                { "MIX", 0.50f },
            }},
            { 0, 2, 0, 4369, 2, 1 }
        },
        {
            "Morse Dust",
            3,
            {{
                { "DENSITY", 0.28f },
                { "BLOOM", 0.12f },
                { "MOTION", 0.72f },
                { "TEXTURE", 0.96f },
                { "SPACE", 0.76f },
                { "GRAVITY", 0.52f },
                { "MEMORY", 0.10f },
                { "CLARITY", 0.92f },
                { "FREEZE", 0.00f },
                { "SPARKLE", 0.06f },
                { "WARMTH", 0.18f },
                { "MIX", 0.38f },
            }},
            { 1, 3, 0, 33825, 0, 2 }
        },
        {
            "Broken Constellation",
            3,
            {{
                { "DENSITY", 0.36f },
                { "BLOOM", 0.24f },
                { "MOTION", 0.94f },
                { "TEXTURE", 0.94f },
                { "SPACE", 0.72f },
                { "GRAVITY", 0.34f },
                { "MEMORY", 0.18f },
                { "CLARITY", 0.76f },
                { "FREEZE", 0.00f },
                { "SPARKLE", 0.08f },
                { "WARMTH", 0.28f },
                { "MIX", 0.46f },
            }},
            { 0, 2, 0, 4369, 0, 2 }
        },
    }};

    juce::Rectangle<float> asFloat(juce::Rectangle<int> bounds)
    {
        return bounds.toFloat();
    }

    void drawPanel(juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        auto r = asFloat(bounds);
        g.setGradientFill(juce::ColourGradient(panelRaised.withAlpha(0.70f), r.getX(), r.getY(), panel, r.getRight(), r.getBottom(), false));
        g.fillRoundedRectangle(r, 8.0f);

        g.setColour(stroke.withAlpha(0.85f));
        g.drawRoundedRectangle(r.reduced(0.5f), 8.0f, 1.0f);
    }

    void drawBubblesMark(juce::Graphics& g, juce::Rectangle<float> r, float)
    {
        g.setColour(amber); g.drawEllipse(r.getX()+20, r.getY()+22, 34, 34, 1.6f);
        g.setColour(panel); g.fillEllipse(r.getX()+8, r.getY()+8, 30, 30);
        g.setColour(ink); g.drawEllipse(r.getX()+8, r.getY()+8, 30, 30, 1.6f);
        g.drawEllipse(r.getX(), r.getY()+37, 7, 7, 1.4f);
        g.fillEllipse(r.getX()+42, r.getY()+8, 4, 4);
    }
}

BubbleCloudAudioProcessorEditor::BubbleCloudAudioProcessorEditor(BubbleCloudAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    bubblesLookAndFeel = std::make_unique<BubblesLookAndFeel>();
    setLookAndFeel(bubblesLookAndFeel.get());

    for (int i = 0; i < (int)factoryPresets.size(); ++i)
        presetBox.addItem(factoryPresets[(size_t)i].name, i + 1);
    presetBox.setName("Factory preset");
    presetBox.setSelectedId(1, juce::dontSendNotification);
    presetBox.onChange = [this] { applyPreset(presetBox.getSelectedItemIndex()); };
    addAndMakeVisible(presetBox);

    qualityBox.setName("Quality profile");
    qualityBox.addItem("Eco", 1);
    qualityBox.addItem("Balanced", 2);
    qualityBox.addItem("Studio", 3);
    qualityBox.addItem("Ultra", 4);
    qualityAttachment = std::make_unique<ComboBoxAttachment>(audioProcessor.treeState, "QUALITY_PROFILE", qualityBox);
    addAndMakeVisible(qualityBox);

    freezeButton.setClickingTogglesState(true);
    freezeButton.onClick = [this] { setParameterAsToggle("FREEZE", freezeButton.getToggleState()); };
    freezeButton.setTooltip("Latch the current granular memory until Freeze is disabled.");
    addAndMakeVisible(freezeButton);

    captureButton.setTooltip("Momentarily capture the current granular memory while the button is held.");
    captureButton.onStateChange = [this] { audioProcessor.setCaptureHeld(captureButton.isDown()); };
    addAndMakeVisible(captureButton);

    storeSceneAButton.setTooltip("Start a new morph pair: store the current sound in A and seed B with the same sound.");
    storeSceneAButton.onClick = [this] {
        audioProcessor.captureScene(0);
        audioProcessor.captureScene(1);
        setParameterValue("MORPH", 1.0f);
    };
    addAndMakeVisible(storeSceneAButton);

    storeSceneBButton.setTooltip("Store the current controls as morph scene B and move to B.");
    storeSceneBButton.onClick = [this] {
        audioProcessor.captureScene(1);
        setParameterValue("MORPH", 1.0f);
    };
    addAndMakeVisible(storeSceneBButton);

    morphSlider.setName("Scene morph");
    morphSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    morphSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    morphSlider.setPopupDisplayEnabled(true, true, this);
    morphSlider.setNumDecimalPlacesToDisplay(2);
    morphSlider.setColour(juce::Slider::trackColourId, amber);
    morphSlider.setColour(juce::Slider::backgroundColourId, BubblesTheme::meterTrack);
    morphAttachment = std::make_unique<SliderAttachment>(audioProcessor.treeState, "MORPH", morphSlider);
    addAndMakeVisible(morphSlider);

    morphLabel.setText("SCENE A  <  MORPH  >  SCENE B", juce::dontSendNotification);
    morphLabel.setJustificationType(juce::Justification::centred);
    morphLabel.setColour(juce::Label::textColourId, textMuted);
    morphLabel.setFont(BubblesTheme::headingFont(10.5f));
    addAndMakeVisible(morphLabel);

    tempoSyncButton.setClickingTogglesState(true);
    tempoSyncButton.onClick = [this] { setParameterAsToggle("TEMPO_SYNC", tempoSyncButton.getToggleState()); };
    tempoSyncButton.setTooltip("Lock rhythmic emissions to the DAW tempo and PPQ position.");
    addAndMakeVisible(tempoSyncButton);

    rhythmDivisionBox.addItem("1/4 Grid", 1);
    rhythmDivisionBox.addItem("1/8 Grid", 2);
    rhythmDivisionBox.addItem("1/16 Grid", 3);
    rhythmDivisionBox.addItem("1/32 Grid", 4);
    rhythmDivisionBox.setTooltip("Rhythmic step division.");
    advancedComboAttachments.push_back(std::make_unique<ComboBoxAttachment>(audioProcessor.treeState, "RHYTHM_DIVISION", rhythmDivisionBox));
    addAndMakeVisible(rhythmDivisionBox);

    burstModeBox.addItem("Single", 1);
    burstModeBox.addItem("Spray", 2);
    burstModeBox.addItem("Strum", 3);
    burstModeBox.addItem("Swarm", 4);
    burstModeBox.addItem("Reverse Swell", 5);
    burstModeBox.setTooltip("Emission gesture used on active rhythm steps.");
    advancedComboAttachments.push_back(std::make_unique<ComboBoxAttachment>(audioProcessor.treeState, "BURST_MODE", burstModeBox));
    addAndMakeVisible(burstModeBox);

    pitchModeBox.addItem("Pitch: Macro", 1);
    pitchModeBox.addItem("Pitch: +1 Oct", 2);
    pitchModeBox.addItem("Pitch: -1 Oct", 3);
    pitchModeBox.addItem("Pitch: Fifth", 4);
    pitchModeBox.setTooltip("Fixed pitch override for the granular voices.");
    advancedComboAttachments.push_back(std::make_unique<ComboBoxAttachment>(audioProcessor.treeState, "PITCH_MODE_OVERRIDE", pitchModeBox));
    addAndMakeVisible(pitchModeBox);

    motionShapeBox.addItem("Motion: Triangle", 1);
    motionShapeBox.addItem("Motion: Smooth", 2);
    motionShapeBox.addItem("Motion: Hold", 3);
    motionShapeBox.setTooltip("Shape used by the internal motion modulation.");
    advancedComboAttachments.push_back(std::make_unique<ComboBoxAttachment>(audioProcessor.treeState, "MOTION_SHAPE", motionShapeBox));
    addAndMakeVisible(motionShapeBox);

    freezeMidiModeBox.addItem("MIDI: Latch", 1);
    freezeMidiModeBox.addItem("MIDI: Momentary", 2);
    freezeMidiModeBox.setTooltip("Latch toggles on note-on; Momentary follows note-on/note-off.");
    advancedComboAttachments.push_back(std::make_unique<ComboBoxAttachment>(audioProcessor.treeState, "FREEZE_MIDI_MODE", freezeMidiModeBox));
    addAndMakeVisible(freezeMidiModeBox);

    static constexpr const char* pitchClasses[] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    for (int note = 0; note < 128; ++note) {
        const auto noteName = juce::String(pitchClasses[note % 12]) + juce::String(note / 12 - 1)
            + "  (" + juce::String(note) + ")";
        freezeMidiNoteBox.addItem(noteName, note + 1);
    }
    freezeMidiNoteBox.setTooltip("MIDI note assigned to Freeze/Capture; default is C4 (note 60).");
    advancedComboAttachments.push_back(std::make_unique<ComboBoxAttachment>(audioProcessor.treeState, "FREEZE_MIDI_NOTE", freezeMidiNoteBox));
    addAndMakeVisible(freezeMidiNoteBox);

    for (int i = 0; i < (int)rhythmStepButtons.size(); ++i) {
        auto& step = rhythmStepButtons[(std::size_t)i];
        step.setButtonText(juce::String(i + 1));
        step.setClickingTogglesState(true);
        step.setTooltip("Toggle rhythm step " + juce::String(i + 1));
        step.onClick = [this] { commitRhythmPattern(); };
        addAndMakeVisible(step);
    }

    addControl(macroControls, "DENSITY", "Density", "emission");
    addControl(macroControls, "BLOOM", "Bloom", "body");
    addControl(macroControls, "TEXTURE", "Texture", "grain");
    addControl(macroControls, "MOTION", "Motion", "drift");
    addControl(macroControls, "SPACE", "Space", "stereo");
    addControl(macroControls, "MIX", "Mix", "wet");

    addControl(secondaryControls, "MEMORY", "Memory", "past");
    addControl(secondaryControls, "GRAVITY", "Gravity", "trigger");
    addControl(secondaryControls, "CLARITY", "Clarity", "edge");
    addControl(secondaryControls, "SPARKLE", "Sparkle", "shimmer");
    addControl(secondaryControls, "WARMTH", "Warmth", "tone");

    cloudVisualizer = std::make_unique<CloudVisualizer>();
    addAndMakeVisible(*cloudVisualizer);

    const auto initialTelemetry = audioProcessor.getTelemetrySnapshot();
    rhythmPlayheadStep = initialTelemetry.tempoSync ? initialTelemetry.rhythmStep : -1;
    cloudVisualizer->setTelemetry(initialTelemetry);
    updateToggleControls();
    previousPreset.onClick = [this] { presetBox.setSelectedItemIndex((presetBox.getSelectedItemIndex() + (int)factoryPresets.size() - 1) % (int)factoryPresets.size(), juce::sendNotificationSync); };
    nextPreset.onClick = [this] { presetBox.setSelectedItemIndex((presetBox.getSelectedItemIndex() + 1) % (int)factoryPresets.size(), juce::sendNotificationSync); };
    previousPreset.setTooltip("Previous factory preset"); nextPreset.setTooltip("Next factory preset");
    addAndMakeVisible(previousPreset); addAndMakeVisible(nextPreset);
    freezeButton.setButtonText("FREEZE"); captureButton.setButtonText("CAPTURE");
    storeSceneAButton.setButtonText("A   STORE"); storeSceneBButton.setButtonText("B   STORE");
    tempoSyncButton.setButtonText("SYNC");
    setSize(editorWidth, editorHeight);
    setResizable(false, false);
    startTimerHz(30);
}

BubbleCloudAudioProcessorEditor::~BubbleCloudAudioProcessorEditor()
{
    audioProcessor.setCaptureHeld(false);
    setLookAndFeel(nullptr);
}

BubbleCloudAudioProcessorEditor::ControlBinding& BubbleCloudAudioProcessorEditor::addControl(
    std::vector<std::unique_ptr<ControlBinding>>& target,
    const juce::String& parameterId,
    const juce::String& title,
    const juce::String& role)
{
    auto control = std::make_unique<ControlBinding>();
    control->parameterId = parameterId;
    control->title = title;
    control->role = role;

    control->slider.setName(title);
    control->slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    control->slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    control->slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.18f,
                                        juce::MathConstants<float>::pi * 2.82f,
                                        true);
    control->slider.setColour(juce::Slider::rotarySliderFillColourId, cyan);
    control->slider.setColour(juce::Slider::rotarySliderOutlineColourId, stroke);

    control->titleLabel.setText(title.toUpperCase(), juce::dontSendNotification);
    control->titleLabel.setJustificationType(juce::Justification::centred);
    control->titleLabel.setColour(juce::Label::textColourId, ink);
    control->titleLabel.setFont(BubblesTheme::headingFont(12.0f));

    addAndMakeVisible(control->slider);
    addAndMakeVisible(control->titleLabel);
    control->slider.setPopupDisplayEnabled(true, true, this);
    control->slider.setTooltip(title + " / " + role);

    auto* result = control.get();
    result->slider.onValueChange = [this, result] { updateControlValue(*result); };
    sliderAttachments.push_back(std::make_unique<SliderAttachment>(audioProcessor.treeState, parameterId, result->slider));
    updateControlValue(*result);

    target.push_back(std::move(control));
    return *result;
}

void BubbleCloudAudioProcessorEditor::applyPreset(int presetIndex)
{
    if (presetIndex < 0 || presetIndex >= (int)factoryPresets.size())
        return;

    const auto& preset = factoryPresets[(size_t)presetIndex];
    setParameterValue("TEMPO_SYNC", (float)preset.advanced.tempoSyncEnabled);
    setParameterValue("RHYTHM_DIVISION", (float)preset.advanced.rhythmDivision);
    setParameterValue("BURST_MODE", (float)preset.advanced.burstMode);
    setParameterValue("RHYTHM_PATTERN", (float)preset.advanced.rhythmPattern);
    setParameterValue("PITCH_MODE_OVERRIDE", (float)preset.advanced.pitchModeOverride);
    setParameterValue("MOTION_SHAPE", (float)preset.advanced.motionShape);

    for (const auto& macro : preset.macros)
        setParameterValue(macro.parameterId, macro.value);

    setParameterValue("QUALITY_PROFILE", (float)preset.qualityProfile);
    updateToggleControls();
}

void BubbleCloudAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(background);
    const auto layout = BubblesTheme::Layout(getLocalBounds());
    for (auto r : {layout.header, layout.macros, layout.chamber, layout.performance, layout.tonal, layout.rhythm}) drawPanel(g, r);
    drawBubblesMark(g, {30, 24, 64, 64}, 1);
    g.setColour(ink); g.setFont(BubblesTheme::brandFont());
    g.drawText("B U B B L E S", 112, 26, 330, 38, juce::Justification::centredLeft);
    g.setFont(BubblesTheme::captionFont(9.5f));
    g.drawText("G R A N U L A R   C L O U D   I N S T R U M E N T", 113, 66, 400, 20, juce::Justification::centredLeft);
    auto heading = [&](juce::String text, juce::Rectangle<int> r) {
        g.setColour(ink); g.setFont(BubblesTheme::headingFont());
        g.drawText(text, r.getX()+18, r.getY()+10, 210, 20, juce::Justification::centredLeft);
        g.setColour(stroke); g.drawLine((float)r.getX()+230, (float)r.getY()+20, (float)r.getRight()-18, (float)r.getY()+20);
    };
    heading("P E R F O R M A N C E", layout.performance);
    heading("T O N A L   S H A P I N G", layout.tonal);
    heading("R H Y T H M   L A B", layout.rhythm);
    const char* captions[] = {"SYNC", "DIVISION", "BURST MODE", "PITCH MODE", "MOTION SHAPE", "FREEZE MIDI", "MIDI NOTE"};
    for (int i=0; i<7; ++i) {
        g.setColour(textMuted); g.setFont(BubblesTheme::captionFont(9.0f));
        g.drawText(captions[i], layout.rhythm.getX()+18+i*(BubblesTheme::selectorWidth+BubblesTheme::selectorGap), layout.rhythm.getY()+36, 151, 18, juce::Justification::centredLeft);
    }
    g.setColour(textMuted); g.setFont(BubblesTheme::captionFont(9.0f));
    g.drawText("PATTERN", layout.rhythm.getX()+18, layout.rhythm.getY()+104, 80, 26, juce::Justification::centredLeft);
    for (auto section : {layout.macros, layout.tonal}) {
        int count = section == layout.macros ? 6 : 5;
        for (int i=1; i<count; ++i) { float x=(float)section.getX()+section.getWidth()*i/(float)count;
            g.setColour(stroke.withAlpha(0.6f)); g.drawLine(x, (float)section.getY()+35, x, (float)section.getBottom()-25); }
    }
}

void BubbleCloudAudioProcessorEditor::resized()
{
    const auto l = BubblesTheme::Layout(getLocalBounds());
    auto h = l.header.reduced(20, 24); qualityBox.setBounds(h.removeFromRight(110)); h.removeFromRight(18);
    nextPreset.setBounds(h.removeFromRight(40)); h.removeFromRight(8);
    presetBox.setBounds(h.removeFromRight(236)); h.removeFromRight(8); previousPreset.setBounds(h.removeFromRight(40));
    layoutControls(macroControls, l.macros.reduced(12, 8), 6);
    layoutControls(secondaryControls, l.tonal.withTrimmedTop(28).reduced(18, 4), 5);
    cloudVisualizer->setBounds(l.chamber.reduced(14));
    auto p = l.performance.reduced(18); p.removeFromTop(35);
    auto row=p.removeFromTop(68); freezeButton.setBounds(row.removeFromLeft((row.getWidth()-10)/2)); row.removeFromLeft(10); captureButton.setBounds(row);
    p.removeFromTop(20); row=p.removeFromTop(46);
    storeSceneAButton.setBounds(row.removeFromLeft((row.getWidth()-10)/2)); row.removeFromLeft(10); storeSceneBButton.setBounds(row);
    p.removeFromTop(20); morphLabel.setBounds(p.removeFromTop(24)); morphSlider.setBounds(p.removeFromTop(42));
    auto r=l.rhythm.reduced(18); r.removeFromTop(36); auto selectors=r.removeFromTop(32);
    juce::Component* components[] = {&tempoSyncButton,&rhythmDivisionBox,&burstModeBox,&pitchModeBox,&motionShapeBox,&freezeMidiModeBox,&freezeMidiNoteBox};
    const char* names[] = { "Tempo sync", "Rhythm division", "Burst mode", "Pitch mode", "Motion shape", "Freeze MIDI mode", "Freeze MIDI note" };
    for (int i = 0; i < 7; ++i) {
        components[i]->setName(names[i]);
        components[i]->setBounds(selectors.removeFromLeft(BubblesTheme::selectorWidth));
        selectors.removeFromLeft(BubblesTheme::selectorGap);
    }
    r.removeFromTop(15); r.removeFromLeft(90); auto steps=r.removeFromTop(30); int sw=steps.getWidth()/16;
    for (auto& button : rhythmStepButtons) button.setBounds(steps.removeFromLeft(sw).reduced(3,0));
}

void BubbleCloudAudioProcessorEditor::layoutControls(std::vector<std::unique_ptr<ControlBinding>>& controls,
                                                     juce::Rectangle<int> bounds, int columns)
{
    int cw=bounds.getWidth()/columns;
    for (size_t i=0; i<controls.size(); ++i) {
        auto cell=juce::Rectangle<int>(bounds.getX()+(int)i*cw,bounds.getY(),cw,bounds.getHeight()).reduced(8,3);
        controls[i]->titleLabel.setBounds(cell.removeFromBottom(24));
        int side=juce::jmin(cell.getWidth(),cell.getHeight());
        controls[i]->slider.setBounds(cell.withSizeKeepingCentre(side,side));
    }
}

void BubbleCloudAudioProcessorEditor::updateControlValue(ControlBinding& control)
{
    auto percent = juce::roundToInt((float)control.slider.getValue() * 100.0f);
    control.slider.setTooltip(control.title + " / " + control.role + " / " + juce::String(percent) + "%");
}

void BubbleCloudAudioProcessorEditor::setParameterValue(const juce::String& parameterId, float value)
{
    if (auto* parameter = audioProcessor.treeState.getParameter(parameterId))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
        parameter->endChangeGesture();
    }
}

void BubbleCloudAudioProcessorEditor::setParameterAsToggle(const juce::String& parameterId, bool enabled)
{
    setParameterValue(parameterId, enabled ? 1.0f : 0.0f);
}

void BubbleCloudAudioProcessorEditor::updateToggleControls()
{
    freezeButton.setToggleState(audioProcessor.isFreezeActive(), juce::dontSendNotification);
    if (auto* value = audioProcessor.treeState.getRawParameterValue("TEMPO_SYNC"))
        tempoSyncButton.setToggleState(value->load() >= 0.5f, juce::dontSendNotification);
    updateRhythmPatternButtons();
}

void BubbleCloudAudioProcessorEditor::updateRhythmPatternButtons()
{
    const auto* value = audioProcessor.treeState.getRawParameterValue("RHYTHM_PATTERN");
    if (value == nullptr)
        return;

    const auto pattern = (uint32_t)juce::roundToInt(juce::jlimit(0.0f, 65535.0f, value->load()));
    for (int i = 0; i < (int)rhythmStepButtons.size(); ++i) {
        const bool enabled = (pattern & (1u << i)) != 0;
        auto& button = rhythmStepButtons[(std::size_t)i];
        button.setToggleState(enabled, juce::dontSendNotification);
        button.getProperties().set("rhythmPlayhead", i == rhythmPlayheadStep);
        button.repaint();
    }
}

void BubbleCloudAudioProcessorEditor::commitRhythmPattern()
{
    uint32_t pattern = 0;
    for (int i = 0; i < (int)rhythmStepButtons.size(); ++i) {
        if (rhythmStepButtons[(std::size_t)i].getToggleState())
            pattern |= (1u << i);
    }
    setParameterValue("RHYTHM_PATTERN", (float)pattern);
}

void BubbleCloudAudioProcessorEditor::timerCallback()
{
    const auto telemetry = audioProcessor.getTelemetrySnapshot();
    rhythmPlayheadStep = telemetry.tempoSync ? telemetry.rhythmStep : -1;
    if (cloudVisualizer)
        cloudVisualizer->setTelemetry(telemetry);
    updateToggleControls();
    if (cloudVisualizer)
        cloudVisualizer->repaint();
}
