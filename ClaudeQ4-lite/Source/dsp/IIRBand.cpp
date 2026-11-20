/*
    Claude-Q 4 - zero latency band processor
*/
#include "IIRBand.h"

namespace cq
{
    namespace
    {
        inline bool sameCoef (const SvfRunCoef& a, const SvfRunCoef& b) noexcept
        {
            return a.g == b.g && a.k == b.k && a.m0 == b.m0 && a.m1 == b.m1 && a.m2 == b.m2;
        }
        inline SvfRunCoef lerpCoef (const SvfRunCoef& a, const SvfRunCoef& b, double t) noexcept
        {
            SvfRunCoef r;
            r.g  = a.g  + (b.g  - a.g)  * t;
            r.k  = a.k  + (b.k  - a.k)  * t;
            r.m0 = a.m0 + (b.m0 - a.m0) * t;
            r.m1 = a.m1 + (b.m1 - a.m1) * t;
            r.m2 = a.m2 + (b.m2 - a.m2) * t;
            return r;
        }
    }

    SvfRunCoef IIRBand::toRun (const SvfCoeffs& c, double gainFold) noexcept
    {
        SvfRunCoef r;
        r.g = c.g; r.k = c.k;
        r.m0 = c.m0 * gainFold; r.m1 = c.m1 * gainFold; r.m2 = c.m2 * gainFold;
        return r;
    }

    void IIRBand::prepare (double sampleRate, int controlInterval) noexcept
    {
        fs = sampleRate;
        ctrl = clampT (controlInterval, 8, kMaxBlock);
        const double tau = 0.020;
        smoothAlpha = 1.0 - std::exp (-(double) ctrl / (tau * fs));
        morphBlocksTotal = std::max (1, (int) std::lround (0.025 * fs / ctrl));
        xfadeTotal = std::max (64, (int) std::lround (0.020 * fs));
        reset();
    }

    void IIRBand::reset() noexcept
    {
        for (auto& s : slots) { s.clearState(); for (auto& a : s.accurate) a = false; }
        xfadeRemaining = 0;
        morphRemaining = 0;
        pendingStructure = false;
        cacheValid = false;
        for (auto& c : cache) c.tag = -100000;
        sLogF = tLogF; sGain = tGain; sLogQ = tLogQ;
        wet = wetTarget;
        idle = (wet <= 0.0);
        designDirty = true;
        if (! idle) initSlot (slots[(size_t) active], target);
    }

    BandShapeParams IIRBand::currentShape (double gainDb) const noexcept
    {
        BandShapeParams b;
        const auto& s = slots[(size_t) active];
        b.shape = s.shape;
        b.slope = s.slope;
        b.freq = std::exp (sLogF);
        b.q = std::exp (sLogQ);
        b.gainDb = shapeUsesGain (s.shape) ? gainDb : 0.0;
        b.gainQ = target.gainQ;
        return b;
    }

    void IIRBand::initSlot (Slot& s, const BandParams& p) noexcept
    {
        s.shape = p.shape;
        s.slope = sanitiseSlope (p.shape, p.slope);
        s.place = mono ? Placement::Stereo : p.place;
        BandShapeParams b;
        b.shape = s.shape; b.slope = s.slope; b.freq = std::exp (sLogF); b.q = std::exp (sLogQ);
        b.gainDb = shapeUsesGain (s.shape) ? sGain : 0.0; b.gainQ = p.gainQ;
        AnalogFilter a;
        makeAnalogPrototype (b, a);
        s.num = std::min (digitalSectionCount (a), kMaxSections);
        for (int i = 0; i < s.num; ++i)
        {
            s.second[(size_t) i] = digitalSectionIsSecond (a, i);
            DigitalSection d;
            designSection (a, i, b.freq, fs, false, d);
            s.cur[(size_t) i] = s.next[(size_t) i] = s.tgt[(size_t) i] = toRun (toSvf (d), i == 0 ? a.gain : 1.0);
            s.accurate[(size_t) i] = false;
            s.moving[(size_t) i] = false;
        }
        s.clearState();
    }

    void IIRBand::setTarget (const BandParams& p, double scale, bool isMono) noexcept
    {
        const BandParams old = target;
        target = p;
        mono = isMono;
        if (mono) target.place = Placement::Stereo;
        gainScale = scale;

        tLogF = std::log (clampT (p.freq, kMinFreq, kMaxFreq));
        tGain = shapeUsesGain (p.shape) ? clampT (p.gainDb * scale, -66.0, 66.0) : 0.0;
        tLogQ = std::log (clampT (p.q, kMinQ, kMaxQ));

        const bool isActive = target.isActive();
        wetTarget = isActive ? 1.0 : 0.0;

        if (isActive && idle)
        {
            // start from silence: snap parameters, fresh structure, fade in
            sLogF = tLogF; sGain = tGain; sLogQ = tLogQ;
            xfadeRemaining = 0; morphRemaining = 0; pendingStructure = false;
            initSlot (slots[(size_t) active], target);
            designDirty = true;
            cacheValid = false;
            wet = 0.0;
            idle = false;
            return;
        }
        if (! isActive) return;

        Slot& s = slots[(size_t) active];
        const Slope sl = sanitiseSlope (target.shape, target.slope);
        const Placement pl = mono ? Placement::Stereo : target.place;
        const bool structural = (s.shape != target.shape || s.slope != sl || s.place != pl);
        if (structural || old.gainQ != target.gainQ)
        {
            // does the topology change?
            BandShapeParams b = currentShape (sGain);
            b.shape = target.shape; b.slope = sl;
            AnalogFilter a; makeAnalogPrototype (b, a);
            const int n = std::min (digitalSectionCount (a), kMaxSections);
            bool sameTopology = (n == s.num) && (pl == s.place);
            for (int i = 0; sameTopology && i < n; ++i)
                if (digitalSectionIsSecond (a, i) != s.second[(size_t) i]) sameTopology = false;

            if (sameTopology)
            {
                s.shape = target.shape;
                s.slope = sl;
                morphRemaining = morphBlocksTotal;
                designDirty = true;
                cacheValid = false;
                for (auto& acc : s.accurate) acc = false;
            }
            else if (xfadeRemaining > 0)
            {
                pendingStructure = true;
            }
            else
            {
                // cross-fade to a fresh filter with the new structure
                const int other = 1 - active;
                const int prevActive = active;
                active = other;
                initSlot (slots[(size_t) active], target);
                // freeze the old slot on its current coefficients
                auto& o = slots[(size_t) prevActive];
                for (int i = 0; i < o.num; ++i) { o.next[(size_t) i] = o.tgt[(size_t) i] = o.cur[(size_t) i]; o.moving[(size_t) i] = false; }
                xfadeRemaining = xfadeTotal;
                designDirty = true;
                cacheValid = false;
                morphRemaining = 0;
            }
        }
    }

    bool IIRBand::isSettled() const noexcept
    {
        return ! smoothing && morphRemaining == 0 && ! designDirty && xfadeRemaining == 0 && wet == wetTarget;
    }

    void IIRBand::computeCell (int idx, CacheCell& cell) noexcept
    {
        const auto& s = slots[(size_t) active];
        AnalogFilter a;
        makeAnalogPrototype (currentShape ((double) idx), a);
        for (int i = 0; i < s.num && i < kMaxCacheSections; ++i)
        {
            DigitalSection d;
            designSection (a, i, std::exp (sLogF), fs, true, d);
            cell.c[(size_t) i] = toRun (toSvf (d), i == 0 ? a.gain : 1.0);
        }
        cell.tag = idx;
    }

    bool IIRBand::cacheLookup (double gainDb, int& budget, std::array<SvfRunCoef, kMaxSections>& out) noexcept
    {
        const auto& s = slots[(size_t) active];
        if (s.num > kMaxCacheSections) return false;
        if (! cacheValid || cacheShape != s.shape || cacheSlope != s.slope || cacheLogF != sLogF
            || cacheLogQ != sLogQ || cacheGainQ != target.gainQ)
        {
            for (auto& c : cache) c.tag = -100000;
            cacheShape = s.shape; cacheSlope = s.slope; cacheLogF = sLogF; cacheLogQ = sLogQ; cacheGainQ = target.gainQ;
            cacheValid = true;
        }
        const double g = clampT (gainDb, -66.0, 66.0);
        const int i0 = (int) std::floor (g);
        const double fr = g - i0;

        auto getCell = [&] (int idx) -> const CacheCell*
        {
            CacheCell& c = cache[(size_t) (((idx % kCacheCells) + kCacheCells) % kCacheCells)];
            if (c.tag == idx) return &c;
            if (budget <= 0) return nullptr;
            computeCell (idx, c);
            budget -= s.num;
            return &c;
        };
        const CacheCell* c0 = getCell (i0);
        if (c0 == nullptr) return false;
        if (fr < 1.0e-9)
        {
            for (int i = 0; i < s.num; ++i) out[(size_t) i] = c0->c[(size_t) i];
            return true;
        }
        const CacheCell* c1 = getCell (i0 + 1);
        if (c1 == nullptr || c0->tag != i0) return false;   // (c0 may have been evicted by c1)
        for (int i = 0; i < s.num; ++i) out[(size_t) i] = lerpCoef (c0->c[(size_t) i], c1->c[(size_t) i], fr);
        return true;
    }

    void IIRBand::control (int n, double dynGainDb, int& budget) noexcept
    {
        if (idle) return;
        (void) n;

        // ---- parameter smoothing (log frequency, dB gain, log Q)
        bool moved = false;
        auto smooth = [&] (double& sv, double tv, double tol)
        {
            const double d = tv - sv;
            if (d == 0.0) return;
            if (std::abs (d) <= tol) sv = tv;
            else { sv += d * smoothAlpha; if (std::abs (tv - sv) <= tol) sv = tv; }
            moved = true;
        };
        smooth (sLogF, tLogF, 2.0e-5);
        smooth (sGain, tGain, 2.0e-4);
        smooth (sLogQ, tLogQ, 2.0e-5);
        smoothing = moved;

        Slot& s = slots[(size_t) active];
        const bool dyn = target.isDynamic();
        const double effGain = sGain + (dyn ? dynGainDb : 0.0);

        std::array<SvfRunCoef, kMaxSections> tgt;
        bool haveTarget = false;

        if (moved)
        {
            AnalogFilter a; makeAnalogPrototype (currentShape (effGain), a);
            const double f0 = std::exp (sLogF);
            for (int i = 0; i < s.num; ++i)
            {
                DigitalSection d;
                designSection (a, i, f0, fs, false, d);
                tgt[(size_t) i] = toRun (toSvf (d), i == 0 ? a.gain : 1.0);
                s.accurate[(size_t) i] = false;
            }
            haveTarget = true;
            designDirty = true;
            cacheValid = false;
        }
        else if (dyn)
        {
            if (! cacheLookup (effGain, budget, tgt))
            {
                AnalogFilter a; makeAnalogPrototype (currentShape (effGain), a);
                const double f0 = std::exp (sLogF);
                for (int i = 0; i < s.num; ++i)
                {
                    DigitalSection d;
                    designSection (a, i, f0, fs, false, d);
                    tgt[(size_t) i] = toRun (toSvf (d), i == 0 ? a.gain : 1.0);
                }
            }
            haveTarget = true;
            designDirty = true;   // when dynamics end, return to the exact static design
            for (auto& acc : s.accurate) acc = false;
        }
        else if (designDirty)
        {
            AnalogFilter a; makeAnalogPrototype (currentShape (effGain), a);
            const double f0 = std::exp (sLogF);
            bool allAccurate = true;
            bool upgraded = false;
            for (int i = 0; i < s.num; ++i)
            {
                if (s.accurate[(size_t) i]) { tgt[(size_t) i] = s.tgt[(size_t) i]; continue; }
                DigitalSection d;
                if (budget > 0)
                {
                    designSection (a, i, f0, fs, true, d);
                    --budget;
                    s.accurate[(size_t) i] = true;
                    upgraded = true;
                }
                else
                {
                    designSection (a, i, f0, fs, false, d);
                    allAccurate = false;
                }
                tgt[(size_t) i] = toRun (toSvf (d), i == 0 ? a.gain : 1.0);
            }
            haveTarget = true;
            designDirty = ! allAccurate;
            if (upgraded && morphRemaining == 0) morphRemaining = 4;   // glide from the fast to the exact design
        }

        // ---- next coefficients (with optional coefficient-domain morph)
        if (haveTarget)
            for (int i = 0; i < s.num; ++i) s.tgt[(size_t) i] = tgt[(size_t) i];
        const double t = morphRemaining > 0 ? 1.0 / morphRemaining : 1.0;
        for (int i = 0; i < s.num; ++i)
        {
            const SvfRunCoef& c = s.cur[(size_t) i];
            const SvfRunCoef nx = (t < 1.0) ? lerpCoef (c, s.tgt[(size_t) i], t) : s.tgt[(size_t) i];
            s.moving[(size_t) i] = ! sameCoef (nx, c);
            s.next[(size_t) i] = nx;
        }
        if (morphRemaining > 0) --morphRemaining;
        lastWasDynamic = dyn;
        lastEffGain = effGain;
    }

    void IIRBand::runSection2 (const SvfRunCoef& a, const SvfRunCoef& b, bool interp, std::array<double, 2>& st, double* x, int n) noexcept
    {
        double ic1 = st[0], ic2 = st[1];
        if (! interp)
        {
            const double g = a.g, k = a.k;
            const double a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
            const double m0 = a.m0, m1 = a.m1, m2 = a.m2;
            for (int i = 0; i < n; ++i)
            {
                const double v0 = x[i];
                const double v3 = v0 - ic2;
                const double v1 = a1 * ic1 + a2 * v3;
                const double v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = 2.0 * v1 - ic1;
                ic2 = 2.0 * v2 - ic2;
                x[i] = m0 * v0 + m1 * v1 + m2 * v2;
            }
        }
        else
        {
            const double inv = 1.0 / n;
            double g = a.g, k = a.k, m0 = a.m0, m1 = a.m1, m2 = a.m2;
            const double dg = (b.g - a.g) * inv, dk = (b.k - a.k) * inv;
            const double d0 = (b.m0 - a.m0) * inv, d1 = (b.m1 - a.m1) * inv, d2 = (b.m2 - a.m2) * inv;
            for (int i = 0; i < n; ++i)
            {
                g += dg; k += dk; m0 += d0; m1 += d1; m2 += d2;
                const double a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
                const double v0 = x[i];
                const double v3 = v0 - ic2;
                const double v1 = a1 * ic1 + a2 * v3;
                const double v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = 2.0 * v1 - ic1;
                ic2 = 2.0 * v2 - ic2;
                x[i] = m0 * v0 + m1 * v1 + m2 * v2;
            }
        }
        st[0] = ic1; st[1] = ic2;
    }

    void IIRBand::runSection1 (const SvfRunCoef& a, const SvfRunCoef& b, bool interp, std::array<double, 2>& st, double* x, int n) noexcept
    {
        double s = st[0];
        if (! interp)
        {
            const double G = a.g, m0 = a.m0, m1 = a.m1;
            for (int i = 0; i < n; ++i)
            {
                const double v0 = x[i];
                const double v = (v0 - s) * G;
                const double lp = v + s;
                s = lp + v;
                x[i] = m0 * v0 + m1 * lp;
            }
        }
        else
        {
            const double inv = 1.0 / n;
            double G = a.g, m0 = a.m0, m1 = a.m1;
            const double dG = (b.g - a.g) * inv, d0 = (b.m0 - a.m0) * inv, d1 = (b.m1 - a.m1) * inv;
            for (int i = 0; i < n; ++i)
            {
                G += dG; m0 += d0; m1 += d1;
                const double v0 = x[i];
                const double v = (v0 - s) * G;
                const double lp = v + s;
                s = lp + v;
                x[i] = m0 * v0 + m1 * lp;
            }
        }
        st[0] = s;
    }

    void IIRBand::runSlot (Slot& s, double* L, double* R, int n) noexcept
    {
        auto runChannel = [&] (std::array<std::array<double, 2>, kMaxSections>& st, double* x)
        {
            for (int i = 0; i < s.num; ++i)
            {
                const auto& a = s.cur[(size_t) i];
                const auto& b = s.next[(size_t) i];
                if (s.second[(size_t) i]) runSection2 (a, b, s.moving[(size_t) i], st[(size_t) i], x, n);
                else                      runSection1 (a, b, s.moving[(size_t) i], st[(size_t) i], x, n);
            }
        };

        Placement pl = s.place;
        if (R == nullptr) pl = Placement::Stereo;
        switch (pl)
        {
            case Placement::Stereo:
                runChannel (s.st0, L);
                if (R != nullptr) runChannel (s.st1, R);
                break;
            case Placement::Left:  runChannel (s.st0, L); break;
            case Placement::Right: runChannel (s.st1, R); break;
            case Placement::Mid:
            case Placement::Side:
            {
                double mid[kMaxBlock], side[kMaxBlock];
                for (int i = 0; i < n; ++i) { mid[i] = 0.5 * (L[i] + R[i]); side[i] = 0.5 * (L[i] - R[i]); }
                if (pl == Placement::Mid) runChannel (s.st0, mid);
                else                      runChannel (s.st0, side);
                for (int i = 0; i < n; ++i) { L[i] = mid[i] + side[i]; R[i] = mid[i] - side[i]; }
                break;
            }
            default: break;
        }
    }

    void IIRBand::process (double* L, double* R, int n) noexcept
    {
        if (idle || n <= 0) return;
        n = std::min (n, kMaxBlock);

        const bool wetRamping = (wet != wetTarget);
        const bool needDry = wetRamping || wet < 1.0;
        if (needDry)
        {
            std::memcpy (dry[0], L, sizeof (double) * (size_t) n);
            if (R != nullptr) std::memcpy (dry[1], R, sizeof (double) * (size_t) n);
        }

        if (xfadeRemaining > 0)
        {
            Slot& sNew = slots[(size_t) active];
            Slot& sOld = slots[(size_t) (1 - active)];
            std::memcpy (tmpA[0], L, sizeof (double) * (size_t) n);
            std::memcpy (tmpB[0], L, sizeof (double) * (size_t) n);
            if (R != nullptr)
            {
                std::memcpy (tmpA[1], R, sizeof (double) * (size_t) n);
                std::memcpy (tmpB[1], R, sizeof (double) * (size_t) n);
            }
            runSlot (sOld, tmpA[0], R != nullptr ? tmpA[1] : nullptr, n);
            runSlot (sNew, tmpB[0], R != nullptr ? tmpB[1] : nullptr, n);
            const double step = 1.0 / xfadeTotal;
            double w = 1.0 - xfadeRemaining * step;
            for (int i = 0; i < n; ++i)
            {
                w = std::min (1.0, w + step);
                // equal power-ish (sin^2) fade, both paths are correlated so use a linear sum of weights = 1
                L[i] = tmpA[0][i] + (tmpB[0][i] - tmpA[0][i]) * w;
                if (R != nullptr) R[i] = tmpA[1][i] + (tmpB[1][i] - tmpA[1][i]) * w;
            }
            xfadeRemaining = std::max (0, xfadeRemaining - n);
            for (int i = 0; i < sNew.num; ++i) sNew.cur[(size_t) i] = sNew.next[(size_t) i];
            if (xfadeRemaining == 0)
            {
                sOld.clearState();
                if (pendingStructure)
                {
                    pendingStructure = false;
                    setTarget (target, gainScale, mono);
                }
            }
        }
        else
        {
            Slot& s = slots[(size_t) active];
            runSlot (s, L, R, n);
            for (int i = 0; i < s.num; ++i) s.cur[(size_t) i] = s.next[(size_t) i];
        }

        if (needDry)
        {
            const double rampStep = 1.0 / std::max (1.0, 0.02 * fs);
            double w = wet;
            for (int i = 0; i < n; ++i)
            {
                if (w < wetTarget)      w = std::min (wetTarget, w + rampStep);
                else if (w > wetTarget) w = std::max (wetTarget, w - rampStep);
                L[i] = dry[0][i] + (L[i] - dry[0][i]) * w;
                if (R != nullptr) R[i] = dry[1][i] + (R[i] - dry[1][i]) * w;
            }
            wet = w;
            if (wet <= 0.0 && wetTarget <= 0.0)
            {
                idle = true;
                for (auto& sl : slots) sl.clearState();
                xfadeRemaining = 0;
                pendingStructure = false;
            }
        }
    }
}
