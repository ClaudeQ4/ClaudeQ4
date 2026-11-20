// EQEngine integration tests + CPU benchmark
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
    std::printf ("%s %s (%.4g, %.4g)\n", ok ? "PASS" : "FAIL", what, a, b);
}

static void runSilence (EQEngine& e, int samples, int block = 512)
{
    std::vector<float> L ((size_t) block, 0.f), R ((size_t) block, 0.f);
    float* io[2] = { L.data(), R.data() };
    for (int pos = 0; pos < samples; pos += block) e.process (io, 2, nullptr, 0, block);
}

static void waitLatency (EQEngine& e, int expected)
{
    for (int i = 0; i < 400 && e.getLatency() != expected; ++i)
    {
        runSilence (e, 512);
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }
}

static std::vector<float> impulseResponse (EQEngine& e, int len, int ch = 0)
{
    std::vector<float> L ((size_t) len, 0.f), R ((size_t) len, 0.f);
    (ch == 0 ? L : R)[0] = 1.0f;
    for (int pos = 0; pos < len; pos += 512)
    {
        float* io[2] = { L.data() + pos, R.data() + pos };
        e.process (io, 2, nullptr, 0, std::min (512, len - pos));
    }
    return L;
}

static cplx dft (const std::vector<float>& h, double f, double fs, int start = 0)
{
    cplx acc (0, 0);
    for (size_t n = (size_t) start; n < h.size(); ++n) acc += (double) h[n] * std::polar (1.0, -kTwoPi * f * (double) (n - (size_t) start) / fs);
    return acc;
}

int main()
{
    const double fs = 48000.0;
    EngineParams p;
    auto& b0 = p.bands[0]; b0.used = true; b0.shape = Shape::Bell; b0.freq = 2000; b0.gainDb = 8; b0.q = 1.2;
    auto& b1 = p.bands[1]; b1.used = true; b1.shape = Shape::HighCut; b1.slope = Slope::dB24; b1.freq = 14000;
    p.version = 1;
    AnalogFilter a0, a1; makeAnalogPrototype (b0.shapeParams (1.0), a0); makeAnalogPrototype (b1.shapeParams (1.0), a1);
    auto analog = [&] (double f) { return a0.eval (f / b0.freq) * a1.eval (f / b1.freq); };

    auto eOwner = std::make_unique<EQEngine>(); EQEngine& e = *eOwner;
    e.prepare (fs, 512, 2, p);
    e.setParams (p);
    runSilence (e, 48000);

    // ---- zero latency response
    {
        auto h = impulseResponse (e, 1 << 15);
        double worst = 0;
        for (double f : { 100.0, 1000.0, 2000.0, 5000.0, 10000.0, 14000.0 }) worst = std::max (worst, std::abs (gainToDb (std::abs (dft (h, f, fs))) - gainToDb (std::abs (analog (f)))));
        check (e.getLatency() == 0 && worst < 0.2, "zero latency: latency 0, magnitude vs analog (dB)", e.getLatency(), worst);
    }
    // ---- natural phase
    {
        p.global.mode = ProcessingMode::NaturalPhase; p.version++;
        e.setParams (p);
        waitLatency (e, 128);
        runSilence (e, 24000);
        std::this_thread::sleep_for (std::chrono::milliseconds (50));
        runSilence (e, 24000);
        auto h = impulseResponse (e, 1 << 15);
        double worstM = 0, worstP = 0;
        for (double f : { 100.0, 1000.0, 2000.0, 5000.0, 10000.0, 14000.0, 18000.0 })
        {
            const cplx m = dft (h, f, fs), x = analog (f) * std::polar (1.0, -kTwoPi * f * 128 / fs);
            worstM = std::max (worstM, std::abs (gainToDb (std::abs (m)) - gainToDb (std::abs (x))));
            worstP = std::max (worstP, std::abs (std::arg (m / x)) * 180 / kPi);
        }
        check (e.getLatency() == 128 && worstM < 0.05 && worstP < 0.5, "natural phase: latency 128, |H| err dB / phase err deg", worstM, worstP);
    }
    // ---- linear phase
    {
        p.global.mode = ProcessingMode::LinearPhase; p.global.resolution = LPResolution::Medium; p.version++;
        e.setParams (p);
        waitLatency (e, 5120);
        runSilence (e, 48000);
        auto h = impulseResponse (e, 1 << 15);
        int pk = 0; for (size_t i = 0; i < h.size(); ++i) if (std::abs (h[i]) > std::abs (h[(size_t) pk])) pk = (int) i;
        double worst = 0;
        for (double f : { 100.0, 1000.0, 2000.0, 5000.0, 10000.0, 14000.0 }) worst = std::max (worst, std::abs (gainToDb (std::abs (dft (h, f, fs))) - gainToDb (std::abs (analog (f)))));
        check (e.getLatency() == 5120 && pk == 5120 && worst < 0.05, "linear phase: latency 5120, peak at latency, |H| err", pk, worst);
    }
    // ---- bypass is latency compensated
    {
        p.global.bypass = true; p.version++;
        e.setParams (p);
        runSilence (e, 4096);
        auto h = impulseResponse (e, 16384);
        int pk = 0; for (size_t i = 0; i < h.size(); ++i) if (std::abs (h[i]) > std::abs (h[(size_t) pk])) pk = (int) i;
        check (pk == 5120 && std::abs (h[(size_t) pk] - 1.0f) < 1e-6, "bypass delayed by the latency", pk, h[(size_t) pk]);
        p.global.bypass = false; p.version++;
        e.setParams (p);
    }
    // ---- dynamic EQ in linear phase: loud 2 kHz burst gets compressed
    {
        auto& b2 = p.bands[2]; b2.used = true; b2.shape = Shape::Bell; b2.freq = 2000; b2.q = 2; b2.gainDb = 0; b2.dynRange = -9; b2.autoThreshold = false; b2.thresholdDb = -30;
        p.version++;
        e.setParams (p);
        runSilence (e, 48000);
        const int len = 96000;
        std::vector<float> L ((size_t) len), R ((size_t) len);
        for (int i = 0; i < len; ++i) L[(size_t) i] = R[(size_t) i] = (float) ((i > 24000 ? 0.5 : 0.01) * std::sin (kTwoPi * 2000.0 * i / fs));
        for (int pos = 0; pos < len; pos += 512) { float* io[2] = { L.data() + pos, R.data() + pos }; e.process (io, 2, nullptr, 0, std::min (512, len - pos)); std::this_thread::sleep_for (std::chrono::microseconds (300)); }
        double pkOut = 0; for (int i = len - 8000; i < len; ++i) pkOut = std::max (pkOut, (double) std::abs (L[(size_t) i]));
        // static path: bell +8 dB at 2 kHz -> 0.5 * 2.51 = 1.26 without dynamics
        const double staticPeak = 0.5 * std::abs (analog (2000.0));
        const double red = gainToDb (staticPeak) - gainToDb (pkOut);
        check (red > 4.0 && red < 10.0, "dynamic bell compresses the loud tone in linear phase (dB)", red, e.getDynamicGainDb (2));
        p.bands[2].used = false; p.version++;
        e.setParams (p);
    }
    // ---- spectral band adds one STFT frame of latency
    {
        p.global.mode = ProcessingMode::ZeroLatency; p.version++;
        auto& b3 = p.bands[3]; b3.used = true; b3.shape = Shape::Bell; b3.freq = 4000; b3.q = 1; b3.dynRange = -6; b3.spectral = true;
        e.setParams (p);
        waitLatency (e, SpectralEngine::frameSizeFor (LPResolution::Medium, fs));
        check (e.getLatency() == 4096, "spectral band: zero latency + STFT frame", e.getLatency(), 4096);
        p.bands[3].used = false; p.version++;
        e.setParams (p);
        waitLatency (e, 0);
        check (e.getLatency() == 0, "spectral band removed: back to zero latency", e.getLatency(), 0);
    }
    // ---- solo: bell band soloed -> band limited output
    {
        e.setSolo (0, false);
        runSilence (e, 4800);
        std::vector<float> L (48000), R (48000);
        std::mt19937 rng (5); std::normal_distribution<float> nd (0, 0.1f);
        for (size_t i = 0; i < L.size(); ++i) L[i] = R[i] = nd (rng);
        for (int pos = 0; pos < 48000; pos += 512) { float* io[2] = { L.data() + pos, R.data() + pos }; e.process (io, 2, nullptr, 0, std::min (512, 48000 - pos)); }
        auto band = [&] (double f) { double acc = 0; for (int k = -3; k <= 3; ++k) acc += std::norm (dft (L, f * std::pow (1.02, k), fs, 24000)); return 10 * std::log10 (acc); };
        const double inBand = band (2000), outLow = band (200), outHigh = band (12000);
        check (inBand - outLow > 20 && inBand - outHigh > 20, "solo isolates the band region (dB below in-band: 200 Hz, 12 kHz)", inBand - outLow, inBand - outHigh);
        e.setSolo (-1, false);
    }

    // ---- CPU benchmark: 24 dynamic bells, stereo, 48 kHz
    for (int mode = 0; mode < 3; ++mode)
    {
        EngineParams q;
        for (int b = 0; b < kMaxBands; ++b)
        {
            auto& bb = q.bands[(size_t) b]; bb.used = true; bb.shape = Shape::Bell; bb.freq = 40.0 * std::pow (1.3, b); bb.gainDb = (b % 2 ? 3 : -3); bb.q = 2;
            bb.dynRange = (b % 2 ? -6 : 6); bb.slope = b % 3 == 0 ? Slope::dB24 : Slope::dB12;
        }
        q.global.mode = (ProcessingMode) mode; q.global.resolution = LPResolution::Medium; q.version = 100;
        auto ebOwner = std::make_unique<EQEngine>(); EQEngine& eb = *ebOwner; eb.prepare (fs, 512, 2, q); eb.setParams (q);
        std::vector<float> L (512), R (512);
        std::mt19937 rng (7); std::normal_distribution<float> nd (0, 0.2f);
        // warm up
        for (int i = 0; i < 200; ++i) { for (int k = 0; k < 512; ++k) { L[(size_t) k] = nd (rng); R[(size_t) k] = nd (rng); } float* io[2] = { L.data(), R.data() }; eb.process (io, 2, nullptr, 0, 512); }
        double worstBlock = 0, total = 0;
        const int blocks = 2000;
        for (int i = 0; i < blocks; ++i)
        {
            for (int k = 0; k < 512; ++k) { L[(size_t) k] = nd (rng) * (i % 40 < 20 ? 1.0f : 0.1f); R[(size_t) k] = nd (rng); }
            float* io[2] = { L.data(), R.data() };
            const auto t0 = std::chrono::high_resolution_clock::now();
            eb.process (io, 2, nullptr, 0, 512);
            const double us = std::chrono::duration<double, std::micro> (std::chrono::high_resolution_clock::now() - t0).count();
            total += us; worstBlock = std::max (worstBlock, us);
        }
        const double blockTime = 512.0 / fs * 1e6;
        std::printf ("BENCH %-13s 24 dynamic bells: avg %.1f%% CPU of one core, worst block %.1f%% of its deadline\n",
                     mode == 0 ? "ZeroLatency" : (mode == 1 ? "NaturalPhase" : "LinearPhase"), 100.0 * total / blocks / blockTime, 100.0 * worstBlock / blockTime);
    }

    std::printf ("%d failures\n", failures);
    return failures;
}
