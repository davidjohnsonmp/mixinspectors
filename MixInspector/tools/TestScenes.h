// MixInspector - synthetic test mixes shared by the unit tests and the screenshot tool
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cmath>
#include <random>
#include <vector>

namespace mi::scenes
{
constexpr double kTwoPi = 6.283185307179586;

struct Scene
{
    std::vector<float> left, right;
};

inline double midiToHz (double m) { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }

/** Harmonic tone. `rightJitter` gives each harmonic a random phase offset on the right
    channel (up to +-rightJitter radians): partial decorrelation without a time shift. */
inline void addTone (Scene& s, double fs, double midi, int harmonics, float amp, float gainL, float gainR,
              double rightJitter = 0.0)
{
    std::mt19937 rng ((unsigned) (midi * 100.0));
    std::uniform_real_distribution<double> jitter (-rightJitter, rightJitter);
    const double f0 = midiToHz (midi);
    for (int h = 1; h <= harmonics && f0 * h < fs * 0.45; ++h)
    {
        const double w = kTwoPi * f0 * h / fs;
        const double phL = 0.37 * h + midi, phR = phL + jitter (rng);
        for (size_t i = 0; i < s.left.size(); ++i)
        {
            const float a = amp / (float) h;
            s.left[i]  += gainL * a * (float) std::sin (w * (double) i + phL);
            s.right[i] += gainR * a * (float) std::sin (w * (double) i + phR);
        }
    }
}

inline void addDrums (Scene& s, double fs, unsigned seed)
{
    std::mt19937 rng (seed);
    std::normal_distribution<float> nd (0.0f, 1.0f);
    float hl1 = 0, hl2 = 0, hr1 = 0, hr2 = 0;

    for (size_t i = 0; i < s.left.size(); ++i)
    {
        const double t = (double) i / fs;

        // kick every half second, mono, tuned to A1 like the bass
        const double tk = std::fmod (t, 0.5);
        const double kick = 0.45 * std::exp (-tk / 0.11) * std::sin (kTwoPi * (55.0 * tk + 40.0 * (1.0 - std::exp (-tk / 0.03)) * 0.03));

        // hats every eighth note, independent noise per side, crude high-pass
        const double th = std::fmod (t, 0.25);
        const float env = (float) (0.06 * std::exp (-th / 0.025));
        const float nl = nd (rng), nr = nd (rng);
        const float hl = nl - 2.0f * hl1 + hl2, hr = nr - 2.0f * hr1 + hr2;
        hl2 = hl1; hl1 = nl; hr2 = hr1; hr1 = nr;

        s.left[i]  += (float) kick + env * hl;
        s.right[i] += (float) kick + env * hr;
    }
}

/** Centred bass and kick, a gently wide A-major pad, a lead slightly right, wide hats. */
inline Scene healthyMix (double fs, int n)
{
    Scene s { std::vector<float> ((size_t) n, 0.0f), std::vector<float> ((size_t) n, 0.0f) };
    addTone (s, fs, 33, 5, 0.20f, 1.0f, 1.0f);                       // A1 bass, centred
    for (double m : { 57.0, 61.0, 64.0 })                              // A major pad, gently wide
        addTone (s, fs, m, 6, 0.07f, 1.0f, 1.0f, 1.2);
    addTone (s, fs, 76, 3, 0.06f, 0.75f, 1.0f);                        // E5 lead, a touch right
    addDrums (s, fs, 3);
    return s;
}

/** Wide bass, an E/F clash in the pad, and the right channel 21 samples late. */
inline Scene problemMix (double fs, int n)
{
    Scene s { std::vector<float> ((size_t) n, 0.0f), std::vector<float> ((size_t) n, 0.0f) };
    addTone (s, fs, 33, 5, 0.20f, 1.0f, 1.0f, 2.2);                   // bass smeared across the stereo field
    for (double m : { 57.0, 61.0, 64.0, 65.0 })                        // pad with an E/F clash
        addTone (s, fs, m, 6, 0.07f, 1.0f, 1.0f, 1.2);
    addTone (s, fs, 76, 3, 0.06f, 0.75f, 1.0f);
    addDrums (s, fs, 4);

    // the whole right channel arrives 21 samples late
    const int d = 21;
    for (int i = n - 1; i >= d; --i)
        s.right[(size_t) i] = s.right[(size_t) (i - d)];
    for (int i = 0; i < d; ++i)
        s.right[(size_t) i] = 0.0f;
    return s;
}

} // namespace mi::scenes
