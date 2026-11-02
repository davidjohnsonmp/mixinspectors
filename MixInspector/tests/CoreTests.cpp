// MixInspector - unit tests for the analysis core (no JUCE needed)
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Analyzer.h"
#include "TestScenes.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <string>
#include <vector>

using namespace mi;

namespace
{
int failures = 0, checks = 0;

void check (bool ok, const std::string& what, double value)
{
    ++checks;
    if (! ok)
    {
        ++failures;
        std::printf ("  FAIL  %s  (got %.4f)\n", what.c_str(), value);
    }
}

#define CHECK_RANGE(value, lo, hi) check ((value) >= (lo) && (value) <= (hi), #value " in [" #lo ", " #hi "]", (value))
#define CHECK_TRUE(cond)           check ((cond), #cond, 0.0)

using Signal = std::vector<float>;

Signal noise (unsigned seed, int n, float sd = 0.1f)
{
    std::mt19937 rng (seed);
    std::normal_distribution<float> dist (0.0f, sd);
    Signal s ((size_t) n);
    for (auto& v : s) v = dist (rng);
    return s;
}

Signal delayed (const Signal& x, int d, float gain = 1.0f)
{
    Signal y (x.size(), 0.0f);
    for (size_t i = 0; i < x.size(); ++i)
    {
        const long j = (long) i - d;
        if (j >= 0 && j < (long) x.size())
            y[i] = gain * x[(size_t) j];
    }
    return y;
}

Signal tones (const std::vector<double>& midiNotes, double fs, int n, int harmonics)
{
    Signal s ((size_t) n, 0.0f);
    for (double m : midiNotes)
    {
        const double f0 = 440.0 * std::pow (2.0, (m - 69.0) / 12.0);
        for (int h = 1; h <= harmonics && f0 * h < fs / 2; ++h)
            for (int i = 0; i < n; ++i)
                s[(size_t) i] += (float) (0.15 / h * std::sin (2.0 * kPi * f0 * h * i / fs + 0.7 * h + m));
    }
    return s;
}

/** Feed the analyzer in host-sized blocks. */
const Results& run (Analyzer& a, const Signal& l, const Signal& r, int block = 512)
{
    for (size_t pos = 0; pos < l.size(); pos += (size_t) block)
    {
        const int len = (int) std::min ((size_t) block, l.size() - pos);
        a.process (l.data() + pos, r.data() + pos, len);
    }
    return a.getResults();
}

void test (const char* name, const std::function<void()>& body)
{
    const int before = failures;
    body();
    std::printf ("%s  %s\n", failures == before ? "ok  " : "FAIL", name);
}
} // namespace

int main()
{
    const double fs = 48000.0;
    const int n = (int) (fs * 3.0);

    test ("identical channels: correlation +1, mono, no delay", []
    {
        Analyzer a; a.prepare (48000.0);
        const auto x = noise (1, 144000);
        const Results r = run (a, x, x);
        CHECK_TRUE (r.active);
        CHECK_RANGE (r.correlation, 0.99f, 1.0f);
        CHECK_RANGE (r.width, 0.0f, 0.02f);
        CHECK_RANGE (r.monoSumDb, -0.1f, 0.1f);
        CHECK_RANGE (r.delaySamples, -0.5f, 0.5f);
        CHECK_RANGE (r.delayConfidence, 0.8f, 1.0f);
        CHECK_TRUE (! r.polarityInverted);
        for (int b = 0; b < kNumBroadBands; ++b)
            CHECK_RANGE (r.bandCorrelation[(size_t) b], 0.99f, 1.0f);
    });

    test ("inverted channel: correlation -1, polarity flag, mono cancels", []
    {
        Analyzer a; a.prepare (48000.0);
        const auto x = noise (2, 144000);
        const auto y = delayed (x, 0, -1.0f);
        const Results r = run (a, x, y);
        CHECK_RANGE (r.correlation, -1.0f, -0.99f);
        CHECK_RANGE (r.width, 1.98f, 2.0f);
        CHECK_RANGE (r.monoSumDb, -60.0f, -40.0f);
        CHECK_TRUE (r.polarityInverted);
        CHECK_RANGE (r.delayConfidence, 0.8f, 1.0f);
    });

    test ("independent channels: correlation 0, width 100%, mono -3 dB", []
    {
        Analyzer a; a.prepare (48000.0);
        const Results r = run (a, noise (3, 144000), noise (4, 144000));
        CHECK_RANGE (r.correlation, -0.1f, 0.1f);
        CHECK_RANGE (r.width, 0.9f, 1.1f);
        CHECK_RANGE (r.monoSumDb, -3.5f, -2.5f);
        CHECK_RANGE (r.delayConfidence, 0.0f, 0.3f);
    });

    test ("right channel 24 samples late is measured as +24 samples / 0.5 ms", []
    {
        Analyzer a; a.prepare (48000.0);
        const auto x = noise (5, 144000);
        const Results r = run (a, x, delayed (x, 24));
        CHECK_RANGE (r.delaySamples, 23.5f, 24.5f);
        CHECK_RANGE (r.delayMs, 0.48f, 0.52f);
        CHECK_RANGE (r.delayConfidence, 0.5f, 1.0f);
        CHECK_TRUE (! r.polarityInverted);
    });

    test ("left channel 7 samples late is measured as -7 samples", []
    {
        Analyzer a; a.prepare (48000.0);
        const auto x = noise (6, 144000);
        const Results r = run (a, delayed (x, 7), x);
        CHECK_RANGE (r.delaySamples, -7.5f, -6.5f);
    });

    test ("inverted AND 10 samples late: both detected", []
    {
        Analyzer a; a.prepare (48000.0);
        const auto x = noise (7, 144000);
        const Results r = run (a, x, delayed (x, 10, -1.0f));
        CHECK_TRUE (r.polarityInverted);
        CHECK_RANGE (r.delaySamples, 9.5f, 10.5f);
    });

    test ("delay survives a wide mix (shared delayed source + independent ambience)", []
    {
        Analyzer a; a.prepare (48000.0);
        const auto src = noise (8, 144000);
        auto l = noise (9, 144000, 0.07f), rr = noise (10, 144000, 0.07f);
        const auto d = delayed (src, 15);
        for (size_t i = 0; i < l.size(); ++i) { l[i] += src[i]; rr[i] += d[i]; }
        const Results r = run (a, l, rr);
        CHECK_RANGE (r.delaySamples, 14.5f, 15.5f);
        CHECK_RANGE (r.delayConfidence, 0.4f, 1.0f);
    });

    test ("healthy test mix: no false alarms", []
    {
        Analyzer a; a.prepare (48000.0);
        const auto s = scenes::healthyMix (48000.0, 240000);
        const Results r = run (a, s.left, s.right);
        CHECK_RANGE (r.correlation, 0.6f, 1.0f);
        CHECK_RANGE (r.bandCorrelation[0], 0.9f, 1.0f);
        CHECK_RANGE (r.delaySamples, -0.99f, 0.99f);   // a chorused pad is not a time offset
        CHECK_TRUE (! r.polarityInverted);
        CHECK_RANGE (r.monoSumDb, -1.0f, 0.1f);
        CHECK_RANGE (r.dissonance, 0.0f, 35.0f);
        CHECK_TRUE (r.numClashes == 0);
        CHECK_RANGE (r.chroma[9], 0.5f, 1.0f);    // A
        CHECK_RANGE (r.chroma[1], 0.1f, 1.0f);    // C# shows even though it sits on the bass's 5th harmonic
        CHECK_RANGE (r.chroma[4], 0.1f, 1.0f);    // E
        CHECK_RANGE (r.chroma[5], 0.0f, 0.05f);   // no F
    });

    test ("problem test mix: offset, wide lows and the E/F clash are all found", []
    {
        Analyzer a; a.prepare (48000.0);
        const auto s = scenes::problemMix (48000.0, 240000);
        const Results r = run (a, s.left, s.right);
        CHECK_RANGE (r.delaySamples, 19.5f, 22.5f);   // right channel is 21 samples late
        CHECK_RANGE (r.bandCorrelation[0], -1.0f, 0.5f);
        CHECK_TRUE (r.numClashes >= 1);
        if (r.numClashes >= 1)
            CHECK_TRUE (Analyzer::noteName (r.clashes[0].noteA) == "E4" && Analyzer::noteName (r.clashes[0].noteB) == "F4");
        CHECK_RANGE (r.dissonance, 35.0f, 100.0f);
    });

    test ("balance: right at half level reads -6 dB", []
    {
        Analyzer a; a.prepare (48000.0);
        const auto x = noise (11, 144000);
        const Results r = run (a, x, delayed (x, 0, 0.5f));
        CHECK_RANGE (r.balanceDb, -6.2f, -5.8f);
        CHECK_RANGE (r.correlation, 0.99f, 1.0f);
    });

    test ("per-band: mono lows + independent highs", [fs, n]
    {
        Analyzer a; a.prepare (fs);
        auto l = tones ({ 28, 31, 35 }, fs, n, 1);   // 41-62 Hz sines, identical in both channels
        auto rr = l;
        // independent high-passed noise (4th-order difference ~ +24 dB/oct)
        auto hp = [] (Signal s) { for (int k = 0; k < 4; ++k) for (size_t i = s.size() - 1; i > 0; --i) s[i] -= s[i - 1]; return s; };
        const auto hl = hp (noise (12, n, 0.02f)), hr = hp (noise (13, n, 0.02f));
        for (int i = 0; i < n; ++i) { l[(size_t) i] += hl[(size_t) i]; rr[(size_t) i] += hr[(size_t) i]; }
        const Results r = run (a, l, rr);
        CHECK_RANGE (r.bandCorrelation[0], 0.95f, 1.0f);
        CHECK_RANGE (r.bandCorrelation[3], -0.3f, 0.3f);
        CHECK_RANGE (r.bandWidth[1], 0.0f, 0.05f);        // 63 Hz
        CHECK_RANGE (r.bandWidth[9], 0.7f, 1.3f);         // 16 kHz
        CHECK_TRUE (r.octaveBandActive[1]);
        CHECK_TRUE (r.broadBandActive[3]);
    });

    test ("silence: inactive, no dissonance", []
    {
        Analyzer a; a.prepare (48000.0);
        const Signal z (96000, 0.0f);
        const Results r = run (a, z, z);
        CHECK_TRUE (! r.active);
        CHECK_TRUE (! r.tonal);
        CHECK_RANGE (r.dissonance, 0.0f, 0.01f);
    });

    test ("white noise has no tonal partials", []
    {
        Analyzer a; a.prepare (48000.0);
        const auto x = noise (14, 144000);
        const Results r = run (a, x, x);
        CHECK_RANGE ((float) r.numPeaks, 0.0f, 2.0f);
        CHECK_RANGE (r.dissonance, 0.0f, 10.0f);
    });

    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        const std::string label = "dissonance ranks intervals sensibly @ " + std::to_string ((int) rate) + " Hz";
        test (label.c_str(), [rate]
        {
            const int len = (int) (rate * 2.5);
            auto score = [rate, len] (std::vector<double> notes)
            {
                Analyzer a; a.prepare (rate);
                const auto s = tones (notes, rate, len, 6);
                return run (a, s, s).dissonance;
            };
            const float single = score ({ 60 }), fifth = score ({ 60, 67 }), triad = score ({ 60, 64, 67 });
            const float tritone = score ({ 60, 66 }), maj2 = score ({ 60, 62 }), min2 = score ({ 60, 61 });
            CHECK_RANGE (single, 0.0f, 5.0f);
            CHECK_RANGE (fifth, 0.0f, 15.0f);
            CHECK_RANGE (triad, 10.0f, 35.0f);
            CHECK_RANGE (min2, 60.0f, 100.0f);
            CHECK_TRUE (single < fifth && fifth < triad && triad < tritone && tritone < maj2 && maj2 < min2);
        });
    }

    test ("clash list names the clashing notes", []
    {
        Analyzer a; a.prepare (48000.0);
        const auto s = tones ({ 36, 76, 77 }, 48000.0, 120000, 8);   // C2 bass + E5/F5 clash
        const Results r = run (a, s, s);
        CHECK_TRUE (r.numClashes >= 1);
        if (r.numClashes >= 1)
        {
            CHECK_TRUE (Analyzer::noteName (r.clashes[0].noteA) == "E5");
            CHECK_TRUE (Analyzer::noteName (r.clashes[0].noteB) == "F5");
        }
        CHECK_RANGE (r.chroma[4], 0.5f, 1.0f);   // E
        CHECK_RANGE (r.chroma[5], 0.5f, 1.0f);   // F
        CHECK_RANGE (r.chroma[0], 0.5f, 1.0f);   // C
    });

    test ("note names and helpers", []
    {
        CHECK_TRUE (Analyzer::noteName (60) == "C4");
        CHECK_TRUE (Analyzer::noteName (61) == "C#4");
        CHECK_TRUE (Analyzer::noteName (69) == "A4");
        CHECK_RANGE (Analyzer::pairRoughness (440.0f, 440.0f), 0.0f, 0.001f);
        CHECK_RANGE (Analyzer::pairRoughness (440.0f, 466.16f), 0.9f, 1.0f);
        CHECK_RANGE (Analyzer::spectrumPointFreq (0), 19.9f, 20.1f);
        CHECK_RANGE (Analyzer::spectrumPointFreq (kSpectrumPoints - 1), 19900.0f, 20100.0f);
    });

    test ("reset, response changes and scope buffer", []
    {
        Analyzer a; a.prepare (48000.0);
        const auto x = noise (15, 48000);
        run (a, x, x);
        float l[512], r[512];
        CHECK_TRUE (a.copyRecentSamples (l, r, 512) == 512);
        CHECK_TRUE (l[511] == x[47999] && r[511] == x[47999]);
        a.setResponse (Response::Fast);
        run (a, x, x);
        a.setResponse (Response::Slow);
        run (a, x, x);
        a.reset();
        CHECK_TRUE (! a.getResults().active);
        CHECK_TRUE (a.copyRecentSamples (l, r, 512) == 0);
    });

    std::printf ("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
