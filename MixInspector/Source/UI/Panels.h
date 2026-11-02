// MixInspector - stereo & harmonic analysis plugin
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Theme.h"
#include "Analyzer.h"

#include <array>
#include <vector>

namespace mi::ui
{
/** A checker panel: title, a one-word verdict with a status colour, the measurement,
    and one plain-language sentence explaining what the verdict means. */
class Panel : public juce::Component
{
public:
    explicit Panel (const juce::String& titleText);

    void paint (juce::Graphics&) override;

    virtual void update (const Results& results, const Analyzer& analyzer) = 0;
    virtual void clear() = 0;

    Level getLevel() const noexcept                 { return level; }
    const juce::String& getVerdict() const noexcept { return verdict; }
    const juce::String& getHint() const noexcept    { return hint; }

protected:
    virtual void paintContent (juce::Graphics&, juce::Rectangle<float> area) = 0;
    void setVerdict (Level newLevel, const juce::String& label, const juce::String& explanation);

private:
    juce::SharedResourcePointer<Fonts> fonts;
    juce::String title, verdict, hint;
    Level level = Level::idle;
};

//==============================================================================
class CorrelationPanel : public Panel
{
public:
    CorrelationPanel();
    void update (const Results&, const Analyzer&) override;
    void clear() override;

private:
    void paintContent (juce::Graphics&, juce::Rectangle<float>) override;

    bool active = false;
    float value = 0.0f, lowestRecent = 0.0f;
    std::array<float, kNumBroadBands> bands {};
    std::array<bool, kNumBroadBands> bandActive {};
    History history { 300 };   // 10 s at 30 updates per second
};

//==============================================================================
class PhasePanel : public Panel
{
public:
    PhasePanel();
    void update (const Results&, const Analyzer&) override;
    void clear() override;

private:
    void paintContent (juce::Graphics&, juce::Rectangle<float>) override;
    void paintScope (juce::Graphics&, juce::Rectangle<float>);

    static constexpr int kScopeSamples = 1024;
    static constexpr int kTrails = 3;
    std::array<std::vector<float>, kTrails> trailL, trailR;
    std::array<int, kTrails> trailCount {};
    float scopePeak = 0.05f;

    bool active = false, inverted = false;
    float delayMs = 0.0f, delaySamples = 0.0f, confidence = 0.0f, monoSum = 0.0f;
    std::array<float, kNumOctaveBands> bandMonoSum {};
    std::array<bool, kNumOctaveBands> bandActive {};
};

//==============================================================================
class WidthPanel : public Panel
{
public:
    WidthPanel();
    void update (const Results&, const Analyzer&) override;
    void clear() override;

private:
    void paintContent (juce::Graphics&, juce::Rectangle<float>) override;

    bool active = false;
    float width = 0.0f, balance = 0.0f, sideToMid = -60.0f;
    std::array<float, kNumOctaveBands> bandWidth {};
    std::array<bool, kNumOctaveBands> bandActive {};
};

//==============================================================================
class DissonancePanel : public Panel
{
public:
    DissonancePanel();
    void update (const Results&, const Analyzer&) override;
    void clear() override;

private:
    void paintContent (juce::Graphics&, juce::Rectangle<float>) override;
    void paintSpectrum (juce::Graphics&, juce::Rectangle<float>);
    void paintNotes (juce::Graphics&, juce::Rectangle<float>);

    bool active = false, tonal = false;
    float score = 0.0f, tension = 0.0f, roughness = 0.0f;
    float displayTop = -20.0f;
    std::array<float, kSpectrumPoints> spectrum {};
    std::array<Peak, kMaxPeaks> peaks {};
    int numPeaks = 0;
    std::array<float, 12> chroma {};
    std::array<Clash, kMaxClashes> clashes {};
    int numClashes = 0;
};

} // namespace mi::ui
