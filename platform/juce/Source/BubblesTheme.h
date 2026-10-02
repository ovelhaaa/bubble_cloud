#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace BubblesTheme
{
    inline const juce::Colour background { 0xff0c0d0c };
    inline const juce::Colour panel { 0xff181a18 }, panelRaised { 0xff2a2c29 };
    inline const juce::Colour ink { 0xffeee9dd }, textMuted { 0xffa6a398 };
    inline const juce::Colour stroke { 0xff41433d }, amber { 0xffefba5a };
    inline const juce::Colour cyan { 0xff78bebb }, aqua { 0xff91d6cf };
    inline const juce::Colour buttonSurface { 0xff222321 };
    inline const juce::Colour knobEdge { 0xff55554e }, knobTrack { 0xff353630 };
    inline const juce::Colour chamberDeep { 0xff090f0f }, lowPitch { 0xff83b9b4 };
    inline const juce::Colour meterTrack { 0xff252925 };

    constexpr int editorWidth = 1160, editorHeight = 900;
    constexpr int outerMargin = 10, panelGap = 3;
    constexpr int selectorWidth = 151, selectorGap = 8;

    inline juce::Font font(float size, bool bold = false)
    {
        auto result = juce::Font(size, bold ? juce::Font::bold : juce::Font::plain);
        result.setExtraKerningFactor(bold ? 0.10f : 0.035f);
        return result;
    }

    // Shared rectangles keep panel painting and child placement in agreement.
    struct Layout
    {
        juce::Rectangle<int> header, macros, chamber, performance, tonal, rhythm;

        explicit Layout(juce::Rectangle<int> bounds)
        {
            bounds = bounds.reduced(outerMargin);
            header = bounds.removeFromTop(94);
            bounds.removeFromTop(panelGap);
            macros = bounds.removeFromTop(170);
            bounds.removeFromTop(panelGap);
            auto centre = bounds.removeFromTop(280);
            performance = centre.removeFromRight(318);
            centre.removeFromRight(panelGap);
            chamber = centre;
            bounds.removeFromTop(panelGap);
            tonal = bounds.removeFromTop(167);
            bounds.removeFromTop(panelGap);
            rhythm = bounds;
        }
    };
}
