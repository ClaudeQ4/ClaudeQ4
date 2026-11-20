/*
    Claude-Q 4 - spectral dynamics (STFT)

    Spectral bands keep their static curve but their dynamic part works per frequency
    bin: only the frequencies inside the band that rise above the (per bin) threshold are
    compressed or expanded.  The detector spectrum can be tilted (+3 dB/oct, default on)
    and smoothed across frequency according to the "density" control (low density reacts
    to wide areas, high density to narrow resonances).  Automatic threshold = local
    spectral envelope (1 octave smoothing) averaged over time, so resonances that stick
    out of their surroundings are what triggers.

    Processing is zero phase per frame (sqrt-Hann WOLA, 75% overlap), i.e. linear phase,
    with a latency of one frame.
*/
#pragma once

#include "BandParams.h"
#include "FFT.h"

namespace cq
{
    class SpectralEngine
    {
    public:
        static constexpr int kDisplayPoints = 160;

        static int frameSizeFor (LPResolution r, double fs) noexcept
        {
            int n = r == LPResolution::Low ? 2048 : (r == LPResolution::Medium ? 4096 : 8192);
            if (fs > 50000.0)  n *= 2;
            if (fs > 100000.0) n *= 2;
            if (fs > 200000.0) n *= 2;
            return n;
        }

        void configure (double sampleRate, int frameSize, int numChannels);   // allocation
        void reset() noexcept;
        int getLatency() const noexcept { return N; }
        int getFrameSize() const noexcept { return N; }

        // once per host block (cheap unless band parameters changed)
        void setParams (const EngineParams& p) noexcept;

        // in place; sc may be nullptr (no side chain)
        void process (float* L, float* R, const float* scL, const float* scR, int n) noexcept;

        // display: dynamic gain change (dB) of a spectral band at log spaced points 20 Hz .. 20 kHz
        float getDisplayGain (int band, int point) const noexcept { return display[(size_t) band][(size_t) point].load (std::memory_order_relaxed); }

    private:
        struct BandCache
        {
            bool valid = false;
            BandParams p {};
            double gainScale = 1.0;
            std::vector<float> staticDb;   // band's own static curve (dB) per bin
            std::vector<float> weight;     // 0..1 region weight per bin
            std::vector<float> dyn;        // current dynamic gain (dB) per bin (ballistics state)
            std::vector<float> ref;        // automatic threshold reference (dB) per bin
            bool refInit = false;
        };

        void processFrame() noexcept;
        void updateBandCache (int b) noexcept;
        void smoothAcross (float* v, int count, double widthOct) noexcept;

        double fs = 48000.0;
        int N = 4096, H = 1024, K = 2049, numCh = 2;
        RealFFT fft;
        AlignedFloats winA, winS, tmp, spec[2], scSpec[2];
        std::array<std::vector<float>, 2> inBuf, scBuf, outAcc, outBuf;
        int hopPos = 0;
        bool scPresent = false;
        float norm = 1.0f, calib = 1.0f;

        EngineParams params {};
        std::array<BandCache, kMaxBands> cache;
        std::vector<float> det, lvl, smooth1, tilt, tiltPow, gainDbBin, gL, gR, gM, gS;
        std::vector<double> binF;
        bool anySpectral = false;

        std::array<std::array<std::atomic<float>, kDisplayPoints>, kMaxBands> display;
        std::array<int, kDisplayPoints> displayBin {};
    };
}
