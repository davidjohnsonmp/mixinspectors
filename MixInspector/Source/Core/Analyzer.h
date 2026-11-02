// MixInspector - stereo & harmonic analysis plugin
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "FFT.h"

#include <array>
#include <complex>
#include <string>
#include <vector>

namespace mi
{
constexpr int kNumBroadBands   = 4;   // Low / Low-mid / High-mid / High (correlation)
constexpr int kNumOctaveBands  = 10;  // 31.5 Hz ... 16 kHz (phase + width)
constexpr int kMaxPeaks        = 64;  // tonal partials considered for dissonance
constexpr int kMaxClashes      = 3;
constexpr int kSpectrumPoints  = 160; // log-spaced display spectrum, 20 Hz - 20 kHz

/** How quickly the meters follow the music. */
enum class Response { Fast = 0, Normal = 1, Slow = 2 };

struct Peak
{
    float freq = 0.0f;       // Hz
    float levelDb = -120.0f; // dBFS (mono sum)
    float roughness = 0.0f;  // 0..1, how strongly neighbouring partials roughen this one
    bool  fundamental = false; // true if this looks like a played note rather than an overtone
};

struct Clash
{
    int noteA = -1, noteB = -1; // MIDI note numbers of the clashing partials
    float amount = 0.0f;        // 0..1, share of total roughness
};

/** Everything the UI needs, refreshed by Analyzer::process(). */
struct Results
{
    // general
    bool  active  = false;       // signal above the -70 dBFS gate
    float levelLDb = -120.0f, levelRDb = -120.0f;

    // 1 - correlation
    float correlation = 0.0f;                              // -1 .. +1
    std::array<float, kNumBroadBands> bandCorrelation {};  // -1 .. +1
    std::array<bool,  kNumBroadBands> broadBandActive {};

    // 2 - phase
    bool  polarityInverted = false;
    float delaySamples = 0.0f;     // > 0 : right channel lags left
    float delayMs = 0.0f;
    float delayConfidence = 0.0f;  // 0 .. 1
    float monoSumDb = 0.0f;        // level change when folded to mono (0 = no loss)
    std::array<float, kNumOctaveBands> bandMonoSumDb {};
    std::array<float, kNumOctaveBands> bandPhaseDeg {};
    std::array<float, kNumOctaveBands> bandLevelDb {};
    std::array<bool,  kNumOctaveBands> octaveBandActive {};

    // 3 - stereo width
    float width = 0.0f;            // 0 = mono, 1 = fully decorrelated, 2 = fully out of phase
    float balanceDb = 0.0f;        // > 0 : right is louder
    float sideToMidDb = -120.0f;
    std::array<float, kNumOctaveBands> bandWidth {};

    // 4 - harmonic dissonance
    bool  tonal = false;           // at least one clear tonal partial
    float dissonance = 0.0f;       // 0 .. 100, overall score (70% tension + 30% roughness)
    float tension = 0.0f;          // 0 .. 100, clashing intervals between the notes being played
    float roughness = 0.0f;        // 0 .. 100, sensory beating between partials (register dependent)
    float rawRoughness = 0.0f;     // un-mapped roughness measure
    std::array<float, 12> chroma {};   // pitch-class weight of the played notes, 0 .. 1 (C = 0)
    std::array<Clash, kMaxClashes> clashes {};
    int numClashes = 0;
    std::array<Peak, kMaxPeaks> peaks {};
    int numPeaks = 0;
    std::array<float, kSpectrumPoints> spectrumDb {};   // mono-sum spectrum, dBFS
};

/**
    The analysis engine. Feed it stereo audio with process(); read getResults().
    Not thread-safe: call everything from one thread (the plugin uses the UI thread).
*/
class Analyzer
{
public:
    Analyzer();

    void prepare (double sampleRate);
    void setResponse (Response r);
    Response getResponse() const noexcept { return response; }
    void reset();

    void process (const float* left, const float* right, int numSamples);

    const Results& getResults() const noexcept { return results; }
    double getSampleRate() const noexcept      { return fs; }
    int    getFftSize() const noexcept         { return fft.getSize(); }
    int    getDissonanceFftSize() const noexcept { return dissFft.getSize(); }

    /** Copies the most recent samples (oldest first) for the goniometer. Returns the count. */
    int copyRecentSamples (float* left, float* right, int maxSamples) const;

    static float       octaveBandCentre (int band);
    static const char* octaveBandLabel (int band);
    static const char* broadBandName (int band);
    static float       spectrumPointFreq (int index);
    static std::string noteName (int midiNote);

    /** Plomp-Levelt / Sethares roughness of two partials, normalised so the worst case is 1. */
    static float pairRoughness (float f1, float f2) noexcept;

private:
    using Complex = std::complex<float>;

    struct BandAccumulator
    {
        double ll = 0.0, rr = 0.0;
        double lrRe = 0.0, lrIm = 0.0;
    };

    void configureCoefficients();
    void processFrame();
    void updateBandResults();
    void updateDelay();
    void processDissonanceFrame();
    void updateDissonance();
    void updateSpectrum();
    void updateTimeDomainResults();

    double fs = 48000.0;
    Response response = Response::Normal;

    // stereo analysis: ~5.9 Hz resolution, hop of a quarter frame
    FFT fft;
    int fftSize = 8192, hopSize = 2048;
    std::vector<float> window;
    std::vector<Complex> fftBuffer, gccBuffer;

    // harmonic analysis: 4x longer frame (~1.5 Hz resolution) so semitones resolve down to ~100 Hz
    FFT dissFft;
    int dissSize = 32768;
    std::vector<float> dissWindow;
    std::vector<Complex> dissBuffer;
    int framesSinceDissonance = 0;

    // circular input buffer, long enough for the harmonic analysis frame
    std::vector<float> ringL, ringR;
    int ringPos = 0, ringFilled = 0, samplesSinceFrame = 0;

    // goniometer history
    static constexpr int kScopeSize = 4096;
    std::array<float, kScopeSize> scopeL {}, scopeR {};
    int scopePos = 0, scopeFilled = 0;

    // time-domain exponential averages
    double tdCoeff = 0.0;
    double eLL = 0.0, eRR = 0.0, eLR = 0.0;

    // frame-domain smoothing coefficients
    double bandCoeff = 0.0, gccCoeff = 0.0, spectrumCoeff = 0.0, dissonanceCoeff = 0.0;

    std::array<BandAccumulator, kNumOctaveBands> octaveAcc {};
    std::array<BandAccumulator, kNumBroadBands>  broadAcc {};
    std::vector<int> binToOctave, binToBroad;

    std::vector<Complex> crossSpectrum;   // smoothed L * conj(R), per bin
    std::vector<float> autoL, autoR;      // smoothed |L|^2, |R|^2, per bin
    std::vector<float> monoPower;         // smoothed |(L+R)/2|^2, per bin of the long frame

    // scratch space for the dissonance analysis (allocated in prepare)
    struct ClashAcc { int pcA, pcB, noteA, noteB; float amount, unused; };
    std::vector<float> scratchDb;
    std::vector<Peak> candidates;
    std::vector<ClashAcc> clashAcc;

    float smoothedDissonance = 0.0f, smoothedTension = 0.0f, smoothedRoughness = 0.0f;
    bool haveFrame = false;

    Results results;
};

} // namespace mi
