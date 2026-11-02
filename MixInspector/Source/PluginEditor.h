// MixInspector - stereo & harmonic analysis plugin
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "PluginProcessor.h"
#include "UI/MainView.h"

class MixInspectorEditor : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit MixInspectorEditor (MixInspectorProcessor&);
    ~MixInspectorEditor() override;

    void resized() override;

private:
    void timerCallback() override;

    MixInspectorProcessor& audioProcessor;
    mi::ui::MainView view;
    std::vector<float> scratchL, scratchR;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixInspectorEditor)
};
