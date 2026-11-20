/*
    Claude-Q 4 - per band dynamics
*/
#include "Dynamics.h"

namespace cq
{
    void BandDynamics::prepare (double sampleRate, int controlInterval) noexcept
    {
        fs = sampleRate;
        ctrl = controlInterval;
        lastMode1 = lastMode2 = -1;
        reset();
    }

    void BandDynamics::reset() noexcept
    {
        f1.reset(); f2.reset(); a1f.reset(); a2f.reset();
        power[0] = 0.0;
        levelDb = prevLevelDb = -120.0;
        avgDb = -60.0;
        avgInit = false;
        gainDb = 0.0;
        sustain = 0.0;
        riseRate = 0.0;
    }

    void BandDynamics::setParams (const BandParams& p, double rangeScale) noexcept
    {
        params = p;
        range = p.dynRange * rangeScale;
        active = p.isDynamic() && std::abs (range) > 1.0e-3;
        updateFilters();
    }

    void BandDynamics::updateFilters() noexcept
    {
        int m1 = 0, m2 = 0;
        double fr1 = 1000.0, fr2 = 1000.0, q1 = 0.7071;
        if (params.triggerFree)
        {
            m1 = 2; fr1 = clampT (params.triggerLo, 10.0, 30000.0);
            m2 = 1; fr2 = clampT (params.triggerHi, 10.0, 30000.0);
            if (fr2 < fr1) std::swap (fr1, fr2);
            detFreq = std::sqrt (fr1 * fr2);
        }
        else
        {
            switch (params.shape)
            {
                case Shape::Bell:
                    m1 = 3; fr1 = params.freq; q1 = clampT (params.q, 0.3, 40.0);
                    detFreq = params.freq;
                    break;
                case Shape::LowShelf:
                    m1 = 1; fr1 = params.freq;
                    detFreq = std::max (20.0, params.freq * 0.5);
                    break;
                case Shape::HighShelf:
                case Shape::TiltShelf:
                    m1 = 2; fr1 = params.freq;
                    detFreq = std::min (16000.0, params.freq * 2.0);
                    break;
                default:
                    detFreq = 1000.0;
                    break;
            }
        }
        const double nyqLimit = 0.45 * fs;
        if (m1 != lastMode1 || fr1 != lastF1 || q1 != lastQ1)
        {
            f1.set (m1, std::min (fr1, nyqLimit), q1, fs);
            a1f.set (m1, std::min (fr1, nyqLimit), q1, fs);
            lastMode1 = m1; lastF1 = fr1; lastQ1 = q1;
        }
        if (m2 != lastMode2 || fr2 != lastF2)
        {
            f2.set (m2, std::min (fr2, nyqLimit), 0.7071, fs);
            a2f.set (m2, std::min (fr2, nyqLimit), 0.7071, fs);
            lastMode2 = m2; lastF2 = fr2;
        }
        const double tauRms = clampT (2.0 / detFreq, 0.0003, 0.02);
        rmsCoef = std::exp (-1.0 / (tauRms * fs));
    }

    double BandDynamics::processBlock (const double* a, const double* b, int n) noexcept
    {
        if (n <= 0) return gainDb;
        double p = power[0];
        const double c = rmsCoef, ic = 1.0 - rmsCoef;
        if (b == nullptr)
        {
            for (int i = 0; i < n; ++i)
            {
                const double y = f2.tick (f1.tick (a[i], 0), 0);
                p = p * c + ic * (y * y);
            }
        }
        else
        {
            for (int i = 0; i < n; ++i)
            {
                const double ya = f2.tick (f1.tick (a[i], 0), 0);
                const double yb = f2.tick (f1.tick (b[i], 1), 1);
                p = p * c + ic * std::max (ya * ya, yb * yb);
            }
        }
        if (! (p > 1.0e-30)) p = (p > 0.0 && std::isfinite (p)) ? p : 0.0;
        power[0] = p;

        const double dt = (double) n / fs;
        prevLevelDb = levelDb;
        levelDb = 10.0 * std::log10 (p + 1.0e-24) + 3.0103;   // a sine reads its peak level

        // automatic threshold: follows the long term level of the trigger signal
        if (levelDb > -70.0)
        {
            if (! avgInit) { avgDb = levelDb; avgInit = true; }
            else avgDb += (levelDb - avgDb) * (1.0 - std::exp (-dt / 2.5));
        }
        thresholdDb = params.autoThreshold ? avgDb + 4.0 : params.thresholdDb;

        double target = 0.0;
        if (active)
        {
            const double over = levelDb - thresholdDb;
            const double W = 6.0;
            double ok;
            if (over <= -0.5 * W) ok = 0.0;
            else if (over >= 0.5 * W) ok = over;
            else ok = (over + 0.5 * W) * (over + 0.5 * W) / (2.0 * W);
            const double amount = ok * (2.0 / 3.0);
            const double r = std::abs (range);
            target = (range < 0.0 ? -r : r) * softUnit (amount / r);
        }

        // program dependent ballistics
        const double rise = (levelDb - prevLevelDb) / (dt * 1000.0);   // dB per ms
        double tauA, tauR;
        if (params.autoTimes)
        {
            tauA = clampT (1.6 / detFreq, 0.0005, 0.02) / (1.0 + clampT (rise / 0.8, 0.0, 3.0));
            tauR = clampT (12.0 / detFreq, 0.025, 0.25) * (1.0 + 3.0 * sustain);
        }
        else
        {
            tauA = clampT (params.attackMs, 0.05, 1000.0) * 1.0e-3;
            tauR = clampT (params.releaseMs, 1.0, 5000.0) * 1.0e-3;
        }
        const bool attacking = std::abs (target) > std::abs (gainDb) && (target * gainDb >= 0.0 || gainDb == 0.0);
        const double tau = attacking ? tauA : tauR;
        const double coef = std::exp (-dt / std::max (tau, 1.0e-5));
        gainDb = target + (gainDb - target) * coef;
        if (std::abs (gainDb) < 1.0e-6 && target == 0.0) gainDb = 0.0;
        sustain += ((std::abs (target) > 0.5 ? 1.0 : 0.0) - sustain) * (1.0 - std::exp (-dt / 0.4));
        return gainDb;
    }

    void BandDynamics::auditionBlock (const double* a, const double* b, double* out, int n) noexcept
    {
        (void) b;
        for (int i = 0; i < n; ++i) out[i] = a2f.tick (a1f.tick (a[i], 0), 0);
    }
}
