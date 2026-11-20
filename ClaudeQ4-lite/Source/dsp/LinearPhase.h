/*
    Claude-Q 4 - linear phase engine

    The whole EQ (all non-spectral bands, with per band stereo placement) is expressed
    per frequency bin as a real 2x2 matrix in the L/R domain (zero phase), turned into
    up to four FIR kernels (Blackman windowed) and run through a uniformly partitioned
    overlap-save convolver.  Mixed L/R and M/S placements are handled exactly in a
    single stage (no doubled latency).

    Kernels are built on a background thread and swapped in with a one-partition
    cross-fade, so parameter changes (and dynamic EQ up to "High") are click free.
*/
#pragma once

#include "BandParams.h"
#include "FFT.h"
#include <memory>

namespace cq
{
    struct LPKernelSet
    {
        enum Layout { Single = 0, DiagLR = 1, Full = 2 };
        Layout layout = Single;
        int K = 0, P = 0;
        std::array<AlignedFloats, 4> spec;   // [kernel] -> K partitions of 2P floats (pffft unordered)

        void allocate (int numPartitions, int partition)
        {
            K = numPartitions; P = partition;
            for (auto& s : spec) s.allocate ((size_t) K * (size_t) (2 * P));
        }
        float* part (int kernel, int k) noexcept { return spec[(size_t) kernel].data() + (size_t) k * (size_t) (2 * P); }
        const float* part (int kernel, int k) const noexcept { return spec[(size_t) kernel].data() + (size_t) k * (size_t) (2 * P); }
    };

    // Builds kernel sets from parameters (runs on the background thread)
    class LPKernelBuilder
    {
    public:
        void configure (double sampleRate, int kernelLength, int partition);
        // dynGainsDb: per band dynamic gain offset (dB), may be nullptr
        void build (const EngineParams& params, const double* dynGainsDb, bool includeSpectral, LPKernelSet& out);
        int getKernelLength() const noexcept { return L; }
        int getPartition() const noexcept    { return P; }

    private:
        double fs = 48000.0;
        int L = 0, P = 0, N = 0;
        RealFFT bigFFT, partFFT;
        AlignedFloats freqBuf, timeBuf, segBuf, window;
        std::vector<double> m00, m01, m10, m11, binW;
    };

    class LinearPhaseConvolver
    {
    public:
        // allocation - not on the audio thread
        void configure (int kernelLength, int partition, int numChannels);
        void reset() noexcept;
        int getLatency() const noexcept { return L / 2 + P; }
        int getKernelLength() const noexcept { return L; }
        int getPartition() const noexcept { return P; }

        // the convolver owns two kernel sets; the worker fills the back set
        LPKernelSet* beginWriteBack() noexcept;     // nullptr if the back set is busy
        void endWriteBack (bool publish) noexcept;
        bool hasKernel() const noexcept { return frontValid; }
        bool isBackFree() const noexcept { return backState.load() == 0; }

        // audio thread
        void process (float* L, float* R, int n) noexcept;

    private:
        void processPartition() noexcept;
        void convolve (const LPKernelSet& ks, float* out0, float* out1) noexcept;

        int L = 0, P = 0, K = 0, numCh = 2;
        RealFFT fft;
        std::array<LPKernelSet, 2> sets;
        int front = 0;
        bool frontValid = false;
        std::atomic<int> backState { 0 };   // 0 free, 1 writing, 2 ready
        bool xfading = false;

        std::array<AlignedFloats, 2> fdl;     // K spectra per input channel
        int fdlPos = 0;
        std::array<AlignedFloats, 2> inFifo, prevIn, outFifo;
        int fifoPos = 0;
        AlignedFloats tbuf, acc, ybuf, yOld0, yOld1, yNew0, yNew1;
    };
}
