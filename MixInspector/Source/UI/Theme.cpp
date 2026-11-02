// MixInspector - stereo & harmonic analysis plugin
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Theme.h"
#include "BinaryData.h"

namespace mi::ui
{
juce::Colour levelColour (Level level)
{
    switch (level)
    {
        case Level::good:    return colours::good;
        case Level::caution: return colours::caution;
        case Level::problem: return colours::problem;
        case Level::idle:
        default:             return colours::inkFaint;
    }
}

//==============================================================================
Fonts::Fonts()
{
    regular  = juce::Typeface::createSystemTypefaceFor (BinaryData::BarlowSemiCondensedRegular_ttf,
                                                        (size_t) BinaryData::BarlowSemiCondensedRegular_ttfSize);
    medium   = juce::Typeface::createSystemTypefaceFor (BinaryData::BarlowSemiCondensedMedium_ttf,
                                                        (size_t) BinaryData::BarlowSemiCondensedMedium_ttfSize);
    semibold = juce::Typeface::createSystemTypefaceFor (BinaryData::BarlowSemiCondensedSemiBold_ttf,
                                                        (size_t) BinaryData::BarlowSemiCondensedSemiBold_ttfSize);
}

juce::Font Fonts::get (Weight weight, float height) const
{
    const auto& tf = weight == Weight::regular ? regular : weight == Weight::medium ? medium : semibold;
    if (tf == nullptr)
        return juce::Font (juce::FontOptions (height));
    return juce::Font (juce::FontOptions (tf).withHeight (height));
}

juce::Font font (Weight weight, float height)
{
    const juce::SharedResourcePointer<Fonts> fonts;
    return fonts->get (weight, height);
}

//==============================================================================
static bool isDigitChar (juce::juce_wchar c) { return c >= '0' && c <= '9'; }

float tabularWidth (const juce::String& text, const juce::Font& f)
{
    const float cell = juce::GlyphArrangement::getStringWidth (f, "0");
    float width = 0.0f;
    for (auto p = text.getCharPointer(); ! p.isEmpty(); ++p)
    {
        const auto c = *p;
        width += isDigitChar (c) ? cell : juce::GlyphArrangement::getStringWidth (f, juce::String::charToString (c));
    }
    return width;
}

void drawTabular (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                  juce::Justification justification, const juce::Font& f)
{
    g.setFont (f);
    const float cell = juce::GlyphArrangement::getStringWidth (f, "0");
    const float total = tabularWidth (text, f);

    float x = area.getX();
    if (justification.testFlags (juce::Justification::horizontallyCentred)) x = area.getCentreX() - total * 0.5f;
    else if (justification.testFlags (juce::Justification::right))          x = area.getRight() - total;

    const juce::Justification cellJust (justification.getOnlyVerticalFlags() | juce::Justification::horizontallyCentred);

    for (auto p = text.getCharPointer(); ! p.isEmpty(); ++p)
    {
        const auto c = *p;
        const auto s = juce::String::charToString (c);
        const float w = isDigitChar (c) ? cell : juce::GlyphArrangement::getStringWidth (f, s);
        g.drawText (s, juce::Rectangle<float> (x, area.getY(), w + 1.0f, area.getHeight()), cellJust, false);
        x += w;
    }
}

const juce::String& minusSign()
{
    static const juce::String m (juce::CharPointer_UTF8 ("\xe2\x88\x92"));
    return m;
}

juce::String formatNumber (float value, int decimals, bool showPlus)
{
    const double scale = std::pow (10.0, decimals);
    const double rounded = std::round ((double) value * scale) / scale;
    const juce::String body (std::abs (rounded), decimals);

    if (rounded < 0.0)              return minusSign() + body;
    if (showPlus && rounded > 0.0)  return "+" + body;
    return body;
}

void drawPanelGrid (juce::Graphics& g, juce::Rectangle<float> area, int columns, int rows)
{
    g.setColour (colours::gridFine);
    for (int i = 1; i < columns; ++i)
    {
        const float x = area.getX() + area.getWidth() * (float) i / (float) columns;
        g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());
    }
    for (int i = 1; i < rows; ++i)
    {
        const float y = area.getY() + area.getHeight() * (float) i / (float) rows;
        g.drawHorizontalLine (juce::roundToInt (y), area.getX(), area.getRight());
    }
}

juce::Colour severityColour (float amount01)
{
    const float t = juce::jlimit (0.0f, 1.0f, amount01);
    if (t < 0.5f)
        return colours::good.interpolatedWith (colours::caution, t * 2.0f);
    return colours::caution.interpolatedWith (colours::problem, (t - 0.5f) * 2.0f);
}

//==============================================================================
LookAndFeel::LookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, colours::background);
    setColour (juce::TextButton::buttonColourId, colours::panel);
    setColour (juce::TextButton::buttonOnColourId, colours::ink);
    setColour (juce::TextButton::textColourOffId, colours::inkDim);
    setColour (juce::TextButton::textColourOnId, colours::background);
    setColour (juce::ComboBox::outlineColourId, colours::grid);
}

juce::Typeface::Ptr LookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName() && fonts->medium != nullptr)
        return fonts->medium;
    return LookAndFeel_V4::getTypefaceForFont (f);
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return fonts->get (Weight::medium, juce::jmin (15.0f, (float) buttonHeight * 0.55f));
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                        bool isMouseOverButton, bool isButtonDown)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const float corner = 4.0f;

    const bool left  = ! button.isConnectedOnLeft();
    const bool right = ! button.isConnectedOnRight();

    juce::Path p;
    p.addRoundedRectangle (bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(),
                           corner, corner, left, right, left, right);

    juce::Colour fill = button.getToggleState() ? colours::ink : colours::panel;
    if (! button.getToggleState() && (isMouseOverButton || isButtonDown))
        fill = colours::panelEdge;
    if (button.getToggleState() && isButtonDown)
        fill = fill.darker (0.1f);

    g.setColour (fill);
    g.fillPath (p);
    g.setColour (colours::grid);
    g.strokePath (p, juce::PathStrokeType (1.0f));
}

//==============================================================================
History::History (int capacityToUse) : values ((size_t) juce::jmax (1, capacityToUse), 0.0f) {}

void History::push (float value)
{
    values[(size_t) writePos] = value;
    writePos = (writePos + 1) % (int) values.size();
    count = juce::jmin (count + 1, (int) values.size());
}

void History::clear()
{
    std::fill (values.begin(), values.end(), 0.0f);
    writePos = count = 0;
}

float History::get (int indexFromOldest) const
{
    const int cap = (int) values.size();
    const int idx = (writePos - count + indexFromOldest + cap * 2) % cap;
    return values[(size_t) idx];
}

float History::minimumOfLast (int n) const
{
    n = juce::jmin (n, count);
    float m = 1.0e9f;
    for (int i = count - n; i < count; ++i)
        m = juce::jmin (m, get (i));
    return n > 0 ? m : 0.0f;
}

} // namespace mi::ui
