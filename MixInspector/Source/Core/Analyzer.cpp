// MixInspector - stereo & harmonic analysis plugin
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Analyzer.h"

#include <algorithm>
#include <cmath>

namespace mi
{
namespace
{
    constexpr double kTiny   = 1.0e-20;
    constexpr double kGateMs = 1.0e-7;      // -70 dBFS mean square: below this we call it silence

    const char* const kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    const float kOctaveCentres[kNumOctaveBands] = { 31.5f, 63.0f, 125.0f, 250.0f, 500.0f,
                                                    1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f };
    const char* const kOctaveLabels[kNumOctaveBands] = { "31", "63", "125", "250", "500",
                                                         "1k", "2k", "4k", "8k", "16k" };

    const float kBroadEdges[kNumBroadBands + 1] = { 20.0f, 150.0f, 1000.0f, 5000.0f, 24000.0f };
    const char* const kBroadNames[kNumBroadBands] = { "Low", "Low-mid", "High-mid", "High" };

    float responseTime (Response r)
    {
        switch (r)
        {
            case Response::Fast: return 0.1f;
            case Response::Slow: return 1.0f;
            case Response::Normal:
            default:             return 0.3f;
        }
    }

    inline float powerToDb (double power)   { return (float) (10.0 * std::log10 (std::max (power, kTiny))); }
    template <typename T> inline T clampv (T v, T lo, T hi) { return std::min (hi, std::max (lo, v)); }

    inline int frequencyToMidi (float f) { return (int) std::lround (69.0 + 12.0 * std::log2 (f / 440.0)); }
    inline int pitchClass (int midi)     { return ((midi % 12) + 12) % 12; }

    /** Correlation of two signals given their energies and cross term.
        Returns 0 when one side is effectively silent (60 dB below the other). */
    inline float correlationOf (double ll, double rr, double lr)
    {
        const double hi = std::max (ll, rr), lo = std::min (ll, rr);
        if (hi < kTiny || lo < hi * 1.0e-6)
            return 0.0f;
        return (float) clampv (lr / std::sqrt (ll * rr), -1.0, 1.0);
    }
}

//==============================================================================
Analyzer::Analyzer()
{
    prepare (48000.0);
}

void Analyzer::prepare (double sampleRate)
{
    fs = sampleRate > 0.0 ? sampleRate : 48000.0;

    // keep the frequency resolution (~5.9 Hz) roughly constant across sample rates
    int order = 13;
    if (fs > 50000.0)  order = 14;
    if (fs > 100000.0) order = 15;

    fft.setOrder (order);
    fftSize = fft.getSize();
    hopSize = fftSize / 4;

    window.resize ((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * i / fftSize));

    fftBuffer.assign ((size_t) fftSize, {});
    gccBuffer.assign ((size_t) fftSize, {});

    dissFft.setOrder (order + 2);
    dissSize = dissFft.getSize();
    dissWindow.resize ((size_t) dissSize);
    for (int i = 0; i < dissSize; ++i)
        dissWindow[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * i / dissSize));
    dissBuffer.assign ((size_t) dissSize, {});

    ringL.assign ((size_t) dissSize, 0.0f);
    ringR.assign ((size_t) dissSize, 0.0f);

    const int numBins = fftSize / 2 + 1;
    binToOctave.assign ((size_t) numBins, -1);
    binToBroad.assign ((size_t) numBins, -1);

    for (int k = 1; k < numBins; ++k)
    {
        const double f = k * fs / fftSize;

        for (int b = 0; b < kNumOctaveBands; ++b)
        {
            const double lo = kOctaveCentres[b] / std::sqrt (2.0);
            const double hi = kOctaveCentres[b] * std::sqrt (2.0);
            if (f >= lo && f < hi)
                binToOctave[(size_t) k] = b;
        }

        for (int b = 0; b < kNumBroadBands; ++b)
            if (f >= kBroadEdges[b] && f < kBroadEdges[b + 1])
                binToBroad[(size_t) k] = b;
    }

    crossSpectrum.assign ((size_t) numBins, {});
    autoL.assign ((size_t) numBins, 0.0f);
    autoR.assign ((size_t) numBins, 0.0f);
    monoPower.assign ((size_t) dissSize / 2 + 1, 0.0f);

    scratchDb.assign ((size_t) dissSize / 2 + 1, -200.0f);
    candidates.clear();
    candidates.reserve ((size_t) dissSize / 8);
    clashAcc.clear();
    clashAcc.reserve (kMaxPeaks * kMaxPeaks);

    configureCoefficients();
    reset();
}

void Analyzer::configureCoefficients()
{
    const double tau     = responseTime (response);
    const double hopSec  = hopSize / fs;
    const double dissHop = 2.0 * hopSec;                          // harmonic frame runs every 2nd hop

    tdCoeff         = std::exp (-1.0 / (tau * fs));
    bandCoeff       = std::exp (-hopSec / tau);
    gccCoeff        = std::exp (-hopSec / 1.5);                  // delay estimate needs a long average
    spectrumCoeff   = std::exp (-dissHop / 0.2);
    dissonanceCoeff = std::exp (-dissHop / std::max (0.25, tau));
}

void Analyzer::setResponse (Response r)
{
    response = r;
    configureCoefficients();
}

void Analyzer::reset()
{
    std::fill (ringL.begin(), ringL.end(), 0.0f);
    std::fill (ringR.begin(), ringR.end(), 0.0f);
    ringPos = ringFilled = samplesSinceFrame = framesSinceDissonance = 0;

    scopeL.fill (0.0f);
    scopeR.fill (0.0f);
    scopePos = scopeFilled = 0;

    eLL = eRR = eLR = 0.0;
    octaveAcc.fill ({});
    broadAcc.fill ({});

    std::fill (crossSpectrum.begin(), crossSpectrum.end(), Complex {});
    std::fill (autoL.begin(), autoL.end(), 0.0f);
    std::fill (autoR.begin(), autoR.end(), 0.0f);
    std::fill (monoPower.begin(), monoPower.end(), 0.0f);

    smoothedDissonance = smoothedTension = smoothedRoughness = 0.0f;
    haveFrame = false;
    results = Results {};
    results.spectrumDb.fill (-120.0f);
}

//==============================================================================
void Analyzer::process (const float* left, const float* right, int numSamples)
{
    if (left == nullptr || numSamples <= 0)
        return;
    if (right == nullptr)
        right = left;

    const double a = tdCoeff, b = 1.0 - a;
    const int mask = dissSize - 1;

    for (int i = 0; i < numSamples; ++i)
    {
        const double l = left[i], r = right[i];

        eLL = a * eLL + b * l * l;
        eRR = a * eRR + b * r * r;
        eLR = a * eLR + b * l * r;

        ringL[(size_t) ringPos] = left[i];
        ringR[(size_t) ringPos] = right[i];
        ringPos = (ringPos + 1) & mask;
        ringFilled = std::min (ringFilled + 1, dissSize);

        scopeL[(size_t) scopePos] = left[i];
        scopeR[(size_t) scopePos] = right[i];
        scopePos = (scopePos + 1) % kScopeSize;
        scopeFilled = std::min (scopeFilled + 1, kScopeSize);

        if (++samplesSinceFrame >= hopSize && ringFilled >= fftSize)
        {
            samplesSinceFrame = 0;
            processFrame();

            if (++framesSinceDissonance >= 2)
            {
                framesSinceDissonance = 0;
                processDissonanceFrame();
            }
        }
    }

    // keep the averages out of denormal territory during long silences
    if (eLL < 1.0e-30) eLL = 0.0;
    if (eRR < 1.0e-30) eRR = 0.0;
    if (std::abs (eLR) < 1.0e-30) eLR = 0.0;

    updateTimeDomainResults();
}

void Analyzer::updateTimeDomainResults()
{
    results.levelLDb = powerToDb (eLL);
    results.levelRDb = powerToDb (eRR);
    results.active   = std::max (eLL, eRR) > kGateMs;

    const double sum = eLL + eRR;

    results.correlation = correlationOf (eLL, eRR, eLR);

    if (sum > kTiny)
    {
        results.width     = (float) clampv (1.0 - 2.0 * eLR / sum, 0.0, 2.0);
        results.monoSumDb = clampv (powerToDb ((sum + 2.0 * eLR) / (2.0 * sum)), -60.0f, 6.03f);

        const double mid  = std::max (0.0, (sum + 2.0 * eLR) * 0.25);
        const double side = std::max (0.0, (sum - 2.0 * eLR) * 0.25);
        results.sideToMidDb = clampv (powerToDb (side) - powerToDb (mid), -60.0f, 60.0f);

        if (eLL > kTiny && eRR > kTiny)
            results.balanceDb = clampv (powerToDb (eRR) - powerToDb (eLL), -40.0f, 40.0f);
        else
            results.balanceDb = eRR > eLL ? 40.0f : -40.0f;
    }
    else
    {
        results.width = 0.0f;
        results.monoSumDb = 0.0f;
        results.sideToMidDb = -60.0f;
        results.balanceDb = 0.0f;
    }
}

//==============================================================================
void Analyzer::processFrame()
{
    const int n = fftSize, mask = n - 1, numBins = n / 2 + 1;
    const int ringMask = dissSize - 1;
    const int start = ringPos - n;   // the newest n samples, oldest first

    for (int i = 0; i < n; ++i)
    {
        const int idx = (start + i) & ringMask;
        const float w = window[(size_t) i];
        fftBuffer[(size_t) i] = { ringL[(size_t) idx] * w, ringR[(size_t) idx] * w };
    }

    // two real FFTs for the price of one complex FFT: L in the real part, R in the imaginary part
    fft.perform (fftBuffer.data(), false);

    std::array<BandAccumulator, kNumOctaveBands> octFrame {};
    std::array<BandAccumulator, kNumBroadBands>  broadFrame {};

    const float g = (float) gccCoeff;

    for (int k = 0; k < numBins; ++k)
    {
        const Complex z  = fftBuffer[(size_t) k];
        const Complex zc = std::conj (fftBuffer[(size_t) ((n - k) & mask)]);

        const Complex xl ((z.real() + zc.real()) * 0.5f, (z.imag() + zc.imag()) * 0.5f);
        const float dr = z.real() - zc.real(), di = z.imag() - zc.imag();
        const Complex xr (di * 0.5f, -dr * 0.5f);

        const float pl = xl.real() * xl.real() + xl.imag() * xl.imag();
        const float pr = xr.real() * xr.real() + xr.imag() * xr.imag();

        // L * conj(R)
        const float lrRe = xl.real() * xr.real() + xl.imag() * xr.imag();
        const float lrIm = xl.imag() * xr.real() - xl.real() * xr.imag();

        if (const int o = binToOctave[(size_t) k]; o >= 0)
        {
            octFrame[(size_t) o].ll += pl;
            octFrame[(size_t) o].rr += pr;
            octFrame[(size_t) o].lrRe += lrRe;
            octFrame[(size_t) o].lrIm += lrIm;
        }

        if (const int bb = binToBroad[(size_t) k]; bb >= 0)
        {
            broadFrame[(size_t) bb].ll += pl;
            broadFrame[(size_t) bb].rr += pr;
            broadFrame[(size_t) bb].lrRe += lrRe;
            broadFrame[(size_t) bb].lrIm += lrIm;
        }

        Complex& cs = crossSpectrum[(size_t) k];
        cs = { g * cs.real() + (1.0f - g) * lrRe, g * cs.imag() + (1.0f - g) * lrIm };
        autoL[(size_t) k] = g * autoL[(size_t) k] + (1.0f - g) * pl;
        autoR[(size_t) k] = g * autoR[(size_t) k] + (1.0f - g) * pr;
    }

    const double c = bandCoeff, d = 1.0 - c;
    auto smooth = [c, d] (BandAccumulator& acc, const BandAccumulator& fr)
    {
        acc.ll   = c * acc.ll   + d * fr.ll;
        acc.rr   = c * acc.rr   + d * fr.rr;
        acc.lrRe = c * acc.lrRe + d * fr.lrRe;
        acc.lrIm = c * acc.lrIm + d * fr.lrIm;
    };

    for (int b = 0; b < kNumOctaveBands; ++b) smooth (octaveAcc[(size_t) b], octFrame[(size_t) b]);
    for (int b = 0; b < kNumBroadBands; ++b)  smooth (broadAcc[(size_t) b], broadFrame[(size_t) b]);

    haveFrame = true;

    updateBandResults();
    updateDelay();
}

void Analyzer::processDissonanceFrame()
{
    const int n = dissSize, mask = n - 1, numBins = n / 2 + 1;

    // mono sum (L+R)/2 of the newest n samples (zeros before the buffer has filled)
    for (int i = 0; i < n; ++i)
    {
        const int idx = (ringPos + i) & mask;
        dissBuffer[(size_t) i] = { 0.5f * (ringL[(size_t) idx] + ringR[(size_t) idx]) * dissWindow[(size_t) i], 0.0f };
    }

    dissFft.perform (dissBuffer.data(), false);

    const float s = (float) spectrumCoeff;
    for (int k = 0; k < numBins; ++k)
        monoPower[(size_t) k] = s * monoPower[(size_t) k] + (1.0f - s) * std::norm (dissBuffer[(size_t) k]);

    updateDissonance();
    updateSpectrum();
}

//==============================================================================
void Analyzer::updateBandResults()
{
    // sum of |X|^2 over a band -> mean-square level (Hann window, one-sided spectrum)
    const double scale = 16.0 / (3.0 * (double) fftSize * (double) fftSize);

    double maxOct = 0.0;
    for (const auto& acc : octaveAcc)
        maxOct = std::max (maxOct, acc.ll + acc.rr);

    for (int b = 0; b < kNumOctaveBands; ++b)
    {
        const auto& acc = octaveAcc[(size_t) b];
        const double total = acc.ll + acc.rr;
        const double ms = 0.5 * total * scale;

        results.bandLevelDb[(size_t) b]      = powerToDb (ms);
        results.octaveBandActive[(size_t) b] = ms > 1.0e-8 && total > maxOct * 1.0e-5;

        if (total > kTiny)
        {
            results.bandMonoSumDb[(size_t) b] = clampv (powerToDb ((total + 2.0 * acc.lrRe) / (2.0 * total)), -60.0f, 6.03f);
            results.bandWidth[(size_t) b]     = (float) clampv (1.0 - 2.0 * acc.lrRe / total, 0.0, 2.0);
            results.bandPhaseDeg[(size_t) b]  = (float) (std::atan2 (acc.lrIm, acc.lrRe) * 180.0 / kPi);
        }
        else
        {
            results.bandMonoSumDb[(size_t) b] = 0.0f;
            results.bandWidth[(size_t) b]     = 0.0f;
            results.bandPhaseDeg[(size_t) b]  = 0.0f;
        }
    }

    double maxBroad = 0.0;
    for (const auto& acc : broadAcc)
        maxBroad = std::max (maxBroad, acc.ll + acc.rr);

    for (int b = 0; b < kNumBroadBands; ++b)
    {
        const auto& acc = broadAcc[(size_t) b];
        const double total = acc.ll + acc.rr;

        results.broadBandActive[(size_t) b] = 0.5 * total * scale > 1.0e-8 && total > maxBroad * 1.0e-5;
        results.bandCorrelation[(size_t) b] = correlationOf (acc.ll, acc.rr, acc.lrRe);
    }
}

//==============================================================================
/*  Inter-channel delay + polarity via GCC-PHAT on the long-term cross spectrum.
    Each bin's phase is weighted by its coherence, so decorrelated (wide) parts of the
    mix don't drown out the part of the signal that is shared between L and R. */
void Analyzer::updateDelay()
{
    const int n = fftSize, mask = n - 1;

    const int kLo = std::max (1, (int) std::ceil (40.0 * n / fs));
    const int kHi = std::min (n / 2 - 1, (int) std::floor (16000.0 * n / fs));

    float maxAuto = 0.0f;
    for (int k = kLo; k <= kHi; ++k)
        maxAuto = std::max (maxAuto, autoL[(size_t) k] + autoR[(size_t) k]);

    std::fill (gccBuffer.begin(), gccBuffer.end(), Complex {});

    const float floorPower = std::max (maxAuto * 1.0e-7f, 1.0e-12f);
    double weightSum = 0.0;

    for (int k = kLo; k <= kHi; ++k)
    {
        const float pl = autoL[(size_t) k], pr = autoR[(size_t) k];
        if (pl < floorPower || pr < floorPower)
            continue;

        const Complex cs = crossSpectrum[(size_t) k];
        const float mag = std::abs (cs);
        if (mag <= 0.0f)
            continue;

        const float coherence = std::min (1.0f, (mag * mag) / (pl * pr));
        const float w = coherence / mag;

        gccBuffer[(size_t) k]       = { cs.real() * w, cs.imag() * w };
        gccBuffer[(size_t) (n - k)] = { cs.real() * w, -cs.imag() * w };
        weightSum += 2.0 * coherence;
    }

    if (weightSum < 1.0)
    {
        results.delayConfidence = 0.0f;
        results.delaySamples = results.delayMs = 0.0f;
        results.polarityInverted = false;
        return;
    }

    fft.perform (gccBuffer.data(), true);

    const int maxLag = std::min (n / 4, (int) (0.02 * fs));   // +-20 ms
    int best = 0;
    double bestScore = -1.0;

    for (int lag = -maxLag; lag <= maxLag; ++lag)
    {
        const double v = gccBuffer[(size_t) (lag & mask)].real();
        // slight preference for small lags resolves the ambiguity of periodic material
        const double score = std::abs (v) * (1.0 - 0.15 * std::abs (lag) / maxLag);
        if (score > bestScore)
        {
            bestScore = score;
            best = lag;
        }
    }

    // Steady tones with a fixed L/R phase difference (chorused pads etc.) can nudge the peak a
    // few samples off zero. A real offset with broadband content gives a sharp peak far above
    // the zero-lag value, so only accept a non-zero lag if it clearly beats lag 0.
    if (best != 0 && std::abs (gccBuffer[0].real()) >= 0.8 * std::abs (gccBuffer[(size_t) (best & mask)].real()))
        best = 0;

    const double y0 = std::abs (gccBuffer[(size_t) ((best - 1) & mask)].real());
    const double y1 = std::abs (gccBuffer[(size_t) (best & mask)].real());
    const double y2 = std::abs (gccBuffer[(size_t) ((best + 1) & mask)].real());
    const double den = y0 - 2.0 * y1 + y2;
    const double delta = den < 0.0 ? clampv (0.5 * (y0 - y2) / den, -0.5, 0.5) : 0.0;

    const double peakValue = gccBuffer[(size_t) (best & mask)].real();

    // the IFFT peaks at -delay when R lags L (see tests)
    results.delaySamples     = (float) -(best + delta);
    results.delayMs          = (float) (results.delaySamples * 1000.0 / fs);
    results.delayConfidence  = (float) clampv (std::abs (peakValue) / weightSum, 0.0, 1.0);
    results.polarityInverted = peakValue < 0.0;
}

//==============================================================================
float Analyzer::pairRoughness (float f1, float f2) noexcept
{
    // Sethares' parameterisation of the Plomp-Levelt dissonance curve
    static const float peak = []
    {
        const double x = std::log (5.75 / 3.5) / 2.25;
        return (float) (std::exp (-3.5 * x) - std::exp (-5.75 * x));
    }();

    const float lo = std::min (f1, f2), hi = std::max (f1, f2);
    const float sCoef = 0.24f / (0.0207f * lo + 18.96f);
    const float x = sCoef * (hi - lo);
    const float d = std::exp (-3.5f * x) - std::exp (-5.75f * x);
    return std::max (0.0f, d / peak);
}

void Analyzer::updateDissonance()
{
    const int n = dissSize, numBins = n / 2 + 1;
    const float dbOffset = (float) (20.0 * std::log10 (4.0 / n));   // |X| of a Hann-windowed sine -> amplitude

    constexpr int kFloorHalfWidth = 32;   // bins either side used to estimate the local noise floor
    const int kMin = std::max (3, (int) std::ceil (40.0 * n / fs));
    const int kMax = std::min (numBins - 4, (int) std::floor (5000.0 * n / fs));
    const int scanLo = std::max (1, kMin - kFloorHalfWidth), scanHi = std::min (numBins - 1, kMax + kFloorHalfWidth);

    float maxDb = -200.0f;
    for (int k = scanLo; k <= scanHi; ++k)
    {
        const float v = 10.0f * std::log10 (std::max (monoPower[(size_t) k], 1.0e-24f)) + dbOffset;
        scratchDb[(size_t) k] = v;
        if (k >= kMin && k <= kMax)
            maxDb = std::max (maxDb, v);
    }

    //--- 1. find tonal partials ------------------------------------------------
    candidates.clear();

    if (maxDb > -80.0f)
    {
        std::array<float, 2 * kFloorHalfWidth + 1> floorBuf {};

        for (int k = kMin; k <= kMax; ++k)
        {
            const float v = scratchDb[(size_t) k];
            if (! (v > scratchDb[(size_t) k - 1] && v >= scratchDb[(size_t) k + 1]
                   && v > scratchDb[(size_t) k - 2] && v >= scratchDb[(size_t) k + 2]))
                continue;

            if (v < maxDb - 50.0f || v < -80.0f)
                continue;

            // must stand well clear of the median level around it: rejects noise and window skirts
            int count = 0;
            for (int j = std::max (scanLo, k - kFloorHalfWidth); j <= std::min (scanHi, k + kFloorHalfWidth); ++j)
                if (std::abs (j - k) > 3)
                    floorBuf[(size_t) count++] = scratchDb[(size_t) j];

            std::nth_element (floorBuf.begin(), floorBuf.begin() + count / 2, floorBuf.begin() + count);
            if (v - floorBuf[(size_t) count / 2] < 15.0f)
                continue;

            const float a = scratchDb[(size_t) k - 1], c = scratchDb[(size_t) k + 1];
            const float den = a - 2.0f * v + c;
            const float p = den < 0.0f ? clampv (0.5f * (a - c) / den, -0.5f, 0.5f) : 0.0f;

            Peak pk;
            pk.freq = (float) (((double) k + (double) p) * fs / n);
            pk.levelDb = v - 0.25f * (a - c) * p;
            candidates.push_back (pk);
        }
    }

    std::sort (candidates.begin(), candidates.end(),
               [] (const Peak& x, const Peak& y) { return x.levelDb > y.levelDb; });
    if ((int) candidates.size() > kMaxPeaks)
        candidates.resize (kMaxPeaks);

    const bool active = std::max (eLL, eRR) > kGateMs;
    const int numPeaks = (int) candidates.size();
    results.numPeaks = numPeaks;
    results.tonal = numPeaks > 0 && active;
    results.chroma.fill (0.0f);
    results.numClashes = 0;

    float roughness01 = 0.0f, tension01 = 0.0f, rawRoughness = 0.0f;

    if (numPeaks > 0)
    {
        const float topDb = candidates[0].levelDb;
        std::array<float, kMaxPeaks> amp {}, intensity {};
        float ampSum = 0.0f;

        for (int i = 0; i < numPeaks; ++i)
        {
            amp[(size_t) i] = std::pow (10.0f, (candidates[(size_t) i].levelDb - topDb) / 20.0f);
            ampSum += amp[(size_t) i];
        }

        //--- 2. sensory roughness (Plomp-Levelt / Sethares), all partial pairs -----
        float total = 0.0f;
        for (int i = 0; i < numPeaks; ++i)
        {
            for (int j = i + 1; j < numPeaks; ++j)
            {
                const float r = pairRoughness (candidates[(size_t) i].freq, candidates[(size_t) j].freq);
                if (r < 1.0e-4f)
                    continue;

                const float ai = amp[(size_t) i], aj = amp[(size_t) j];
                total += std::min (ai, aj) * r;
                intensity[(size_t) i] += r * std::min (1.0f, aj / ai);
                intensity[(size_t) j] += r * std::min (1.0f, ai / aj);
            }
        }

        rawRoughness = total / std::max (ampSum, 1.0e-6f);
        roughness01  = 1.0f - std::exp (-rawRoughness / 0.5f);

        //--- 3. separate played notes from their overtones ---------------------------
        std::array<int, kMaxPeaks> byFreq {};
        std::array<bool, kMaxPeaks> fundamental {};   // strict: used for tension and clashes
        std::array<bool, kMaxPeaks> shownAsNote {};   // looser: used for the note display
        for (int i = 0; i < numPeaks; ++i)
        {
            byFreq[(size_t) i] = i;
            fundamental[(size_t) i] = shownAsNote[(size_t) i] = true;
        }
        std::sort (byFreq.begin(), byFreq.begin() + numPeaks,
                   [this] (int x, int y) { return candidates[(size_t) x].freq < candidates[(size_t) y].freq; });

        // A partial is an overtone if it sits on harmonic m (2..10) of a lower partial and is
        // no louder than a natural ~1/m roll-off allows. Overtones of different notes often
        // coincide and add up, so the tension measure uses generous slack (fewer false clashes),
        // while the note display uses less, so a chord tone that lands on a bass overtone
        // (C#4 over an A1 bass, say) still shows up.
        for (int jj = 1; jj < numPeaks; ++jj)
        {
            const int j = byFreq[(size_t) jj];
            for (int ii = 0; ii < jj; ++ii)
            {
                const int i = byFreq[(size_t) ii];
                const float ratio = candidates[(size_t) j].freq / candidates[(size_t) i].freq;
                const int m = (int) std::lround (ratio);
                if (m < 2 || m > 10 || std::abs (1200.0f * std::log2 (ratio / (float) m)) > 35.0f)
                    continue;

                const float rolloff = amp[(size_t) i] / (float) m;
                if (amp[(size_t) j] <= rolloff * 2.0f)
                    fundamental[(size_t) j] = false;
                if (amp[(size_t) j] <= rolloff * (m <= 4 ? 2.0f : 1.4f))
                    shownAsNote[(size_t) j] = false;
            }
        }

        //--- 4. tonal tension between the notes (octave-equivalent interval classes) -
        static constexpr float kIntervalTension[7] = { 0.0f,    // unison / octave
                                                       1.0f,    // minor 2nd / major 7th
                                                       0.6f,    // major 2nd / minor 7th
                                                       0.2f,    // minor 3rd / major 6th
                                                       0.15f,   // major 3rd / minor 6th
                                                       0.05f,   // perfect 4th / 5th
                                                       0.55f }; // tritone
        std::array<float, 12> pcWeight {}, pcBest {}, pcShown {};
        std::array<int, 12> pcNote;
        pcNote.fill (-1);

        for (int i = 0; i < numPeaks; ++i)
        {
            const int midi = frequencyToMidi (candidates[(size_t) i].freq);
            const int pc = pitchClass (midi);
            if (shownAsNote[(size_t) i])
                pcShown[(size_t) pc] += amp[(size_t) i];
            if (! fundamental[(size_t) i])
                continue;
            pcWeight[(size_t) pc] += amp[(size_t) i];
            if (amp[(size_t) i] > pcBest[(size_t) pc])
            {
                pcBest[(size_t) pc] = amp[(size_t) i];
                pcNote[(size_t) pc] = midi;
            }
        }

        clashAcc.clear();
        double num = 0.0, den = 0.0;

        for (int p = 0; p < 12; ++p)
        {
            for (int q = p + 1; q < 12; ++q)
            {
                const double w = (double) pcWeight[(size_t) p] * pcWeight[(size_t) q];
                if (w <= 0.0)
                    continue;

                const int ic = std::min (q - p, 12 - (q - p));
                num += w * kIntervalTension[ic];
                den += w;

                if (kIntervalTension[ic] >= 0.5f)
                    clashAcc.push_back ({ p, q,
                                          std::min (pcNote[(size_t) p], pcNote[(size_t) q]),   // lower note first
                                          std::max (pcNote[(size_t) p], pcNote[(size_t) q]),
                                          (float) (w * kIntervalTension[ic]), 0.0f });
            }
        }

        tension01 = den > 0.0 ? clampv ((float) (num / den) / 0.8f, 0.0f, 1.0f) : 0.0f;

        // out-of-tune unisons: two clear notes on the same semitone, audibly apart
        for (int i = 0; i < numPeaks; ++i)
        {
            for (int j = i + 1; j < numPeaks; ++j)
            {
                if (! fundamental[(size_t) i] || ! fundamental[(size_t) j])
                    continue;
                const float fi = candidates[(size_t) i].freq, fj = candidates[(size_t) j].freq;
                const int mi = frequencyToMidi (fi);
                if (mi != frequencyToMidi (fj) || std::abs (1200.0f * std::log2 (fj / fi)) < 12.0f)
                    continue;
                const float a = std::min (amp[(size_t) i], amp[(size_t) j]);
                if (a >= 0.2f)
                    clashAcc.push_back ({ pitchClass (mi), pitchClass (mi), mi, mi, a * a, 0.0f });
            }
        }

        float clashTotal = 0.0f;
        for (const auto& ca : clashAcc)
            clashTotal += ca.amount;

        std::sort (clashAcc.begin(), clashAcc.end(),
                   [] (const ClashAcc& x, const ClashAcc& y) { return x.amount > y.amount; });

        for (const auto& ca : clashAcc)
        {
            if (results.numClashes >= kMaxClashes || ca.amount < 0.05f * clashTotal || ca.amount < 0.01f)
                break;
            results.clashes[(size_t) results.numClashes++] = { ca.noteA, ca.noteB, ca.amount / clashTotal };
        }

        //--- 5. outputs for the display ----------------------------------------------
        for (int i = 0; i < numPeaks; ++i)
        {
            Peak& out = results.peaks[(size_t) i];
            out = candidates[(size_t) i];
            // weak partials beating against a loud neighbour matter less than loud ones
            out.roughness = std::min (1.0f, intensity[(size_t) i]) * std::sqrt (amp[(size_t) i]);
            out.fundamental = shownAsNote[(size_t) i];
        }

        const float maxPc = *std::max_element (pcShown.begin(), pcShown.end());
        if (maxPc > 0.0f)
            for (int p = 0; p < 12; ++p)
                results.chroma[(size_t) p] = pcShown[(size_t) p] / maxPc;
    }

    if (! active)
        roughness01 = tension01 = 0.0f;

    const float score = 100.0f * (0.7f * tension01 + 0.3f * roughness01);

    const float c = (float) dissonanceCoeff;
    smoothedDissonance = c * smoothedDissonance + (1.0f - c) * score;
    smoothedTension    = c * smoothedTension    + (1.0f - c) * 100.0f * tension01;
    smoothedRoughness  = c * smoothedRoughness  + (1.0f - c) * 100.0f * roughness01;

    results.dissonance   = smoothedDissonance;
    results.tension      = smoothedTension;
    results.roughness    = smoothedRoughness;
    results.rawRoughness = rawRoughness;
}

//==============================================================================
void Analyzer::updateSpectrum()
{
    const int n = dissSize, numBins = n / 2 + 1;
    const float dbOffset = (float) (20.0 * std::log10 (4.0 / n));
    const double binsPerHz = n / fs;

    for (int i = 0; i < kSpectrumPoints; ++i)
    {
        const double f0 = spectrumPointFreq (i);
        const double f1 = i + 1 < kSpectrumPoints ? spectrumPointFreq (i + 1)
                                                  : f0 * spectrumPointFreq (1) / spectrumPointFreq (0);
        const double b0 = f0 * binsPerHz, b1 = f1 * binsPerHz;
        const int k0 = (int) b0, k1 = (int) b1;

        float power = 0.0f;
        if (k0 >= numBins - 1)
            power = 0.0f;
        else if (k1 <= k0)
        {
            const float t = (float) (b0 - k0);
            power = monoPower[(size_t) k0] * (1.0f - t) + monoPower[(size_t) k0 + 1] * t;
        }
        else
            for (int k = k0; k < std::min (k1, numBins); ++k)
                power = std::max (power, monoPower[(size_t) k]);

        results.spectrumDb[(size_t) i] = std::max (-120.0f, 10.0f * std::log10 (std::max (power, 1.0e-24f)) + dbOffset);
    }
}

//==============================================================================
int Analyzer::copyRecentSamples (float* left, float* right, int maxSamples) const
{
    const int count = std::min (maxSamples, scopeFilled);
    int idx = (scopePos - count + kScopeSize) % kScopeSize;

    for (int i = 0; i < count; ++i)
    {
        left[i]  = scopeL[(size_t) idx];
        right[i] = scopeR[(size_t) idx];
        idx = (idx + 1) % kScopeSize;
    }
    return count;
}

float Analyzer::octaveBandCentre (int band)        { return kOctaveCentres[clampv (band, 0, kNumOctaveBands - 1)]; }
const char* Analyzer::octaveBandLabel (int band)   { return kOctaveLabels[clampv (band, 0, kNumOctaveBands - 1)]; }
const char* Analyzer::broadBandName (int band)     { return kBroadNames[clampv (band, 0, kNumBroadBands - 1)]; }

float Analyzer::spectrumPointFreq (int index)
{
    return 20.0f * std::pow (1000.0f, (float) index / (float) (kSpectrumPoints - 1));
}

std::string Analyzer::noteName (int midiNote)
{
    if (midiNote < 0)
        return "-";
    return std::string (kNoteNames[pitchClass (midiNote)]) + std::to_string (midiNote / 12 - 1);
}

} // namespace mi
