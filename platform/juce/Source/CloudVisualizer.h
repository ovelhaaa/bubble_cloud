#pragma once
#include <cmath>
#include "BubblesTheme.h"
#include "PluginProcessor.h"
class CloudVisualizer : public juce::Component
{
public:
    CloudVisualizer() = default;

    void setTelemetry(const BubbleCloudTelemetry& next);

    void paint(juce::Graphics& g) override;

private:
    static juce::String engineStateName(int state);

    void drawMeter(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& label,
                   float value, juce::Colour colour);

    BubbleCloudTelemetry telemetry;
    float smoothedPeakLeft = 0.0f;
    float smoothedPeakRight = 0.0f;
    float smoothedEnvelope = 0.0f;
    float spawnPulse = 0.0f;
};

