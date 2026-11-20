/*
    Claude-Q 4 - spectrum analyzer

    Audio thread -> AnalyzerTap (lock-free single producer / single consumer FIFO, mono sum)
    Message thread -> SpectrumAnalyzer::update() : windowed FFT (4-term Blackman-Harris,
    low leakage for the 120 dB range), peak-preserving mapping onto a fixed logarithmic
    display grid (10 Hz .. 30 kHz), power-domain ballistics.  The spectra are stored
    without tilt; the 4.5 dB/oct (or any other) tilt is applied when drawing, so collision
    detection and EQ matching compare unweighted data.
*/
#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "dsp/FFT.h"
#include "dsp/DspCommon.h"

namespace cq
{
    class AnalyzerTap
    {
    public:
        static constexpr int kSize = 1 << 17;

        AnalyzerTap() : fifo (kSize) { buffer.resize ((size_t) kSize, 0.0f); }

        // audio thread.  b may be nullptr (mono)
        void push (const float* a, const float* b, int n) noexcept
        {
            int s1, n1, s2, n2;
            fifo.prepareToWrite (n, s1, n1, s2, n2);
            auto copy = [&] (int start, int count, int offset)
            {
                float* d = buffer.data() + start;
                if (b != nullptr) for (int i = 0; i < count; ++i) d[i] = 0.5f * (a[offset + i] + b[offset + i]);
                else              std::memcpy (d, a + offset, sizeof (float) * (size_t) count);
            };
            if (n1 > 0) copy (s1, n1, 0);
            if (n2 > 0) copy (s2, n2, n1);
            fifo.finishedWrite (n1 + n2);
        }

        // message thread
        int read (float* dst, int maxN) noexcept
        {
            int s1, n1, s2, n2;
            fifo.prepareToRead (maxN, s1, n1, s2, n2);
            if (n1 > 0) std::memcpy (dst, buffer.data() + s1, sizeof (float) * (size_t) n1);
            if (n2 > 0) std::memcpy (dst + n1, buffer.data() + s2, sizeof (float) * (size_t) n2);
            fifo.finishedRead (n1 + n2);
            return n1 + n2;
        }
        int available() const noexcept { return fifo.getNumReady(); }

    private:
        juce::AbstractFifo fifo;
        std::vector<float> buffer;
    };

    struct AnalyzerSettings
    {
        bool pre = true, post = true, sc = false;
        int resolution = 2;          // 0 low .. 4 maximum
        int speed = 2;               // 0 very slow .. 4 very fast
        float tilt = 4.5f;           // dB / octave
        float range = 90.0f;         // 60, 90, 120 dB
        bool freeze = false;
        bool collisions = true;
        bool spectrumGrab = true;
    };

    class SpectrumAnalyzer
    {
    public:
        static constexpr int kPoints = 480;
        static double pointFreq (int i) noexcept { return kMinFreq * std::pow (kMaxFreq / kMinFreq, (double) i / (kPoints - 1)); }
        static double freqToPoint (double f) noexcept { return std::log (std::max (f, 1.0) / kMinFreq) / std::log (kMaxFreq / kMinFreq) * (kPoints - 1); }

        SpectrumAnalyzer();

        // message thread: consume the tap and refresh the spectrum.  Returns true if it changed.
        bool update (AnalyzerTap& tap, double sampleRate, double nowSeconds, int resolution, int speed, bool freeze);

        const float* getDb() const noexcept       { return smoothDb.data(); }   // display (ballistics), no tilt
        const float* getAverageDb() const noexcept { return avgDb.data(); }     // ~3 s average, no tilt
        float getPeakDb() const noexcept          { return peakDb; }
        bool hasSignal() const noexcept           { return peakDb > -140.0f; }
        double getLastUpdate() const noexcept     { return lastUpdate; }

        // most prominent peaks of a dB curve (indices into the display grid)
        static void findPeaks (const float* db, int n, int maxPeaks, float minProminence, float floorDb, std::vector<int>& out);

    private:
        void configure (double sampleRate, int resolution);
        void analyseFrame (double dt, int speed);

        double fs = 0.0;
        int res = -1, N = 0, hop = 1024;
        RealFFT fft;
        AlignedFloats window, frame, spectrum;
        std::vector<float> history, scratch, binPow;
        int histPos = 0, sinceHop = 0;
        float norm = 1.0f;
        std::array<int, kPoints> pLo {}, pHi {};
        std::array<float, kPoints> pFrac {};
        std::array<float, kPoints> smoothPow {}, smoothDb {}, avgPow {}, avgDb {};
        float peakDb = -200.0f;
        double lastUpdate = -1.0;
    };

    // long term average of a spectrum (EQ match)
    struct SpectrumAccumulator
    {
        std::array<double, SpectrumAnalyzer::kPoints> sum {};
        int count = 0;
        void reset() noexcept { sum.fill (0.0); count = 0; }
        void add (const float* db) noexcept
        {
            for (int i = 0; i < SpectrumAnalyzer::kPoints; ++i) sum[(size_t) i] += dbToPower (db[i]);
            ++count;
        }
        float getDb (int i) const noexcept { return count > 0 ? (float) powerToDb (sum[(size_t) i] / count) : -200.0f; }
    };
}
