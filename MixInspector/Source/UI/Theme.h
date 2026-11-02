// MixInspector - stereo & harmonic analysis plugin
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace mi::ui
{
/** Palette: anodised instrument slate, paper-white ink, one phosphor trace colour,
    and three status colours that always mean the same thing. */
namespace colours
{
    inline const juce::Colour background { 0xff1c2629 };
    inline const juce::Colour panel      { 0xff222e32 };
    inline const juce::Colour panelEdge  { 0xff2d3c41 };
    inline const juce::Colour grid       { 0xff34454b };
    inline const juce::Colour gridFine   { 0xff2a383c };
    inline const juce::Colour ink        { 0xffece6d6 };
    inline const juce::Colour inkDim     { 0xff93a2a5 };
    inline const juce::Colour inkFaint   { 0xff5f6f73 };
    inline const juce::Colour trace      { 0xff9ee6d3 };
    inline const juce::Colour good       { 0xff86c99b };
    inline const juce::Colour caution    { 0xffe7b450 };
    inline const juce::Colour problem    { 0xffea6b5f };
}

enum class Level { idle, good, caution, problem };
juce::Colour levelColour (Level level);

enum class Weight { regular, medium, semibold };

/** The embedded Barlow Semi Condensed typefaces, shared by every open editor. */
struct Fonts
{
    Fonts();
    juce::Font get (Weight weight, float height) const;

    juce::Typeface::Ptr regular, medium, semibold;
};

/** Convenience accessor (only valid while an editor holds the shared Fonts). */
juce::Font font (Weight weight, float height);

/** Draws text with every digit in an equal-width cell, so live numbers don't jitter. */
void drawTabular (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                  juce::Justification justification, const juce::Font& f);
float tabularWidth (const juce::String& text, const juce::Font& f);

/** Number formatting with a real minus sign (U+2212). */
juce::String formatNumber (float value, int decimals, bool showPlus = false);
const juce::String& minusSign();

/** Small shared drawing helpers. */
void drawPanelGrid (juce::Graphics& g, juce::Rectangle<float> area, int columns, int rows);
juce::Colour severityColour (float amount01);   // 0 = good ... 1 = problem

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isMouseOverButton, bool isButtonDown) override;

private:
    juce::SharedResourcePointer<Fonts> fonts;
};

/** Fixed-length history of a value, for the scrolling plots. */
class History
{
public:
    explicit History (int capacity = 300);

    void push (float value);
    void clear();
    int size() const noexcept           { return count; }
    int capacity() const noexcept       { return (int) values.size(); }
    float get (int indexFromOldest) const;
    float minimumOfLast (int n) const;

private:
    std::vector<float> values;
    int writePos = 0, count = 0;
};

} // namespace mi::ui
