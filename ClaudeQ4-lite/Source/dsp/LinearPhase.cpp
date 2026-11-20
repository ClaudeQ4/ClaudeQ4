/*
    Claude-Q 4 - linear phase engine
*/
#include "LinearPhase.h"

namespace cq
{
    //==============================================================================
    void LPKernelBuilder::configure (double sampleRate, int kernelLength, int partition)
    {
        fs = sampleRate;
        L = kernelLength;
        P = partition;
        N = 2 * L;
        bigFFT.init (N);
        partFFT.init (2 * P);
        freqBuf.allocate ((size_t) N);
        timeBuf.allocate ((size_t) N);
        segBuf.allocate ((size_t) (2 * P));
        window.allocate ((size_t) L);
        for (int n = 0; n < L; ++n)
        {
            const double x = kTwoPi * n / L;
            window[(size_t) n] = (float) (0.42 - 0.5 * std::cos (x) + 0.08 * std::cos (2.0 * x));   // Blackman, peak at L/2
        }
        const size_t nb = (size_t) (N / 2 + 1);
        m00.assign (nb, 1.0); m01.assign (nb, 0.0); m10.assign (nb, 0.0); m11.assign (nb, 1.0);
        binW.resize (nb);
        for (size_t k = 0; k < nb; ++k) binW[k] = (double) k * fs / N;
    }

    void LPKernelBuilder::build (const EngineParams& params, const double* dynGainsDb, bool includeSpectral, LPKernelSet& out)
    {
        const size_t nb = (size_t) (N / 2 + 1);
        std::fill (m00.begin(), m00.end(), 1.0);
        std::fill (m01.begin(), m01.end(), 0.0);
        std::fill (m10.begin(), m10.end(), 0.0);
        std::fill (m11.begin(), m11.end(), 1.0);

        bool anyLR = false, anyMS = false;
        for (int b = 0; b < kMaxBands; ++b)
        {
            const auto& bp = params.bands[(size_t) b];
            if (! bp.isActive() || (bp.isSpectral() && ! includeSpectral)) continue;
            const double dyn = (dynGainsDb != nullptr && bp.isDynamic()) ? dynGainsDb[b] : 0.0;
            AnalogFilter a;
            makeAnalogPrototype (bp.shapeParams (params.global.gainScale, dyn), a);
            const double invF0 = 1.0 / clampT (bp.freq, kMinFreq, kMaxFreq);
            const Placement pl = bp.place;
            if (pl == Placement::Left || pl == Placement::Right) anyLR = true;
            if (pl == Placement::Mid || pl == Placement::Side) anyMS = true;

            for (size_t k = 0; k < nb; ++k)
            {
                const double h = std::sqrt (a.magSq (binW[k] * invF0));
                switch (pl)
                {
                    case Placement::Stereo: m00[k] *= h; m01[k] *= h; m10[k] *= h; m11[k] *= h; break;
                    case Placement::Left:   m00[k] *= h; m01[k] *= h; break;
                    case Placement::Right:  m10[k] *= h; m11[k] *= h; break;
                    case Placement::Mid:
                    case Placement::Side:
                    {
                        const double p = 0.5 * (h + 1.0);
                        const double q = (pl == Placement::Mid) ? 0.5 * (h - 1.0) : 0.5 * (1.0 - h);
                        const double a00 = p * m00[k] + q * m10[k], a01 = p * m01[k] + q * m11[k];
                        const double a10 = q * m00[k] + p * m10[k], a11 = q * m01[k] + p * m11[k];
                        m00[k] = a00; m01[k] = a01; m10[k] = a10; m11[k] = a11;
                        break;
                    }
                    default: break;
                }
            }
        }

        LPKernelSet::Layout layout = LPKernelSet::Single;
        if (anyMS) layout = LPKernelSet::Full;
        else if (anyLR) layout = LPKernelSet::DiagLR;
        out.layout = layout;

        const int K = (L + P - 1) / P;
        if (out.K != K || out.P != P) out.allocate (K, P);

        const std::vector<double>* srcs[4] = { &m00, &m11, nullptr, nullptr };
        int numKernels = 1;
        if (layout == LPKernelSet::DiagLR) numKernels = 2;
        if (layout == LPKernelSet::Full) { numKernels = 4; srcs[0] = &m00; srcs[1] = &m01; srcs[2] = &m10; srcs[3] = &m11; }

        const float invN = 1.0f / (float) N;
        for (int j = 0; j < numKernels; ++j)
        {
            const auto& m = *srcs[j];
            float* fb = freqBuf.data();
            fb[0] = (float) m[0];
            fb[1] = (float) m[(size_t) (N / 2)];
            for (int k = 1; k < N / 2; ++k) { fb[2 * k] = (float) m[(size_t) k]; fb[2 * k + 1] = 0.0f; }
            bigFFT.inverseOrdered (fb, timeBuf.data());
            // centre the zero phase response at L/2 and window it
            for (int k = 0; k < K; ++k)
            {
                float* sb = segBuf.data();
                for (int i = 0; i < P; ++i)
                {
                    const int n = k * P + i;
                    float v = 0.0f;
                    if (n < L)
                    {
                        const int idx = (n - L / 2 + N) % N;
                        v = timeBuf[(size_t) idx] * invN * window[(size_t) n];
                    }
                    sb[i] = v;
                }
                std::memset (sb + P, 0, sizeof (float) * (size_t) P);
                partFFT.forward (sb, out.part (j, k));
            }
        }
    }

    //==============================================================================
    void LinearPhaseConvolver::configure (int kernelLength, int partition, int numChannels)
    {
        L = kernelLength;
        P = partition;
        K = (L + P - 1) / P;
        numCh = std::max (1, std::min (2, numChannels));
        fft.init (2 * P);
        for (auto& s : sets) s.allocate (K, P);
        for (int c = 0; c < 2; ++c)
        {
            fdl[(size_t) c].allocate ((size_t) K * (size_t) (2 * P));
            inFifo[(size_t) c].allocate ((size_t) P);
            prevIn[(size_t) c].allocate ((size_t) P);
            outFifo[(size_t) c].allocate ((size_t) P);
        }
        tbuf.allocate ((size_t) (2 * P));
        acc.allocate ((size_t) (2 * P));
        ybuf.allocate ((size_t) (2 * P));
        yOld0.allocate ((size_t) P); yOld1.allocate ((size_t) P);
        yNew0.allocate ((size_t) P); yNew1.allocate ((size_t) P);
        front = 0;
        frontValid = false;
        backState.store (0);
        xfading = false;
        reset();
    }

    void LinearPhaseConvolver::reset() noexcept
    {
        for (auto& f : fdl) f.clear();
        for (auto& f : inFifo) f.clear();
        for (auto& f : prevIn) f.clear();
        for (auto& f : outFifo) f.clear();
        fdlPos = 0;
        fifoPos = 0;
    }

    LPKernelSet* LinearPhaseConvolver::beginWriteBack() noexcept
    {
        int expected = 0;
        if (! backState.compare_exchange_strong (expected, 1)) return nullptr;
        return &sets[(size_t) (1 - front)];
    }

    void LinearPhaseConvolver::endWriteBack (bool publish) noexcept
    {
        backState.store (publish ? 2 : 0);
    }

    void LinearPhaseConvolver::convolve (const LPKernelSet& ks, float* out0, float* out1) noexcept
    {
        const float scale = 1.0f / (float) (2 * P);
        const size_t stride = (size_t) (2 * P);
        for (int c = 0; c < numCh; ++c)
        {
            acc.clear();
            for (int k = 0; k < K; ++k)
            {
                const int idx = (fdlPos - k + K) % K;
                const float* X0 = fdl[0].data() + (size_t) idx * stride;
                const float* X1 = fdl[1].data() + (size_t) idx * stride;
                switch (ks.layout)
                {
                    case LPKernelSet::Single:
                        fft.convolveAccumulate (c == 0 ? X0 : X1, ks.part (0, k), acc.data(), scale);
                        break;
                    case LPKernelSet::DiagLR:
                        fft.convolveAccumulate (c == 0 ? X0 : X1, ks.part (c == 0 ? 0 : 1, k), acc.data(), scale);
                        break;
                    case LPKernelSet::Full:
                        if (numCh == 1) { fft.convolveAccumulate (X0, ks.part (0, k), acc.data(), scale); break; }
                        fft.convolveAccumulate (X0, ks.part (c == 0 ? 0 : 2, k), acc.data(), scale);
                        fft.convolveAccumulate (X1, ks.part (c == 0 ? 1 : 3, k), acc.data(), scale);
                        break;
                    default: break;
                }
            }
            fft.inverse (acc.data(), ybuf.data());
            float* dst = (c == 0) ? out0 : out1;
            std::memcpy (dst, ybuf.data() + P, sizeof (float) * (size_t) P);
        }
    }

    void LinearPhaseConvolver::processPartition() noexcept
    {
        const size_t stride = (size_t) (2 * P);
        for (int c = 0; c < numCh; ++c)
        {
            std::memcpy (tbuf.data(), prevIn[(size_t) c].data(), sizeof (float) * (size_t) P);
            std::memcpy (tbuf.data() + P, inFifo[(size_t) c].data(), sizeof (float) * (size_t) P);
            fft.forward (tbuf.data(), fdl[(size_t) c].data() + (size_t) fdlPos * stride);
            std::memcpy (prevIn[(size_t) c].data(), inFifo[(size_t) c].data(), sizeof (float) * (size_t) P);
        }

        if (! xfading && backState.load (std::memory_order_acquire) == 2) xfading = true;

        if (frontValid) convolve (sets[(size_t) front], yOld0.data(), yOld1.data());
        if (xfading)
        {
            convolve (sets[(size_t) (1 - front)], yNew0.data(), yNew1.data());
            if (frontValid)
            {
                const float inc = 1.0f / (float) P;
                for (int i = 0; i < P; ++i)
                {
                    const float w = (float) (i + 1) * inc;
                    outFifo[0][(size_t) i] = yOld0[(size_t) i] + (yNew0[(size_t) i] - yOld0[(size_t) i]) * w;
                    if (numCh > 1) outFifo[1][(size_t) i] = yOld1[(size_t) i] + (yNew1[(size_t) i] - yOld1[(size_t) i]) * w;
                }
            }
            else
            {
                std::memcpy (outFifo[0].data(), yNew0.data(), sizeof (float) * (size_t) P);
                if (numCh > 1) std::memcpy (outFifo[1].data(), yNew1.data(), sizeof (float) * (size_t) P);
            }
            front = 1 - front;
            frontValid = true;
            xfading = false;
            backState.store (0, std::memory_order_release);
        }
        else if (frontValid)
        {
            std::memcpy (outFifo[0].data(), yOld0.data(), sizeof (float) * (size_t) P);
            if (numCh > 1) std::memcpy (outFifo[1].data(), yOld1.data(), sizeof (float) * (size_t) P);
        }
        else
        {
            outFifo[0].clear();
            outFifo[1].clear();
        }
        fdlPos = (fdlPos + 1) % K;
    }

    void LinearPhaseConvolver::process (float* Lp, float* Rp, int n) noexcept
    {
        if (K == 0) return;
        for (int i = 0; i < n; ++i)
        {
            inFifo[0][(size_t) fifoPos] = Lp[i];
            Lp[i] = outFifo[0][(size_t) fifoPos];
            if (numCh > 1 && Rp != nullptr)
            {
                inFifo[1][(size_t) fifoPos] = Rp[i];
                Rp[i] = outFifo[1][(size_t) fifoPos];
            }
            if (++fifoPos == P)
            {
                processPartition();
                fifoPos = 0;
            }
        }
    }
}
