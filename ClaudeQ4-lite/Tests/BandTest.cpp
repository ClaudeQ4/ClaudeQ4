// IIRBand runtime tests: measured impulse response vs analog, modulation robustness
#include "../Source/dsp/IIRBand.h"
#include <cstdio>
#include <vector>
#include <random>

using namespace cq;

static void runBlocks (IIRBand& b, std::vector<double>& L, std::vector<double>& R, int ctrl, double dynGain = 0.0)
{
    const int N = (int) L.size();
    for (int pos = 0; pos < N; pos += ctrl)
    {
        const int n = std::min (ctrl, N - pos);
        int budget = 4;
        b.control (n, dynGain, budget);
        b.process (L.data() + pos, R.data() + pos, n);
    }
}

static double measureDb (const std::vector<double>& h, double f, double fs)
{
    cplx acc (0, 0);
    for (size_t n = 0; n < h.size(); ++n) acc += h[n] * std::polar (1.0, -kTwoPi * f * (double) n / fs);
    return gainToDb (std::abs (acc));
}

int main()
{
    const double fs = 48000.0;
    const int ctrl = 32;
    int failures = 0;

    struct Case { Shape shape; Slope slope; double f, g, q; Placement pl; };
    const Case cases[] = {
        { Shape::Bell, Slope::dB12, 1000, 12, 1, Placement::Stereo },
        { Shape::Bell, Slope::dB48, 3000, -9, 4, Placement::Stereo },
        { Shape::LowShelf, Slope::dB24, 120, 6, 1, Placement::Left },
        { Shape::HighShelf, Slope::dB12, 9000, -6, 0.7, Placement::Stereo },
        { Shape::LowCut, Slope::dB48, 80, 0, 1, Placement::Stereo },
        { Shape::HighCut, Slope::dB24, 12000, 0, 1, Placement::Stereo },
        { Shape::Notch, Slope::dB12, 2500, 0, 8, Placement::Stereo },
        { Shape::BandPass, Slope::dB24, 700, 0, 2, Placement::Stereo },
        { Shape::LowCut, Slope::Brickwall, 200, 0, 1, Placement::Stereo },
        { Shape::FlatTilt, Slope::dB12, 1000, 12, 1, Placement::Stereo },
    };

    for (const auto& c : cases)
    {
        IIRBand b; b.prepare (fs, ctrl);
        BandParams p; p.used = true; p.enabled = true; p.shape = c.shape; p.slope = c.slope; p.freq = c.f; p.gainDb = c.g; p.q = c.q; p.place = c.pl;
        b.setTarget (p, 1.0, false);
        // settle: 0.5 s of silence
        std::vector<double> L (24000, 0.0), R (24000, 0.0);
        runBlocks (b, L, R, ctrl);
        // impulse response
        std::vector<double> hL (65536, 0.0), hR (65536, 0.0);
        hL[0] = 1.0; hR[0] = 1.0;
        runBlocks (b, hL, hR, ctrl);
        AnalogFilter a; makeAnalogPrototype (p.shapeParams (1.0), a);
        double worst = 0, worstF = 0;
        for (double f : { 30.0, 60.0, 100.0, 200.0, 400.0, 700.0, 1000.0, 1500.0, 2500.0, 4000.0, 6000.0, 9000.0, 12000.0, 15000.0, 18000.0 })
        {
            const double ma = gainToDb (std::sqrt (a.magSq (f / c.f)));
            const double mm = measureDb (hL, f, fs);
            if (ma < -40.0 && mm < -40.0) continue;
            const double e = std::abs (ma - mm);
            if (e > worst) { worst = e; worstF = f; }
        }
        const bool ok = worst < 0.25 || c.slope == Slope::Brickwall;
        if (! ok) ++failures;
        std::printf ("%s %-10s %-10s f=%6.0f: max |measured - analog| = %.3f dB (at %.0f Hz) settled=%d\n",
                     ok ? "PASS" : "FAIL", shapeName (c.shape), slopeName (c.slope), c.f, worst, worstF, (int) b.isSettled());
    }

    // ---- modulation torture: fast sweeps, gain flips, shape/slope/placement changes on noise
    {
        IIRBand b; b.prepare (fs, ctrl);
        std::mt19937 rng (1);
        std::normal_distribution<double> nd (0.0, 0.3);
        BandParams p; p.used = true; p.shape = Shape::Bell; p.slope = Slope::dB12; p.freq = 100; p.gainDb = 18; p.q = 8;
        b.setTarget (p, 1.0, false);
        double maxAbs = 0; bool finite = true;
        const int total = (int) fs * 8;
        std::vector<double> L (ctrl), R (ctrl);
        for (int pos = 0, blk = 0; pos < total; pos += ctrl, ++blk)
        {
            if (blk % 300 == 0)
            {
                const int k = (blk / 300) % 7;
                p.freq = 20.0 * std::pow (1000.0, ((blk / 300) % 13) / 12.0);
                p.gainDb = (k % 2 ? 1 : -1) * (6.0 + 3 * k);
                p.q = 0.3 * std::pow (2.0, k);
                if (k == 3) p.shape = Shape::HighShelf;
                if (k == 4) p.slope = Slope::dB48;
                if (k == 5) p.place = Placement::Mid;
                if (k == 6) { p.shape = Shape::Bell; p.slope = Slope::dB12; p.place = Placement::Stereo; }
                if (blk % 2100 == 0) p.enabled = ! p.enabled;
                b.setTarget (p, 1.0, false);
            }
            for (int i = 0; i < ctrl; ++i) { L[(size_t) i] = nd (rng); R[(size_t) i] = nd (rng); }
            int budget = 3;
            const double dyn = 10.0 * std::sin (pos * 1e-3);
            b.control (ctrl, dyn, budget);
            b.process (L.data(), R.data(), ctrl);
            for (int i = 0; i < ctrl; ++i)
            {
                if (! std::isfinite (L[(size_t) i]) || ! std::isfinite (R[(size_t) i])) finite = false;
                maxAbs = std::max (maxAbs, std::max (std::abs (L[(size_t) i]), std::abs (R[(size_t) i])));
            }
        }
        const bool ok = finite && maxAbs < 200.0;
        if (! ok) ++failures;
        std::printf ("%s modulation torture: finite=%d max|y|=%.2f\n", ok ? "PASS" : "FAIL", (int) finite, maxAbs);
    }

    // ---- frequency sweep smoothness: sine through a moving narrow bell, check for clicks
    {
        IIRBand b; b.prepare (fs, ctrl);
        BandParams p; p.used = true; p.shape = Shape::Bell; p.freq = 200; p.gainDb = 15; p.q = 4;
        b.setTarget (p, 1.0, false);
        std::vector<double> L ((size_t) fs * 3), R ((size_t) fs * 3);
        for (size_t i = 0; i < L.size(); ++i) L[i] = R[i] = 0.25 * std::sin (kTwoPi * 1000.0 * (double) i / fs);
        for (size_t pos = 0, blk = 0; pos < L.size(); pos += ctrl, ++blk)
        {
            p.freq = 200.0 * std::pow (25.0, std::min (1.0, (double) pos / (fs * 2.0)));   // sweep 200 Hz -> 5 kHz in 2 s
            b.setTarget (p, 1.0, false);
            int budget = 3;
            b.control (ctrl, 0.0, budget);
            b.process (L.data() + pos, R.data() + pos, ctrl);
        }
        // the second difference of a smooth signal is small; clicks show up as spikes
        double maxD2 = 0;
        for (size_t i = 2; i < L.size(); ++i) maxD2 = std::max (maxD2, std::abs (L[i] - 2 * L[i - 1] + L[i - 2]));
        const double ref = 0.25 * std::pow (kTwoPi * 1000.0 / fs, 2.0) * dbToGain (15.0);
        const bool ok = maxD2 < ref * 1.5;
        if (! ok) ++failures;
        std::printf ("%s sweep smoothness: max 2nd diff %.5f (smooth reference %.5f)\n", ok ? "PASS" : "FAIL", maxD2, ref);
    }

    std::printf ("%d failures\n", failures);
    return failures;
}
