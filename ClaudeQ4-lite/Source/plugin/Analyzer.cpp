/*
    Claude-Q 4 - spectrum analyzer
*/
#include "Analyzer.h"

namespace cq
{
    SpectrumAnalyzer::SpectrumAnalyzer()
    {
        smoothPow.fill (0.0f);
        smoothDb.fill (-200.0f);
        avgPow.fill (0.0f);
        avgDb.fill (-200.0f);
    }

    void SpectrumAnalyzer::configure (double sampleRate, int resolution)
    {
        fs = sampleRate;
        res = juce::jlimit (0, 4, resolution);
        static const int base[] = { 2048, 4096, 8192, 16384, 32768 };
        N = base[res];
        if (fs > 50000.0)  N *= 2;
        if (fs > 100000.0) N *= 2;
        if (fs > 200000.0) N *= 2;
        hop = juce::jlimit (256, 4096, N / 8);
        // never fewer than ~40 frames per second (overlap grows with the resolution)
        hop = std::min (hop, (int) (fs / 40.0));

        fft.init (N);
        window.allocate ((size_t) N);
        frame.allocate ((size_t) N);
        spectrum.allocate ((size_t) N);
        history.assign ((size_t) N, 0.0f);
        scratch.assign ((size_t) AnalyzerTap::kSize, 0.0f);
        binPow.assign ((size_t) (N / 2 + 1), 0.0f);
        histPos = 0;
        sinceHop = 0;

        // 4-term Blackman-Harris
        double sum = 0.0;
        for (int i = 0; i < N; ++i)
        {
            const double x = kTwoPi * i / N;
            const double w = 0.35875 - 0.48829 * std::cos (x) + 0.14128 * std::cos (2 * x) - 0.01168 * std::cos (3 * x);
            window[(size_t) i] = (float) w;
            sum += w;
        }
        // a full scale sine reads 0 dB
        norm = (float) std::pow (2.0 / sum, 2.0);

        const double binHz = fs / N;
        for (int i = 0; i < kPoints; ++i)
        {
            const double f = pointFreq (i);
            const double fLo = i > 0 ? std::sqrt (f * pointFreq (i - 1)) : f;
            const double fHi = i < kPoints - 1 ? std::sqrt (f * pointFreq (i + 1)) : f;
            const double b = f / binHz;
            pFrac[(size_t) i] = (float) b;
            pLo[(size_t) i] = juce::jlimit (0, N / 2, (int) std::ceil (fLo / binHz));
            pHi[(size_t) i] = juce::jlimit (0, N / 2, (int) std::floor (fHi / binHz));
        }
        smoothPow.fill (0.0f);
        smoothDb.fill (-200.0f);
        avgPow.fill (0.0f);
        avgDb.fill (-200.0f);
        peakDb = -200.0f;
    }

    bool SpectrumAnalyzer::update (AnalyzerTap& tap, double sampleRate, double now, int resolution, int speed, bool freeze)
    {
        if (sampleRate <= 0.0) return false;
        // several editors may drive the same analyzer (collision view): rate limit
        if (lastUpdate >= 0.0 && now - lastUpdate < 0.006) return false;
        if (! juce::exactlyEqual (sampleRate, fs) || resolution != res) configure (sampleRate, resolution);
        const double gap = lastUpdate < 0.0 ? 1.0 : now - lastUpdate;
        lastUpdate = now;

        int got = tap.read (scratch.data(), (int) scratch.size());
        if (freeze) return false;
        if (gap > 0.5)
        {
            // stale data after a pause: keep only the most recent part
            sinceHop = 0;
        }
        bool changed = false;
        const float* src = scratch.data();
        // limit the work per call (at most 4 frames), older data only refreshes the history
        const int maxFrames = 4;
        if (got > hop * maxFrames + N)
        {
            const int skip = got - (hop * maxFrames + N);
            src += skip;
            got -= skip;
        }
        const int firstAnalysed = got - hop * maxFrames;
        const int expectedFrames = std::max (1, std::min (maxFrames, (sinceHop + got) / std::max (1, hop)));
        const double dtFrame = std::max ((double) hop / fs, std::min (gap, 0.25) / expectedFrames);
        for (int i = 0; i < got; ++i)
        {
            history[(size_t) histPos] = src[i];
            if (++histPos >= N) histPos = 0;
            if (++sinceHop >= hop)
            {
                sinceHop = 0;
                if (i >= firstAnalysed)
                {
                    analyseFrame (dtFrame, speed);
                    changed = true;
                }
            }
        }
        if (got == 0 && gap > 0.05)
        {
            // no audio arrives (transport stopped / no processing): let the display decay
            const int steps = juce::jlimit (1, 8, (int) (gap / dtFrame));
            for (int s = 0; s < steps; ++s)
            {
                std::fill (history.begin(), history.end(), 0.0f);
                analyseFrame (dtFrame, speed);
            }
            changed = true;
        }
        return changed;
    }

    void SpectrumAnalyzer::analyseFrame (double dt, int speed)
    {
        // unroll the circular history (oldest first) and window it
        float* fr = frame.data();
        const float* w = window.data();
        int p = histPos;
        for (int i = 0; i < N; ++i)
        {
            fr[i] = history[(size_t) p] * w[i];
            if (++p >= N) p = 0;
        }
        fft.forwardOrdered (fr, spectrum.data());
        const float* s = spectrum.data();
        const int half = N / 2;
        binPow[0] = s[0] * s[0] * norm * 0.25f;
        binPow[(size_t) half] = s[1] * s[1] * norm * 0.25f;
        for (int k = 1; k < half; ++k)
        {
            const float re = s[2 * k], im = s[2 * k + 1];
            binPow[(size_t) k] = (re * re + im * im) * norm;
        }

        static const double taus[] = { 1.6, 0.7, 0.3, 0.12, 0.05 };
        const double tau = taus[juce::jlimit (0, 4, speed)];
        const float aFall = (float) (1.0 - std::exp (-dt / tau));
        const float aRise = (float) (1.0 - std::exp (-dt / (tau * 0.25)));
        const float aAvg = (float) (1.0 - std::exp (-dt / 3.0));
        float pk = -200.0f;

        for (int i = 0; i < kPoints; ++i)
        {
            float pw;
            const int lo = pLo[(size_t) i], hi = pHi[(size_t) i];
            if (hi - lo >= 1)
            {
                pw = 0.0f;
                for (int k = lo; k <= hi; ++k) pw = std::max (pw, binPow[(size_t) k]);
            }
            else
            {
                const float b = pFrac[(size_t) i];
                const int k0 = juce::jlimit (0, half - 1, (int) b);
                const float t = juce::jlimit (0.0f, 1.0f, b - (float) k0);
                // interpolate in the log domain (smooth low end)
                const float d0 = std::log (binPow[(size_t) k0] + 1.0e-30f), d1 = std::log (binPow[(size_t) k0 + 1] + 1.0e-30f);
                pw = std::exp (d0 + (d1 - d0) * t);
            }
            float& sp = smoothPow[(size_t) i];
            sp += (pw - sp) * (pw > sp ? aRise : aFall);
            if (! (sp >= 0.0f) || ! std::isfinite (sp)) sp = 0.0f;
            const float db = (float) powerToDb ((double) sp + 1.0e-24);
            smoothDb[(size_t) i] = db;
            pk = std::max (pk, db);

            float& ap = avgPow[(size_t) i];
            ap += (pw - ap) * aAvg;
            if (! std::isfinite (ap)) ap = 0.0f;
            avgDb[(size_t) i] = (float) powerToDb ((double) ap + 1.0e-24);
        }
        peakDb = pk;
    }

    void SpectrumAnalyzer::findPeaks (const float* db, int n, int maxPeaks, float minProminence, float floorDb, std::vector<int>& out)
    {
        out.clear();
        struct Cand { int i; float level; float prom; };
        std::vector<Cand> cands;
        const int halfOct = juce::roundToInt ((n - 1) / std::log2 (kMaxFreq / kMinFreq) * 0.5);
        for (int i = 3; i < n - 3; ++i)
        {
            const float v = db[i];
            if (v < floorDb) continue;
            bool isMax = true;
            for (int k = -3; k <= 3 && isMax; ++k) if (k != 0 && db[i + k] > v) isMax = false;
            if (! isMax) continue;
            float minL = v, minR = v;
            for (int k = 1; k <= halfOct; ++k)
            {
                if (i - k >= 0) minL = std::min (minL, db[i - k]);
                if (i + k < n)  minR = std::min (minR, db[i + k]);
            }
            const float prom = v - std::max (minL, minR);
            if (prom >= minProminence) cands.push_back ({ i, v, prom });
        }
        std::sort (cands.begin(), cands.end(), [] (const Cand& a, const Cand& b) { return a.level + 0.5f * a.prom > b.level + 0.5f * b.prom; });
        const int minSpacing = juce::roundToInt ((n - 1) / std::log2 (kMaxFreq / kMinFreq) / 2.5);   // ~0.4 octave
        for (const auto& c : cands)
        {
            bool ok = true;
            for (int j : out) if (std::abs (j - c.i) < minSpacing) { ok = false; break; }
            if (ok) out.push_back (c.i);
            if ((int) out.size() >= maxPeaks) break;
        }
        std::sort (out.begin(), out.end());
    }
}
