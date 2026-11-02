// MixInspector - stereo & harmonic analysis plugin
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"
#include "PluginEditor.h"

MixInspectorProcessor::MixInspectorProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      fifoL ((size_t) fifoSize, 0.0f),
      fifoR ((size_t) fifoSize, 0.0f)
{
}

void MixInspectorProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate.store (sampleRate);
}

bool MixInspectorProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void MixInspectorProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn = getTotalNumInputChannels();
    const int numSamples = buffer.getNumSamples();

    for (int ch = numIn; ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    // Audio passes through unchanged (input and output share the buffer).
    if (! editorOpen.load (std::memory_order_relaxed) || numIn == 0 || numSamples == 0)
        return;

    const float* left  = buffer.getReadPointer (0);
    const float* right = buffer.getReadPointer (numIn > 1 ? 1 : 0);

    int start1, size1, start2, size2;
    fifo.prepareToWrite (numSamples, start1, size1, start2, size2);

    if (size1 > 0)
    {
        std::copy (left, left + size1, fifoL.begin() + start1);
        std::copy (right, right + size1, fifoR.begin() + start1);
    }
    if (size2 > 0)
    {
        std::copy (left + size1, left + size1 + size2, fifoL.begin() + start2);
        std::copy (right + size1, right + size1 + size2, fifoR.begin() + start2);
    }

    fifo.finishedWrite (size1 + size2);   // if the UI fell behind, the overflow is simply dropped
}

int MixInspectorProcessor::pullSamples (float* left, float* right, int maxSamples)
{
    int start1, size1, start2, size2;
    fifo.prepareToRead (maxSamples, start1, size1, start2, size2);

    if (size1 > 0)
    {
        std::copy (fifoL.begin() + start1, fifoL.begin() + start1 + size1, left);
        std::copy (fifoR.begin() + start1, fifoR.begin() + start1 + size1, right);
    }
    if (size2 > 0)
    {
        std::copy (fifoL.begin() + start2, fifoL.begin() + start2 + size2, left + size1);
        std::copy (fifoR.begin() + start2, fifoR.begin() + start2 + size2, right + size1);
    }

    fifo.finishedRead (size1 + size2);
    return size1 + size2;
}

void MixInspectorProcessor::discardPendingSamples()
{
    fifo.finishedRead (fifo.getNumReady());
}

juce::AudioProcessorEditor* MixInspectorProcessor::createEditor()
{
    return new MixInspectorEditor (*this);
}

//==============================================================================
void MixInspectorProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state ("MixInspector");
    state.setProperty ("version", 1, nullptr);
    state.setProperty ("response", responseIndex, nullptr);
    state.setProperty ("width", editorWidth, nullptr);
    state.setProperty ("height", editorHeight, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void MixInspectorProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        const auto state = juce::ValueTree::fromXml (*xml);
        if (state.hasType ("MixInspector"))
        {
            responseIndex = juce::jlimit (0, 2, (int) state.getProperty ("response", 1));
            editorWidth   = (int) state.getProperty ("width", 0);
            editorHeight  = (int) state.getProperty ("height", 0);
        }
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MixInspectorProcessor();
}
