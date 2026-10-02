#include "CloudVisualizer.h"

void CloudVisualizer::setTelemetry(const BubbleCloudTelemetry& next)
{
    telemetry = next;
    smoothedPeakLeft = juce::jmax(next.peakLeft, smoothedPeakLeft * 0.82f);
    smoothedPeakRight = juce::jmax(next.peakRight, smoothedPeakRight * 0.82f);
    smoothedEnvelope += 0.24f * (next.envelope - smoothedEnvelope);
    spawnPulse = juce::jlimit(0.0f, 1.0f, spawnPulse * 0.72f + (float)next.spawnCount * 0.08f);
}

void CloudVisualizer::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour(BubblesTheme::chamberDeep);
    g.fillRoundedRectangle(bounds, 8.0f);

    const float energy = juce::jlimit(0.0f, 1.0f,
                                     smoothedEnvelope * 1.8f
                                     + 0.5f * juce::jmax(smoothedPeakLeft, smoothedPeakRight));
    juce::ColourGradient wash(BubblesTheme::cyan.withAlpha(0.025f + energy * 0.08f), bounds.getX(), bounds.getY(),
                              (telemetry.frozen ? BubblesTheme::aqua : BubblesTheme::cyan).withAlpha(0.10f + energy * 0.15f),
                              bounds.getRight(), bounds.getBottom(), false);
    g.setGradientFill(wash);
    g.fillRoundedRectangle(bounds.reduced(1.0f), 8.0f);

    auto content = getLocalBounds().reduced(12);
    auto titleArea = content.removeFromTop(26);
    auto meterBounds = content.removeFromBottom(20);
    content.removeFromBottom(6);
    auto particleBounds = content.toFloat().reduced(4.0f, 2.0f);

    g.setColour(BubblesTheme::aqua.withAlpha(0.07f));
    for (int i=1; i<8; ++i) {
        float x=particleBounds.getX()+particleBounds.getWidth()*i/8;
        g.drawLine(x,particleBounds.getY(),x,particleBounds.getBottom());
    }
    g.drawLine(particleBounds.getX(), particleBounds.getCentreY(), particleBounds.getRight(), particleBounds.getCentreY());
    for (int orbit=0; orbit<3; ++orbit) {
        juce::Path path; path.addEllipse(particleBounds.reduced(20.0f+orbit*25.0f, 35.0f+orbit*6.0f));
        path.applyTransform(juce::AffineTransform::rotation((orbit-1)*0.21f, particleBounds.getCentreX(), particleBounds.getCentreY()));
        g.setColour(BubblesTheme::aqua.withAlpha(0.12f + energy*0.10f)); g.strokePath(path, juce::PathStrokeType(0.7f));
    }
    g.setColour(BubblesTheme::aqua.withAlpha(0.12f));
    g.drawVerticalLine((int)particleBounds.getCentreX(), particleBounds.getY(), particleBounds.getBottom());

    if (spawnPulse > 0.02f) {
        const float pulseSize = 24.0f + spawnPulse * juce::jmin(particleBounds.getWidth(), particleBounds.getHeight()) * 0.55f;
        g.setColour(BubblesTheme::aqua.withAlpha(0.12f * spawnPulse));
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
        // Fine grains follow the real voice phase and pan; no synthetic voice count.
        for (int grain=0; grain<12; ++grain) {
            float t=(float)grain/12.0f;
            float gx=x+std::sin((float)i*1.73f+grain*2.4f+phase*6)* (22.0f+voice.gain*45.0f);
            float gy=y+std::cos((float)i*2.1f+grain*1.7f+phase*8)* (12.0f+voice.gain*22.0f);
            g.setColour(BubblesTheme::aqua.withAlpha((1.0f-t)*0.38f));
            g.fillEllipse(gx,gy,1.2f+t,1.2f+t);
        }
        const float shapedGain = std::sqrt(juce::jlimit(0.0f, 1.0f, voice.gain));
        const float size = 5.0f + shapedGain * 15.0f + (voice.bubbleClass == 0 ? 3.5f : 0.0f);
        const float alpha = juce::jlimit(0.24f, 0.92f, 0.34f + shapedGain * 0.58f);
        const auto pitchColour = voice.pitchRate > 1.1f ? BubblesTheme::aqua
            : (voice.pitchRate < 0.9f ? BubblesTheme::lowPitch : BubblesTheme::cyan);
        const auto colour = voice.reverse ? BubblesTheme::aqua.brighter(0.15f) : pitchColour;

        if (telemetry.frozen) {
            g.setColour(colour.withAlpha(alpha * 0.42f));
            g.drawEllipse(x - size * 0.72f, y - size * 0.72f, size * 1.44f, size * 1.44f, 1.0f);
        }
        g.setGradientFill(juce::ColourGradient(colour.withAlpha(alpha*0.30f), x, y, colour.withAlpha(0.0f), x+size*2.0f, y, true));
        g.fillEllipse(x-size*2, y-size*2, size*4, size*4);
        g.setGradientFill(juce::ColourGradient(BubblesTheme::ink.interpolatedWith(colour, 0.65f).withAlpha(alpha), x-size*0.2f, y-size*0.25f, colour.withAlpha(alpha*0.18f), x+size*0.4f, y+size*0.5f, false));
        g.fillEllipse(x - size * 0.5f, y - size * 0.5f, size, size);
        ++renderedVoices;
    }

    if (renderedVoices == 0) {
        g.setColour(BubblesTheme::textMuted.withAlpha(0.34f));
        g.setFont(juce::Font(10.5f, juce::Font::bold));
        g.drawText("WAITING FOR AUDIO", particleBounds.toNearestInt(), juce::Justification::centred);
    }

    auto leftMeter = meterBounds.removeFromLeft(meterBounds.getWidth()/2);
    drawMeter(g, leftMeter.withTrimmedRight(14), "L OUT", std::sqrt(juce::jlimit(0.0f, 1.0f, smoothedPeakLeft)), BubblesTheme::cyan);
    drawMeter(g, meterBounds, "R OUT", std::sqrt(juce::jlimit(0.0f, 1.0f, smoothedPeakRight)), BubblesTheme::aqua);

    g.setColour(BubblesTheme::textMuted);
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    const auto stateText = telemetry.frozen ? "CLOUD CHAMBER / FROZEN"
        : (telemetry.activeVoices > 0 ? "CLOUD CHAMBER / " + engineStateName(telemetry.engineState) : "CLOUD CHAMBER / IDLE");
    g.drawText(stateText, titleArea.removeFromLeft(titleArea.getWidth() * 3 / 4), juce::Justification::centredLeft);
    g.drawText("VOICES " + juce::String(telemetry.activeVoices) + "/"
                   + juce::String(telemetry.activeVoiceLimit),
               titleArea, juce::Justification::centredRight);
}

juce::String CloudVisualizer::engineStateName(int state)
{
    switch (state) {
        case ENGINE_STATE_TRANSIENT_BURST: return "BURST CLOUD";
        case ENGINE_STATE_ATTACK_ONGOING: return "ATTACK CLOUD";
        case ENGINE_STATE_SUSTAIN_BODY: return "SUSTAIN CLOUD";
        case ENGINE_STATE_SPARSE_DECAY: return "DECAY CLOUD";
        default: return "LIVE CLOUD";
    }
}

void CloudVisualizer::drawMeter(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& label,
               float value, juce::Colour colour)
{
    auto labelArea = bounds.removeFromLeft(52);
    g.setColour(BubblesTheme::textMuted);
    g.setFont(10.5f);
    g.drawText(label, labelArea, juce::Justification::centredLeft);

    auto track = bounds.toFloat().reduced(0.0f, 4.0f);
    g.setColour(BubblesTheme::meterTrack);
    g.fillRoundedRectangle(track, 4.0f);
    g.setColour(colour.withAlpha(0.86f));
    g.fillRoundedRectangle(track.withWidth(track.getWidth() * juce::jlimit(0.0f, 1.0f, value)), 4.0f);
}
