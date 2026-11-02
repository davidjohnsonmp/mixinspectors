// MixInspector - stereo & harmonic analysis plugin
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "MainView.h"

namespace mi::ui
{
MainView::MainView()
{
    setLookAndFeel (&lookAndFeel);
    setOpaque (true);

    for (auto* p : std::initializer_list<Panel*> { &correlation, &phase, &width, &dissonance })
        addAndMakeVisible (p);

    auto setupResponse = [this] (juce::TextButton& b, int index, const juce::String& tip)
    {
        b.setClickingTogglesState (true);
        b.setRadioGroupId (1001);
        b.setTooltip (tip);
        b.onClick = [this, index, &b]
        {
            if (b.getToggleState() && index != responseIndex)
            {
                responseIndex = index;
                analyzer.setResponse ((Response) index);
                if (onResponseChange)
                    onResponseChange (index);
            }
        };
        addAndMakeVisible (b);
    };

    setupResponse (fastButton, 0, "Meters react within about 0.1 s");
    setupResponse (normalButton, 1, "Meters average over about 0.3 s");
    setupResponse (slowButton, 2, "Meters average over about 1 s, good for whole sections");
    fastButton.setConnectedEdges (juce::Button::ConnectedOnRight);
    normalButton.setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    slowButton.setConnectedEdges (juce::Button::ConnectedOnLeft);
    normalButton.setToggleState (true, juce::dontSendNotification);

    holdButton.setClickingTogglesState (true);
    holdButton.setTooltip ("Freeze the display to read it while the music plays on");
    holdButton.onClick = [this] { held = holdButton.getToggleState(); };
    addAndMakeVisible (holdButton);

    resetButton.setTooltip ("Clear all averages and history");
    resetButton.onClick = [this] { resetAll(); };
    addAndMakeVisible (resetButton);

    setSize (defaultWidth, defaultHeight);
}

MainView::~MainView()
{
    setLookAndFeel (nullptr);
}

void MainView::prepare (double sampleRate)
{
    if (sampleRate > 0.0 && std::abs (sampleRate - currentRate) > 0.5)
    {
        currentRate = sampleRate;
        analyzer.prepare (sampleRate);
        analyzer.setResponse ((Response) responseIndex);
        for (auto* p : std::initializer_list<Panel*> { &correlation, &phase, &width, &dissonance })
            p->clear();
    }
}

void MainView::feed (const float* left, const float* right, int numSamples)
{
    analyzer.process (left, right, numSamples);
}

void MainView::refreshDisplay()
{
    if (held)
        return;

    const auto& results = analyzer.getResults();
    for (auto* p : std::initializer_list<Panel*> { &correlation, &phase, &width, &dissonance })
    {
        p->update (results, analyzer);
        p->repaint();
    }
}

void MainView::setResponse (int index)
{
    responseIndex = juce::jlimit (0, 2, index);
    analyzer.setResponse ((Response) responseIndex);
    juce::TextButton* buttons[] = { &fastButton, &normalButton, &slowButton };
    buttons[responseIndex]->setToggleState (true, juce::dontSendNotification);
}

void MainView::resetAll()
{
    analyzer.reset();
    for (auto* p : std::initializer_list<Panel*> { &correlation, &phase, &width, &dissonance })
    {
        p->clear();
        p->repaint();
    }
}

void MainView::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    auto h = headerArea.toFloat().withTrimmedLeft (4.0f);
    g.setColour (colours::ink);
    g.setFont (fonts->get (Weight::semibold, 24.0f));
    g.drawText ("MixInspector", h, juce::Justification::centredLeft, false);

    const float nameW = juce::GlyphArrangement::getStringWidth (fonts->get (Weight::semibold, 24.0f), "MixInspector");
    g.setColour (colours::inkDim);
    g.setFont (fonts->get (Weight::regular, 15.0f));
    g.drawText ("Correlation, phase, width and harmony at a glance",
                h.withTrimmedLeft (nameW + 14.0f).withTrimmedTop (3.0f), juce::Justification::centredLeft, true);

    // label for the response control
    auto label = fastButton.getBounds().toFloat().withX ((float) fastButton.getX() - 76.0f).withWidth (68.0f);
    g.setFont (fonts->get (Weight::regular, 14.0f));
    g.drawText ("Response", label, juce::Justification::centredRight, false);
}

void MainView::resized()
{
    auto bounds = getLocalBounds().reduced (12, 0);
    headerArea = bounds.removeFromTop (56);
    bounds.removeFromBottom (12);

    auto controls = headerArea.withSizeKeepingCentre (headerArea.getWidth(), 30);
    resetButton.setBounds (controls.removeFromRight (66));
    controls.removeFromRight (8);
    holdButton.setBounds (controls.removeFromRight (66));
    controls.removeFromRight (20);
    slowButton.setBounds (controls.removeFromRight (64));
    normalButton.setBounds (controls.removeFromRight (72));
    fastButton.setBounds (controls.removeFromRight (64));
    headerArea.setRight (fastButton.getX() - 84);

    const int gap = 10;
    auto topRow = bounds.removeFromTop ((bounds.getHeight() - gap) / 2);
    bounds.removeFromTop (gap);

    correlation.setBounds (topRow.removeFromLeft ((topRow.getWidth() - gap) / 2));
    topRow.removeFromLeft (gap);
    phase.setBounds (topRow);

    width.setBounds (bounds.removeFromLeft ((bounds.getWidth() - gap) / 2));
    bounds.removeFromLeft (gap);
    dissonance.setBounds (bounds);
}

} // namespace mi::ui
