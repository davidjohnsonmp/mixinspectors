// MixInspector - stereo & harmonic analysis plugin
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Panels.h"

namespace mi::ui
{
namespace
{
    const juce::String& dash()
    {
        static const juce::String d (juce::CharPointer_UTF8 ("\xe2\x80\x93"));   // en dash: "no reading"
        return d;
    }

    juce::String noteName (int midi) { return juce::String (Analyzer::noteName (midi)); }

    void drawLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                    juce::Colour colour, float size = 13.0f, Weight weight = Weight::regular,
                    juce::Justification just = juce::Justification::centredLeft)
    {
        g.setColour (colour);
        g.setFont (font (weight, size));
        g.drawText (text, area, just, true);
    }

    void drawReadout (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, bool live)
    {
        g.setColour (live ? colours::ink : colours::inkFaint);
        drawTabular (g, text, area, juce::Justification::centredLeft, font (Weight::semibold, 38.0f));
    }

    /** "Label ........ value" row used for secondary numbers. */
    void drawStat (juce::Graphics& g, const juce::String& label, const juce::String& value,
                   juce::Rectangle<float> area, juce::Colour valueColour = colours::ink)
    {
        drawLabel (g, label, area, colours::inkDim, 13.5f);
        g.setColour (valueColour);
        drawTabular (g, value, area, juce::Justification::centredRight, font (Weight::medium, 15.5f));
    }

    float mapX (float v, float lo, float hi, juce::Rectangle<float> r)
    {
        return juce::jmap (juce::jlimit (lo, hi, v), lo, hi, r.getX(), r.getRight());
    }

    /** Horizontal meter: track, optional tinted zones, tick labels, value bar from `origin`. */
    struct MeterZone { float from, to; juce::Colour tint; };

    void drawMeter (juce::Graphics& g, juce::Rectangle<float> area, float lo, float hi, float origin,
                    float value, bool live, juce::Colour barColour,
                    std::initializer_list<MeterZone> zones,
                    std::initializer_list<std::pair<float, juce::String>> ticks,
                    float marker = std::numeric_limits<float>::quiet_NaN())
    {
        auto track = area.removeFromTop (14.0f);
        g.setColour (colours::gridFine);
        g.fillRoundedRectangle (track, 3.0f);

        for (const auto& z : zones)
        {
            g.setColour (z.tint);
            const float x0 = mapX (z.from, lo, hi, track), x1 = mapX (z.to, lo, hi, track);
            g.fillRect (juce::Rectangle<float> (x0, track.getY(), x1 - x0, track.getHeight()));
        }

        g.setFont (font (Weight::regular, 12.0f));
        for (const auto& [v, text] : ticks)
        {
            const float x = mapX (v, lo, hi, track);
            g.setColour (colours::grid);
            g.drawVerticalLine (juce::roundToInt (x), track.getBottom(), track.getBottom() + 4.0f);

            auto just = juce::Justification::centredTop;
            juce::Rectangle<float> labelArea (x - 40.0f, track.getBottom() + 5.0f, 80.0f, 14.0f);
            if (v <= lo) { just = juce::Justification::topLeft;  labelArea.setX (x); }
            if (v >= hi) { just = juce::Justification::topRight; labelArea = labelArea.withRightX (x); }
            g.setColour (colours::inkDim);
            g.drawText (text, labelArea, just, false);
        }

        if (live)
        {
            const float x0 = mapX (origin, lo, hi, track), x1 = mapX (value, lo, hi, track);
            g.setColour (barColour.withAlpha (0.85f));
            g.fillRect (juce::Rectangle<float> (juce::jmin (x0, x1), track.getY() + 3.0f,
                                                std::abs (x1 - x0), track.getHeight() - 6.0f));
            g.setColour (colours::ink);
            g.fillRect (juce::Rectangle<float> (x1 - 1.0f, track.getY() - 3.0f, 2.0f, track.getHeight() + 6.0f));

            if (! std::isnan (marker))
            {
                const float xm = mapX (marker, lo, hi, track);
                juce::Path tri;
                tri.addTriangle (xm, track.getBottom() - 1.0f, xm - 4.0f, track.getBottom() + 5.0f,
                                 xm + 4.0f, track.getBottom() + 5.0f);
                g.setColour (colours::inkDim);
                g.fillPath (tri);
            }
        }
    }

    juce::String intervalDescription (int noteA, int noteB)
    {
        const int semis = std::abs (noteB - noteA) % 12;
        const int ic = juce::jmin (semis, 12 - semis);
        switch (ic)
        {
            case 1:  return "a half step apart";
            case 2:  return "a whole step apart";
            case 6:  return "a tritone apart";
            default: return {};
        }
    }

    juce::String clashSentence (const Clash& c)
    {
        if (c.noteA == c.noteB)
            return "Two " + noteName (c.noteA) + " notes are slightly out of tune with each other.";
        return noteName (c.noteA) + " rubs against " + noteName (c.noteB) + ", "
             + intervalDescription (c.noteA, c.noteB) + ". Check the key or the tuning.";
    }

    juce::String bandName (int band)
    {
        static const char* const names[kNumBroadBands] = { "Below 150 Hz", "150 Hz to 1 kHz", "1 to 5 kHz", "Above 5 kHz" };
        return names[juce::jlimit (0, kNumBroadBands - 1, band)];
    }
} // namespace

//==============================================================================
Panel::Panel (const juce::String& titleText) : title (titleText)
{
    setOpaque (false);
}

void Panel::setVerdict (Level newLevel, const juce::String& label, const juce::String& explanation)
{
    level = newLevel;
    verdict = label;
    hint = explanation;
}

void Panel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (colours::panel);
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (colours::panelEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

    auto inner = bounds.reduced (16.0f, 14.0f);

    // title row: name on the left, verdict on the right
    auto titleRow = inner.removeFromTop (22.0f);
    drawLabel (g, title, titleRow, colours::ink, 17.0f, Weight::semibold);

    const auto statusColour = levelColour (level);
    const auto verdictFont = fonts->get (Weight::medium, 15.0f);
    const float verdictWidth = juce::GlyphArrangement::getStringWidth (verdictFont, verdict);
    auto verdictArea = titleRow.removeFromRight (verdictWidth + 2.0f);
    g.setColour (statusColour);
    g.setFont (verdictFont);
    g.drawText (verdict, verdictArea, juce::Justification::centredRight, false);
    g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ verdictArea.getX() - 10.0f, titleRow.getCentreY() }));

    inner.removeFromTop (12.0f);

    // explanation at the bottom, in plain words
    auto hintArea = inner.removeFromBottom (36.0f);
    g.setColour (colours::gridFine);
    g.drawHorizontalLine (juce::roundToInt (hintArea.getY()), hintArea.getX(), hintArea.getRight());
    g.setColour (level == Level::idle ? colours::inkFaint : colours::inkDim);
    g.setFont (fonts->get (Weight::regular, 14.0f));
    g.drawFittedText (hint, hintArea.withTrimmedTop (6.0f).toNearestInt(), juce::Justification::topLeft, 2, 0.95f);
    inner.removeFromBottom (8.0f);

    paintContent (g, inner);
}

//==============================================================================
// Correlation
//==============================================================================
CorrelationPanel::CorrelationPanel() : Panel ("Correlation")
{
    clear();
}

void CorrelationPanel::clear()
{
    active = false;
    value = lowestRecent = 0.0f;
    bands.fill (0.0f);
    bandActive.fill (false);
    history.clear();
    setVerdict (Level::idle, "No signal", "Play some audio through the track to start measuring.");
}

void CorrelationPanel::update (const Results& r, const Analyzer&)
{
    active = r.active;
    bands = r.bandCorrelation;
    bandActive = r.broadBandActive;

    if (! active)
    {
        setVerdict (Level::idle, "No signal", "Play some audio through the track to start measuring.");
        return;
    }

    value = r.correlation;
    history.push (value);
    lowestRecent = history.minimumOfLast (90);   // last 3 s

    if (value < 0.0f)
        setVerdict (Level::problem, "Out of phase",
                    "Left and right work against each other. Parts of the mix will cancel in mono.");
    else if (value < 0.3f)
        setVerdict (Level::caution, "Very wide",
                    "Left and right have little in common. Check how the mix sounds in mono.");
    else if (bandActive[0] && bands[0] < 0.5f)
        setVerdict (Level::caution, "Wide low end",
                    "Below 150 Hz the channels differ. Keep bass and kick centred for mono playback.");
    else
        setVerdict (Level::good, "Mono-safe",
                    "Left and right move together, so the mix holds up in mono.");
}

void CorrelationPanel::paintContent (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto colour = levelColour (getLevel());

    // readout + meter
    auto top = area.removeFromTop (44.0f);
    drawReadout (g, active ? formatNumber (value, 2, true) : dash(), top.removeFromLeft (112.0f), active);
    auto meterArea = top.withTrimmedLeft (8.0f).withTrimmedTop (6.0f);
    drawMeter (g, meterArea, -1.0f, 1.0f, 0.0f, value, active, colour,
               { { -1.0f, 0.0f, colours::problem.withAlpha (0.13f) } },
               { { -1.0f, minusSign() + "1" }, { -0.5f, minusSign() + "0.5" }, { 0.0f, "0" },
                 { 0.5f, "+0.5" }, { 1.0f, "+1" } },
               lowestRecent);

    area.removeFromTop (10.0f);

    // per-band values
    auto bandRow = area.removeFromBottom (48.0f);
    area.removeFromBottom (10.0f);
    const float cellW = bandRow.getWidth() / (float) kNumBroadBands;

    for (int b = 0; b < kNumBroadBands; ++b)
    {
        auto cell = bandRow.removeFromLeft (cellW).reduced (b == 0 ? 0.0f : 6.0f, 0.0f).withTrimmedLeft (b == 0 ? 0.0f : 0.0f);
        const bool live = active && bandActive[(size_t) b];
        const float v = bands[(size_t) b];

        drawLabel (g, bandName (b), cell.removeFromTop (16.0f), colours::inkDim, 13.0f);

        auto valueArea = cell.removeFromTop (22.0f);
        juce::Colour vc = colours::inkFaint;
        if (live)   // highs are naturally decorrelated (reverb, cymbals), so only the low band gets a caution colour
            vc = v < 0.0f ? colours::problem : (b == 0 && v < 0.5f ? colours::caution : colours::ink);
        g.setColour (vc);
        drawTabular (g, live ? formatNumber (v, 2, true) : dash(), valueArea, juce::Justification::centredLeft,
                     font (Weight::semibold, 19.0f));

        auto bar = cell.removeFromTop (4.0f).withWidth (juce::jmin (cell.getWidth(), 92.0f));
        g.setColour (colours::gridFine);
        g.fillRect (bar);
        if (live)
        {
            const float x0 = mapX (0.0f, -1.0f, 1.0f, bar), x1 = mapX (v, -1.0f, 1.0f, bar);
            g.setColour (vc);
            g.fillRect (juce::Rectangle<float> (juce::jmin (x0, x1), bar.getY(), std::abs (x1 - x0), bar.getHeight()));
        }
    }

    // 10-second history
    auto plot = area;
    g.setColour (colours::background.withAlpha (0.55f));
    g.fillRoundedRectangle (plot, 4.0f);

    auto yOf = [plot] (float v) { return juce::jmap (v, 1.0f, -1.0f, plot.getY() + 4.0f, plot.getBottom() - 4.0f); };

    g.setColour (colours::problem.withAlpha (0.07f));
    g.fillRect (juce::Rectangle<float> (plot.getX(), yOf (0.0f), plot.getWidth(), yOf (-1.0f) - yOf (0.0f)));

    for (float gv : { 0.5f, -0.5f })
    {
        g.setColour (colours::gridFine);
        g.drawHorizontalLine (juce::roundToInt (yOf (gv)), plot.getX(), plot.getRight());
    }
    g.setColour (colours::grid);
    g.drawHorizontalLine (juce::roundToInt (yOf (0.0f)), plot.getX(), plot.getRight());

    const int n = history.size();
    if (n > 1)
    {
        juce::Path p;
        const float step = plot.getWidth() / (float) (history.capacity() - 1);
        const float x0 = plot.getRight() - step * (float) (n - 1);
        for (int i = 0; i < n; ++i)
        {
            const float x = x0 + step * (float) i, y = yOf (history.get (i));
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (colours::trace.withAlpha (active ? 0.9f : 0.4f));
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved));
    }

    drawLabel (g, "Last 10 seconds", plot.reduced (8.0f, 4.0f).removeFromTop (14.0f), colours::inkFaint, 12.0f);
    drawLabel (g, "+1", plot.reduced (8.0f, 2.0f).removeFromTop (14.0f), colours::inkFaint, 11.5f,
               Weight::regular, juce::Justification::centredRight);
    drawLabel (g, minusSign() + "1", plot.reduced (8.0f, 2.0f).removeFromBottom (14.0f), colours::inkFaint, 11.5f,
               Weight::regular, juce::Justification::centredRight);
}

//==============================================================================
// Phase
//==============================================================================
PhasePanel::PhasePanel() : Panel ("Phase")
{
    for (int t = 0; t < kTrails; ++t)
    {
        trailL[(size_t) t].assign (kScopeSamples, 0.0f);
        trailR[(size_t) t].assign (kScopeSamples, 0.0f);
    }
    clear();
}

void PhasePanel::clear()
{
    trailCount.fill (0);
    scopePeak = 0.05f;
    active = inverted = false;
    delayMs = delaySamples = confidence = monoSum = 0.0f;
    bandMonoSum.fill (0.0f);
    bandActive.fill (false);
    setVerdict (Level::idle, "No signal", "Play some audio through the track to start measuring.");
}

void PhasePanel::update (const Results& r, const Analyzer& analyzer)
{
    // shift the phosphor trails: [0] newest
    for (int t = kTrails - 1; t > 0; --t)
    {
        std::swap (trailL[(size_t) t], trailL[(size_t) t - 1]);
        std::swap (trailR[(size_t) t], trailR[(size_t) t - 1]);
        trailCount[(size_t) t] = trailCount[(size_t) t - 1];
    }
    trailCount[0] = analyzer.copyRecentSamples (trailL[0].data(), trailR[0].data(), kScopeSamples);

    float peak = 0.0f;
    for (int i = 0; i < trailCount[0]; ++i)
        peak = juce::jmax (peak, std::abs (trailL[0][(size_t) i]), std::abs (trailR[0][(size_t) i]));
    scopePeak = juce::jmax (peak, scopePeak * 0.94f, 0.002f);

    active = r.active;
    inverted = r.polarityInverted;
    delayMs = r.delayMs;
    delaySamples = r.delaySamples;
    confidence = r.delayConfidence;
    monoSum = r.monoSumDb;
    bandMonoSum = r.bandMonoSumDb;
    bandActive = r.octaveBandActive;

    if (! active)
    {
        setVerdict (Level::idle, "No signal", "Play some audio through the track to start measuring.");
        return;
    }

    int worstBand = -1;
    for (int b = 0; b < kNumOctaveBands; ++b)
        if (bandActive[(size_t) b] && (worstBand < 0 || bandMonoSum[(size_t) b] < bandMonoSum[(size_t) worstBand]))
            worstBand = b;

    const bool offset = confidence >= 0.4f && std::abs (delaySamples) >= 1.0f;

    if (inverted && confidence >= 0.5f)
        setVerdict (Level::problem, "Polarity flipped",
                    "One channel is upside down. Flip the polarity of the left or the right channel.");
    else if (monoSum < -6.0f)
        setVerdict (Level::problem, "Cancels in mono",
                    "Folding to mono loses " + formatNumber (-monoSum, 1) + " dB. Something in the mix is out of phase.");
    else if (offset)
        setVerdict (Level::caution, "Channels offset",
                    juce::String (delaySamples > 0.0f ? "Right" : "Left") + " arrives "
                        + formatNumber (std::abs (delayMs), 2) + " ms late. In mono this causes comb filtering.");
    else if (worstBand >= 0 && bandMonoSum[(size_t) worstBand] < -6.0f)
        setVerdict (Level::caution, "Cancels at " + juce::String (Analyzer::octaveBandLabel (worstBand)) + " Hz",
                    "This band drops " + formatNumber (-bandMonoSum[(size_t) worstBand], 1)
                        + " dB in mono. Check doubled parts or multi-mic recordings.");
    else
        setVerdict (Level::good, "Coherent", "Left and right line up in time and polarity.");
}

void PhasePanel::paintScope (juce::Graphics& g, juce::Rectangle<float> sq)
{
    g.setColour (colours::background);
    g.fillRoundedRectangle (sq, 4.0f);

    const auto c = sq.getCentre();
    const float half = sq.getWidth() * 0.5f - 6.0f;

    g.setColour (colours::gridFine);
    g.drawLine (c.x, sq.getY() + 6.0f, c.x, sq.getBottom() - 6.0f);
    g.drawLine (sq.getX() + 6.0f, c.y, sq.getRight() - 6.0f, c.y);
    g.drawEllipse (juce::Rectangle<float> (half * 2.0f, half * 2.0f).withCentre (c), 1.0f);
    g.drawEllipse (juce::Rectangle<float> (half, half).withCentre (c), 1.0f);

    const float d = half * 0.7071f;
    g.setColour (colours::grid);
    g.drawLine (c.x - d, c.y - d, c.x + d, c.y + d, 1.0f);
    g.drawLine (c.x + d, c.y - d, c.x - d, c.y + d, 1.0f);

    g.setFont (font (Weight::medium, 12.5f));
    g.setColour (colours::inkDim);
    g.drawText ("L", juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ c.x - d - 6.0f, c.y - d - 6.0f }), juce::Justification::centred);
    g.drawText ("R", juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ c.x + d + 6.0f, c.y - d - 6.0f }), juce::Justification::centred);
    g.drawText ("Mono", juce::Rectangle<float> (40.0f, 14.0f).withCentre ({ c.x, sq.getY() + 12.0f }).translated (24.0f, 0.0f),
                juce::Justification::centredLeft);

    // phosphor: newest trace bright, older ones fading
    const float alphas[kTrails] = { 0.75f, 0.28f, 0.12f };
    const float k = half * 0.92f / scopePeak * 0.7071f;

    g.saveState();
    g.reduceClipRegion (sq.toNearestInt());
    for (int t = kTrails - 1; t >= 0; --t)
    {
        const int n = trailCount[(size_t) t];
        if (n < 2)
            continue;

        juce::Path p;
        for (int i = 0; i < n; ++i)
        {
            const float l = trailL[(size_t) t][(size_t) i], r = trailR[(size_t) t][(size_t) i];
            const float x = c.x + (r - l) * k;
            const float y = c.y - (l + r) * k;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (colours::trace.withAlpha (active ? alphas[t] : alphas[t] * 0.3f));
        g.strokePath (p, juce::PathStrokeType (t == 0 ? 1.2f : 1.0f));
    }
    g.restoreState();
}

void PhasePanel::paintContent (juce::Graphics& g, juce::Rectangle<float> area)
{
    const float side = juce::jmin (area.getHeight(), area.getWidth() * 0.44f);
    auto scopeArea = area.removeFromLeft (side);
    paintScope (g, scopeArea.withSizeKeepingCentre (side, side));

    area.removeFromLeft (18.0f);

    // readouts
    auto rowH = 34.0f;
    auto row = area.removeFromTop (rowH);

    auto drawRow = [&g] (juce::Rectangle<float> r, const juce::String& label, const juce::String& value,
                         const juce::String& sub, juce::Colour valueColour)
    {
        drawLabel (g, label, r, colours::inkDim, 14.0f);
        auto right = r.withTrimmedLeft (r.getWidth() * 0.4f);
        g.setColour (valueColour);
        drawTabular (g, value, right.removeFromTop (20.0f), juce::Justification::centredRight, font (Weight::semibold, 18.0f));
        drawLabel (g, sub, right, colours::inkFaint, 12.0f, Weight::regular, juce::Justification::centredRight);
    };

    const bool polarityKnown = active && confidence >= 0.3f;
    drawRow (row, "Polarity",
             active ? (polarityKnown ? (inverted ? "Inverted" : "Normal") : "Unclear") : dash(),
             polarityKnown ? (inverted ? "one side is upside down" : "both sides agree") : "",
             ! active ? colours::inkFaint : (inverted && confidence >= 0.5f ? colours::problem : colours::ink));

    row = area.removeFromTop (rowH);
    {
        juce::String value = dash(), sub;
        juce::Colour vc = colours::inkFaint;
        if (active && confidence >= 0.3f)
        {
            value = formatNumber (std::abs (delayMs), 2) + " ms";
            vc = colours::ink;
            if (std::abs (delaySamples) < 0.5f)
                sub = "aligned";
            else
            {
                sub = juce::String (delaySamples > 0.0f ? "right" : "left") + " late by "
                    + formatNumber (std::abs (delaySamples), 1) + " samples";
                if (confidence >= 0.4f && std::abs (delaySamples) >= 1.0f)
                    vc = colours::caution;
            }
        }
        else if (active)
            sub = "too little in common";

        drawRow (row, "Time offset", value, sub, vc);
    }

    row = area.removeFromTop (rowH);
    drawRow (row, "Mono sum", active ? formatNumber (monoSum, 1, true) + " dB" : dash(),
             active ? "level change in mono" : "",
             ! active ? colours::inkFaint : (monoSum < -6.0f ? colours::problem : (monoSum < -3.5f ? colours::caution : colours::ink)));

    area.removeFromTop (8.0f);

    // mono sum by band: bars hang down from 0 dB
    drawLabel (g, "Mono sum by band", area.removeFromTop (16.0f), colours::inkDim, 13.0f);
    area.removeFromTop (4.0f);
    auto labels = area.removeFromBottom (14.0f);
    auto chart = area;

    g.setColour (colours::background.withAlpha (0.55f));
    g.fillRoundedRectangle (chart, 3.0f);

    const float floorDb = -12.0f;
    auto yOf = [chart, floorDb] (float db) { return juce::jmap (juce::jlimit (floorDb, 0.0f, db), 0.0f, floorDb, chart.getY() + 2.0f, chart.getBottom() - 2.0f); };

    g.setColour (colours::gridFine);
    for (float db : { -3.0f, -6.0f, -9.0f })
        g.drawHorizontalLine (juce::roundToInt (yOf (db)), chart.getX(), chart.getRight());

    const float colW = chart.getWidth() / (float) kNumOctaveBands;
    for (int b = 0; b < kNumOctaveBands; ++b)
    {
        const auto col = juce::Rectangle<float> (chart.getX() + colW * (float) b, chart.getY(), colW, chart.getHeight()).reduced (colW * 0.2f, 0.0f);
        const bool live = active && bandActive[(size_t) b];
        const float db = bandMonoSum[(size_t) b];

        if (live)
        {
            const float y0 = yOf (0.0f), y1 = juce::jmax (yOf (db), y0 + 2.0f);
            g.setColour (db < -6.0f ? colours::problem : db < -3.5f ? colours::caution : colours::trace.withAlpha (0.7f));
            g.fillRect (juce::Rectangle<float> (col.getX(), y0, col.getWidth(), y1 - y0));
        }
        else
        {
            g.setColour (colours::inkFaint.withAlpha (0.35f));
            g.fillRect (juce::Rectangle<float> (col.getX(), yOf (0.0f), col.getWidth(), 2.0f));
        }

        drawLabel (g, Analyzer::octaveBandLabel (b), juce::Rectangle<float> (chart.getX() + colW * (float) b, labels.getY(), colW, labels.getHeight()),
                   colours::inkFaint, 11.0f, Weight::regular, juce::Justification::centred);
    }

    drawLabel (g, minusSign() + "12 dB", chart.reduced (4.0f, 1.0f).removeFromBottom (13.0f), colours::inkFaint, 11.0f,
               Weight::regular, juce::Justification::centredRight);
}

//==============================================================================
// Stereo width
//==============================================================================
WidthPanel::WidthPanel() : Panel ("Stereo width")
{
    clear();
}

void WidthPanel::clear()
{
    active = false;
    width = balance = 0.0f;
    sideToMid = -60.0f;
    bandWidth.fill (0.0f);
    bandActive.fill (false);
    setVerdict (Level::idle, "No signal", "Play some audio through the track to start measuring.");
}

void WidthPanel::update (const Results& r, const Analyzer&)
{
    active = r.active;
    width = r.width;
    balance = r.balanceDb;
    sideToMid = r.sideToMidDb;
    bandWidth = r.bandWidth;
    bandActive = r.octaveBandActive;

    if (! active)
    {
        setVerdict (Level::idle, "No signal", "Play some audio through the track to start measuring.");
        return;
    }

    float widestLow = 0.0f;
    for (int b = 0; b < 3; ++b)   // 31, 63 and 125 Hz bands
        if (bandActive[(size_t) b])
            widestLow = juce::jmax (widestLow, bandWidth[(size_t) b]);

    if (width > 1.3f)
        setVerdict (Level::problem, "More side than mid",
                    "Out-of-phase content outweighs the centre. The mix will thin out in mono.");
    else if (widestLow > 0.4f)
        setVerdict (Level::caution, "Wide bass",
                    "Below 180 Hz the image spreads to " + juce::String (juce::roundToInt (widestLow * 100.0f))
                        + "%. Narrow the lows for a solid mono low end.");
    else if (std::abs (balance) > 3.0f)
        setVerdict (Level::caution, balance > 0.0f ? "Leans right" : "Leans left",
                    juce::String ("The ") + (balance > 0.0f ? "right" : "left") + " side is "
                        + formatNumber (std::abs (balance), 1) + " dB louder than the other.");
    else if (width < 0.05f)
        setVerdict (Level::good, "Mono", "Left and right are identical, so there is no stereo image.");
    else
        setVerdict (Level::good, "Balanced image", "A healthy spread with a solid centre.");
}

void WidthPanel::paintContent (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto colour = levelColour (getLevel());

    auto top = area.removeFromTop (44.0f);
    drawReadout (g, active ? juce::String (juce::roundToInt (width * 100.0f)) + "%" : dash(), top.removeFromLeft (112.0f), active);

    auto stats = top.withTrimmedLeft (24.0f);
    juce::String balanceText = dash();
    if (active)
        balanceText = std::abs (balance) < 0.25f ? "centred"
                                                 : formatNumber (std::abs (balance), 1) + " dB " + (balance > 0.0f ? "right" : "left");
    drawStat (g, "Balance", balanceText, stats.removeFromTop (22.0f),
              active && std::abs (balance) > 3.0f ? colours::caution : (active ? colours::ink : colours::inkFaint));
    drawStat (g, "Side vs mid", active ? formatNumber (sideToMid, 1, true) + " dB" : dash(), stats.removeFromTop (22.0f),
              active ? colours::ink : colours::inkFaint);

    area.removeFromTop (8.0f);
    auto meterArea = area.removeFromTop (34.0f);
    drawMeter (g, meterArea, 0.0f, 2.0f, 0.0f, width, active, colour,
               { { 1.0f, 1.3f, colours::caution.withAlpha (0.10f) }, { 1.3f, 2.0f, colours::problem.withAlpha (0.13f) } },
               { { 0.0f, "Mono" }, { 1.0f, "Fully wide" }, { 2.0f, "Out of phase" } });

    area.removeFromTop (8.0f);

    // width per octave band
    drawLabel (g, "Width by band", area.removeFromTop (16.0f), colours::inkDim, 13.0f);
    area.removeFromTop (4.0f);
    auto labels = area.removeFromBottom (14.0f);
    auto chart = area;

    g.setColour (colours::background.withAlpha (0.55f));
    g.fillRoundedRectangle (chart, 3.0f);

    auto yOf = [chart] (float w) { return juce::jmap (juce::jlimit (0.0f, 2.0f, w), 0.0f, 2.0f, chart.getBottom() - 2.0f, chart.getY() + 2.0f); };

    g.setColour (colours::grid);
    {
        const float y = yOf (1.0f);
        for (float x = chart.getX() + 2.0f; x < chart.getRight(); x += 6.0f)
            g.drawHorizontalLine (juce::roundToInt (y), x, juce::jmin (x + 3.0f, chart.getRight()));
    }
    drawLabel (g, "100%", juce::Rectangle<float> (chart.getRight() - 44.0f, yOf (1.0f) - 14.0f, 40.0f, 13.0f),
               colours::inkFaint, 11.0f, Weight::regular, juce::Justification::centredRight);

    const float colW = chart.getWidth() / (float) kNumOctaveBands;
    for (int b = 0; b < kNumOctaveBands; ++b)
    {
        const auto col = juce::Rectangle<float> (chart.getX() + colW * (float) b, chart.getY(), colW, chart.getHeight()).reduced (colW * 0.2f, 0.0f);
        const bool live = active && bandActive[(size_t) b];
        const float w = bandWidth[(size_t) b];

        if (live)
        {
            const float y1 = yOf (w), y0 = yOf (0.0f);
            juce::Colour c = colours::trace.withAlpha (0.7f);
            if (w > 1.3f)                c = colours::problem;
            else if (b < 3 && w > 0.4f)  c = colours::caution;
            g.setColour (c);
            g.fillRect (juce::Rectangle<float> (col.getX(), juce::jmin (y1, y0 - 2.0f), col.getWidth(), juce::jmax (2.0f, y0 - y1)));
        }
        else
        {
            g.setColour (colours::inkFaint.withAlpha (0.35f));
            g.fillRect (juce::Rectangle<float> (col.getX(), yOf (0.0f) - 2.0f, col.getWidth(), 2.0f));
        }

        drawLabel (g, Analyzer::octaveBandLabel (b), juce::Rectangle<float> (chart.getX() + colW * (float) b, labels.getY(), colW, labels.getHeight()),
                   colours::inkFaint, 11.0f, Weight::regular, juce::Justification::centred);
    }
}

//==============================================================================
// Harmonic dissonance
//==============================================================================
DissonancePanel::DissonancePanel() : Panel ("Harmonic dissonance")
{
    clear();
}

void DissonancePanel::clear()
{
    active = tonal = false;
    score = tension = roughness = 0.0f;
    displayTop = -20.0f;
    spectrum.fill (-120.0f);
    numPeaks = numClashes = 0;
    chroma.fill (0.0f);
    setVerdict (Level::idle, "No signal", "Play some audio through the track to start measuring.");
}

void DissonancePanel::update (const Results& r, const Analyzer&)
{
    active = r.active;
    tonal = r.tonal;
    score = r.dissonance;
    tension = r.tension;
    roughness = r.roughness;
    spectrum = r.spectrumDb;
    peaks = r.peaks;
    numPeaks = r.numPeaks;
    chroma = r.chroma;
    clashes = r.clashes;
    numClashes = r.numClashes;

    float maxDb = -120.0f;
    for (int i = 0; i < kSpectrumPoints; ++i)
        if (Analyzer::spectrumPointFreq (i) >= 40.0f && Analyzer::spectrumPointFreq (i) <= 5000.0f)
            maxDb = juce::jmax (maxDb, spectrum[(size_t) i]);
    displayTop = juce::jmax (maxDb + 6.0f, displayTop - 0.4f, -60.0f);

    if (! active)
    {
        setVerdict (Level::idle, "No signal", "Play some audio through the track to start measuring.");
        return;
    }
    if (! tonal)
    {
        setVerdict (Level::idle, "No clear notes",
                    "Only noise or drums right now. Dissonance is measured on pitched sounds.");
        return;
    }

    const juce::String detail = numClashes > 0 ? clashSentence (clashes[0]) : juce::String();

    if (score > 60.0f)
        setVerdict (Level::problem, "Clashing", detail.isNotEmpty() ? detail : "Many partials rub against each other.");
    else if (score > 35.0f)
        setVerdict (Level::caution, "Tense", detail.isNotEmpty() ? detail
                                                                 : "Partials crowd together and beat. Try spreading the voicing.");
    else
        setVerdict (Level::good, "Consonant", "The notes sit well together.");
}

void DissonancePanel::paintSpectrum (juce::Graphics& g, juce::Rectangle<float> plot)
{
    g.setColour (colours::background.withAlpha (0.55f));
    g.fillRoundedRectangle (plot, 4.0f);

    const float fLo = 40.0f, fHi = 5000.0f, range = 72.0f;
    auto xOf = [plot, fLo, fHi] (float f) { return plot.getX() + plot.getWidth() * std::log (f / fLo) / std::log (fHi / fLo); };
    auto yOf = [plot, this, range] (float db) { return juce::jmap (juce::jlimit (displayTop - range, displayTop, db), displayTop, displayTop - range, plot.getY() + 2.0f, plot.getBottom()); };

    const std::pair<float, const char*> gridFreqs[] = { { 50.0f, "50" }, { 100.0f, "100" }, { 200.0f, "200" },
                                                        { 500.0f, "500" }, { 1000.0f, "1k" }, { 2000.0f, "2k" } };
    g.setColour (colours::gridFine);
    for (const auto& gf : gridFreqs)
        g.drawVerticalLine (juce::roundToInt (xOf (gf.first)), plot.getY(), plot.getBottom());

    juce::Path line, fill;
    bool started = false;
    for (int i = 0; i < kSpectrumPoints; ++i)
    {
        const float f = Analyzer::spectrumPointFreq (i);
        if (f < fLo || f > fHi * 1.05f)
            continue;
        const float x = juce::jmin (xOf (f), plot.getRight()), y = yOf (spectrum[(size_t) i]);
        if (! started)
        {
            line.startNewSubPath (x, y);
            fill.startNewSubPath (x, plot.getBottom());
            fill.lineTo (x, y);
            started = true;
        }
        else
        {
            line.lineTo (x, y);
            fill.lineTo (x, y);
        }
    }
    if (started)
    {
        fill.lineTo (plot.getRight(), plot.getBottom());
        fill.closeSubPath();
        g.setColour (colours::inkFaint.withAlpha (active ? 0.28f : 0.12f));
        g.fillPath (fill);
        g.setColour (colours::inkDim.withAlpha (active ? 0.8f : 0.3f));
        g.strokePath (line, juce::PathStrokeType (1.0f));
    }

    g.setFont (font (Weight::regular, 11.0f));
    g.setColour (colours::inkDim);
    for (const auto& gf : gridFreqs)
        g.drawText (gf.second, juce::Rectangle<float> (xOf (gf.first) + 3.0f, plot.getBottom() - 14.0f, 30.0f, 12.0f),
                    juce::Justification::centredLeft);
    drawLabel (g, "Hz", juce::Rectangle<float> (plot.getRight() - 24.0f, plot.getBottom() - 14.0f, 20.0f, 12.0f),
               colours::inkFaint, 11.0f, Weight::regular, juce::Justification::centredRight);

    if (! active)
        return;

    // partials, coloured by how much they are roughened by their neighbours
    for (int i = numPeaks - 1; i >= 0; --i)
    {
        const auto& p = peaks[(size_t) i];
        if (p.freq < fLo || p.freq > fHi)
            continue;
        const float r = p.fundamental ? 4.5f : 2.8f;
        const auto c = severityColour (p.roughness);
        const auto centre = juce::Point<float> (xOf (p.freq), yOf (p.levelDb));
        g.setColour (c);
        if (p.fundamental)
            g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre), 1.6f);
        else
            g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre));
    }
}

void DissonancePanel::paintNotes (juce::Graphics& g, juce::Rectangle<float> strip)
{
    static const char* const names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    std::array<bool, 12> clashing {};
    for (int i = 0; i < numClashes; ++i)
    {
        clashing[(size_t) (((clashes[(size_t) i].noteA % 12) + 12) % 12)] = true;
        clashing[(size_t) (((clashes[(size_t) i].noteB % 12) + 12) % 12)] = true;
    }

    auto labels = strip.removeFromBottom (14.0f);
    const float cellW = strip.getWidth() / 12.0f;

    for (int p = 0; p < 12; ++p)
    {
        auto cell = juce::Rectangle<float> (strip.getX() + cellW * (float) p, strip.getY(), cellW, strip.getHeight()).reduced (cellW * 0.18f, 0.0f);
        const bool sharp = std::string (names[p]).size() > 1;

        g.setColour (sharp ? colours::background.withAlpha (0.8f) : colours::background.withAlpha (0.45f));
        g.fillRect (cell);

        const float v = active && tonal ? chroma[(size_t) p] : 0.0f;
        if (v > 0.02f)
        {
            const float h = cell.getHeight() * v;
            g.setColour (clashing[(size_t) p] ? colours::problem : colours::trace.withAlpha (0.75f));
            g.fillRect (cell.withTop (cell.getBottom() - h));
        }

        drawLabel (g, names[p], juce::Rectangle<float> (strip.getX() + cellW * (float) p, labels.getY(), cellW, labels.getHeight()),
                   clashing[(size_t) p] ? colours::problem : colours::inkFaint, 11.5f, Weight::medium, juce::Justification::centred);
    }
}

void DissonancePanel::paintContent (juce::Graphics& g, juce::Rectangle<float> area)
{
    auto top = area.removeFromTop (44.0f);
    const bool live = active && tonal;
    auto readoutArea = top.removeFromLeft (112.0f);
    const juce::String scoreText = live ? juce::String (juce::roundToInt (score)) : dash();
    drawReadout (g, scoreText, readoutArea, live);

    const auto bigFont = font (Weight::semibold, 38.0f);
    const float scoreW = tabularWidth (scoreText, bigFont);
    drawLabel (g, "/ 100", readoutArea.withTrimmedLeft (scoreW + 6.0f).withTrimmedTop (14.0f), colours::inkFaint, 15.0f);

    auto stats = top.withTrimmedLeft (24.0f);
    drawStat (g, "Note tension", live ? juce::String (juce::roundToInt (tension)) : dash(), stats.removeFromTop (22.0f),
              live ? colours::ink : colours::inkFaint);
    drawStat (g, "Roughness", live ? juce::String (juce::roundToInt (roughness)) : dash(), stats.removeFromTop (22.0f),
              live ? colours::ink : colours::inkFaint);

    area.removeFromTop (10.0f);
    auto notes = area.removeFromBottom (46.0f);
    area.removeFromBottom (8.0f);

    paintSpectrum (g, area);
    paintNotes (g, notes);
}

} // namespace mi::ui
