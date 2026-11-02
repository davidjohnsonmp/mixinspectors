// MixInspector - renders the UI fed with synthetic test mixes to PNG files.
// Usage: MixInspectorSnapshot <output-folder> [scale]
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "MainView.h"
#include "TestScenes.h"

namespace
{
using mi::scenes::Scene;

bool render (mi::ui::MainView& view, const Scene& scene, double fs, float scale, const juce::File& file)
{
    view.getAnalyzer().reset();
    const int n = (int) scene.left.size();
    const int block = 512, refreshEvery = (int) (fs / 30.0);
    int sinceRefresh = 0;

    for (int pos = 0; pos < n; pos += block)
    {
        const int len = juce::jmin (block, n - pos);
        view.feed (scene.left.data() + pos, scene.right.data() + pos, len);
        if ((sinceRefresh += len) >= refreshEvery)
        {
            sinceRefresh = 0;
            view.refreshDisplay();
        }
    }
    view.refreshDisplay();

    const auto image = view.createComponentSnapshot (view.getLocalBounds(), true, scale);
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat png;
    const bool ok = out.openedOk() && png.writeImageToStream (image, out);
    std::printf ("%s %s\n", ok ? "wrote" : "FAILED", file.getFullPathName().toRawUTF8());
    return ok;
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;

    const juce::File folder = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
                                       : juce::File::getCurrentWorkingDirectory();
    const float scale = argc > 2 ? (float) std::atof (argv[2]) : 1.0f;
    folder.createDirectory();

    const double fs = 48000.0;
    const int n = (int) (fs * 5.0);

    bool ok = true;
    {
        mi::ui::MainView view;
        view.setSize (mi::ui::MainView::defaultWidth, mi::ui::MainView::defaultHeight);
        view.prepare (fs);

        ok &= render (view, mi::scenes::healthyMix (fs, n), fs, scale, folder.getChildFile ("healthy-mix.png"));

        view.prepare (44100.0);   // forces a clean re-prepare
        view.prepare (fs);
        ok &= render (view, mi::scenes::problemMix (fs, n), fs, scale, folder.getChildFile ("problem-mix.png"));

        view.prepare (44100.0);
        view.prepare (fs);
        ok &= render (view, Scene { std::vector<float> ((size_t) fs, 0.0f), std::vector<float> ((size_t) fs, 0.0f) },
                      fs, scale, folder.getChildFile ("no-signal.png"));
    }

    return ok ? 0 : 1;
}
