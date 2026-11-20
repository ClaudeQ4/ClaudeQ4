/*
    Claude-Q 4 - band fitter (EQ Sketch, EQ Match)
*/
#include "BandFitter.h"
#include <cmath>
#include <algorithm>

namespace cq
{
    namespace
    {
        struct P { Shape shape; double lf, g, lq; };   // log freq, gain dB, log Q
        thread_local double maxBellQ = 10.0;
        thread_local double gainReg = 1.0e-3;          // scaled with the total weight of the target

        void bandResponse (const P& p, const std::vector<double>& freqs, std::vector<double>& out)
        {
            BandShapeParams s;
            s.shape = p.shape;
            s.slope = Slope::dB12;
            s.freq = std::exp (p.lf);
            s.gainDb = p.g;
            s.q = std::exp (p.lq);
            AnalogFilter af;
            makeAnalogPrototype (s, af);
            out.resize (freqs.size());
            for (size_t i = 0; i < freqs.size(); ++i)
                out[i] = 10.0 * std::log10 (std::max (af.magSq (freqs[i] / s.freq), 1.0e-12));
        }

        void clampParams (P& p)
        {
            p.lf = clampT (p.lf, std::log (16.0), std::log (24000.0));
            p.g = clampT (p.g, -24.0, 24.0);
            if (p.shape == Shape::Bell) p.lq = clampT (p.lq, std::log (0.12), std::log (maxBellQ));
            else                        p.lq = clampT (p.lq, std::log (0.4), std::log (1.6));
        }

        double cost (const std::vector<double>& target, const std::vector<double>& w, const std::vector<double>& model, const std::vector<P>& bands)
        {
            double c = 0.0;
            for (size_t i = 0; i < target.size(); ++i)
            {
                const double e = target[i] - model[i];
                c += w[i] * e * e;
            }
            for (const auto& b : bands)
            {
                c += gainReg * b.g * b.g;                                  // prefer small gains
                const double narrow = std::max (0.0, b.lq - std::log (4.0));
                c += 0.02 * narrow * narrow * (1.0 + 0.05 * b.g * b.g);    // and no needlessly sharp bands
            }
            return c;
        }

        // Levenberg-Marquardt over the parameters of bands[first..]
        void refine (const std::vector<double>& freqs, const std::vector<double>& target, const std::vector<double>& w,
                     std::vector<P>& bands, size_t first, int iterations)
        {
            const size_t nb = bands.size() - first;
            if (nb == 0) return;
            const size_t np = nb * 3, n = freqs.size();
            std::vector<std::vector<double>> resp (bands.size());
            for (size_t k = 0; k < bands.size(); ++k) bandResponse (bands[k], freqs, resp[k]);
            auto sumModel = [&] (std::vector<double>& m)
            {
                m.assign (n, 0.0);
                for (const auto& r : resp) for (size_t i = 0; i < n; ++i) m[i] += r[i];
            };
            std::vector<double> model;
            sumModel (model);
            double c = cost (target, w, model, bands);
            double lambda = 1.0e-2;
            std::vector<double> J (np * n), JTJ (np * np), JTr (np), delta (np), tmp;
            const double h[3] = { 0.004, 0.02, 0.006 };

            for (int it = 0; it < iterations; ++it)
            {
                // Jacobian (numerical, per band)
                for (size_t k = 0; k < nb; ++k)
                {
                    const size_t bi = first + k;
                    for (int q = 0; q < 3; ++q)
                    {
                        P pp = bands[bi];
                        if (q == 0) pp.lf += h[0]; else if (q == 1) pp.g += h[1]; else pp.lq += h[2];
                        bandResponse (pp, freqs, tmp);
                        double* col = &J[(k * 3 + (size_t) q) * n];
                        for (size_t i = 0; i < n; ++i) col[i] = (tmp[i] - resp[bi][i]) / h[q];
                    }
                }
                std::fill (JTJ.begin(), JTJ.end(), 0.0);
                std::fill (JTr.begin(), JTr.end(), 0.0);
                for (size_t a = 0; a < np; ++a)
                {
                    const double* ja = &J[a * n];
                    for (size_t i = 0; i < n; ++i) JTr[a] += w[i] * ja[i] * (target[i] - model[i]);
                    for (size_t b2 = a; b2 < np; ++b2)
                    {
                        const double* jb = &J[b2 * n];
                        double s = 0.0;
                        for (size_t i = 0; i < n; ++i) s += w[i] * ja[i] * jb[i];
                        JTJ[a * np + b2] = JTJ[b2 * np + a] = s;
                    }
                }
                // gain regularisation gradient
                for (size_t k = 0; k < nb; ++k)
                {
                    JTr[k * 3 + 1] -= gainReg * bands[first + k].g;
                    JTJ[(k * 3 + 1) * np + k * 3 + 1] += gainReg;
                }

                bool improved = false;
                for (int attempt = 0; attempt < 8 && ! improved; ++attempt)
                {
                    // solve (JTJ + lambda diag) delta = JTr  (Gaussian elimination, partial pivoting)
                    std::vector<double> A (JTJ), bvec (JTr);
                    for (size_t a = 0; a < np; ++a) A[a * np + a] += lambda * (JTJ[a * np + a] + 1.0e-6);
                    bool ok = true;
                    for (size_t col = 0; col < np && ok; ++col)
                    {
                        size_t piv = col;
                        for (size_t r = col + 1; r < np; ++r) if (std::abs (A[r * np + col]) > std::abs (A[piv * np + col])) piv = r;
                        if (std::abs (A[piv * np + col]) < 1.0e-14) { ok = false; break; }
                        if (piv != col)
                        {
                            for (size_t c2 = 0; c2 < np; ++c2) std::swap (A[col * np + c2], A[piv * np + c2]);
                            std::swap (bvec[col], bvec[piv]);
                        }
                        for (size_t r = col + 1; r < np; ++r)
                        {
                            const double f = A[r * np + col] / A[col * np + col];
                            if (f == 0.0) continue;
                            for (size_t c2 = col; c2 < np; ++c2) A[r * np + c2] -= f * A[col * np + c2];
                            bvec[r] -= f * bvec[col];
                        }
                    }
                    if (! ok) { lambda *= 10.0; continue; }
                    for (size_t r = np; r-- > 0;)
                    {
                        double s = bvec[r];
                        for (size_t c2 = r + 1; c2 < np; ++c2) s -= A[r * np + c2] * delta[c2];
                        delta[r] = s / A[r * np + r];
                    }
                    std::vector<P> trial (bands);
                    for (size_t k = 0; k < nb; ++k)
                    {
                        auto& t = trial[first + k];
                        t.lf += clampT (delta[k * 3], -0.7, 0.7);
                        t.g  += clampT (delta[k * 3 + 1], -12.0, 12.0);
                        t.lq += clampT (delta[k * 3 + 2], -0.7, 0.7);
                        clampParams (t);
                    }
                    std::vector<std::vector<double>> tresp (resp);
                    for (size_t k = 0; k < nb; ++k) bandResponse (trial[first + k], freqs, tresp[first + k]);
                    std::vector<double> tmodel (n, 0.0);
                    for (const auto& r : tresp) for (size_t i = 0; i < n; ++i) tmodel[i] += r[i];
                    const double tc = cost (target, w, tmodel, trial);
                    if (tc < c)
                    {
                        const double rel = (c - tc) / std::max (c, 1.0e-12);
                        bands = trial; resp = tresp; model = tmodel; c = tc;
                        lambda = std::max (1.0e-7, lambda * 0.3);
                        improved = true;
                        if (rel < 1.0e-5) it = iterations;   // converged
                    }
                    else lambda *= 4.0;
                }
                if (! improved) break;
            }
        }

        double maxWeightedError (const std::vector<double>& target, const std::vector<double>& w, const std::vector<double>& model)
        {
            double m = 0.0;
            for (size_t i = 0; i < target.size(); ++i) m = std::max (m, std::sqrt (w[i]) * std::abs (target[i] - model[i]));
            return m;
        }
    }

    std::vector<FittedBand> fitResponse (const std::vector<double>& freqs, const std::vector<double>& targetDb,
                                         const std::vector<double>& weights, int maxBands, double toleranceDb, bool allowShelves, double maxQ)
    {
        maxBellQ = clampT (maxQ, 0.5, 40.0);
        double wsum = 0.0;
        for (double w : weights) wsum += w;
        gainReg = 2.0e-4 * std::max (1.0, wsum);
        std::vector<FittedBand> out;
        const size_t n = freqs.size();
        if (n < 4 || targetDb.size() != n || weights.size() != n || maxBands <= 0) return out;

        std::vector<P> bands;
        std::vector<double> model (n, 0.0), resid (n), tmp;
        auto rebuildModel = [&]
        {
            std::fill (model.begin(), model.end(), 0.0);
            for (const auto& b : bands)
            {
                bandResponse (b, freqs, tmp);
                for (size_t i = 0; i < n; ++i) model[i] += tmp[i];
            }
        };
        const double octPerPoint = std::log2 (freqs.back() / freqs.front()) / (double) (n - 1);
        const int smoothHalf = std::max (1, (int) std::round (1.0 / 6.0 / octPerPoint));

        // weighted region (where the target matters most)
        size_t lo = 0, hi = n - 1;
        while (lo < n && weights[lo] < 0.99) ++lo;
        while (hi > lo && weights[hi] < 0.99) --hi;
        if (lo >= n) { lo = 0; hi = n - 1; }

        for (int iter = 0; iter < maxBands; ++iter)
        {
            for (size_t i = 0; i < n; ++i) resid[i] = targetDb[i] - model[i];
            if (maxWeightedError (targetDb, weights, model) < toleranceDb) break;

            // smoothed residual -> strongest deviation
            std::vector<double> sm (n, 0.0);
            for (size_t i = 0; i < n; ++i)
            {
                double s = 0.0; int c = 0;
                for (int k = -smoothHalf; k <= smoothHalf; ++k)
                {
                    const long j = (long) i + k;
                    if (j < 0 || j >= (long) n) continue;
                    s += resid[(size_t) j]; ++c;
                }
                sm[i] = s / std::max (1, c);
            }
            size_t best = lo;
            for (size_t i = lo; i <= hi; ++i)
                if (weights[i] * std::abs (sm[i]) > weights[best] * std::abs (sm[best])) best = i;
            const double g0 = sm[best];
            if (std::abs (g0) < toleranceDb * 0.5) break;

            // bandwidth at half gain
            size_t l = best, r = best;
            while (l > 0 && std::abs (sm[l - 1]) > std::abs (g0) * 0.5 && sm[l - 1] * g0 > 0.0) --l;
            while (r + 1 < n && std::abs (sm[r + 1]) > std::abs (g0) * 0.5 && sm[r + 1] * g0 > 0.0) ++r;
            const double bw = std::max (0.2, std::log2 (freqs[r] / freqs[l]));
            const double q0 = 1.0 / (2.0 * std::sinh (kLn2 * 0.5 * bw));

            std::vector<P> candidates;
            candidates.push_back ({ Shape::Bell, std::log (freqs[best]), g0, std::log (clampT (q0, 0.15, maxBellQ)) });
            if (allowShelves)
            {
                // a deviation that extends to the edge of the region is better served by a shelf
                if (l <= lo + 1) candidates.push_back ({ Shape::LowShelf, std::log (freqs[std::min (r, hi)]), g0, 0.0 });
                if (r + 1 >= hi) candidates.push_back ({ Shape::HighShelf, std::log (freqs[std::max (l, lo)]), g0, 0.0 });
            }

            const double before = cost (targetDb, weights, model, bands);
            std::vector<P> bestSet;
            double bestCost = before;
            for (auto cand : candidates)
            {
                clampParams (cand);
                std::vector<P> trial (bands);
                trial.push_back (cand);
                refine (freqs, targetDb, weights, trial, trial.size() - 1, 25);   // the new band alone
                refine (freqs, targetDb, weights, trial, 0, 20);                  // everything
                std::vector<double> m (n, 0.0);
                for (const auto& b : trial) { bandResponse (b, freqs, tmp); for (size_t i = 0; i < n; ++i) m[i] += tmp[i]; }
                const double c = cost (targetDb, weights, m, trial);
                if (c < bestCost) { bestCost = c; bestSet = trial; }
            }
            if (bestSet.empty() || bestCost > before * 0.97) break;
            bands = bestSet;
            rebuildModel();
        }
        if (! bands.empty()) refine (freqs, targetDb, weights, bands, 0, 40);

        for (const auto& b : bands)
        {
            if (std::abs (b.g) < 0.15) continue;
            FittedBand fb;
            fb.shape = b.shape;
            fb.freq = clampT (std::exp (b.lf), kMinFreq, kMaxFreq);
            fb.gain = clampT (b.g, kMinGain, kMaxGain);
            fb.q = clampT (std::exp (b.lq), kMinQ, kMaxQ);
            out.push_back (fb);
        }
        std::sort (out.begin(), out.end(), [] (const FittedBand& a, const FittedBand& b) { return a.freq < b.freq; });
        return out;
    }
}
