// Spectral dynamics test: transparency/latency, and resonance taming
#include "../Source/dsp/Spectral.h"
#include <cstdio>
#include <vector>
#include <random>
using namespace cq;

static double toneLevel (const std::vector<float>& x, int start, int len, double f, double fs)
{
    cplx acc (0, 0);
    for (int n = 0; n < len; ++n) { const double w = 0.5 - 0.5 * std::cos (kTwoPi * n / len); acc += (double) x[(size_t) (start + n)] * w * std::polar (1.0, -kTwoPi * f * n / fs); }
    return gainToDb (2.0 * std::abs (acc) / (0.5 * len));
}

int main()
{
    int failures = 0;
    const double fs = 48000;
    SpectralEngine se; se.configure (fs, SpectralEngine::frameSizeFor (LPResolution::Medium, fs), 2);
    EngineParams ep;
    // 1) no spectral band: identity with latency N
    se.setParams (ep);
    {
        const int len = 20000;
        std::vector<float> L ((size_t) len, 0.f), R ((size_t) len, 0.f);
        L[100] = 1.0f; R[100] = 0.5f;
        se.process (L.data(), R.data(), nullptr, nullptr, len);
        int pk = 0; for (int i = 0; i < len; ++i) if (std::abs (L[(size_t) i]) > std::abs (L[(size_t) pk])) pk = i;
        double err = 0; for (int i = 0; i < len; ++i) if (i != pk) err = std::max (err, (double) std::abs (L[(size_t) i]));
        const bool ok = pk - 100 == se.getLatency() && std::abs (L[(size_t) pk] - 1.0f) < 1e-4 && err < 1e-4 && std::abs (R[(size_t) pk] - 0.5f) < 1e-4;
        if (! ok) ++failures;
        std::printf ("%s identity: latency %d (reported %d), peak %.6f, residual %.2e\n", ok ? "PASS" : "FAIL", pk - 100, se.getLatency(), L[(size_t) pk], err);
    }
    // 2) resonance: pink-ish noise + loud 3 kHz tone; spectral band 1-8 kHz compress -12 dB
    {
        se.reset();
        auto& b = ep.bands[0]; b.used = true; b.shape = Shape::Bell; b.freq = 3000; b.q = 0.5; b.gainDb = 0; b.dynRange = -12; b.spectral = true; b.density = 0.8;
        se.setParams (ep);
        std::mt19937 rng (3); std::normal_distribution<double> nd (0, 0.05);
        const int len = (int) fs * 4;
        std::vector<float> L ((size_t) len), R ((size_t) len), dry ((size_t) len);
        for (int i = 0; i < len; ++i) { const double t = i / fs; const float v = (float) (nd (rng) + 0.3 * std::sin (kTwoPi * 3000 * t) * (i > len / 2 ? 1.0 : 0.0)); L[(size_t) i] = R[(size_t) i] = dry[(size_t) i] = v; }
        for (int pos = 0; pos < len; pos += 512) se.process (L.data() + pos, R.data() + pos, nullptr, nullptr, std::min (512, len - pos));
        const int lat = se.getLatency();
        const int a0 = len - 16384 - lat - 100;
        const double toneIn = toneLevel (dry, a0, 16384, 3000, fs), toneOut = toneLevel (L, a0 + lat, 16384, 3000, fs);
        const double noiseIn = toneLevel (dry, a0, 16384, 5500, fs), noiseOut = toneLevel (L, a0 + lat, 16384, 5500, fs);
        const double quietIn = toneLevel (dry, 20000, 16384, 3000, fs), quietOut = toneLevel (L, 20000 + lat, 16384, 3000, fs);
        const bool ok = (toneIn - toneOut) > 6.0 && std::abs (noiseIn - noiseOut) < 3.0 && std::abs (quietIn - quietOut) < 3.0;
        if (! ok) ++failures;
        std::printf ("%s resonance: tone %.1f -> %.1f dB (reduced %.1f), nearby noise %.1f -> %.1f, before tone %.1f -> %.1f, display %.1f dB\n",
                     ok ? "PASS" : "FAIL", toneIn, toneOut, toneIn - toneOut, noiseIn, noiseOut, quietIn, quietOut, se.getDisplayGain (0, 115));
    }
    std::printf ("%d failures\n", failures);
    return failures;
}
