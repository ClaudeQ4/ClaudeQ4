/*
    Claude-Q 4 - natural phase corrector

    The zero latency IIR chain already matches the analog magnitude response; its phase
    still deviates from the analog prototype towards Nyquist (the analog response keeps
    going above fs/2, a digital one cannot).  This stage adds a short FIR correction
    C(f) = H_analog(f) / H_iir(f) (2x2 matrix form for mixed L/R and M/S bands), centred
    at a small delay D, so that the complete chain reproduces the analog magnitude AND
    phase response (to within ~0.01 dB / 0.01 degrees up to 20 kHz) with only D samples
    of latency (~2.7 ms).
*/
#pragma once

#include "BandParams.h"
#include "FFT.h"

namespace cq
{
    // What the corrector needs to know about one band of the running IIR chain
    struct NPBandSnapshot
    {
        bool active = false;
        Placement place = Placement::Stereo;
        BandShapeParams shape {};       // analog prototype parameters (smoothed, incl. dynamic gain)
        int num = 0;
        std::array<bool, kMaxSections> second {};
        std::array<double, kMaxSections * 5> coefs {};   // g, k, m0, m1, m2 per section
    };

    struct NPSnapshot
    {
        std::array<NPBandSnapshot, kMaxBands> bands {};
        double wet[kMaxBands] {};
        uint32_t serial = 0;
    };

    struct NPKernelSet
    {
        int layout = 0;   // 0 single, 1 diagonal L/R, 2 full 2x2
        int length = 0;
        std::array<std::vector<float>, 4> rev;   // time reversed kernels
    };

    class NPKernelBuilder
    {
    public:
        void configure (double sampleRate, int kernelLength);
        void build (const NPSnapshot& snap, NPKernelSet& out);

    private:
        double fs = 48000.0;
        int Lc = 256, N = 2048;
        RealFFT fft;
        AlignedFloats freq, time;
        std::vector<cplx> a00, a01, a10, a11, d00, d01, d10, d11;
        std::vector<double> win;
    };

    class NaturalPhaseCorrector
    {
    public:
        static int kernelLengthFor (double fs) noexcept
        {
            if (fs > 200000.0) return 2048;
            if (fs > 100000.0) return 1024;
            if (fs > 50000.0)  return 512;
            return 256;
        }

        void configure (double sampleRate, int numChannels);
        void reset() noexcept;
        int getLatency() const noexcept { return Lc / 2; }

        NPKernelSet* beginWriteBack() noexcept;
        void endWriteBack (bool publish) noexcept;
        bool isBackFree() const noexcept { return backState.load() == 0; }

        void process (float* L, float* R, int n) noexcept;

    private:
        static float dot (const float* a, const float* b, int n) noexcept;

        int Lc = 256, numCh = 2;
        std::array<NPKernelSet, 2> sets;
        int front = 0;
        bool frontValid = false;
        std::atomic<int> backState { 0 };
        int xfadePos = 0, xfadeLen = 512;
        bool xfading = false;
        std::array<std::vector<float>, 2> hist;   // doubled history
        int pos = 0;
    };
}
