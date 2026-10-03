#include "BubblesLookAndFeel.h"

BubblesLookAndFeel::BubblesLookAndFeel()
{
    setColour(juce::ComboBox::backgroundColourId, BubblesTheme::panelRaised);
    setColour(juce::ComboBox::outlineColourId, BubblesTheme::stroke);
    setColour(juce::ComboBox::textColourId, BubblesTheme::ink);
    setColour(juce::PopupMenu::backgroundColourId, BubblesTheme::panelRaised);
    setColour(juce::PopupMenu::textColourId, BubblesTheme::ink);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, BubblesTheme::amber.withAlpha(0.20f));
    setColour(juce::PopupMenu::highlightedTextColourId, BubblesTheme::ink);
    setColour(juce::Slider::thumbColourId, BubblesTheme::amber);
    setColour(juce::TooltipWindow::backgroundColourId, BubblesTheme::panelRaised);
    setColour(juce::TooltipWindow::textColourId, BubblesTheme::ink);
    setColour(juce::TooltipWindow::outlineColourId, BubblesTheme::stroke);
    setColour(juce::TextButton::buttonColourId, BubblesTheme::buttonSurface);
    setColour(juce::TextButton::buttonOnColourId, BubblesTheme::amber.withAlpha(0.28f));
    setColour(juce::TextButton::textColourOffId, BubblesTheme::ink);
    setColour(juce::TextButton::textColourOnId, BubblesTheme::ink);
}

void BubblesLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                      float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                      juce::Slider&)
{
    auto rawBounds = juce::Rectangle<float>((float)x, (float)y, (float)width, (float)height).reduced(4.0f);
    auto side = juce::jmin(rawBounds.getWidth(), rawBounds.getHeight());
    auto bounds = rawBounds.withSizeKeepingCentre(side, side).reduced(7.0f);
    auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    auto centre = bounds.getCentre();
    auto lineW = juce::jmax(1.8f, radius * 0.045f);
    auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    for (int tick = 0; tick <= 32; ++tick) {
        const float t = rotaryStartAngle + (rotaryEndAngle - rotaryStartAngle) * tick / 32.0f;
        auto v = juce::Point<float>(std::sin(t), -std::cos(t));
        g.setColour((tick % 4 == 0 ? BubblesTheme::textMuted : BubblesTheme::knobEdge)
                        .withAlpha(tick % 4 == 0 ? 0.58f : 0.30f));
        g.drawLine({ centre + v * (radius + 4.0f), centre + v * (radius + (tick % 4 == 0 ? 8.0f : 6.0f)) }, 1.0f);
    }
    g.setColour(BubblesTheme::background);
    g.fillEllipse(bounds.reduced(radius * 0.10f).translated(0.0f, 3.0f));
    g.setGradientFill(juce::ColourGradient(BubblesTheme::panelRaised.brighter(0.12f), centre.x, bounds.getY(), BubblesTheme::panel.darker(0.5f), centre.x, bounds.getBottom(), false));
    g.fillEllipse(bounds.reduced(radius * 0.16f));

    g.setColour(BubblesTheme::knobEdge);
    g.drawEllipse(bounds.reduced(radius * 0.11f), 1.0f);

    juce::Path backgroundArc;
    backgroundArc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f,
                                rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(BubblesTheme::knobTrack);
    g.strokePath(backgroundArc, juce::PathStrokeType(lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path valueArc;
    valueArc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f,
                           rotaryStartAngle, angle, true);
    juce::ColourGradient glow(BubblesTheme::amber, bounds.getX(), bounds.getY(), BubblesTheme::amber, bounds.getRight(), bounds.getBottom(), false);
    g.setGradientFill(glow);
    g.strokePath(valueArc, juce::PathStrokeType(lineW + 0.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    auto pointerLength = radius * 0.58f;
    auto pointerThickness = juce::jmax(2.0f, radius * 0.035f);
    juce::Path pointer;
    pointer.addRoundedRectangle(-pointerThickness * 0.5f, -pointerLength, pointerThickness, radius * 0.33f, pointerThickness);
    pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));
    g.setColour(BubblesTheme::ink.withAlpha(0.92f));
    g.fillPath(pointer);

    
}

void BubblesLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&,
                          bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    auto active = button.getToggleState() || shouldDrawButtonAsDown;
    const bool step = button.getButtonText().getIntValue() > 0;
    const bool rhythmPlayhead = (bool)button.getProperties().getWithDefault("rhythmPlayhead", false);
    auto fill = rhythmPlayhead ? BubblesTheme::amber.withAlpha(0.28f)
                               : (active ? BubblesTheme::amber.withAlpha(0.22f) : BubblesTheme::buttonSurface);
    if (shouldDrawButtonAsHighlighted)
        fill = fill.brighter(0.12f);

    if (step && active) fill = BubblesTheme::amber.withAlpha(0.85f);
    g.setGradientFill(juce::ColourGradient(fill.brighter(0.07f), 0, 0, fill.darker(0.15f), 0, bounds.getBottom(), false));
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(rhythmPlayhead ? BubblesTheme::ink.withAlpha(0.92f)
                               : (active ? BubblesTheme::amber.withAlpha(0.78f) : BubblesTheme::stroke));
    g.drawRoundedRectangle(bounds, 6.0f, rhythmPlayhead ? 2.0f : 1.0f);
}

void BubblesLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    auto r=b.getLocalBounds(); const auto text=b.getButtonText();
    const auto drawFocusOutline = [&] {
        if (b.hasKeyboardFocus(true)) {
            g.setColour(BubblesTheme::amber);
            g.drawRoundedRectangle(b.getLocalBounds().toFloat().reduced(3), 4, 1);
        }
    };
    const bool step=text.getIntValue()>0;
    g.setColour(step && b.getToggleState() ? BubblesTheme::background : BubblesTheme::ink);
    g.setFont(step ? BubblesTheme::controlFont(10.0f) : BubblesTheme::controlFont(11.0f));
    if (text == "A   STORE" || text == "B   STORE") {
        auto scene = text.substring(0, 1);
        g.setColour(b.getToggleState() || b.isDown() ? BubblesTheme::amber : BubblesTheme::ink);
        g.setFont(BubblesTheme::headingFont(18.0f));
        g.drawText(scene, r.removeFromLeft(48), juce::Justification::centred);
        g.setColour(BubblesTheme::textMuted);
        g.setFont(BubblesTheme::captionFont(8.5f));
        g.drawText("STORE", r.reduced(4), juce::Justification::centredLeft);
        drawFocusOutline();
        return;
    }
    if (text == "FREEZE" || text == "CAPTURE") {
        const float cx=r.getCentreX(), cy=r.getY()+22.0f;
        if (text == "FREEZE") {
            for(int i=0; i<3; ++i) {
                float a=i*juce::MathConstants<float>::pi/3;
                float dx=std::cos(a)*8, dy=std::sin(a)*8;
                g.drawLine(cx-dx,cy-dy,cx+dx,cy+dy,1.3f);
            }
        } else {
            g.setColour(BubblesTheme::amber.withAlpha(b.isDown()?0.28f:0.10f)); g.fillEllipse(cx-12,cy-12,24,24);
            g.setColour(BubblesTheme::amber); g.fillEllipse(cx-6,cy-6,12,12);
        }
        g.setColour(b.getToggleState() || b.isDown() ? BubblesTheme::amber : BubblesTheme::ink);
        r.removeFromTop(38);
    }
    g.drawText(text,r.reduced(4),juce::Justification::centred);
    drawFocusOutline();
}

void BubblesLookAndFeel::drawComboBox(juce::Graphics& g, int w, int h, bool down, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float>(0, 0, (float)w, (float)h).reduced(0.5f);
    g.setColour(BubblesTheme::panel.darker(0.4f)); g.fillRoundedRectangle(r, 5.0f);
    g.setColour(down || box.hasKeyboardFocus(true) ? BubblesTheme::amber
                                                   : (box.isMouseOver() ? BubblesTheme::knobEdge
                                                                        : BubblesTheme::stroke.withAlpha(0.62f)));
    g.drawRoundedRectangle(r, 5.0f, 1.0f);
    juce::Path arrow; arrow.startNewSubPath(w - 22.0f, h * 0.45f);
    arrow.lineTo(w - 17.0f, h * 0.58f); arrow.lineTo(w - 12.0f, h * 0.45f);
    g.setColour(BubblesTheme::ink); g.strokePath(arrow, juce::PathStrokeType(1.5f));
}

juce::Font BubblesLookAndFeel::getComboBoxFont(juce::ComboBox&)
{ return BubblesTheme::controlFont(11.0f); }

juce::Font BubblesLookAndFeel::getTextButtonFont(juce::TextButton&, int)
{ return BubblesTheme::controlFont(11.0f); }

void BubblesLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                      juce::Slider::SliderStyle, juce::Slider&)
{
    auto r = juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h).reduced(1.0f);
    g.setColour(BubblesTheme::panel.darker(0.4f)); g.fillRoundedRectangle(r, 5.0f);
    g.setColour(BubblesTheme::stroke); g.drawRoundedRectangle(r, 5.0f, 1.0f);
    float cy = r.getCentreY();
    g.setColour(BubblesTheme::textMuted); g.drawLine(r.getX()+12, cy, r.getRight()-12, cy, 1.0f);
    for (int i=0; i<=8; ++i) { float tx=r.getX()+12+(r.getWidth()-24)*i/8.0f; g.drawLine(tx, cy-3, tx, cy+3); }
    g.setColour(BubblesTheme::amber.withAlpha(0.13f)); g.fillEllipse(pos-17, cy-17, 34, 34);
    g.setColour(BubblesTheme::panelRaised); g.fillEllipse(pos-10, cy-10, 20, 20);
    g.setColour(BubblesTheme::amber); g.drawEllipse(pos-10, cy-10, 20, 20, 2.5f);
}
