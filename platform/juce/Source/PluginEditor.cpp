#include "PluginEditor.h"

#include <array>
#include <cmath>

namespace
{
    constexpr int editorWidth = 1080;
    constexpr int editorHeight = 760;

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
        {
            "Pick Halo",
            1,
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
            0,
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
            1,
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
            0,
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
            1,
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
            2,
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
            1,
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
            2,
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
            1,
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
            1,
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
            2,
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
            2,
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
            2,
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
            1,
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
            1,
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
            1,
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
        const bool rhythmPlayhead = (bool)button.getProperties().getWithDefault("rhythmPlayhead", false);
        auto fill = rhythmPlayhead ? amber.withAlpha(0.28f)
                                   : (active ? cyan.withAlpha(0.22f) : juce::Colour(0xff172631));
        if (shouldDrawButtonAsHighlighted)
            fill = fill.brighter(0.12f);

        g.setColour(fill);
        g.fillRoundedRectangle(bounds, 6.0f);
        g.setColour(rhythmPlayhead ? amber.withAlpha(0.92f)
                                   : (active ? cyan.withAlpha(0.78f) : stroke));
        g.drawRoundedRectangle(bounds, 6.0f, 1.0f);
    }
};

class BubbleCloudAudioProcessorEditor::CloudVisualizer : public juce::Component
{
public:
    CloudVisualizer() = default;

    void setTelemetry(const BubbleCloudTelemetry& next)
    {
        telemetry = next;
        smoothedPeakLeft = juce::jmax(next.peakLeft, smoothedPeakLeft * 0.82f);
        smoothedPeakRight = juce::jmax(next.peakRight, smoothedPeakRight * 0.82f);
        smoothedEnvelope += 0.24f * (next.envelope - smoothedEnvelope);
        spawnPulse = juce::jlimit(0.0f, 1.0f, spawnPulse * 0.72f + (float)next.spawnCount * 0.08f);
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour(juce::Colour(0xff081018));
        g.fillRoundedRectangle(bounds, 8.0f);

        const float energy = juce::jlimit(0.0f, 1.0f,
                                         smoothedEnvelope * 1.8f
                                         + 0.5f * juce::jmax(smoothedPeakLeft, smoothedPeakRight));
        juce::ColourGradient wash(cyan.withAlpha(0.025f + energy * 0.08f), bounds.getX(), bounds.getY(),
                                  (telemetry.frozen ? aqua : cyan).withAlpha(0.10f + energy * 0.15f),
                                  bounds.getRight(), bounds.getBottom(), false);
        g.setGradientFill(wash);
        g.fillRoundedRectangle(bounds.reduced(1.0f), 8.0f);

        auto content = getLocalBounds().reduced(18);
        auto titleArea = content.removeFromTop(26);
        auto meterBounds = content.removeFromBottom(50);
        content.removeFromBottom(8);
        auto particleBounds = content.toFloat().reduced(4.0f, 2.0f);

        g.setColour(stroke.withAlpha(0.35f));
        g.drawVerticalLine((int)particleBounds.getCentreX(), particleBounds.getY(), particleBounds.getBottom());

        if (spawnPulse > 0.02f) {
            const float pulseSize = 24.0f + spawnPulse * juce::jmin(particleBounds.getWidth(), particleBounds.getHeight()) * 0.55f;
            g.setColour(aqua.withAlpha(0.12f * spawnPulse));
            g.drawEllipse(particleBounds.getCentreX() - pulseSize * 0.5f,
                          particleBounds.getCentreY() - pulseSize * 0.5f,
                          pulseSize, pulseSize, 1.5f);
        }

        int renderedVoices = 0;
        for (std::size_t i = 0; i < telemetry.voices.size(); ++i) {
            const auto& voice = telemetry.voices[i];
            if (!voice.active)
                continue;

            const float phase = juce::jlimit(0.0f, 1.0f, voice.phase);
            const float jitter = std::sin((float)i * 2.173f + phase * 9.0f) * particleBounds.getWidth() * 0.025f;
            const float x = particleBounds.getCentreX()
                + voice.pan * particleBounds.getWidth() * 0.43f + jitter;
            const float classOffset = ((float)voice.bubbleClass - 1.0f) * particleBounds.getHeight() * 0.045f;
            const float y = particleBounds.getY()
                + (0.08f + phase * 0.84f) * particleBounds.getHeight() + classOffset;
            const float shapedGain = std::sqrt(juce::jlimit(0.0f, 1.0f, voice.gain));
            const float size = 3.0f + shapedGain * 7.5f + (voice.bubbleClass == 0 ? 1.5f : 0.0f);
            const float alpha = juce::jlimit(0.24f, 0.92f, 0.34f + shapedGain * 0.58f);
            const auto pitchColour = voice.pitchRate > 1.1f ? aqua
                : (voice.pitchRate < 0.9f ? juce::Colour(0xffa995ff) : cyan);
            const auto colour = voice.reverse ? amber : pitchColour;

            if (telemetry.frozen) {
                g.setColour(colour.withAlpha(alpha * 0.42f));
                g.drawEllipse(x - size * 0.72f, y - size * 0.72f, size * 1.44f, size * 1.44f, 1.0f);
            }
            g.setColour(colour.withAlpha(alpha));
            g.fillEllipse(x - size * 0.5f, y - size * 0.5f, size, size);
            ++renderedVoices;
        }

        if (renderedVoices == 0) {
            g.setColour(textMuted.withAlpha(0.34f));
            g.setFont(juce::Font(10.5f, juce::Font::bold));
            g.drawText("WAITING FOR AUDIO", particleBounds.toNearestInt(), juce::Justification::centred);
        }

        drawMeter(g, meterBounds.removeFromTop(16), "L OUT", std::sqrt(juce::jlimit(0.0f, 1.0f, smoothedPeakLeft)), cyan);
        meterBounds.removeFromTop(8);
        drawMeter(g, meterBounds.removeFromTop(16), "R OUT", std::sqrt(juce::jlimit(0.0f, 1.0f, smoothedPeakRight)), aqua);

        g.setColour(textMuted);
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        const auto stateText = telemetry.frozen ? "FROZEN CLOUD"
            : (telemetry.activeVoices > 0 ? engineStateName(telemetry.engineState) : "IDLE CLOUD");
        g.drawText(stateText, titleArea.removeFromLeft(titleArea.getWidth() / 2), juce::Justification::centredLeft);
        g.drawText("VOICES " + juce::String(telemetry.activeVoices) + "/"
                       + juce::String(telemetry.activeVoiceLimit),
                   titleArea, juce::Justification::centredRight);
    }

private:
    static juce::String engineStateName(int state)
    {
        switch (state) {
            case ENGINE_STATE_TRANSIENT_BURST: return "BURST CLOUD";
            case ENGINE_STATE_ATTACK_ONGOING: return "ATTACK CLOUD";
            case ENGINE_STATE_SUSTAIN_BODY: return "SUSTAIN CLOUD";
            case ENGINE_STATE_SPARSE_DECAY: return "DECAY CLOUD";
            default: return "LIVE CLOUD";
        }
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

    BubbleCloudTelemetry telemetry;
    float smoothedPeakLeft = 0.0f;
    float smoothedPeakRight = 0.0f;
    float smoothedEnvelope = 0.0f;
    float spawnPulse = 0.0f;
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

    morphSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    morphSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 22);
    morphSlider.setNumDecimalPlacesToDisplay(2);
    morphSlider.setColour(juce::Slider::trackColourId, cyan);
    morphSlider.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff1a2833));
    morphAttachment = std::make_unique<SliderAttachment>(audioProcessor.treeState, "MORPH", morphSlider);
    addAndMakeVisible(morphSlider);

    morphLabel.setText("SCENE A  <  MORPH  >  SCENE B", juce::dontSendNotification);
    morphLabel.setJustificationType(juce::Justification::centred);
    morphLabel.setColour(juce::Label::textColourId, textMuted);
    morphLabel.setFont(juce::Font(10.5f, juce::Font::bold));
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

    auto content = getLocalBounds().reduced(22);
    auto rhythm = content.removeFromBottom(156);
    content.removeFromBottom(16);
    auto right = content.removeFromRight(280);
    content.removeFromRight(16);
    content.removeFromTop(82);
    auto secondary = content.removeFromBottom(142);
    content.removeFromBottom(16);

    drawPanel(g, content);
    drawPanel(g, secondary);
    drawPanel(g, right);
    drawPanel(g, rhythm);

    auto performanceLabel = right.reduced(18);
    auto performanceArea = performanceLabel.removeFromBottom(150);
    performanceArea.removeFromTop(2);
    g.setColour(textMuted);
    g.setFont(juce::Font(10.5f, juce::Font::bold));
    g.drawText("PERFORMANCE", performanceArea.removeFromTop(18), juce::Justification::centredLeft);

    auto rhythmLabel = rhythm.reduced(18);
    g.setColour(textMuted);
    g.setFont(juce::Font(10.5f, juce::Font::bold));
    g.drawText("RHYTHM LAB  /  16 STEP PATTERN", rhythmLabel.removeFromTop(18), juce::Justification::centredLeft);
}

void BubbleCloudAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced(22);
    auto header = bounds.removeFromTop(64);

    header.removeFromLeft(310);
    presetBox.setBounds(header.removeFromLeft(220).reduced(0, 10));
    header.removeFromLeft(10);
    qualityBox.setBounds(header.removeFromLeft(150).reduced(0, 10));

    auto content = getLocalBounds().reduced(22);
    auto rhythm = content.removeFromBottom(156);
    content.removeFromBottom(16);
    auto right = content.removeFromRight(280);
    content.removeFromRight(16);
    content.removeFromTop(82);
    auto secondary = content.removeFromBottom(142);
    content.removeFromBottom(16);

    auto rightContent = right.reduced(18);
    auto performanceArea = rightContent.removeFromBottom(150);
    rightContent.removeFromBottom(12);
    if (cloudVisualizer)
        cloudVisualizer->setBounds(rightContent);

    performanceArea.removeFromTop(20);
    auto freezeRow = performanceArea.removeFromTop(34);
    freezeButton.setBounds(freezeRow.removeFromLeft(freezeRow.getWidth() / 2).reduced(0, 2));
    freezeRow.removeFromLeft(8);
    captureButton.setBounds(freezeRow.reduced(0, 2));

    auto storeRow = performanceArea.removeFromTop(32);
    storeSceneAButton.setBounds(storeRow.removeFromLeft(storeRow.getWidth() / 2).reduced(0, 3));
    storeRow.removeFromLeft(8);
    storeSceneBButton.setBounds(storeRow.reduced(0, 3));
    morphLabel.setBounds(performanceArea.removeFromTop(20));
    morphSlider.setBounds(performanceArea.reduced(0, 1));

    layoutControls(macroControls, content.reduced(24, 22), 3);

    auto secondaryContent = secondary.reduced(20, 20);
    layoutControls(secondaryControls, secondaryContent, 5);

    auto rhythmContent = rhythm.reduced(18);
    rhythmContent.removeFromTop(22);
    auto selectorRow = rhythmContent.removeFromTop(42);
    const int selectorGap = 8;
    tempoSyncButton.setBounds(selectorRow.removeFromLeft(104).reduced(0, 3));
    selectorRow.removeFromLeft(selectorGap);
    rhythmDivisionBox.setBounds(selectorRow.removeFromLeft(112).reduced(0, 3));
    selectorRow.removeFromLeft(selectorGap);
    burstModeBox.setBounds(selectorRow.removeFromLeft(138).reduced(0, 3));
    selectorRow.removeFromLeft(selectorGap);
    pitchModeBox.setBounds(selectorRow.removeFromLeft(136).reduced(0, 3));
    selectorRow.removeFromLeft(selectorGap);
    motionShapeBox.setBounds(selectorRow.removeFromLeft(142).reduced(0, 3));
    selectorRow.removeFromLeft(selectorGap);
    freezeMidiModeBox.setBounds(selectorRow.removeFromLeft(136).reduced(0, 3));
    selectorRow.removeFromLeft(selectorGap);
    freezeMidiNoteBox.setBounds(selectorRow.reduced(0, 3));

    rhythmContent.removeFromTop(10);
    auto stepsRow = rhythmContent.removeFromTop(42);
    const int stepWidth = stepsRow.getWidth() / (int)rhythmStepButtons.size();
    for (int i = 0; i < (int)rhythmStepButtons.size(); ++i)
        rhythmStepButtons[(std::size_t)i].setBounds(stepsRow.removeFromLeft(stepWidth).reduced(2, 2));
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
