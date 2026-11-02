// MixInspector - stereo & harmonic analysis plugin
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

/**
    Passes audio through untouched and copies it into a lock-free FIFO.
    All analysis happens on the UI side, and only while the editor is open,
    so the plugin costs next to nothing on the audio thread.
*/
class MixInspectorProcessor : public juce::AudioProcessor
{
public:
    MixInspectorProcessor();
    ~MixInspectorProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override                              { return 1; }
    int getCurrentProgram() override                           { return 0; }
    void setCurrentProgram (int) override                      {}
    const juce::String getProgramName (int) override           { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Used by the editor (message thread)
    void setEditorOpen (bool isOpen) noexcept { editorOpen.store (isOpen); }
    double getAnalysisSampleRate() const noexcept { return currentSampleRate.load(); }

    /** Pulls up to maxSamples stereo frames out of the FIFO. Returns the number read. */
    int pullSamples (float* left, float* right, int maxSamples);
    void discardPendingSamples();

    // remembered UI settings
    int responseIndex = 1;
    int editorWidth = 0, editorHeight = 0;

private:
    static constexpr int fifoSize = 1 << 16;
    juce::AbstractFifo fifo { fifoSize };
    std::vector<float> fifoL, fifoR;

    std::atomic<bool> editorOpen { false };
    std::atomic<double> currentSampleRate { 48000.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixInspectorProcessor)
};
