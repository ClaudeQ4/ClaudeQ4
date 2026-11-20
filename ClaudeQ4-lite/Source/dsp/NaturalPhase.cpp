/*
    Claude-Q 4 - natural phase corrector
*/
#include "NaturalPhase.h"

#if defined (__SSE__) || defined (_M_X64) || (defined (_M_IX86_FP) && _M_IX86_FP >= 1)
 #include <xmmintrin.h>
 #define CQ_DOT_SSE 1
#elif defined (__ARM_NEON) || defined (__ARM_NEON__) || defined (_M_ARM64)
 #include <arm_neon.h>
 #define CQ_DOT_NEON 1
#endif

namespace cq
{
    namespace
    {
        inline cplx svfSectionResponse (bool second, const double* c, cplx z1) noexcept
        {
            if (second)
            {
                const double g = c[0], k = c[1];
                const cplx s = (1.0 / g) * (1.0 - z1) / (1.0 + z1);
                const cplx D = s * s + k * s + 1.0;
                return c[2] + c[3] * s / D + c[4] / D;
            }
            const double G = c[0];
            const cplx lp = G * (1.0 + z1) / (1.0 - (1.0 - 2.0 * G) * z1);
            return c[2] + c[3] * lp;
        }

        inline void applyPlacement (Placement pl, cplx h, cplx& m00, cplx& m01, cplx& m10, cplx& m11) noexcept
        {
            switch (pl)
            {
                case Placement::Stereo: m00 *= h; m01 *= h; m10 *= h; m11 *= h; break;
                case Placement::Left:   m00 *= h; m01 *= h; break;
                case Placement::Right:  m10 *= h; m11 *= h; break;
                case Placement::Mid:
                case Placement::Side:
                {
                    const cplx p = 0.5 * (h + 1.0);
                    const cplx q = (pl == Placement::Mid) ? 0.5 * (h - 1.0) : 0.5 * (1.0 - h);
                    const cplx a00 = p * m00 + q * m10, a01 = p * m01 + q * m11;
                    const cplx a10 = q * m00 + p * m10, a11 = q * m01 + p * m11;
                    m00 = a00; m01 = a01; m10 = a10; m11 = a11;
                    break;
                }
                default: break;
            }
        }

        inline cplx safeDiv (cplx a, cplx b) noexcept
        {
            const double nb = std::abs (b);
            if (nb < 1.0e-5) return cplx (1.0, 0.0);
            cplx r = a / b;
            const double m = std::abs (r);
            if (m > 4.0) r *= 4.0 / m;              // never correct by more than +12 dB
            else if (m < 0.25 && std::abs (a) > 1.0e-5) r *= 0.25 / m;
            return r;
        }
    }

    //==============================================================================
    void NPKernelBuilder::configure (double sampleRate, int kernelLength)
    {
        fs = sampleRate;
        Lc = kernelLength;
        N = 8 * Lc;
        fft.init (N);
        freq.allocate ((size_t) N);
        time.allocate ((size_t) N);
        const size_t nb = (size_t) (N / 2 + 1);
        for (auto* v : { &a00, &a01, &a10, &a11, &d00, &d01, &d10, &d11 }) v->assign (nb, cplx (0.0, 0.0));
        win.resize ((size_t) Lc);
        for (int n = 0; n < Lc; ++n) win[(size_t) n] = 0.5 - 0.5 * std::cos (kTwoPi * n / Lc);
    }

    void NPKernelBuilder::build (const NPSnapshot& snap, NPKernelSet& out)
    {
        const size_t nb = (size_t) (N / 2 + 1);
        for (size_t k = 0; k < nb; ++k)
        {
            a00[k] = d00[k] = a11[k] = d11[k] = cplx (1.0, 0.0);
            a01[k] = d01[k] = a10[k] = d10[k] = cplx (0.0, 0.0);
        }
        bool anyLR = false, anyMS = false;
        for (int b = 0; b < kMaxBands; ++b)
        {
            const auto& bs = snap.bands[(size_t) b];
            if (! bs.active || bs.num <= 0) continue;
            if (bs.place == Placement::Left || bs.place == Placement::Right) anyLR = true;
            if (bs.place == Placement::Mid || bs.place == Placement::Side) anyMS = true;
            AnalogFilter af;
            makeAnalogPrototype (bs.shape, af);
            const double invF0 = 1.0 / clampT (bs.shape.freq, kMinFreq, kMaxFreq);
            const double w = clampT (snap.wet[b], 0.0, 1.0);
            for (size_t k = 0; k < nb; ++k)
            {
                const double om = kPi * (double) k / (double) (N / 2);
                const double f = (double) k * fs / N;
                cplx ha = af.eval (f * invF0);
                const cplx z1 = std::polar (1.0, -om);
                cplx hd (1.0, 0.0);
                for (int s = 0; s < bs.num; ++s)
                    hd *= svfSectionResponse (bs.second[(size_t) s], bs.coefs.data() + 5 * s, z1);
                if (w < 1.0) { ha = 1.0 + w * (ha - 1.0); hd = 1.0 + w * (hd - 1.0); }
                applyPlacement (bs.place, ha, a00[k], a01[k], a10[k], a11[k]);
                applyPlacement (bs.place, hd, d00[k], d01[k], d10[k], d11[k]);
            }
        }

        out.layout = anyMS ? 2 : (anyLR ? 1 : 0);
        out.length = Lc;
        const int numK = out.layout == 2 ? 4 : (out.layout == 1 ? 2 : 1);

        const double D = 0.5 * Lc;
        for (int j = 0; j < numK; ++j)
        {
            float* fb = freq.data();
            for (size_t k = 0; k < nb; ++k)
            {
                cplx c;
                if (out.layout == 2)
                {
                    const cplx det = d00[k] * d11[k] - d01[k] * d10[k];
                    if (std::abs (det) < 1.0e-8) c = (j == 0 || j == 3) ? cplx (1.0, 0.0) : cplx (0.0, 0.0);
                    else
                    {
                        // C = A * inv(D)
                        const cplx i00 = d11[k] / det, i01 = -d01[k] / det, i10 = -d10[k] / det, i11 = d00[k] / det;
                        switch (j)
                        {
                            case 0: c = a00[k] * i00 + a01[k] * i10; break;
                            case 1: c = a00[k] * i01 + a01[k] * i11; break;
                            case 2: c = a10[k] * i00 + a11[k] * i10; break;
                            default: c = a10[k] * i01 + a11[k] * i11; break;
                        }
                    }
                }
                else if (out.layout == 1) c = (j == 0) ? safeDiv (a00[k], d00[k]) : safeDiv (a11[k], d11[k]);
                else c = safeDiv (a00[k], d00[k]);

                // fade the phase correction out over the last few percent below Nyquist
                const double x = (double) k / (double) (N / 2);
                if (x > 0.94)
                {
                    const double t = std::min (1.0, (x - 0.94) / 0.06);
                    const double mag = std::abs (c);
                    const double ph = std::arg (c) * (1.0 - t);
                    c = std::polar (mag, ph);
                }
                // centre at D samples
                const double om = kPi * x;
                c *= std::polar (1.0, -om * D);
                if (k == 0) fb[0] = (float) c.real();
                else if (k == nb - 1) fb[1] = (float) c.real();
                else { fb[2 * k] = (float) c.real(); fb[2 * k + 1] = (float) c.imag(); }
            }
            fft.inverseOrdered (fb, time.data());
            auto& rv = out.rev[(size_t) j];
            rv.resize ((size_t) Lc);
            const double invN = 1.0 / N;
            for (int n = 0; n < Lc; ++n)
                rv[(size_t) (Lc - 1 - n)] = (float) (time[(size_t) n] * invN * win[(size_t) n]);
        }
        for (int j = numK; j < 4; ++j) out.rev[(size_t) j].assign ((size_t) Lc, 0.0f);
    }

    //==============================================================================
    void NaturalPhaseCorrector::configure (double sampleRate, int numChannels)
    {
        Lc = kernelLengthFor (sampleRate);
        numCh = std::max (1, std::min (2, numChannels));
        for (auto& s : sets)
        {
            s.layout = 0; s.length = Lc;
            for (auto& r : s.rev) r.assign ((size_t) Lc, 0.0f);
            s.rev[0][(size_t) (Lc - 1 - Lc / 2)] = 1.0f;   // pure delay until the first kernel arrives
        }
        for (auto& h : hist) h.assign ((size_t) (2 * Lc), 0.0f);
        front = 0;
        frontValid = true;
        backState.store (0);
        xfading = false;
        xfadeLen = std::max (128, Lc * 2);
        pos = 0;
    }

    void NaturalPhaseCorrector::reset() noexcept
    {
        for (auto& h : hist) std::fill (h.begin(), h.end(), 0.0f);
        pos = 0;
    }

    NPKernelSet* NaturalPhaseCorrector::beginWriteBack() noexcept
    {
        int expected = 0;
        if (! backState.compare_exchange_strong (expected, 1)) return nullptr;
        return &sets[(size_t) (1 - front)];
    }

    void NaturalPhaseCorrector::endWriteBack (bool publish) noexcept
    {
        backState.store (publish ? 2 : 0);
    }

    float NaturalPhaseCorrector::dot (const float* a, const float* b, int n) noexcept
    {
       #if CQ_DOT_SSE
        __m128 s0 = _mm_setzero_ps(), s1 = _mm_setzero_ps(), s2 = _mm_setzero_ps(), s3 = _mm_setzero_ps();
        int i = 0;
        for (; i + 16 <= n; i += 16)
        {
            s0 = _mm_add_ps (s0, _mm_mul_ps (_mm_loadu_ps (a + i),      _mm_loadu_ps (b + i)));
            s1 = _mm_add_ps (s1, _mm_mul_ps (_mm_loadu_ps (a + i + 4),  _mm_loadu_ps (b + i + 4)));
            s2 = _mm_add_ps (s2, _mm_mul_ps (_mm_loadu_ps (a + i + 8),  _mm_loadu_ps (b + i + 8)));
            s3 = _mm_add_ps (s3, _mm_mul_ps (_mm_loadu_ps (a + i + 12), _mm_loadu_ps (b + i + 12)));
        }
        __m128 s = _mm_add_ps (_mm_add_ps (s0, s1), _mm_add_ps (s2, s3));
        alignas (16) float t[4];
        _mm_store_ps (t, s);
        float r = (t[0] + t[1]) + (t[2] + t[3]);
        for (; i < n; ++i) r += a[i] * b[i];
        return r;
       #elif CQ_DOT_NEON
        float32x4_t s0 = vdupq_n_f32 (0.0f), s1 = vdupq_n_f32 (0.0f), s2 = vdupq_n_f32 (0.0f), s3 = vdupq_n_f32 (0.0f);
        int i = 0;
        for (; i + 16 <= n; i += 16)
        {
            s0 = vmlaq_f32 (s0, vld1q_f32 (a + i),      vld1q_f32 (b + i));
            s1 = vmlaq_f32 (s1, vld1q_f32 (a + i + 4),  vld1q_f32 (b + i + 4));
            s2 = vmlaq_f32 (s2, vld1q_f32 (a + i + 8),  vld1q_f32 (b + i + 8));
            s3 = vmlaq_f32 (s3, vld1q_f32 (a + i + 12), vld1q_f32 (b + i + 12));
        }
        const float32x4_t s = vaddq_f32 (vaddq_f32 (s0, s1), vaddq_f32 (s2, s3));
        float r = vgetq_lane_f32 (s, 0) + vgetq_lane_f32 (s, 1) + vgetq_lane_f32 (s, 2) + vgetq_lane_f32 (s, 3);
        for (; i < n; ++i) r += a[i] * b[i];
        return r;
       #else
        float r0 = 0, r1 = 0, r2 = 0, r3 = 0;
        int i = 0;
        for (; i + 4 <= n; i += 4) { r0 += a[i] * b[i]; r1 += a[i + 1] * b[i + 1]; r2 += a[i + 2] * b[i + 2]; r3 += a[i + 3] * b[i + 3]; }
        float r = (r0 + r1) + (r2 + r3);
        for (; i < n; ++i) r += a[i] * b[i];
        return r;
       #endif
    }

    void NaturalPhaseCorrector::process (float* Lp, float* Rp, int n) noexcept
    {
        const bool stereo = (numCh > 1 && Rp != nullptr);
        for (int i = 0; i < n; ++i)
        {
            if (! xfading && backState.load (std::memory_order_acquire) == 2) { xfading = true; xfadePos = 0; }

            hist[0][(size_t) pos] = Lp[i]; hist[0][(size_t) (pos + Lc)] = Lp[i];
            if (stereo) { hist[1][(size_t) pos] = Rp[i]; hist[1][(size_t) (pos + Lc)] = Rp[i]; }
            const float* h0 = hist[0].data() + pos + 1;
            const float* h1 = stereo ? hist[1].data() + pos + 1 : h0;

            auto run = [&] (const NPKernelSet& ks, float& y0, float& y1)
            {
                switch (ks.layout)
                {
                    case 0:
                        y0 = dot (ks.rev[0].data(), h0, Lc);
                        y1 = stereo ? dot (ks.rev[0].data(), h1, Lc) : y0;
                        break;
                    case 1:
                        y0 = dot (ks.rev[0].data(), h0, Lc);
                        y1 = stereo ? dot (ks.rev[1].data(), h1, Lc) : y0;
                        break;
                    default:
                        if (! stereo) { y0 = y1 = dot (ks.rev[0].data(), h0, Lc); break; }
                        y0 = dot (ks.rev[0].data(), h0, Lc) + dot (ks.rev[1].data(), h1, Lc);
                        y1 = dot (ks.rev[2].data(), h0, Lc) + dot (ks.rev[3].data(), h1, Lc);
                        break;
                }
            };

            float y0 = 0, y1 = 0;
            run (sets[(size_t) front], y0, y1);
            if (xfading)
            {
                float n0 = 0, n1 = 0;
                run (sets[(size_t) (1 - front)], n0, n1);
                const float w = (float) (xfadePos + 1) / (float) xfadeLen;
                y0 += (n0 - y0) * w;
                y1 += (n1 - y1) * w;
                if (++xfadePos >= xfadeLen)
                {
                    front = 1 - front;
                    xfading = false;
                    backState.store (0, std::memory_order_release);
                }
            }
            Lp[i] = y0;
            if (stereo) Rp[i] = y1;
            if (++pos >= Lc) pos = 0;
        }
    }
}
