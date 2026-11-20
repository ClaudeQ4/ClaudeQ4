/*
    Claude-Q 4 - spectral dynamics (STFT)
*/
#include "Spectral.h"

namespace cq
{
    void SpectralEngine::configure (double sampleRate, int frameSize, int numChannels)
    {
        fs = sampleRate;
        N = frameSize;
        H = N / 4;
        K = N / 2 + 1;
        numCh = std::max (1, std::min (2, numChannels));
        fft.init (N);
        winA.allocate ((size_t) N); winS.allocate ((size_t) N); tmp.allocate ((size_t) N);
        double sumW = 0.0;
        for (int n = 0; n < N; ++n)
        {
            const double w = std::sqrt (0.5 - 0.5 * std::cos (kTwoPi * n / N));
            winA[(size_t) n] = (float) w;
            winS[(size_t) n] = (float) w;
            sumW += w;
        }
        // sum of winA*winS over the 4 overlapping frames = 2  ->  0.5, and pffft's inverse is unscaled (N)
        norm = (float) (0.5 / N);
        calib = (float) (2.0 / sumW);   // |X| * calib = amplitude of a sine
        for (int c = 0; c < 2; ++c)
        {
            spec[c].allocate ((size_t) N);
            scSpec[c].allocate ((size_t) N);
            inBuf[(size_t) c].assign ((size_t) N, 0.0f);
            scBuf[(size_t) c].assign ((size_t) N, 0.0f);
            outAcc[(size_t) c].assign ((size_t) N, 0.0f);
            outBuf[(size_t) c].assign ((size_t) H, 0.0f);
        }
        for (auto& bc : cache)
        {
            bc.valid = false;
            bc.staticDb.assign ((size_t) K, 0.0f);
            bc.weight.assign ((size_t) K, 0.0f);
            bc.dyn.assign ((size_t) K, 0.0f);
            bc.ref.assign ((size_t) K, -60.0f);
            bc.refInit = false;
        }
        det.assign ((size_t) K, 0.0f); lvl.assign ((size_t) K, 0.0f); smooth1.assign ((size_t) K, 0.0f);
        tilt.assign ((size_t) K, 0.0f); tiltPow.assign ((size_t) K, 1.0f); gainDbBin.assign ((size_t) K, 0.0f);
        gL.assign ((size_t) K, 1.0f); gR.assign ((size_t) K, 1.0f); gM.assign ((size_t) K, 1.0f); gS.assign ((size_t) K, 1.0f);
        binF.resize ((size_t) K);
        for (int k = 0; k < K; ++k)
        {
            binF[(size_t) k] = (double) k * fs / N;
            tilt[(size_t) k] = k == 0 ? -30.0f : (float) (3.0 * std::log2 (std::max (binF[(size_t) k], 10.0) / 1000.0));
            tiltPow[(size_t) k] = (float) std::pow (10.0, tilt[(size_t) k] / 10.0);
        }
        for (int j = 0; j < kDisplayPoints; ++j)
        {
            const double f = 20.0 * std::pow (1000.0, (double) j / (kDisplayPoints - 1));
            displayBin[(size_t) j] = clampT ((int) std::lround (f * N / fs), 0, K - 1);
        }
        for (auto& d : display) for (auto& v : d) v.store (0.0f);
        hopPos = 0;
    }

    void SpectralEngine::reset() noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            std::fill (inBuf[(size_t) c].begin(), inBuf[(size_t) c].end(), 0.0f);
            std::fill (scBuf[(size_t) c].begin(), scBuf[(size_t) c].end(), 0.0f);
            std::fill (outAcc[(size_t) c].begin(), outAcc[(size_t) c].end(), 0.0f);
            std::fill (outBuf[(size_t) c].begin(), outBuf[(size_t) c].end(), 0.0f);
        }
        for (auto& bc : cache)
        {
            std::fill (bc.dyn.begin(), bc.dyn.end(), 0.0f);
            bc.refInit = false;
        }
        hopPos = 0;
    }

    void SpectralEngine::setParams (const EngineParams& p) noexcept
    {
        params = p;
        anySpectral = false;
        for (int b = 0; b < kMaxBands; ++b)
        {
            const auto& bp = p.bands[(size_t) b];
            auto& bc = cache[(size_t) b];
            if (! bp.isSpectral()) { bc.valid = false; continue; }
            anySpectral = true;
            const auto& o = bc.p;
            const bool same = bc.valid && o.shape == bp.shape && o.slope == bp.slope && o.freq == bp.freq && o.gainDb == bp.gainDb
                           && o.q == bp.q && o.gainQ == bp.gainQ && bc.gainScale == p.global.gainScale;
            if (! same) updateBandCache (b);
        }
    }

    void SpectralEngine::updateBandCache (int b) noexcept
    {
        auto& bc = cache[(size_t) b];
        const auto& bp = params.bands[(size_t) b];
        bc.p = bp;
        bc.gainScale = params.global.gainScale;
        AnalogFilter a;
        makeAnalogPrototype (bp.shapeParams (params.global.gainScale), a);
        BandShapeParams unit = bp.shapeParams (1.0);
        unit.gainDb = 12.0;
        unit.gainQ = false;
        AnalogFilter u;
        makeAnalogPrototype (unit, u);
        const double invF0 = 1.0 / clampT (bp.freq, kMinFreq, kMaxFreq);
        double mn = 1e9, mx = -1e9;
        for (int k = 0; k < K; ++k)
        {
            const double w = binF[(size_t) k] * invF0;
            bc.staticDb[(size_t) k] = (float) (10.0 * std::log10 (std::max (a.magSq (w), 1e-30)));
            const double wd = 10.0 * std::log10 (std::max (u.magSq (w), 1e-30));
            bc.weight[(size_t) k] = (float) wd;
            mn = std::min (mn, wd); mx = std::max (mx, wd);
        }
        const bool tiltType = (bp.shape == Shape::TiltShelf || bp.shape == Shape::FlatTilt);
        for (int k = 0; k < K; ++k)
        {
            double wv = bc.weight[(size_t) k];
            wv = tiltType ? (mx > mn ? (wv - mn) / (mx - mn) : 0.0) : wv / 12.0;
            bc.weight[(size_t) k] = (float) clampT (wv, 0.0, 1.0);
        }
        bc.valid = true;
    }

    void SpectralEngine::smoothAcross (float* v, int count, double widthOct) noexcept
    {
        const double r = std::exp2 (widthOct) - 1.0;
        // forward
        double y = v[0];
        for (int k = 1; k < count; ++k)
        {
            const double width = std::max (0.35, 0.5 * k * r);
            const double a = std::exp (-1.0 / width);
            y = a * y + (1.0 - a) * v[k];
            v[k] = (float) y;
        }
        // backward
        y = v[count - 1];
        for (int k = count - 2; k >= 0; --k)
        {
            const double width = std::max (0.35, 0.5 * k * r);
            const double a = std::exp (-1.0 / width);
            y = a * y + (1.0 - a) * v[k];
            v[k] = (float) y;
        }
    }

    void SpectralEngine::process (float* Lp, float* Rp, const float* scL, const float* scR, int n) noexcept
    {
        scPresent = (scL != nullptr);
        const bool stereo = (numCh > 1 && Rp != nullptr);
        for (int i = 0; i < n; ++i)
        {
            const int idx = N - H + hopPos;
            inBuf[0][(size_t) idx] = Lp[i];
            if (stereo) inBuf[1][(size_t) idx] = Rp[i];
            if (scPresent)
            {
                scBuf[0][(size_t) idx] = scL[i];
                scBuf[1][(size_t) idx] = scR != nullptr ? scR[i] : scL[i];
            }
            Lp[i] = outBuf[0][(size_t) hopPos];
            if (stereo) Rp[i] = outBuf[1][(size_t) hopPos];
            if (++hopPos == H)
            {
                processFrame();
                hopPos = 0;
            }
        }
    }

    void SpectralEngine::processFrame() noexcept
    {
        const int chans = numCh;
        // ---- analysis
        for (int c = 0; c < chans; ++c)
        {
            const float* in = inBuf[(size_t) c].data();
            for (int k = 0; k < N; ++k) tmp[(size_t) k] = in[k] * winA[(size_t) k];
            fft.forwardOrdered (tmp.data(), spec[c].data());
        }
        bool needSc = false;
        for (int b = 0; b < kMaxBands; ++b)
            if (params.bands[(size_t) b].isSpectral() && params.bands[(size_t) b].extSidechain) needSc = true;
        if (needSc && scPresent)
        {
            for (int c = 0; c < 2; ++c)
            {
                const float* in = scBuf[(size_t) c].data();
                for (int k = 0; k < N; ++k) tmp[(size_t) k] = in[k] * winA[(size_t) k];
                fft.forwardOrdered (tmp.data(), scSpec[c].data());
            }
        }

        auto re = [] (const float* s, int k, int Kk) -> float { return k == 0 ? s[0] : (k == Kk - 1 ? s[1] : s[2 * k]); };
        auto im = [] (const float* s, int k, int Kk) -> float { return (k == 0 || k == Kk - 1) ? 0.0f : s[2 * k + 1]; };

        // keep a copy of the input spectra for detection (bands are applied in place);
        // scSpec doubles as that storage when the side chain is not needed
        const float* inSpec0 = spec[0].data();
        const float* inSpec1 = chans > 1 ? spec[1].data() : spec[0].data();
        AlignedFloats* detSrc = spec;
        if (! (needSc && scPresent))
        {
            std::memcpy (scSpec[0].data(), inSpec0, sizeof (float) * (size_t) N);
            std::memcpy (scSpec[1].data(), inSpec1, sizeof (float) * (size_t) N);
            detSrc = scSpec;
        }

        const double dt = (double) H / fs;
        const float cal2 = calib * calib;
        for (int b = 0; b < kMaxBands; ++b)
        {
            const auto& bp = params.bands[(size_t) b];
            auto& bc = cache[(size_t) b];
            if (! bp.isSpectral()) continue;
            if (! bc.valid) updateBandCache (b);

            const float* s0 = (bp.extSidechain && needSc && scPresent) ? scSpec[0].data() : detSrc[0].data();
            const float* s1 = (bp.extSidechain && needSc && scPresent) ? scSpec[1].data() : detSrc[1].data();

            for (int k = 0; k < K; ++k)
            {
                const float r0 = re (s0, k, K), i0 = im (s0, k, K);
                const float r1 = re (s1, k, K), i1 = im (s1, k, K);
                float p;
                switch (bp.place)
                {
                    case Placement::Left:  p = r0 * r0 + i0 * i0; break;
                    case Placement::Right: p = r1 * r1 + i1 * i1; break;
                    case Placement::Mid:   { const float a = 0.5f * (r0 + r1), c2 = 0.5f * (i0 + i1); p = a * a + c2 * c2; break; }
                    case Placement::Side:  { const float a = 0.5f * (r0 - r1), c2 = 0.5f * (i0 - i1); p = a * a + c2 * c2; break; }
                    default:               p = std::max (r0 * r0 + i0 * i0, r1 * r1 + i1 * i1); break;
                }
                float pw = p * cal2;
                if (bp.spectralTilt) pw *= tiltPow[(size_t) k];
                lvl[(size_t) k] = pw;
            }
            // density: energy preserving smoothing across frequency (power domain)
            const double density = clampT (bp.density, 0.0, 1.0);
            smoothAcross (lvl.data(), K, 0.03 + (1.0 - density) * 1.2);
            for (int k = 0; k < K; ++k) lvl[(size_t) k] = 10.0f * std::log10 (lvl[(size_t) k] + 1.0e-24f);

            float* thrArr = det.data();
            if (bp.autoThreshold)
            {
                // spectral envelope: log domain smoothing ignores narrow peaks (resonances)
                std::memcpy (thrArr, lvl.data(), sizeof (float) * (size_t) K);
                smoothAcross (thrArr, K, 1.0);
                const double aRef = bc.refInit ? (1.0 - std::exp (-dt / 0.6)) : 1.0;
                for (int k = 0; k < K; ++k)
                {
                    float& rf = bc.ref[(size_t) k];
                    rf += (float) ((thrArr[k] - rf) * aRef);
                    thrArr[k] = rf + 6.0f;
                }
                bc.refInit = true;
            }
            else
            {
                for (int k = 0; k < K; ++k) thrArr[k] = (float) bp.thresholdDb;
            }

            const double range = bp.dynRange * params.global.gainScale;
            const double ar = std::max (std::abs (range), 1.0e-6);
            const bool autoT = bp.autoTimes;
            const double tauAm = clampT (bp.attackMs, 0.05, 1000.0) * 1.0e-3, tauRm = clampT (bp.releaseMs, 1.0, 5000.0) * 1.0e-3;

            float* gdb = gainDbBin.data();
            for (int k = 0; k < K; ++k)
            {
                const float wgt = bc.weight[(size_t) k];
                float& dv = bc.dyn[(size_t) k];
                if (wgt > 1.0e-3f)
                {
                    const double over = (double) lvl[(size_t) k] - thrArr[k];
                    double ok;
                    if (over <= -3.0) ok = 0.0;
                    else if (over >= 3.0) ok = over;
                    else ok = (over + 3.0) * (over + 3.0) / 12.0;
                    const double target = (range < 0.0 ? -ar : ar) * softUnit (ok * (2.0 / 3.0) / ar);
                    const double f = std::max (binF[(size_t) k], 20.0);
                    const bool attacking = std::abs (target) > std::abs ((double) dv);
                    const double tau = autoT ? (attacking ? clampT (2.0 / f, 0.002, 0.02) : clampT (25.0 / f, 0.04, 0.3))
                                             : std::max (attacking ? tauAm : tauRm, 0.5 * dt);
                    dv = (float) (target + ((double) dv - target) * std::exp (-dt / tau));
                }
                else dv = 0.0f;
                gdb[k] = bc.staticDb[(size_t) k] + dv * wgt;
            }
            for (int j = 0; j < kDisplayPoints; ++j)
            {
                const int k = displayBin[(size_t) j];
                display[(size_t) b][(size_t) j].store (bc.dyn[(size_t) k] * bc.weight[(size_t) k], std::memory_order_relaxed);
            }

            // apply the band's gain curve to the output spectra according to its placement
            float* o0 = spec[0].data();
            float* o1 = chans > 1 ? spec[1].data() : spec[0].data();
            for (int k = 0; k < K; ++k)
            {
                const float g = std::pow (10.0f, gdb[k] * 0.05f);
                const int ir = k == 0 ? 0 : (k == K - 1 ? 1 : 2 * k);
                const int ii = (k == 0 || k == K - 1) ? -1 : 2 * k + 1;
                auto mulBoth = [&] (float* s, float gg) { s[ir] *= gg; if (ii >= 0) s[ii] *= gg; };
                switch (chans > 1 ? bp.place : Placement::Stereo)
                {
                    case Placement::Left:  mulBoth (o0, g); break;
                    case Placement::Right: mulBoth (o1, g); break;
                    case Placement::Mid:
                    case Placement::Side:
                    {
                        float mr = 0.5f * (o0[ir] + o1[ir]), sr = 0.5f * (o0[ir] - o1[ir]);
                        float mi = 0.0f, si = 0.0f;
                        if (ii >= 0) { mi = 0.5f * (o0[ii] + o1[ii]); si = 0.5f * (o0[ii] - o1[ii]); }
                        if (bp.place == Placement::Mid) { mr *= g; mi *= g; } else { sr *= g; si *= g; }
                        o0[ir] = mr + sr; o1[ir] = mr - sr;
                        if (ii >= 0) { o0[ii] = mi + si; o1[ii] = mi - si; }
                        break;
                    }
                    default:
                        mulBoth (o0, g);
                        if (chans > 1) mulBoth (o1, g);
                        break;
                }
            }
        }

        // ---- synthesis (weighted overlap-add)
        for (int c = 0; c < chans; ++c)
        {
            fft.inverseOrdered (spec[c].data(), tmp.data());
            auto& acc = outAcc[(size_t) c];
            for (int k = 0; k < N; ++k) acc[(size_t) k] += tmp[(size_t) k] * winS[(size_t) k] * norm;
            std::memcpy (outBuf[(size_t) c].data(), acc.data(), sizeof (float) * (size_t) H);
            std::memmove (acc.data(), acc.data() + H, sizeof (float) * (size_t) (N - H));
            std::fill (acc.begin() + (N - H), acc.end(), 0.0f);
            auto& ib = inBuf[(size_t) c];
            std::memmove (ib.data(), ib.data() + H, sizeof (float) * (size_t) (N - H));
        }
        if (scPresent)
            for (int c = 0; c < 2; ++c)
            {
                auto& sb = scBuf[(size_t) c];
                std::memmove (sb.data(), sb.data() + H, sizeof (float) * (size_t) (N - H));
            }
    }
}
