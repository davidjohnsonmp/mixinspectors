// MixInspector - stereo & harmonic analysis plugin
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Panels.h"

#include <functional>

namespace mi::ui
{
/** The whole plugin window: a header with the controls and the four checker panels.
    Owns the Analyzer; everything here runs on the message thread. */
class MainView : public juce::Component
{
public:
    MainView();
    ~MainView() override;

    /** Reconfigures the analysis if the sample rate changed. */
    void prepare (double sampleRate);

    /** Analyse a block of stereo audio. */
    void feed (const float* left, const float* right, int numSamples);

    /** Pushes the latest results into the panels and repaints them (unless held). */
    void refreshDisplay();

    void setResponse (int index);   // 0 = fast, 1 = normal, 2 = slow
    int  getResponse() const noexcept { return responseIndex; }
    std::function<void (int)> onResponseChange;

    Analyzer& getAnalyzer() noexcept { return analyzer; }

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int defaultWidth = 1040, defaultHeight = 720;
    static constexpr int minWidth = 860, minHeight = 600;

private:
    void resetAll();

    LookAndFeel lookAndFeel;   // declared first so it outlives the components using it
    juce::SharedResourcePointer<Fonts> fonts;
    juce::TooltipWindow tooltips { this, 600 };

    Analyzer analyzer;
    double currentRate = 0.0;
    int responseIndex = 1;
    bool held = false;

    CorrelationPanel correlation;
    PhasePanel phase;
    WidthPanel width;
    DissonancePanel dissonance;

    juce::TextButton fastButton { "Fast" }, normalButton { "Normal" }, slowButton { "Slow" };
    juce::TextButton holdButton { "Hold" }, resetButton { "Reset" };
    juce::Rectangle<int> headerArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};

} // namespace mi::ui
