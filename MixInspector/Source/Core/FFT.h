// MixInspector - stereo & harmonic analysis plugin
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cmath>
#include <complex>
#include <utility>
#include <vector>

namespace mi
{
constexpr double kPi = 3.14159265358979323846;

/** Small, dependency-free radix-2 complex FFT (in place, unscaled).
    Kept free of JUCE so the analysis core can be unit-tested on its own. */
class FFT
{
public:
    using Complex = std::complex<float>;

    explicit FFT (int initialOrder = 13) { setOrder (initialOrder); }

    void setOrder (int newOrder)
    {
        order = newOrder;
        n = 1 << order;

        twiddles.resize ((size_t) n / 2);
        for (int k = 0; k < n / 2; ++k)
        {
            const double a = -2.0 * kPi * k / n;
            twiddles[(size_t) k] = { (float) std::cos (a), (float) std::sin (a) };
        }

        bitReverse.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < order; ++b)
                if (i & (1 << b))
                    r |= 1 << (order - 1 - b);
            bitReverse[(size_t) i] = r;
        }
    }

    int getSize() const noexcept  { return n; }
    int getOrder() const noexcept { return order; }

    /** Forward transform uses e^{-j...}; inverse uses e^{+j...}. Neither is scaled. */
    void perform (Complex* data, bool inverse) const noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const int j = bitReverse[(size_t) i];
            if (i < j)
                std::swap (data[i], data[j]);
        }

        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len / 2;
            const int step = n / len;

            for (int i = 0; i < n; i += len)
            {
                for (int j = 0; j < half; ++j)
                {
                    const Complex& tw = twiddles[(size_t) (j * step)];
                    const float wr = tw.real();
                    const float wi = inverse ? -tw.imag() : tw.imag();

                    Complex& a = data[i + j];
                    Complex& b = data[i + j + half];

                    // manual complex multiply (avoids the slow NaN-checking path)
                    const float vr = b.real() * wr - b.imag() * wi;
                    const float vi = b.real() * wi + b.imag() * wr;

                    b = { a.real() - vr, a.imag() - vi };
                    a = { a.real() + vr, a.imag() + vi };
                }
            }
        }
    }

private:
    int order = 0, n = 0;
    std::vector<Complex> twiddles;
    std::vector<int> bitReverse;
};

} // namespace mi
