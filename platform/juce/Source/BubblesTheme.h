#pragma once
#include "BubblesFontData.h"
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

    enum class FontWeight { light, regular, bold };

    inline juce::Typeface::Ptr typeface(FontWeight weight)
    {
        const auto makeTypeface = [](BubblesFontData::Weight fontWeight) {
            const auto& font = BubblesFontData::data(fontWeight);
            return juce::Typeface::createSystemTypefaceFor(font.getData(), font.getSize());
        };
        static auto light = makeTypeface(BubblesFontData::Weight::light);
        static auto regular = makeTypeface(BubblesFontData::Weight::regular);
        static auto bold = makeTypeface(BubblesFontData::Weight::bold);
        return weight == FontWeight::light ? light : (weight == FontWeight::bold ? bold : regular);
    }

    inline juce::Font makeFont(float size, FontWeight weight, float tracking = 0.0f)
    {
        juce::Font result(typeface(weight));
        result.setHeight(size);
        result.setExtraKerningFactor(tracking);
        return result;
    }

    inline juce::Font controlFont(float size = 11.0f) { return makeFont(size, FontWeight::regular); }
    inline juce::Font regularFont(float size = 12.0f) { return makeFont(size, FontWeight::regular); }
    inline juce::Font captionFont(float size = 10.0f) { return makeFont(size, FontWeight::light, 0.035f); }
    inline juce::Font headingFont(float size = 12.0f) { return makeFont(size, FontWeight::bold, 0.075f); }
    inline juce::Font brandFont(float size = 29.0f) { return makeFont(size, FontWeight::bold, 0.13f); }

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
