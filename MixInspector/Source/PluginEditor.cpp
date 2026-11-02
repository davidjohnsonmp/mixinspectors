// MixInspector - stereo & harmonic analysis plugin
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginEditor.h"

using mi::ui::MainView;

MixInspectorEditor::MixInspectorEditor (MixInspectorProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), scratchL (4096), scratchR (4096)
{
    addAndMakeVisible (view);

    view.prepare (audioProcessor.getAnalysisSampleRate());
    view.setResponse (audioProcessor.responseIndex);
    view.onResponseChange = [this] (int index) { audioProcessor.responseIndex = index; };

    // start from fresh audio rather than whatever was left in the FIFO
    audioProcessor.discardPendingSamples();
    audioProcessor.setEditorOpen (true);

    // read the remembered size first: setResizeLimits() may resize (and overwrite it)
    const int w = audioProcessor.editorWidth  > 0 ? audioProcessor.editorWidth  : MainView::defaultWidth;
    const int h = audioProcessor.editorHeight > 0 ? audioProcessor.editorHeight : MainView::defaultHeight;

    setResizable (true, true);
    setResizeLimits (MainView::minWidth, MainView::minHeight, 2000, 1400);
    setSize (w, h);

    startTimerHz (30);
}

MixInspectorEditor::~MixInspectorEditor()
{
    stopTimer();
    audioProcessor.setEditorOpen (false);
}

void MixInspectorEditor::resized()
{
    view.setBounds (getLocalBounds());
    audioProcessor.editorWidth = getWidth();
    audioProcessor.editorHeight = getHeight();
}

void MixInspectorEditor::timerCallback()
{
    view.prepare (audioProcessor.getAnalysisSampleRate());

    for (;;)
    {
        const int n = audioProcessor.pullSamples (scratchL.data(), scratchR.data(), (int) scratchL.size());
        if (n <= 0)
            break;
        view.feed (scratchL.data(), scratchR.data(), n);
    }

    view.refreshDisplay();
}
