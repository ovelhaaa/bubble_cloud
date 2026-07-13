#include "PluginEditor.h"

#include <array>
#include <cmath>

namespace
{
    constexpr int editorWidth = 980;
    constexpr int editorHeight = 580;

    const juce::Colour ink = juce::Colour(0xffeef7ff);
    const juce::Colour textMuted = juce::Colour(0xff8fa7b7);
    const juce::Colour panel = juce::Colour(0xff101922);
    const juce::Colour panelRaised = juce::Colour(0xff14212c);
    const juce::Colour stroke = juce::Colour(0xff243746);
    const juce::Colour cyan = juce::Colour(0xff55d7ff);
    const juce::Colour aqua = juce::Colour(0xff58ffd0);
    const juce::Colour amber = juce::Colour(0xffffb45e);

    struct MacroSetting
    {
        const char* parameterId;
        float value;
    };

    struct FactoryPreset
    {
        const char* name;
        int qualityProfile;
        std::array<MacroSetting, 12> macros;
    };

    const std::array<FactoryPreset, 4> factoryPresets {{
        {
            "Neutral",
            2,
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
            1,
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
            1,
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
            2,
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
    }};

    juce::Rectangle<float> asFloat(juce::Rectangle<int> bounds)
    {
        return bounds.toFloat();
    }

    void drawPanel(juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        auto r = asFloat(bounds);
        g.setColour(panel);
        g.fillRoundedRectangle(r, 8.0f);

        g.setColour(stroke.withAlpha(0.85f));
        g.drawRoundedRectangle(r.reduced(0.5f), 8.0f, 1.0f);
    }

    void drawBubblesMark(juce::Graphics& g, juce::Rectangle<float> bounds, float intensity)
    {
        g.setColour(juce::Colour(0xff020f12));
        g.fillRoundedRectangle(bounds, 9.0f);

        auto centre = bounds.getCentre();
        auto scale = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.42f;
        const float goldenAngle = 2.39996323f;
        const int count = 78;

        for (int i = 0; i < count; ++i)
        {
            auto t = (float)i / (float)(count - 1);
            auto radius = scale * std::sqrt(t);
            auto angle = (float)i * goldenAngle + 0.28f * std::sin(t * 8.0f);
            auto wobble = 1.0f + 0.08f * std::sin((float)i * 0.73f);
            auto x = centre.x + std::cos(angle) * radius * wobble;
            auto y = centre.y + std::sin(angle) * radius * (0.78f + 0.18f * intensity);
            auto dot = 2.2f + 4.4f * (1.0f - t) + 1.2f * std::sin((float)i * 1.11f);
            auto alpha = 0.26f + 0.72f * (1.0f - t * 0.58f);

            g.setColour(juce::Colour::fromHSV(0.47f + t * 0.08f, 0.95f, 0.95f, alpha));
            g.fillEllipse(x - dot * 0.5f, y - dot * 0.5f, dot, dot);
        }

        g.setColour(cyan.withAlpha(0.16f));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 9.0f, 1.0f);
    }
}

class BubbleCloudAudioProcessorEditor::BubblesLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BubblesLookAndFeel()
    {
        setColour(juce::ComboBox::backgroundColourId, panelRaised);
        setColour(juce::ComboBox::outlineColourId, stroke);
        setColour(juce::ComboBox::textColourId, ink);
        setColour(juce::PopupMenu::backgroundColourId, panelRaised);
        setColour(juce::PopupMenu::textColourId, ink);
        setColour(juce::TextButton::buttonColourId, juce::Colour(0xff172631));
        setColour(juce::TextButton::buttonOnColourId, cyan.withAlpha(0.28f));
        setColour(juce::TextButton::textColourOffId, ink);
        setColour(juce::TextButton::textColourOnId, ink);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider&) override
    {
        auto rawBounds = juce::Rectangle<float>((float)x, (float)y, (float)width, (float)height).reduced(4.0f);
        auto side = juce::jmin(rawBounds.getWidth(), rawBounds.getHeight());
        auto bounds = rawBounds.withSizeKeepingCentre(side, side);
        auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
        auto centre = bounds.getCentre();
        auto lineW = juce::jmax(3.0f, radius * 0.08f);
        auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        g.setColour(juce::Colour(0xff0a1016));
        g.fillEllipse(bounds.reduced(radius * 0.14f));

        g.setColour(juce::Colour(0xff21313d));
        g.drawEllipse(bounds.reduced(radius * 0.11f), 1.0f);

        juce::Path backgroundArc;
        backgroundArc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f,
                                    rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(juce::Colour(0xff283b48));
        g.strokePath(backgroundArc, juce::PathStrokeType(lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path valueArc;
        valueArc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f,
                               rotaryStartAngle, angle, true);
        juce::ColourGradient glow(cyan, bounds.getX(), bounds.getY(), aqua, bounds.getRight(), bounds.getBottom(), false);
        g.setGradientFill(glow);
        g.strokePath(valueArc, juce::PathStrokeType(lineW + 1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        auto pointerLength = radius * 0.58f;
        auto pointerThickness = juce::jmax(2.0f, radius * 0.035f);
        juce::Path pointer;
        pointer.addRoundedRectangle(-pointerThickness * 0.5f, -pointerLength, pointerThickness, pointerLength, pointerThickness);
        pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));
        g.setColour(ink.withAlpha(0.92f));
        g.fillPath(pointer);

        g.setColour(cyan.withAlpha(0.12f));
        g.fillEllipse(bounds.reduced(radius * 0.42f));
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&,
                              bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        auto active = button.getToggleState() || shouldDrawButtonAsDown;
        auto fill = active ? cyan.withAlpha(0.22f) : juce::Colour(0xff172631);
        if (shouldDrawButtonAsHighlighted)
            fill = fill.brighter(0.12f);

        g.setColour(fill);
        g.fillRoundedRectangle(bounds, 6.0f);
        g.setColour(active ? cyan.withAlpha(0.78f) : stroke);
        g.drawRoundedRectangle(bounds, 6.0f, 1.0f);
    }
};

class BubbleCloudAudioProcessorEditor::CloudVisualizer : public juce::Component
{
public:
    explicit CloudVisualizer(BubbleCloudAudioProcessor& owner) : processor(owner) {}

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour(juce::Colour(0xff081018));
        g.fillRoundedRectangle(bounds, 8.0f);

        auto density = readParam("DENSITY");
        auto motion = readParam("MOTION");
        auto space = readParam("SPACE");
        auto freeze = readParam("FREEZE");
        auto energy = juce::jlimit(0.12f, 1.0f, density * 0.72f + freeze * 0.28f);
        auto now = (float)juce::Time::getMillisecondCounterHiRes() * 0.001f;
        auto centre = bounds.getCentre();
        auto count = 38 + (int)std::round(density * 42.0f);

        juce::ColourGradient wash(cyan.withAlpha(0.05f), bounds.getX(), bounds.getY(),
                                  aqua.withAlpha(0.16f + freeze * 0.12f), bounds.getRight(), bounds.getBottom(), false);
        g.setGradientFill(wash);
        g.fillRoundedRectangle(bounds.reduced(1.0f), 8.0f);

        for (int i = 0; i < count; ++i)
        {
            auto phase = (float)i * 0.618f;
            auto orbit = 0.18f + 0.78f * std::fmod((float)i * 0.173f, 1.0f);
            auto drift = now * (0.12f + motion * 0.58f) + phase;
            auto x = centre.x + std::sin(drift * 1.7f + phase) * bounds.getWidth() * (0.12f + space * 0.32f) * orbit;
            auto y = centre.y + std::cos(drift * 1.13f + phase * 0.7f) * bounds.getHeight() * (0.10f + density * 0.28f) * orbit;
            auto size = 1.8f + 5.2f * std::fmod((float)i * 0.391f + energy, 1.0f);
            auto alpha = 0.20f + 0.55f * std::fmod((float)i * 0.277f + energy, 1.0f);

            g.setColour((i % 5 == 0 ? amber : cyan).withAlpha(alpha));
            g.fillEllipse(x - size * 0.5f, y - size * 0.5f, size, size);
        }

        auto meterBounds = getLocalBounds().reduced(18).removeFromBottom(50);
        drawMeter(g, meterBounds.removeFromTop(16), "CLOUD", energy, cyan);
        meterBounds.removeFromTop(8);
        drawMeter(g, meterBounds.removeFromTop(16), "WIDTH", space, aqua);

        g.setColour(textMuted);
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        g.drawText(freeze > 0.5f ? "FROZEN CLOUD" : "LIVE CLOUD", getLocalBounds().reduced(18).removeFromTop(26),
                   juce::Justification::centredLeft);
    }

private:
    float readParam(const char* parameterId) const
    {
        if (auto* value = processor.treeState.getRawParameterValue(parameterId))
            return juce::jlimit(0.0f, 1.0f, value->load());
        return 0.0f;
    }

    void drawMeter(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& label,
                   float value, juce::Colour colour)
    {
        auto labelArea = bounds.removeFromLeft(52);
        g.setColour(textMuted);
        g.setFont(10.5f);
        g.drawText(label, labelArea, juce::Justification::centredLeft);

        auto track = bounds.toFloat().reduced(0.0f, 4.0f);
        g.setColour(juce::Colour(0xff1a2833));
        g.fillRoundedRectangle(track, 4.0f);
        g.setColour(colour.withAlpha(0.86f));
        g.fillRoundedRectangle(track.withWidth(track.getWidth() * juce::jlimit(0.0f, 1.0f, value)), 4.0f);
    }

    BubbleCloudAudioProcessor& processor;
};

BubbleCloudAudioProcessorEditor::BubbleCloudAudioProcessorEditor(BubbleCloudAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    bubblesLookAndFeel = std::make_unique<BubblesLookAndFeel>();
    setLookAndFeel(bubblesLookAndFeel.get());

    for (int i = 0; i < (int)factoryPresets.size(); ++i)
        presetBox.addItem(factoryPresets[(size_t)i].name, i + 1);
    presetBox.setSelectedId(1, juce::dontSendNotification);
    presetBox.onChange = [this] { applyPreset(presetBox.getSelectedItemIndex()); };
    addAndMakeVisible(presetBox);

    qualityBox.addItem("Eco", 1);
    qualityBox.addItem("Balanced", 2);
    qualityBox.addItem("Studio", 3);
    qualityBox.addItem("Ultra", 4);
    qualityAttachment = std::make_unique<ComboBoxAttachment>(audioProcessor.treeState, "QUALITY_PROFILE", qualityBox);
    addAndMakeVisible(qualityBox);

    freezeButton.setClickingTogglesState(true);
    freezeButton.onClick = [this] { setParameterAsToggle("FREEZE", freezeButton.getToggleState()); };
    addAndMakeVisible(freezeButton);

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

    cloudVisualizer = std::make_unique<CloudVisualizer>(audioProcessor);
    addAndMakeVisible(*cloudVisualizer);

    setSize(editorWidth, editorHeight);
    setResizable(false, false);
    startTimerHz(30);
}

BubbleCloudAudioProcessorEditor::~BubbleCloudAudioProcessorEditor()
{
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

    control->slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    control->slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    control->slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.18f,
                                        juce::MathConstants<float>::pi * 2.82f,
                                        true);
    control->slider.setColour(juce::Slider::rotarySliderFillColourId, cyan);
    control->slider.setColour(juce::Slider::rotarySliderOutlineColourId, stroke);

    control->titleLabel.setText(title, juce::dontSendNotification);
    control->titleLabel.setJustificationType(juce::Justification::centred);
    control->titleLabel.setColour(juce::Label::textColourId, ink);
    control->titleLabel.setFont(juce::Font(15.0f, juce::Font::bold));

    control->roleLabel.setText(role.toUpperCase(), juce::dontSendNotification);
    control->roleLabel.setJustificationType(juce::Justification::centred);
    control->roleLabel.setColour(juce::Label::textColourId, textMuted);
    control->roleLabel.setFont(juce::Font(10.5f, juce::Font::bold));

    control->valueLabel.setJustificationType(juce::Justification::centred);
    control->valueLabel.setColour(juce::Label::textColourId, cyan);
    control->valueLabel.setFont(juce::Font(13.0f, juce::Font::bold));

    addAndMakeVisible(control->slider);
    addAndMakeVisible(control->titleLabel);
    addAndMakeVisible(control->roleLabel);
    addAndMakeVisible(control->valueLabel);

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
    for (const auto& macro : preset.macros)
        setParameterValue(macro.parameterId, macro.value);

    setParameterValue("QUALITY_PROFILE", (float)preset.qualityProfile);
    updateToggleControls();
}

void BubbleCloudAudioProcessorEditor::paint(juce::Graphics& g)
{
    juce::ColourGradient background(juce::Colour(0xff05090d), 0.0f, 0.0f,
                                    juce::Colour(0xff0d1a23), (float)getWidth(), (float)getHeight(), false);
    g.setGradientFill(background);
    g.fillAll();

    auto bounds = getLocalBounds().reduced(22);
    auto header = bounds.removeFromTop(64);
    drawBubblesMark(g, header.removeFromLeft(58).toFloat().reduced(4.0f), 0.82f);
    header.removeFromLeft(10);
    g.setColour(ink);
    g.setFont(juce::Font(30.0f, juce::Font::bold));
    auto titleBlock = header.removeFromLeft(210);
    g.drawText("Bubbles", titleBlock.withTrimmedBottom(22), juce::Justification::centredLeft);

    g.setColour(textMuted);
    g.setFont(12.0f);
    g.drawText("Granular performance instrument", 92, 84, 250, 18, juce::Justification::centredLeft);

    auto status = getLocalBounds().reduced(22).removeFromTop(64).removeFromRight(170);
    g.setColour(aqua.withAlpha(0.14f));
    g.fillRoundedRectangle(status.toFloat().reduced(0, 10), 6.0f);
    g.setColour(aqua);
    g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.drawText("ENGINE READY", status.reduced(12, 0), juce::Justification::centred);

    auto content = getLocalBounds().reduced(22);
    content.removeFromTop(82);
    auto right = content.removeFromRight(250);
    content.removeFromRight(16);
    auto secondary = content.removeFromBottom(142);
    content.removeFromBottom(16);

    drawPanel(g, content);
    drawPanel(g, secondary);
    drawPanel(g, right);
}

void BubbleCloudAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced(22);
    auto header = bounds.removeFromTop(64);

    header.removeFromLeft(310);
    presetBox.setBounds(header.removeFromLeft(220).reduced(0, 10));
    header.removeFromLeft(10);
    qualityBox.setBounds(header.removeFromLeft(150).reduced(0, 10));

    bounds.removeFromTop(18);
    auto right = bounds.removeFromRight(250);
    bounds.removeFromRight(16);
    auto secondary = bounds.removeFromBottom(142);
    bounds.removeFromBottom(16);

    if (cloudVisualizer)
        cloudVisualizer->setBounds(right.reduced(18, 18));

    layoutControls(macroControls, bounds.reduced(24, 22), 3);

    auto secondaryContent = secondary.reduced(20, 20);
    auto freezeArea = secondaryContent.removeFromLeft(118).reduced(6, 20);
    freezeButton.setBounds(freezeArea);
    secondaryContent.removeFromLeft(14);
    layoutControls(secondaryControls, secondaryContent, 5);
}

void BubbleCloudAudioProcessorEditor::layoutControls(std::vector<std::unique_ptr<ControlBinding>>& controls,
                                                     juce::Rectangle<int> bounds,
                                                     int columns)
{
    if (controls.empty())
        return;

    auto rows = (int)std::ceil((double)controls.size() / (double)columns);
    auto cellW = bounds.getWidth() / columns;
    auto cellH = bounds.getHeight() / rows;

    for (size_t i = 0; i < controls.size(); ++i)
    {
        auto col = (int)i % columns;
        auto row = (int)i / columns;
        auto cell = juce::Rectangle<int>(bounds.getX() + col * cellW, bounds.getY() + row * cellH, cellW, cellH).reduced(6);
        auto& control = *controls[i];

        control.titleLabel.setBounds(cell.removeFromTop(22));
        control.roleLabel.setBounds(cell.removeFromBottom(16));
        control.valueLabel.setBounds(cell.removeFromBottom(22));
        auto knobSide = juce::jmin(cell.getWidth(), cell.getHeight());
        control.slider.setBounds(cell.withSizeKeepingCentre(knobSide, knobSide).reduced(1));
    }
}

void BubbleCloudAudioProcessorEditor::updateControlValue(ControlBinding& control)
{
    auto percent = juce::roundToInt((float)control.slider.getValue() * 100.0f);
    control.valueLabel.setText(juce::String(percent) + "%", juce::dontSendNotification);
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
    if (auto* value = audioProcessor.treeState.getRawParameterValue("FREEZE"))
        freezeButton.setToggleState(value->load() >= 0.5f, juce::dontSendNotification);
}

void BubbleCloudAudioProcessorEditor::timerCallback()
{
    updateToggleControls();
    if (cloudVisualizer)
        cloudVisualizer->repaint();
}
