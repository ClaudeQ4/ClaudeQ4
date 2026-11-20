// Sample rates, offline determinism, dynamic EQ on silence / denormals, CPU at 96 kHz
#include "../Source/dsp/EQEngine.h"
#include <memory>
#include <cstdio>
#include <vector>
#include <random>
#include <thread>
#include <chrono>

using namespace cq;

static int failures = 0;
static void check (bool ok, const char* what, double a = 0, double b = 0)
{
    if (! ok) ++failures;
    std::printf ("%s %s (%.5g, %.5g)\n", ok ? "PASS" : "FAIL", what, a, b);
}

static cplx dft (const std::vector<float>& h, double f, double fs, int start)
{
    cplx acc (0, 0);
    for (size_t n = (size_t) start; n < h.size(); ++n) acc += (double) h[n] * std::polar (1.0, -kTwoPi * f * (double) (n - (size_t) start) / fs);
    return acc;
}

static void settle (EQEngine& e, int expectedLatency, int block)
{
    std::vector<float> L ((size_t) block, 0.f), R ((size_t) block, 0.f);
    float* io[2] = { L.data(), R.data() };
    for (int i = 0; i < 600 && e.getLatency() != expectedLatency; ++i)
    {
        e.process (io, 2, nullptr, 0, block);
        std::this_thread::sleep_for (std::chrono::milliseconds (3));
    }
    for (int i = 0; i < 200; ++i) e.process (io, 2, nullptr, 0, block);
    std::this_thread::sleep_for (std::chrono::milliseconds (60));
    for (int i = 0; i < 200; ++i) e.process (io, 2, nullptr, 0, block);
}

int main()
{
    // ---- every sample rate: LP latency and magnitude vs analog, NP phase
    for (double fs : { 44100.0, 88200.0, 96000.0, 176400.0, 192000.0 })
    {
        EngineParams p;
        auto& b0 = p.bands[0]; b0.used = true; b0.shape = Shape::Bell; b0.freq = 3000; b0.gainDb = 6; b0.q = 1.5;
        auto& b1 = p.bands[1]; b1.used = true; b1.shape = Shape::LowCut; b1.slope = Slope::dB48; b1.freq = 60;
        auto& b2 = p.bands[2]; b2.used = true; b2.shape = Shape::HighShelf; b2.freq = 9000; b2.gainDb = -4; b2.q = 1.0;
        p.global.mode = ProcessingMode::LinearPhase; p.global.resolution = LPResolution::High; p.version = 1;
        AnalogFilter a0, a1, a2;
        makeAnalogPrototype (b0.shapeParams (1.0), a0); makeAnalogPrototype (b1.shapeParams (1.0), a1); makeAnalogPrototype (b2.shapeParams (1.0), a2);
        auto analog = [&] (double f) { return a0.eval (f / b0.freq) * a1.eval (f / b1.freq) * a2.eval (f / b2.freq); };

        const int block = 512;
        auto eOwner = std::make_unique<EQEngine>(); EQEngine& e = *eOwner;
        e.prepare (fs, block, 2, p);
        const int expect = lpKernelLength (LPResolution::High, fs) / 2 + lpPartitionSize (fs);
        settle (e, expect, block);
        const int len = expect + (1 << 16);
        std::vector<float> L ((size_t) len, 0.f), R ((size_t) len, 0.f);
        L[0] = 1.0f;
        for (int pos = 0; pos < len; pos += block) { float* io[2] = { L.data() + pos, R.data() + pos }; e.process (io, 2, nullptr, 0, std::min (block, len - pos)); }
        double worst = 0.0;
        for (double f : { 100.0, 500.0, 1000.0, 3000.0, 8000.0, 12000.0, 18000.0 })
            worst = std::max (worst, std::abs (gainToDb (std::abs (dft (L, f, fs, 0))) - gainToDb (std::abs (analog (f)))));
        char msg[160];
        std::snprintf (msg, sizeof msg, "fs %.0f: linear phase latency %d and |H| error vs analog (dB)", fs, expect);
        check (e.getLatency() == expect && worst < 0.08, msg, e.getLatency(), worst);

        // natural phase at this rate
        p.global.mode = ProcessingMode::NaturalPhase; p.version++;
        e.setParams (p);
        const int npLat = NaturalPhaseCorrector::kernelLengthFor (fs) / 2;
        settle (e, npLat, block);
        std::fill (L.begin(), L.end(), 0.f); std::fill (R.begin(), R.end(), 0.f);
        L[0] = 1.0f;
        for (int pos = 0; pos < len; pos += block) { float* io[2] = { L.data() + pos, R.data() + pos }; e.process (io, 2, nullptr, 0, std::min (block, len - pos)); }
        double wm = 0.0, wp = 0.0;
        for (double f : { 100.0, 500.0, 1000.0, 3000.0, 8000.0, 12000.0, 18000.0 })
        {
            const cplx m = dft (L, f, fs, 0), x = analog (f) * std::polar (1.0, -kTwoPi * f * npLat / fs);
            wm = std::max (wm, std::abs (gainToDb (std::abs (m)) - gainToDb (std::abs (x))));
            wp = std::max (wp, std::abs (std::arg (m / x)) * 180.0 / kPi);
        }
        std::snprintf (msg, sizeof msg, "fs %.0f: natural phase (latency %d) |H| dB / phase deg vs analog", fs, npLat);
        check (e.getLatency() == npLat && wm < 0.05 && wp < 1.0, msg, wm, wp);
    }

    // ---- offline renders are deterministic (mode switch + dynamic bands + spectral)
    {
        auto render = []
        {
            const double fs = 48000.0;
            EngineParams p;
            for (int b = 0; b < 8; ++b)
            {
                auto& bb = p.bands[(size_t) b]; bb.used = true; bb.shape = Shape::Bell; bb.freq = 80.0 * std::pow (2.2, b); bb.gainDb = b % 2 ? 4 : -4; bb.q = 1.4;
                bb.dynRange = -6; bb.dynBypass = false;
            }
            p.bands[3].spectral = true;
            p.global.mode = ProcessingMode::LinearPhase; p.version = 1;
            auto eOwner = std::make_unique<EQEngine>(); EQEngine& e = *eOwner;
            e.setNonRealtime (true);
            e.prepare (fs, 256, 2, p);
            std::mt19937 rng (3);
            std::normal_distribution<float> nd (0.0f, 0.2f);
            std::vector<float> out;
            std::vector<float> L (256), R (256);
            for (int blk = 0; blk < 800; ++blk)
            {
                if (blk == 300) { p.global.mode = ProcessingMode::NaturalPhase; p.version++; }
                if (blk == 500) { p.bands[2].gainDb = 9; p.version++; }
                e.setParams (p);
                for (int i = 0; i < 256; ++i) { L[(size_t) i] = nd (rng) * (blk % 20 < 10 ? 1.0f : 0.2f); R[(size_t) i] = nd (rng); }
                float* io[2] = { L.data(), R.data() };
                e.process (io, 2, nullptr, 0, 256);
                out.insert (out.end(), L.begin(), L.end());
            }
            return out;
        };
        const auto a = render();
        const auto b = render();
        double maxDiff = 0.0;
        for (size_t i = 0; i < a.size(); ++i) maxDiff = std::max (maxDiff, (double) std::abs (a[i] - b[i]));
        check (maxDiff == 0.0, "offline renders are bit-identical (mode switch, dynamic + spectral bands)", maxDiff);
    }

    // ---- silence in, silence out (no denormals, no noise from dynamics / spectral / character)
    {
        const double fs = 48000.0;
        EngineParams p;
        for (int b = 0; b < kMaxBands; ++b)
        {
            auto& bb = p.bands[(size_t) b]; bb.used = true; bb.shape = Shape::Bell; bb.freq = 30.0 * std::pow (1.33, b); bb.gainDb = 6; bb.q = 3; bb.dynRange = 8;
        }
        p.bands[5].spectral = true;
        p.global.character = CharacterMode::Warm; p.version = 1;
        auto eOwner = std::make_unique<EQEngine>(); EQEngine& e = *eOwner;
        e.prepare (fs, 512, 2, p);
        std::vector<float> L (512), R (512);
        // tiny impulse then a long silence: output must decay to exact silence / tiny values, all finite
        double peakTail = 0.0;
        bool finite = true;
        for (int blk = 0; blk < 400; ++blk)
        {
            std::fill (L.begin(), L.end(), 0.0f); std::fill (R.begin(), R.end(), 0.0f);
            if (blk == 0) L[0] = R[0] = 1.0e-3f;
            float* io[2] = { L.data(), R.data() };
            e.process (io, 2, nullptr, 0, 512);
            for (int i = 0; i < 512; ++i)
            {
                if (! std::isfinite (L[(size_t) i]) || ! std::isfinite (R[(size_t) i])) finite = false;
                if (blk > 300) peakTail = std::max (peakTail, (double) std::abs (L[(size_t) i]));
            }
        }
        check (finite && peakTail < 1.0e-9, "silence after an impulse decays to silence (24 dynamic bands, spectral, warm)", peakTail);
    }

    // ---- CPU at 96 kHz, 24 dynamic bands, every mode
    for (int mode = 0; mode < 3; ++mode)
    {
        const double fs = 96000.0;
        EngineParams q;
        for (int b = 0; b < kMaxBands; ++b)
        {
            auto& bb = q.bands[(size_t) b]; bb.used = true; bb.shape = Shape::Bell; bb.freq = 40.0 * std::pow (1.3, b); bb.gainDb = (b % 2 ? 3 : -3); bb.q = 2;
            bb.dynRange = (b % 2 ? -6 : 6);
        }
        q.global.mode = (ProcessingMode) mode; q.global.resolution = LPResolution::Medium; q.version = 100;
        auto ebOwner = std::make_unique<EQEngine>(); EQEngine& eb = *ebOwner;
        eb.prepare (fs, 512, 2, q);
        settle (eb, mode == 2 ? lpKernelLength (LPResolution::Medium, fs) / 2 + lpPartitionSize (fs) : (mode == 1 ? NaturalPhaseCorrector::kernelLengthFor (fs) / 2 : 0), 512);
        std::vector<float> L (512), R (512);
        std::mt19937 rng (7);
        std::normal_distribution<float> nd (0, 0.2f);
        double worst = 0.0, total = 0.0;
        const int blocks = 3000;
        for (int i = 0; i < blocks; ++i)
        {
            for (int k = 0; k < 512; ++k) { L[(size_t) k] = nd (rng) * (i % 40 < 20 ? 1.0f : 0.1f); R[(size_t) k] = nd (rng); }
            float* io[2] = { L.data(), R.data() };
            const auto t0 = std::chrono::high_resolution_clock::now();
            eb.process (io, 2, nullptr, 0, 512);
            const double us = std::chrono::duration<double, std::micro> (std::chrono::high_resolution_clock::now() - t0).count();
            total += us; worst = std::max (worst, us);
        }
        const double blockUs = 512.0 / fs * 1e6;
        std::printf ("BENCH 96 kHz %-13s 24 dynamic bells: avg %.1f%% of one core, worst block %.1f%% of its deadline\n",
                     mode == 0 ? "ZeroLatency" : (mode == 1 ? "NaturalPhase" : "LinearPhase"), 100.0 * total / blocks / blockUs, 100.0 * worst / blockUs);
    }

    std::printf ("%d failures\n", failures);
    return failures;
}
