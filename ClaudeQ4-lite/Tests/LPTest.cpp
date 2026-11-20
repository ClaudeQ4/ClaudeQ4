// Linear phase engine test: latency, symmetry, magnitude accuracy, M/S correctness
#include "../Source/dsp/LinearPhase.h"
#include <cstdio>
#include <vector>
#include <chrono>

using namespace cq;

static double measureDb (const std::vector<float>& h, int start, int len, double f, double fs)
{
    cplx acc (0, 0);
    for (int n = 0; n < len; ++n) acc += (double) h[(size_t) (start + n)] * std::polar (1.0, -kTwoPi * f * (double) n / fs);
    return gainToDb (std::abs (acc));
}

int main()
{
    int failures = 0;
    const double fs = 48000.0;
    for (int res = 0; res < 5; ++res)
    {
        const int L = lpKernelLength ((LPResolution) res, fs);
        const int P = lpPartitionSize (fs);
        LPKernelBuilder kb; kb.configure (fs, L, P);
        LinearPhaseConvolver conv; conv.configure (L, P, 2);

        EngineParams ep;
        auto& b0 = ep.bands[0]; b0.used = true; b0.shape = Shape::Bell; b0.freq = 1000; b0.gainDb = 9; b0.q = 1.5;
        auto& b1 = ep.bands[1]; b1.used = true; b1.shape = Shape::LowCut; b1.slope = Slope::dB24; b1.freq = 60;
        auto& b2 = ep.bands[2]; b2.used = true; b2.shape = Shape::HighShelf; b2.freq = 8000; b2.gainDb = -6; b2.place = Placement::Side;

        auto t0 = std::chrono::high_resolution_clock::now();
        LPKernelSet* ks = conv.beginWriteBack();
        kb.build (ep, nullptr, false, *ks);
        conv.endWriteBack (true);
        auto t1 = std::chrono::high_resolution_clock::now();

        const int total = L + 2 * P + L + 4 * P;
        std::vector<float> Ls ((size_t) total, 0.0f), Rs ((size_t) total, 0.0f);
        // impulse on L only after the kernel is active (first partition)
        const int t0i = 2 * P;
        Ls[(size_t) t0i] = 1.0f;
        auto t2 = std::chrono::high_resolution_clock::now();
        for (int pos = 0; pos < total; pos += 256)
            conv.process (Ls.data() + pos, Rs.data() + pos, std::min (256, total - pos));
        auto t3 = std::chrono::high_resolution_clock::now();

        // peak position -> latency
        int peak = 0; float pv = 0;
        for (int i = 0; i < total; ++i) if (std::abs (Ls[(size_t) i]) > pv) { pv = std::abs (Ls[(size_t) i]); peak = i; }
        const int latency = peak - t0i;
        // symmetry of the L->L response around the peak
        double asym = 0, energy = 0;
        for (int m = 1; m < L / 2 - 1; ++m)
        {
            const double a = Ls[(size_t) (peak + m)], b = Ls[(size_t) (peak - m)];
            asym += (a - b) * (a - b); energy += a * a + b * b;
        }
        // magnitude: L->L for an L-only impulse = 0.5*(Hst*... ) ; with side band: L->L = 0.5*(H_mid + H_side) where H_mid = bell*cut, H_side = bell*cut*shelf
        double worst = 0;
        AnalogFilter aB, aC, aS;
        makeAnalogPrototype (b0.shapeParams (1.0), aB);
        makeAnalogPrototype (b1.shapeParams (1.0), aC);
        makeAnalogPrototype (b2.shapeParams (1.0), aS);
        for (double f : { 100.0, 300.0, 1000.0, 3000.0, 8000.0, 15000.0 })
        {
            const double hb = std::sqrt (aB.magSq (f / b0.freq)) * std::sqrt (aC.magSq (f / b1.freq));
            const double hs = std::sqrt (aS.magSq (f / b2.freq));
            const double expectLL = 0.5 * (hb + hb * hs);
            const double m = measureDb (Ls, peak - L / 2, L, f, fs);
            worst = std::max (worst, std::abs (m - gainToDb (expectLL)));
        }
        const bool ok = latency == conv.getLatency() && asym / energy < 1e-6 && worst < 0.05;
        if (! ok) ++failures;
        std::printf ("%s res %d: L=%6d P=%d latency %d (expected %d)  asym %.2e  max mag err %.4f dB  build %.1f ms  run %.2f us/sample\n",
                     ok ? "PASS" : "FAIL", res, L, P, latency, conv.getLatency(), asym / energy, worst,
                     std::chrono::duration<double, std::milli> (t1 - t0).count(),
                     std::chrono::duration<double, std::micro> (t3 - t2).count() / total);
    }
    std::printf ("%d failures\n", failures);
    return failures;
}
