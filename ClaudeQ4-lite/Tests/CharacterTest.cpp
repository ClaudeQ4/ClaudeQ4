#include "../Source/dsp/Character.h"
#include <cstdio>
#include <vector>
using namespace cq;
static double bin (const std::vector<double>& x, int start, int len, double f, double fs)
{
    cplx acc (0, 0);
    for (int n = 0; n < len; ++n) { const double w = 0.5 - 0.5 * std::cos (kTwoPi * n / len); acc += x[(size_t) (start + n)] * w * std::polar (1.0, -kTwoPi * f * n / fs); }
    return gainToDb (2.0 * std::abs (acc) / (0.5 * len) + 1e-30);
}
int main()
{
    int failures = 0;
    // halfband: image rejection of the 2x up-sampler
    {
        double c[12]; HalfbandPolyphase::design (12, 0.04, c);
        HalfbandPolyphase up; up.setCoefs (c, 12);
        const double fs2 = 96000; const int len = 1 << 14;
        std::vector<double> y ((size_t) (2 * len));
        for (int i = 0; i < len; ++i) { double a, b; up.upsample (std::sin (kTwoPi * 10000.0 * i / 48000.0), a, b); y[(size_t) (2 * i)] = a; y[(size_t) (2 * i + 1)] = b; }
        const double sig = bin (y, len / 2, len, 10000.0, fs2), img = bin (y, len / 2, len, 38000.0, fs2);
        const double sig2 = [&] { HalfbandPolyphase u2; u2.setCoefs (c, 12); std::vector<double> z ((size_t) (2 * len)); for (int i = 0; i < len; ++i) { double a, b; u2.upsample (std::sin (kTwoPi * 21000.0 * i / 48000.0), a, b); z[(size_t) (2 * i)] = a; z[(size_t) (2 * i + 1)] = b; } return bin (z, len / 2, len, 21000.0, fs2); }();
        const bool ok = sig - img > 110 && std::abs (sig) < 0.1 && std::abs (sig2) < 0.1;
        if (! ok) ++failures;
        std::printf ("%s halfband: 10k passband %.3f dB, 21k passband %.3f dB, image rejection %.1f dB\n", ok ? "PASS" : "FAIL", sig, sig2, sig - img);
    }
    for (int mode : { 1, 2 })
    {
        const double fs = 48000; CharacterProcessor cp; cp.prepare (fs); cp.setMode ((CharacterMode) mode); cp.reset();
        const int len = 1 << 16;
        // THD at 1 kHz -12 dBFS, and aliasing with a 15 kHz sine at -3 dBFS
        std::vector<double> a ((size_t) len), b ((size_t) len), r ((size_t) len, 0.0);
        for (int i = 0; i < len; ++i) { a[(size_t) i] = 0.25 * std::sin (kTwoPi * 1000.0 * i / fs); b[(size_t) i] = 0.7 * std::sin (kTwoPi * 15000.0 * i / fs); }
        cp.process (a.data(), nullptr, len);
        cp.reset(); cp.process (b.data(), nullptr, len);
        const double f1 = bin (a, len / 2, len / 2, 1000, fs), h2 = bin (a, len / 2, len / 2, 2000, fs), h3 = bin (a, len / 2, len / 2, 3000, fs);
        const double s15 = bin (b, len / 2, len / 2, 15000, fs);
        double worstAlias = -300;
        for (double f = 100; f < 23900; f += 100)
            if (std::abs (f - 15000) > 300) worstAlias = std::max (worstAlias, bin (b, len / 2, len / 2, f, fs));
        // small signals must be (nearly) untouched
        CharacterProcessor cq2; cq2.prepare (fs); cq2.setMode ((CharacterMode) mode); cq2.reset();
        std::vector<double> q ((size_t) len); for (int i = 0; i < len; ++i) q[(size_t) i] = 0.001 * std::sin (kTwoPi * 100.0 * i / fs);
        cq2.process (q.data(), nullptr, len);
        const double small = bin (q, len / 2, len / 2, 100, fs);
        const bool ok = worstAlias - s15 < -90 && std::abs (small + 60.0) < 0.05;
        if (! ok) ++failures;
        std::printf ("%s %s: 1k -12dBFS: H2 %.1f dBc, H3 %.1f dBc | 15k -3dBFS: worst alias %.1f dBc | -60 dBFS gain err %.3f dB\n",
                     ok ? "PASS" : "FAIL", mode == 1 ? "Subtle" : "Warm  ", h2 - f1, h3 - f1, worstAlias - s15, small + 60.0);
    }
    std::printf ("%d failures\n", failures);
    return failures;
}
