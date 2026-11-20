/*
    Claude-Q 4 - filter design implementation
*/
#include "FilterDesign.h"
#include <mutex>
#ifdef CQ_DEBUG_FIT
 #include <cstdio>
 #define CQ_FITDBG(...) std::fprintf (stderr, __VA_ARGS__)
#else
 #define CQ_FITDBG(...)
#endif

namespace cq
{
    //==============================================================================
    const char* shapeName (Shape s) noexcept
    {
        switch (s)
        {
            case Shape::Bell:      return "Bell";
            case Shape::LowShelf:  return "Low Shelf";
            case Shape::LowCut:    return "Low Cut";
            case Shape::HighShelf: return "High Shelf";
            case Shape::HighCut:   return "High Cut";
            case Shape::Notch:     return "Notch";
            case Shape::BandPass:  return "Band Pass";
            case Shape::TiltShelf: return "Tilt Shelf";
            case Shape::FlatTilt:  return "Flat Tilt";
            case Shape::AllPass:   return "All Pass";
            default: break;
        }
        return "";
    }

    const char* slopeName (Slope s) noexcept
    {
        static const char* names[] = { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "30 dB/oct",
                                       "36 dB/oct", "48 dB/oct", "72 dB/oct", "96 dB/oct", "Brickwall" };
        const int i = clampT ((int) s, 0, (int) Slope::Count - 1);
        return names[i];
    }

    const char* placementName (Placement p) noexcept
    {
        static const char* names[] = { "Stereo", "Left", "Right", "Mid", "Side" };
        return names[clampT ((int) p, 0, 4)];
    }

    const char* placementShortName (Placement p) noexcept
    {
        static const char* names[] = { "ST", "L", "R", "M", "S" };
        return names[clampT ((int) p, 0, 4)];
    }

    int slopeOrder (Slope s) noexcept
    {
        static const int orders[] = { 1, 2, 3, 4, 5, 6, 8, 12, 16, 0 };
        return orders[clampT ((int) s, 0, (int) Slope::Count - 1)];
    }

    bool shapeUsesGain (Shape s) noexcept
    {
        return s == Shape::Bell || s == Shape::LowShelf || s == Shape::HighShelf
            || s == Shape::TiltShelf || s == Shape::FlatTilt;
    }

    bool shapeSupportsDynamics (Shape s) noexcept { return shapeUsesGain (s); }

    bool shapeUsesSlope (Shape s) noexcept { return s != Shape::FlatTilt; }

    bool slopeValidForShape (Shape s, Slope slope) noexcept
    {
        switch (s)
        {
            case Shape::Bell:
            case Shape::Notch:
            case Shape::BandPass:
                return slope == Slope::dB12 || slope == Slope::dB24 || slope == Slope::dB36
                    || slope == Slope::dB48 || slope == Slope::dB72 || slope == Slope::dB96;
            case Shape::LowCut:
            case Shape::HighCut:
                return true;
            case Shape::FlatTilt:
                return true;
            default:
                return slope != Slope::Brickwall;
        }
    }

    Slope sanitiseSlope (Shape s, Slope slope) noexcept
    {
        if (slopeValidForShape (s, slope)) return slope;
        switch (slope)
        {
            case Slope::dB6:       return Slope::dB12;
            case Slope::dB18:      return Slope::dB24;
            case Slope::dB30:      return Slope::dB36;
            case Slope::Brickwall: return Slope::dB96;
            default: break;
        }
        return Slope::dB12;
    }

    bool shapeUsesQ (Shape s, Slope slope) noexcept
    {
        if (s == Shape::FlatTilt) return false;
        slope = sanitiseSlope (s, slope);
        if (slope == Slope::Brickwall) return false;
        if (slopeOrder (slope) == 1) return false;
        return true;
    }

    double gainQInteractionQ (double q, double gainDb) noexcept
    {
        // analog style "proportional Q": wider at small gains, narrower at large gains
        const double f = std::pow (2.0, (std::abs (gainDb) - 6.0) / 12.0);
        return clampT (q * f, kMinQ, kMaxQ);
    }

    double gainQInteractionGain (double qEff, double gainDb) noexcept
    {
        // a little extra gain when the band gets very narrow
        const double extra = 0.08 * std::max (0.0, std::log2 (qEff / 3.0));
        return gainDb * (1.0 + extra);
    }

    //==============================================================================
    namespace
    {
        inline double butterZeta (int k, int n) noexcept   // k = 1..n/2
        {
            return std::sin ((2.0 * k - 1.0) * kPi / (2.0 * n));
        }

        AnalogSection makeSecond (double n2, double n1, double n0, double d1, double d0, AnalogSection::Kind kind) noexcept
        {
            AnalogSection s;
            s.second = true; s.n2 = n2; s.n1 = n1; s.n0 = n0; s.d1 = d1; s.d0 = d0; s.kind = kind;
            return s;
        }

        AnalogSection makeFirst (double n1, double n0, double d0, AnalogSection::Kind kind) noexcept
        {
            AnalogSection s;
            s.second = false; s.n2 = 0; s.n1 = n1; s.n0 = n0; s.d1 = 0; s.d0 = d0; s.kind = kind;
            return s;
        }

        // quadratic with complex coefficient c:  s^2 - c s + 1 = 0  ->  roots r1 (|r1|>=1), r2
        void quadRoots (cplx c, cplx& r1, cplx& r2) noexcept
        {
            const cplx disc = std::sqrt (c * c - 4.0);
            r1 = 0.5 * (c + disc);
            r2 = 0.5 * (c - disc);
            if (std::abs (r1) < std::abs (r2)) std::swap (r1, r2);
        }

        // Butterworth low pass, order n, Q applied to least damped section
        void buildButter (int n, double q, bool highpass, AnalogFilter& f) noexcept
        {
            for (int k = 1; k <= n / 2; ++k)
            {
                double zeta = butterZeta (k, n);
                if (k == 1) zeta /= q;
                if (highpass) f.add (makeSecond (1, 0, 0, 2 * zeta, 1, AnalogSection::Highpass));
                else          f.add (makeSecond (0, 0, 1, 2 * zeta, 1, AnalogSection::Lowpass));
            }
            if (n & 1)
            {
                if (highpass) f.add (makeFirst (1, 0, 1, AnalogSection::Highpass));
                else          f.add (makeFirst (0, 1, 1, AnalogSection::Lowpass));
            }
        }

        // Butterworth style shelf of order n with linear gain G (low frequencies get G)
        void buildShelf (int n, double G, double q, bool high, AnalogFilter& f) noexcept
        {
            const double a = std::pow (G, 1.0 / (2.0 * n));
            const double b = 1.0 / a;
            for (int k = 1; k <= n / 2; ++k)
            {
                double zeta = butterZeta (k, n);
                if (k == 1) zeta /= q;
                if (! high)
                {
                    f.add (makeSecond (1, 2 * zeta * a, a * a, 2 * zeta * b, b * b, AnalogSection::General));
                }
                else
                {
                    // H(1/s):  (1 + 2 z a s + a^2 s^2) / (1 + 2 z b s + b^2 s^2)  -> monic
                    const double ib2 = 1.0 / (b * b);
                    f.add (makeSecond (a * a * ib2, 2 * zeta * a * ib2, ib2, 2 * zeta / b, ib2, AnalogSection::General));
                }
            }
            if (n & 1)
            {
                if (! high) f.add (makeFirst (1, a, b, AnalogSection::General));
                else        f.add (makeFirst (a / b, 1.0 / b, 1.0 / b, AnalogSection::General));
            }
        }

        // Peaking filter of order 2m: band pass transform of an order m low shelf
        void buildBell (int m, double G, double B, AnalogFilter& f) noexcept
        {
            const double a = std::pow (G, 1.0 / (2.0 * m));
            const double b = 1.0 / a;
            for (int k = 1; k <= m / 2; ++k)
            {
                const double zeta = butterZeta (k, m);
                const double im = std::sqrt (std::max (0.0, 1.0 - zeta * zeta));
                const cplx rz = a * cplx (-zeta, im);   // prototype zero
                const cplx rp = b * cplx (-zeta, im);   // prototype pole
                cplx z1, z2, p1, p2;
                quadRoots (rz * B, z1, z2);
                quadRoots (rp * B, p1, p2);
                // pair upper with upper, lower with lower
                f.add (makeSecond (1, -2 * z1.real(), std::norm (z1), -2 * p1.real(), std::norm (p1), AnalogSection::General));
                f.add (makeSecond (1, -2 * z2.real(), std::norm (z2), -2 * p2.real(), std::norm (p2), AnalogSection::General));
            }
            if (m & 1)
                f.add (makeSecond (1, a * B, 1, b * B, 1, AnalogSection::General));
        }

        // Band stop of order 2m: transform of an order m Butterworth low pass
        void buildNotch (int m, double B, AnalogFilter& f) noexcept
        {
            for (int k = 1; k <= m / 2; ++k)
            {
                const double zeta = butterZeta (k, m);
                const double im = std::sqrt (std::max (0.0, 1.0 - zeta * zeta));
                const cplx p (-zeta, im);
                cplx r1, r2;
                quadRoots (B / p, r1, r2);
                f.add (makeSecond (1, 0, 1, -2 * r1.real(), std::norm (r1), AnalogSection::Notch));
                f.add (makeSecond (1, 0, 1, -2 * r2.real(), std::norm (r2), AnalogSection::Notch));
            }
            if (m & 1)
                f.add (makeSecond (1, 0, 1, B, 1, AnalogSection::Notch));
        }

        // Band pass of order 2m: transform of an order m Butterworth low pass
        void buildBandPass (int m, double B, AnalogFilter& f) noexcept
        {
            for (int k = 1; k <= m / 2; ++k)
            {
                const double zeta = butterZeta (k, m);
                const double im = std::sqrt (std::max (0.0, 1.0 - zeta * zeta));
                const cplx p (-zeta, im);
                cplx r1, r2;
                quadRoots (p * B, r1, r2);
                f.add (makeSecond (0, B, 0, -2 * r1.real(), std::norm (r1), AnalogSection::Bandpass));
                f.add (makeSecond (0, B, 0, -2 * r2.real(), std::norm (r2), AnalogSection::Bandpass));
            }
            if (m & 1)
                f.add (makeSecond (0, B, 0, B, 1, AnalogSection::Bandpass));
        }

        void buildAllPass (int n, double q, AnalogFilter& f) noexcept
        {
            for (int k = 1; k <= n / 2; ++k)
            {
                double zeta = butterZeta (k, n);
                if (k == 1) zeta /= q;
                f.add (makeSecond (1, -2 * zeta, 1, 2 * zeta, 1, AnalogSection::Allpass));
            }
            if (n & 1)
                f.add (makeFirst (-1, 1, 1, AnalogSection::Allpass));
        }

        // constant dB/oct tilt between 5 Hz and 40 kHz, pivot at f0
        void buildFlatTilt (double gainDb, double f0, AnalogFilter& f) noexcept
        {
            const double slopePerOct = gainDb / 5.0;
            const double fLo = 5.0, fHi = 40000.0;
            const double octaves = std::log2 (fHi / fLo);
            const int nSec = 14;
            const double delta = octaves / nSec;
            const double step = slopePerOct * delta;            // dB per section
            const double rho = std::pow (10.0, step / 40.0);
            for (int i = 0; i < nSec; ++i)
            {
                const double c = fLo * std::pow (2.0, (i + 0.5) * delta) / f0;   // normalised centre
                const double wz = c / rho, wp = c * rho;
                // (1 + s/wz) / (1 + s/wp) = (wp/wz) (s + wz) / (s + wp)
                f.add (makeFirst (wp / wz, wp, wp, AnalogSection::General));
            }
            f.gain = 1.0;
            const double m = std::sqrt (f.magSq (1.0));
            f.gain = m > 0 ? 1.0 / m : 1.0;
        }

        // Second order sections with real poles and real zeros are split into two first
        // order sections: each first order section can then be matched exactly at DC, at its
        // corner and at Nyquist, which is far more accurate than one wide biquad fit.
        void splitOverdamped (AnalogFilter& f) noexcept
        {
            AnalogFilter out;
            out.gain = f.gain; out.bilinear = f.bilinear; out.bilinearHighpass = f.bilinearHighpass;
            for (int i = 0; i < f.num; ++i)
            {
                const auto& s = f.sec[(size_t) i];
                const double disc = s.d1 * s.d1 - 4.0 * s.d0;
                bool done = false;
                if (s.second && disc > 0.0 && s.kind != AnalogSection::Allpass && s.kind != AnalogSection::Notch
                    && out.num + 2 <= kMaxSections && (f.num - i - 1) + out.num + 2 <= kMaxSections)
                {
                    const double sq = std::sqrt (disc);
                    double pa = 0.5 * (-s.d1 + sq), pb = 0.5 * (-s.d1 - sq);   // pa: smaller magnitude
                    const double A = -pa, B = -pb;                               // corner frequencies, A <= B
                    if (A > 0.0 && B > 0.0)
                    {
                        if (s.n2 == 0.0 && s.n1 == 0.0)            // low pass: n0 / ((s+A)(s+B))
                        {
                            const double g = std::sqrt (s.n0 / s.d0);
                            out.add (makeFirst (0.0, g * A, A, AnalogSection::Lowpass));
                            out.sec[(size_t) out.num - 1].pairWithNext = true;
                            out.add (makeFirst (0.0, g * B, B, AnalogSection::Lowpass));
                            done = true;
                        }
                        else if (s.n1 == 0.0 && s.n0 == 0.0)       // high pass: n2 s^2 / ((s+A)(s+B))
                        {
                            const double g = std::sqrt (s.n2);
                            out.add (makeFirst (g, 0.0, A, AnalogSection::Highpass));
                            out.sec[(size_t) out.num - 1].pairWithNext = true;
                            out.add (makeFirst (g, 0.0, B, AnalogSection::Highpass));
                            done = true;
                        }
                        else if (s.n2 == 0.0 && s.n0 == 0.0)       // band pass: n1 s / ((s+A)(s+B))
                        {
                            // rising part up to A, falling part above B
                            out.add (makeFirst (1.0, 0.0, A, AnalogSection::Highpass));
                            out.sec[(size_t) out.num - 1].pairWithNext = true;
                            out.add (makeFirst (0.0, s.n1, B, AnalogSection::Lowpass));
                            done = true;
                        }
                        else if (s.n2 > 0.0)
                        {
                            const double dn = s.n1 * s.n1 - 4.0 * s.n0 * s.n2;
                            if (dn >= 0.0 && s.n0 >= 0.0 && s.n1 >= 0.0)
                            {
                                const double sn = std::sqrt (dn);
                                const double za = (s.n1 - sn) / (2.0 * s.n2);   // zero corners (positive)
                                const double zb = (s.n1 + sn) / (2.0 * s.n2);
                                const double g = std::sqrt (s.n2);
                                // (s + za)(s + zb) n2 / ((s + A)(s + B))
                                out.add (makeFirst (g, g * za, A, AnalogSection::General));
                                out.sec[(size_t) out.num - 1].pairWithNext = true;
                                out.add (makeFirst (g, g * zb, B, AnalogSection::General));
                                done = true;
                            }
                        }
                    }
                }
                if (! done) out.add (s);
            }
            out.identity = f.identity;
            f = out;
        }

        // s -> 1/s for every section (low pass -> high pass)
        void invertSections (AnalogFilter& f) noexcept
        {
            for (int i = 0; i < f.num; ++i)
            {
                auto& s = f.sec[(size_t) i];
                if (s.second)
                {
                    const double n2 = s.n0 / s.d0, n1 = s.n1 / s.d0, n0 = s.n2 / s.d0;
                    const double d1 = s.d1 / s.d0, d0 = 1.0 / s.d0;
                    s.n2 = n2; s.n1 = n1; s.n0 = n0; s.d1 = d1; s.d0 = d0;
                    if (s.kind == AnalogSection::Lowpass) s.kind = AnalogSection::Highpass;
                }
                else
                {
                    const double n1 = s.n0 / s.d0, n0 = s.n1 / s.d0, d0 = 1.0 / s.d0;
                    s.n1 = n1; s.n0 = n0; s.d0 = d0;
                    if (s.kind == AnalogSection::Lowpass) s.kind = AnalogSection::Highpass;
                }
            }
        }

        //==========================================================================
        // Elliptic filter design (S. J. Orfanidis, "Lecture notes on elliptic filter design")
        constexpr int kLandenMax = 12;

        int landen (double k, double* v) noexcept
        {
            int n = 0;
            if (k <= 0.0 || k >= 1.0) return 0;
            while (k > 1.0e-17 && n < kLandenMax)
            {
                k = k / (1.0 + std::sqrt (1.0 - k * k));
                k = k * k;
                v[n++] = k;
            }
            return n;
        }

        cplx cde (cplx u, double k) noexcept
        {
            double v[kLandenMax]; const int n = landen (k, v);
            cplx w = std::cos (u * (kPi / 2.0));
            for (int i = n - 1; i >= 0; --i) w = (1.0 + v[i]) * w / (1.0 + v[i] * w * w);
            return w;
        }

        cplx sne (cplx u, double k) noexcept
        {
            double v[kLandenMax]; const int n = landen (k, v);
            cplx w = std::sin (u * (kPi / 2.0));
            for (int i = n - 1; i >= 0; --i) w = (1.0 + v[i]) * w / (1.0 + v[i] * w * w);
            return w;
        }

        cplx asne (cplx w, double k) noexcept
        {
            double v[kLandenMax]; const int n = landen (k, v);
            for (int i = 0; i < n; ++i)
            {
                const double v1 = (i == 0) ? k : v[i - 1];
                w = w / (1.0 + std::sqrt (1.0 - w * w * (v1 * v1))) * (2.0 / (1.0 + v[i]));
            }
            return std::asin (w) * (2.0 / kPi);
        }

        double ellipdeg (int N, double k1) noexcept
        {
            const int L = N / 2;
            const double k1p = std::sqrt (1.0 - k1 * k1);
            double prod = 1.0;
            for (int i = 1; i <= L; ++i)
            {
                const double ui = (2.0 * i - 1.0) / N;
                prod *= std::real (sne (cplx (ui, 0.0), k1p));
            }
            const double kp = std::pow (k1p, (double) N) * std::pow (prod, 4.0);
            return std::sqrt (1.0 - kp * kp);
        }

        EllipticPrototype designElliptic (int N, double Ap, double As) noexcept
        {
            EllipticPrototype e;
            e.order = N;
            const double ep = std::sqrt (std::pow (10.0, Ap / 10.0) - 1.0);
            const double es = std::sqrt (std::pow (10.0, As / 10.0) - 1.0);
            const double k1 = ep / es;
            const double k = ellipdeg (N, k1);
            const int L = N / 2;
            const cplx j (0.0, 1.0);
            const cplx v0 = -j * asne (j / ep, k1) / (double) N;
            for (int i = 1; i <= L; ++i)
            {
                const double ui = (2.0 * i - 1.0) / N;
                const cplx zeta = cde (cplx (ui, 0.0), k);
                e.zeros[(size_t) (i - 1)] = j / (k * zeta);
                e.poles[(size_t) (i - 1)] = j * cde (cplx (ui, 0.0) - j * v0, k);
            }
            e.numPairs = L;
            e.hasRealPole = (N & 1) != 0;
            if (e.hasRealPole)
                e.realPole = std::real (j * sne (j * v0, k));
            e.h0 = (N & 1) ? 1.0 : 1.0 / std::sqrt (1.0 + ep * ep);
            return e;
        }

        void buildBrickwall (bool highpass, AnalogFilter& f) noexcept
        {
            const auto& e = getBrickwallPrototype();
            f.bilinear = true;
            for (int i = 0; i < e.numPairs; ++i)
            {
                const cplx z = e.zeros[(size_t) i];
                cplx p = e.poles[(size_t) i];
                if (p.real() > 0) p = -std::conj (p);
                const double wz2 = std::norm (z);
                const double pp = std::norm (p);
                // (s^2 + wz^2)/wz^2  /  (s^2 - 2Re(p) s + |p|^2)/|p|^2
                f.add (makeSecond (pp / wz2, 0.0, pp, -2.0 * p.real(), pp, AnalogSection::Notch));
            }
            if (e.hasRealPole)
            {
                const double pr = -std::abs (e.realPole);
                f.add (makeFirst (0.0, -pr, -pr, AnalogSection::Lowpass));
            }
            f.gain = e.h0;
            f.bilinearHighpass = highpass;
            if (highpass) invertSections (f);
        }
    }

    const EllipticPrototype& getBrickwallPrototype() noexcept
    {
        // 10th order, 0.02 dB ripple, 110 dB stop band: transition band ~ 1/9 octave
        static const EllipticPrototype proto = designElliptic (10, 0.02, 110.0);
        return proto;
    }

    //==============================================================================
    void makeAnalogPrototype (const BandShapeParams& p, AnalogFilter& f) noexcept
    {
        f.clear();
        const Slope slope = sanitiseSlope (p.shape, p.slope);
        const int n = slopeOrder (slope);
        double q = clampT (p.q, kMinQ, kMaxQ);
        double gDb = clampT (p.gainDb, -72.0, 72.0);

        switch (p.shape)
        {
            case Shape::Bell:
            {
                if (p.gainQ)
                {
                    q = gainQInteractionQ (q, gDb);
                    gDb = gainQInteractionGain (q, gDb);
                }
                buildBell (std::max (1, n / 2), dbToGain (gDb), 1.0 / q, f);
                break;
            }
            case Shape::LowShelf:
            case Shape::HighShelf:
            case Shape::TiltShelf:
            {
                const double G = dbToGain (gDb);
                buildShelf (std::max (1, n), G, q, p.shape != Shape::LowShelf, f);
                if (p.shape == Shape::TiltShelf) f.gain = 1.0 / std::sqrt (G);
                break;
            }
            case Shape::LowCut:
            case Shape::HighCut:
            {
                if (slope == Slope::Brickwall) buildBrickwall (p.shape == Shape::LowCut, f);
                else                           buildButter (std::max (1, n), q, p.shape == Shape::LowCut, f);
                break;
            }
            case Shape::Notch:     buildNotch (std::max (1, n / 2), 1.0 / q, f); break;
            case Shape::BandPass:  buildBandPass (std::max (1, n / 2), 1.0 / q, f); break;
            case Shape::AllPass:   buildAllPass (std::max (1, n), q, f); break;
            case Shape::FlatTilt:
            {
                buildFlatTilt (gDb, clampT (p.freq, kMinFreq, kMaxFreq), f);
                break;
            }
            default: break;
        }
        if (! f.bilinear) splitOverdamped (f);
        f.identity = (f.num == 0 && std::abs (f.gain - 1.0) < 1.0e-12);
    }

    //==============================================================================
    namespace
    {
        inline double xOf (double w) noexcept { const double s = std::sin (0.5 * w); return s * s; }

        // Frequency remap used as the design target of the zero latency filters: identity
        // below kWarpStart, then a smooth compression so that the target response has zero
        // slope at Nyquist (every digital response must).  The digital filter therefore
        // equals the analog prototype exactly up to kWarpStart and approaches it smoothly above.
        constexpr double kWarpStart = 0.76 * kPi;
        inline double warpW (double w) noexcept
        {
            if (w <= kWarpStart) return w;
            const double u = std::min (1.0, (w - kWarpStart) / (kPi - kWarpStart));
            return kWarpStart + (kPi - kWarpStart) * (u - 0.5 * u * u);
        }
        // inverse of warpW; returns a value > pi if the frequency lies beyond the warped range
        inline double unwarpW (double w) noexcept
        {
            if (w <= kWarpStart) return w;
            const double r = (w - kWarpStart) / (kPi - kWarpStart);
            if (r >= 0.5) return 2.0 * kPi;
            return kWarpStart + (kPi - kWarpStart) * (1.0 - std::sqrt (1.0 - 2.0 * r));
        }
        inline double targetMagSq (const AnalogSection& s, double w, double w0) noexcept
        {
            return s.magSq (warpW (w) / w0);
        }

        constexpr double kCompStart = 0.70 * kPi;   // natural frequencies above this get compressed
        constexpr double kCompMax   = 0.985 * kPi;
        constexpr double kMatchMax  = 0.82 * kPi;   // highest frequency used for the centre match

        inline double compressFreq (double x) noexcept
        {
            if (x <= kCompStart) return x;
            const double span = kCompMax - kCompStart;
            return kCompStart + span * std::tanh ((x - kCompStart) / span);
        }

        struct PoleInfo { double a1, a2, pdc, pny, om_a2; };

        PoleInfo polesSecond (const AnalogSection& s, double w0) noexcept
        {
            PoleInfo p {};
            const double disc = s.d1 * s.d1 - 4.0 * s.d0;
            if (disc < 0.0)
            {
                double sigma = -0.5 * s.d1;
                double wd = 0.5 * std::sqrt (-disc);
                const double xn = std::sqrt (s.d0) * w0;
                double scale = w0;
                if (xn > kCompStart) scale = w0 * compressFreq (xn) / xn;
                const double lnr = sigma * scale;   // log radius (negative)
                const double ang = wd * scale;
                const double r = std::exp (lnr);
                p.a1 = -2.0 * r * std::cos (ang);
                p.a2 = r * r;
                // stable forms:  1 + a1 + a2 = (1-r)^2 + 4 r sin^2(ang/2)
                const double om_r = -std::expm1 (lnr);
                const double sh = std::sin (0.5 * ang), ch = std::cos (0.5 * ang);
                p.pdc = om_r * om_r + 4.0 * r * sh * sh;
                p.pny = (1.0 + r) * (1.0 + r) - 4.0 * r * sh * sh;   // 1 - a1 + a2 = (1+r)^2 - 4 r sin^2
                (void) ch;
                p.om_a2 = -std::expm1 (2.0 * lnr);
            }
            else
            {
                const double sq = std::sqrt (disc);
                double p1 = 0.5 * (-s.d1 + sq), p2 = 0.5 * (-s.d1 - sq);
                // compress very high real poles too (keeps numbers sane)
                const double e1 = std::exp (p1 * w0), e2 = std::exp (p2 * w0);
                p.a1 = -(e1 + e2);
                p.a2 = e1 * e2;
                const double om1 = -std::expm1 (p1 * w0), om2 = -std::expm1 (p2 * w0);
                p.pdc = om1 * om2;                       // (1 - e1)(1 - e2)
                p.pny = (1.0 + e1) * (1.0 + e2);
                p.om_a2 = -std::expm1 ((p1 + p2) * w0);
            }
            return p;
        }

        inline void phis (double w, double& phi0, double& phi1, double& phi2) noexcept
        {
            const double sn = std::sin (0.5 * w), cs = std::cos (0.5 * w);
            phi0 = cs * cs; phi1 = sn * sn; phi2 = 4.0 * phi0 * phi1;
        }

        bool matchSection (const AnalogSection& s, double w0, DigitalSection& d, bool invertedPass = false) noexcept
        {
            const double wNyq = kPi / w0;   (void) wNyq;

            if (! s.second)
            {
                const double pl = s.d0 * w0;
                const double zp = std::exp (-pl);
                d.second = false;
                d.a1 = -zp; d.a2 = 0.0; d.b2 = 0.0;
                d.pdc = -std::expm1 (-pl);    // 1 + a1
                d.pny = 1.0 + zp;             // 1 - a1
                if (s.kind == AnalogSection::Allpass)
                {
                    d.b0 = d.a1; d.b1 = 1.0;
                    d.hdc = 1.0; d.hny = -1.0;
                    return true;
                }
                const double h0 = std::sqrt (s.magSq (0.0));
                const double h1 = std::sqrt (targetMagSq (s, kPi, w0));
                const double S0 = h0 * d.pdc, S1 = h1 * d.pny;
                d.b0 = 0.5 * (S0 + S1);
                d.b1 = 0.5 * (S0 - S1);
                d.hdc = h0; d.hny = h1;
                return true;
            }

            d.second = true;
            const PoleInfo pi = polesSecond (s, w0);
            d.a1 = pi.a1; d.a2 = pi.a2; d.pdc = pi.pdc; d.pny = pi.pny; d.omA2 = pi.om_a2;

            const double A0 = pi.pdc * pi.pdc, A1 = pi.pny * pi.pny, A2 = -4.0 * pi.a2;
            const double wn = std::sqrt (s.d0) * w0;
            const double wm = clampT (wn, 1.0e-7, kMatchMax);
            double phi0, phi1, phi2;
            phis (wm, phi0, phi1, phi2);
            const double Am = A0 * phi0 + A1 * phi1 + A2 * phi2;
            const double Tm = targetMagSq (s, wm, w0);

            switch (s.kind)
            {
                case AnalogSection::Allpass:
                {
                    d.b0 = d.a2; d.b1 = d.a1; d.b2 = 1.0;
                    d.hdc = 1.0; d.hny = 1.0; d.bdiff = d.a2 - 1.0;
                    return true;
                }
                case AnalogSection::Highpass:
                {
                    const double c = std::sqrt (std::max (0.0, Tm * Am)) / (4.0 * phi1);
                    d.b0 = c; d.b1 = -2.0 * c; d.b2 = c;
                    d.hdc = 0.0; d.hny = 4.0 * c / pi.pny; d.bdiff = 0.0;
                    return true;
                }
                case AnalogSection::Lowpass:
                {
                    const double T0 = s.magSq (0.0);
                    const double B0 = T0 * A0;
                    double B1 = (Tm * Am - B0 * phi0) / phi1;
                    if (B1 < 0.0) B1 = 0.0;
                    const double W0 = std::sqrt (B0), W1 = std::sqrt (B1);
                    d.b0 = 0.5 * (W0 + W1);
                    d.b1 = W0 - d.b0;
                    d.b2 = 0.0;
                    d.hdc = W0 / pi.pdc; d.hny = W1 / pi.pny; d.bdiff = d.b0;
                    return true;
                }
                case AnalogSection::Notch:
                {
                    const double wz = std::sqrt (s.n0 / s.n2) * w0;
                    if (wz < 0.999 * kPi)
                    {
                        const double T0 = s.magSq (0.0), T1 = targetMagSq (s, kPi, w0);
                        const double cz = std::cos (wz);
                        const double sz2 = 2.0 * std::sin (0.5 * wz) * std::sin (0.5 * wz) * 2.0; // 2 - 2cos
                        (void) T1;
                        const double c = std::sqrt (T0 * A0) / sz2;   // exact DC gain, exact notch frequency
                        d.b0 = c; d.b1 = -2.0 * c * cz; d.b2 = c;
                        d.hdc = c * sz2 / pi.pdc; d.hny = c * (4.0 - sz2) / pi.pny; d.bdiff = 0.0;
                        return true;
                    }
                    break; // zero above Nyquist -> general match
                }
                default: break;
            }

            // Sections that cut (|H| mostly < 1) are designed as the inverse (a boost) and
            // inverted afterwards: boosts are always realisable with matched poles, and this
            // keeps boost/cut symmetry exact.
            if (s.kind == AnalogSection::General && s.n2 > 0.0 && ! invertedPass)
            {
                const double tc = s.magSq (std::sqrt (s.d0));
                const double score = std::log (std::max (s.magSq (0.0), 1e-300)) + std::log (std::max (s.n2 * s.n2, 1e-300))
                                   + 2.0 * std::log (std::max (tc, 1e-300));
                if (score < 0.0)
                {
                    AnalogSection inv;
                    inv.second = true; inv.kind = AnalogSection::General;
                    inv.n2 = 1.0 / s.n2; inv.n1 = s.d1 / s.n2; inv.n0 = s.d0 / s.n2;
                    inv.d1 = s.n1 / s.n2; inv.d0 = s.n0 / s.n2;
                    DigitalSection di;
                    if (matchSection (inv, w0, di, true) && di.bdiff > 1.0e-12 * di.b0)
                    {
                    const double ib0 = 1.0 / di.b0;
                    d.second = true;
                    d.b0 = ib0; d.b1 = di.a1 * ib0; d.b2 = di.a2 * ib0;
                    d.a1 = di.b1 * ib0; d.a2 = di.b2 * ib0;
                    d.pdc = (di.hdc * di.pdc) * ib0;     // (b0+b1+b2)_inv / b0_inv
                    d.pny = (di.hny * di.pny) * ib0;
                    d.omA2 = di.bdiff * ib0;
                    d.hdc = 1.0 / di.hdc; d.hny = 1.0 / di.hny;
                    d.bdiff = di.omA2 * ib0;
                    return true;
                    }
                }
            }

            // general three point match (DC, Nyquist, wm)
            const double T0 = s.magSq (0.0), T1 = targetMagSq (s, kPi, w0);
            const double B0 = T0 * A0, B1 = T1 * A1;
            const double B2 = (Tm * Am - B0 * phi0 - B1 * phi1) / phi2;
            const double W0 = std::sqrt (B0), W1 = std::sqrt (B1);
            const double W = 0.5 * (W0 + W1);
            double disc = W * W + B2;
            bool exact = true;
            if (disc < 0.0) { disc = 0.0; exact = false; }
            const double sd = std::sqrt (disc);
            d.b0 = 0.5 * (W + sd);
            d.b2 = W - d.b0;
            d.b1 = 0.5 * (W0 - W1);
            d.hdc = W0 / pi.pdc;
            d.hny = W1 / pi.pny;
            d.bdiff = sd;   // b0 - b2
            return exact;
        }

        //==========================================================================
        // Rational magnitude interpolation.
        //
        // The squared magnitude of a real biquad is a ratio of quadratics in
        // x = sin^2(w/2):  |H|^2 = P(x)/Q(x).  We pick P and Q (in a scaled monomial
        // basis) so that |H|^2 equals the analog |H|^2 exactly at DC, Nyquist and a few
        // interior frequencies around the section's features, keeping structural zeros
        // (high pass, band pass, notch) exact.  Q and P are then spectrally factored into
        // a stable, minimum phase biquad.  Unlike impulse invariance this never aliases,
        // and unlike the bilinear transform it never cramps.

        // finds v != 0 with M v = 0 for an m x (m+1) matrix (Gauss-Jordan, full pivoting)
        bool nullVector (double M[6][7], int m, double* v) noexcept
        {
            const int n = m + 1;
            int perm[7];
            for (int j = 0; j < n; ++j) perm[j] = j;

            // column equilibration
            double cs[7];
            for (int j = 0; j < n; ++j)
            {
                double mx = 0;
                for (int i = 0; i < m; ++i) mx = std::max (mx, std::abs (M[i][j]));
                cs[j] = mx > 0 ? 1.0 / mx : 1.0;
                for (int i = 0; i < m; ++i) M[i][j] *= cs[j];
            }
            // row equilibration
            for (int i = 0; i < m; ++i)
            {
                double mx = 0;
                for (int j = 0; j < n; ++j) mx = std::max (mx, std::abs (M[i][j]));
                if (mx <= 0) return false;
                for (int j = 0; j < n; ++j) M[i][j] /= mx;
            }

            for (int r = 0; r < m; ++r)
            {
                double best = 0; int pr = r, pc = r;
                for (int i = r; i < m; ++i)
                    for (int j = r; j < n; ++j)
                        if (std::abs (M[i][j]) > best) { best = std::abs (M[i][j]); pr = i; pc = j; }
                if (best < 1.0e-14) return false;
                if (pr != r) for (int j = 0; j < n; ++j) std::swap (M[r][j], M[pr][j]);
                if (pc != r) { for (int i = 0; i < m; ++i) std::swap (M[i][r], M[i][pc]); std::swap (perm[r], perm[pc]); }
                const double inv = 1.0 / M[r][r];
                for (int j = r; j < n; ++j) M[r][j] *= inv;
                for (int i = 0; i < m; ++i)
                {
                    if (i == r) continue;
                    const double f = M[i][r];
                    if (f != 0.0) for (int j = r; j < n; ++j) M[i][j] -= f * M[r][j];
                }
            }
            double xp[7];
            xp[m] = 1.0;
            for (int i = 0; i < m; ++i) xp[i] = -M[i][m];
            for (int j = 0; j < n; ++j) v[perm[j]] = xp[j] * cs[perm[j]];
            return true;
        }

        inline double smoothStep01 (double t) noexcept
        {
            t = clampT (t, 0.0, 1.0);
            return t * t * (3.0 - 2.0 * t);
        }

        // interior interpolation frequencies (rad/sample), ascending
        void choosePoints (double centre, double spanOct, int n, double* pts, double topLimit = 0.9 * kPi) noexcept
        {
            static const double fixed1[1] = { 0.62 * kPi };
            static const double fixed2[2] = { 0.45 * kPi, 0.84 * kPi };
            static const double fixed3[3] = { 0.36 * kPi, 0.62 * kPi, 0.86 * kPi };
            const double* fixedSet = n == 1 ? fixed1 : (n == 2 ? fixed2 : fixed3);

            spanOct = clampT (spanOct, 0.02, 1.6);
            double feat[3];
            for (int k = 0; k < n; ++k)
            {
                const double pos = (n == 1) ? 0.0 : (n == 2 ? (k == 0 ? -1.0 : 1.0) : (double) (k - 1));
                feat[k] = centre * std::pow (2.0, pos * spanOct);
            }
            // keep inside the band with some separation
            for (int k = n - 1; k >= 0; --k)
            {
                const double lim = topLimit * std::pow (0.94, (double) (n - 1 - k));
                feat[k] = std::min (feat[k], lim);
                if (k < n - 1) feat[k] = std::min (feat[k], feat[k + 1] * 0.94);
                feat[k] = std::max (feat[k], 1.0e-9);
            }
            const double t = smoothStep01 ((centre - 0.9 * kPi) / (0.35 * kPi));
            for (int k = 0; k < n; ++k)
                pts[k] = std::exp ((1.0 - t) * std::log (feat[k]) + t * std::log (fixedSet[k]));
        }


        // factor a positive quadratic (in x) into a minimum phase polynomial (up to scale)
        // Q(x) = q0 + q1 x + q2 x^2 -> c0 + c1 z^-1 + c2 z^-2.  Returns false if not factorable.
        bool factorQuadratic (double q0, double q1, double q2, double& c0, double& c1, double& c2, double& rootDisc) noexcept
        {
            const double A0 = q0, A1 = q0 + q1 + q2;
            if (A0 < 0.0 || A1 < 0.0) return false;
            const double sA0 = std::sqrt (A0), sA1 = std::sqrt (A1);
            // disc = W^2 + A2 with W = (sA0 + sA1)/2, A2 = -q2/4, written without cancellation
            const double disc = 0.25 * (2.0 * q0 + q1 + 2.0 * sA0 * sA1);
            if (disc < -1.0e-12 * (A0 + A1 + std::abs (q2))) return false;
            const double sd = std::sqrt (std::max (0.0, disc));
            const double W = 0.5 * (sA0 + sA1);
            c0 = 0.5 * (W + sd);
            c2 = W - c0;
            c1 = 0.5 * (sA0 - sA1);
            rootDisc = sd;
            return c0 > 0.0;
        }

        enum FitLayout { kGeneral, kLow, kHigh, kBand, kNotch };

        bool fitSecondPts (const AnalogSection& s, double w0, FitLayout layout, double xTheta,
                           const double* pts, int nInterior, DigitalSection& d) noexcept
        {
            // scaled variable u = x / xs
            const double xs = std::max (xOf (pts[nInterior / 2]), 1.0e-30);
            const double xNyq = 1.0 / xs;
            const double xT = xTheta / xs;

            double M[6][7] = {};
            int pPow[4] = { 0, 0, 0, 0 };
            int nP = 0;
            switch (layout)
            {
                case kGeneral: nP = 3; pPow[0] = 0; pPow[1] = 1; pPow[2] = 2; break;
                case kLow:     nP = 2; pPow[0] = 0; pPow[1] = 1; break;
                case kHigh:    nP = 1; pPow[0] = 2; break;
                case kBand:    nP = 2; pPow[0] = 1; pPow[1] = 2; break;
                case kNotch:   nP = 1; pPow[0] = 2; break;
            }
            const int nUnk = nP + 3;
            const int nEq = nUnk - 1;

            auto addRow = [&] (int row, double u, double T, bool isNyq)
            {
                for (int k = 0; k < nP; ++k)
                    M[row][k] = (layout == kNotch) ? (u - xT) * (u - xT) : std::pow (u, (double) pPow[k]);
                M[row][nP + 0] = -T;
                M[row][nP + 1] = -T * u;
                M[row][nP + 2] = -T * u * u;
                if (isNyq)
                {
                    const double sc = 1.0 / (1.0 + u * u);
                    for (int k = 0; k < nUnk; ++k) M[row][k] *= sc;
                }
            };

            int row = 0;
            
            const double T0 = s.magSq (0.0), T1 = targetMagSq (s, kPi, w0);
            const bool useDC = (layout == kGeneral || layout == kLow || layout == kNotch);
            if (useDC) addRow (row++, 0.0, T0, false);
            addRow (row++, xNyq, T1, true);
            for (int k = 0; k < nInterior && row < nEq; ++k)
                addRow (row++, xOf (pts[k]) / xs, targetMagSq (s, pts[k], w0), false);
            if (row != nEq) return false;

            double v[7] = {};
            if (! nullVector (M, nEq, v)) { CQ_FITDBG ("nullVector failed\n"); return false; }

            double p[3] = { 0, 0, 0 };
            if (layout == kNotch)
            {
                const double c2 = v[0] / (xs * xs);
                p[0] = c2 * xTheta * xTheta; p[1] = -2.0 * c2 * xTheta; p[2] = c2;
            }
            else
                for (int k = 0; k < nP; ++k) p[pPow[k]] = v[k] / std::pow (xs, (double) pPow[k]);
            double q0 = v[nP + 0], q1 = v[nP + 1] / xs, q2 = v[nP + 2] / (xs * xs);

            const double xm = xOf (pts[nInterior / 2]);
            if (q0 + q1 * xm + q2 * xm * xm < 0.0)
            {
                q0 = -q0; q1 = -q1; q2 = -q2;
                p[0] = -p[0]; p[1] = -p[1]; p[2] = -p[2];
            }
            if (layout == kHigh) p[0] = p[1] = 0.0;
            if (layout == kBand) p[0] = 0.0;
            if (layout == kLow)  p[2] = 0.0;

            double a0c, a1c, a2c, dQ;
            CQ_FITDBG ("pts %g %g %g (pi units) q=%g %g %g p=%g %g %g\n", pts[0]/kPi, nInterior>1?pts[1]/kPi:0, nInterior>2?pts[2]/kPi:0, q0,q1,q2,p[0],p[1],p[2]);
            if (! factorQuadratic (q0, q1, q2, a0c, a1c, a2c, dQ)) { CQ_FITDBG ("Q not factorable\n"); return false; }
            double b0c, b1c, b2c, dP;
            if (layout == kNotch)
            {
                const double c = std::sqrt (std::max (0.0, p[2])) * 0.25;   // P = 16 c^2 (x - xT)^2
                b0c = c; b1c = -2.0 * c * (1.0 - 2.0 * xTheta); b2c = c; dP = 0.0;
            }
            else if (! factorQuadratic (p[0], p[1], p[2], b0c, b1c, b2c, dP)) { CQ_FITDBG ("P not factorable\n"); return false; }

            if (a2c / a0c >= 0.9999999999 || dQ <= 0.0) return false;

            const double ia = 1.0 / a0c;
            d.second = true;
            d.a1 = a1c * ia; d.a2 = a2c * ia;
            d.b0 = b0c * ia; d.b1 = b1c * ia; d.b2 = b2c * ia;
            const double Q0 = q0, Q1 = q0 + q1 + q2;
            const double P0 = p[0], P1 = p[0] + p[1] + p[2];
            d.pdc = std::sqrt (std::max (Q0, 0.0)) * ia;
            d.pny = std::sqrt (std::max (Q1, 0.0)) * ia;
            d.omA2 = dQ * ia;
            d.hdc = Q0 > 0 ? std::sqrt (std::max (P0, 0.0) / Q0) : 0.0;
            d.hny = Q1 > 0 ? std::sqrt (std::max (P1, 0.0) / Q1) : 0.0;
            d.bdiff = (b0c - b2c) * ia;
            return std::isfinite (d.a1) && std::isfinite (d.a2) && std::isfinite (d.b0)
                && std::isfinite (d.b1) && std::isfinite (d.b2) && std::isfinite (d.pdc) && std::isfinite (d.pny);
        }

        // max dB error of a digital section against its analog section at a few check points
        double sectionError (const AnalogSection& s, const DigitalSection& d, double w0, double centre) noexcept
        {
            double err = 0.0;
            auto check = [&] (double w)
            {
                if (w <= 1.0e-9 || w >= kPi) return;
                const double ta = targetMagSq (s, w, w0);
                const double td = std::norm (d.eval (w));
                if (ta < 1.0e-7 && td < 1.0e-7) return;   // both below -70 dB
                err = std::max (err, std::abs (10.0 * std::log10 (std::max (td, 1e-30) / std::max (ta, 1e-30))));
            };
            // broad coverage
            const double lo = std::max (1.0e-7, centre / 32.0);
            {
                const double r = std::pow (0.985 * kPi / lo, 1.0 / 16.0);
                double w = lo * std::pow (r, 0.37);
                for (int i = 0; i < 16; ++i, w *= r) check (w);
            }
            // resonance coverage
            const double zp = s.second ? s.d1 / (2.0 * std::sqrt (s.d0)) : 1.0;
            const double wp = s.second ? std::sqrt (s.d0) * w0 : centre;
            if (zp < 0.5)
                for (int i = -3; i <= 3; ++i) check (wp * std::exp2 (1.44 * zp * (i + 0.13)));
            if (s.second && s.n2 > 0 && s.n0 > 0)
            {
                const double zz = std::abs (s.n1) / (2.0 * std::sqrt (s.n0 * s.n2));
                const double wz = std::sqrt (s.n0 / s.n2) * w0;
                if (zz < 0.5)
                    for (int i = -3; i <= 3; ++i) check (wz * std::exp2 (1.44 * zz * (i + 0.13)));
            }
            return err;
        }

        //==========================================================================
        // Weighted least squares rational fit with Sanathanan-Koerner iterations over a
        // dense frequency grid (relative error in |H|^2, i.e. ~ dB error).
        constexpr int kMaxRows = 72;

        bool householderLS (double A[kMaxRows][6], double* b, int m, int n, double* x) noexcept
        {
            double cs[6];
            for (int j = 0; j < n; ++j)
            {
                double mx = 0;
                for (int i = 0; i < m; ++i) mx = std::max (mx, std::abs (A[i][j]));
                cs[j] = mx > 0 ? 1.0 / mx : 1.0;
                for (int i = 0; i < m; ++i) A[i][j] *= cs[j];
            }
            double v[kMaxRows];
            for (int k = 0; k < n; ++k)
            {
                double nrm = 0;
                for (int i = k; i < m; ++i) nrm += A[i][k] * A[i][k];
                nrm = std::sqrt (nrm);
                if (nrm <= 0) return false;
                const double alpha = A[k][k] > 0 ? -nrm : nrm;
                double vn = 0;
                for (int i = k; i < m; ++i) v[i] = A[i][k];
                v[k] -= alpha;
                for (int i = k; i < m; ++i) vn += v[i] * v[i];
                if (vn <= 0) continue;
                for (int j = k; j < n; ++j)
                {
                    double dot = 0;
                    for (int i = k; i < m; ++i) dot += v[i] * A[i][j];
                    const double f = 2.0 * dot / vn;
                    for (int i = k; i < m; ++i) A[i][j] -= f * v[i];
                }
                double dot = 0;
                for (int i = k; i < m; ++i) dot += v[i] * b[i];
                const double f = 2.0 * dot / vn;
                for (int i = k; i < m; ++i) b[i] -= f * v[i];
            }
            for (int k = n - 1; k >= 0; --k)
            {
                if (std::abs (A[k][k]) < 1.0e-300) return false;
                double sum = b[k];
                for (int j = k + 1; j < n; ++j) sum -= A[k][j] * x[j];
                x[k] = sum / A[k][k];
            }
            for (int j = 0; j < n; ++j) x[j] *= cs[j];
            return true;
        }

        enum FitLayoutLS { lsGeneral, lsLow, lsHigh, lsBand, lsNotch };

        bool fitSecondLS (const AnalogSection& s, double w0, DigitalSection& d, const DigitalSection* init) noexcept
        {
            const double wp = std::sqrt (s.d0) * w0;
            const double zetaP = s.d1 / (2.0 * std::sqrt (s.d0));
            FitLayoutLS layout = lsGeneral;
            switch (s.kind)
            {
                case AnalogSection::Lowpass:  layout = lsLow; break;
                case AnalogSection::Highpass: layout = lsHigh; break;
                case AnalogSection::Bandpass: layout = lsBand; break;
                case AnalogSection::Notch:    layout = lsNotch; break;
                default: break;
            }
            double wz = wp, zetaZ = zetaP, xTheta = 0.0;
            if (layout == lsNotch)
            {
                wz = unwarpW (std::sqrt (s.n0 / s.n2) * w0);
                if (wz >= 0.995 * kPi) layout = lsGeneral;
                else xTheta = xOf (wz);
            }
            if (layout == lsGeneral && s.n2 > 0.0 && s.n0 > 0.0)
            {
                wz = std::sqrt (s.n0 / s.n2) * w0;
                zetaZ = std::abs (s.n1) / (2.0 * std::sqrt (s.n0 * s.n2));
            }

            // ---- frequency grid
            double grid[kMaxRows];
            int ng = 0;
            const double wTop = 0.985 * kPi;
            auto addRange = [&] (double a, double b, int count)
            {
                a = clampT (a, 1.0e-9, wTop); b = clampT (b, 1.0e-9, wTop);
                if (b <= a * 1.0001) { if (ng < kMaxRows - 4) grid[ng++] = a; return; }
                if (count == 1) { grid[ng++] = std::sqrt (a * b); return; }
                const double r = std::pow (b / a, 1.0 / (count - 1));
                double v = a;
                for (int i = 0; i < count && ng < kMaxRows - 4; ++i, v *= r) grid[ng++] = v;
            };
            const double lo = std::min (wp, wz), hi = std::max (wp, wz);
            const double zmin = std::min (zetaP, zetaZ);
            const double ext = std::max (1.5, 3.0 * 1.44 * std::min (zmin, 1.0));
            addRange (lo * std::exp2 (-ext), hi * std::exp2 (ext), 12);
            if (zetaP < 0.4) addRange (wp * std::exp2 (-4.3 * zetaP), wp * std::exp2 (4.3 * zetaP), 9);
            if (layout == lsGeneral && zetaZ < 0.4) addRange (wz * std::exp2 (-4.3 * zetaZ), wz * std::exp2 (4.3 * zetaZ), 9);
            addRange (std::max (1.0e-8, lo / 64.0), wTop, 8);
            for (int i = 0; i < 6 && ng < kMaxRows - 4; ++i) grid[ng++] = (0.40 + 0.115 * i) * kPi;   // upper band coverage
            if (layout == lsNotch)
            {
                const double dz = clampT (1.44 * zetaP, 0.01, 1.0);
                addRange (wz * std::pow (2.0, -3.0 * dz), wz * std::pow (2.0, -0.3 * dz), 5);
                addRange (wz * std::pow (2.0, 0.3 * dz), wz * std::pow (2.0, 3.0 * dz), 5);
            }

            const double centre = clampT (std::sqrt (wp * wz), 1.0e-9, 0.9 * kPi);
            const double xs = std::max (xOf (centre), 1.0e-30);
            const double uT = xTheta / xs;

            double U[kMaxRows], T[kMaxRows], WF[kMaxRows];
            int m = 0;
            double tMax = 0;
            for (int i = 0; i < ng; ++i)
            {
                U[m] = xOf (grid[i]) / xs;
                T[m] = targetMagSq (s, grid[i], w0);
                WF[m] = 1.0;
                tMax = std::max (tMax, T[m]);
                ++m;
            }
            const bool useDC = (layout == lsGeneral || layout == lsLow || layout == lsNotch);
            if (useDC) { U[m] = 0.0; T[m] = s.magSq (0.0); WF[m] = 12.0; tMax = std::max (tMax, T[m]); ++m; }
            U[m] = 1.0 / xs; T[m] = targetMagSq (s, kPi, w0); WF[m] = 12.0; tMax = std::max (tMax, T[m]); ++m;
            const double tFloor = tMax * 1.0e-7;

            int nP = 0; int pPow[3] = { 0, 0, 0 };
            switch (layout)
            {
                case lsGeneral: nP = 3; pPow[0] = 0; pPow[1] = 1; pPow[2] = 2; break;
                case lsLow:     nP = 2; pPow[0] = 0; pPow[1] = 1; break;
                case lsHigh:    nP = 1; pPow[0] = 2; break;
                case lsBand:    nP = 2; pPow[0] = 1; pPow[1] = 2; break;
                case lsNotch:   nP = 1; pPow[0] = 2; break;
            }
            const int n = nP + 2;
            auto basis = [&] (int k, double u)
            {
                if (layout == lsNotch) return (u - uT) * (u - uT);
                return pPow[k] == 0 ? 1.0 : (pPow[k] == 1 ? u : u * u);
            };

            double qPrev[kMaxRows];
            double bestErrLS = 1.0e30;
            bool haveQ = false;
            if (init != nullptr && init->second)
            {
                // start the SK iteration from the matched design's (impulse invariant) poles
                const double A0i = init->pdc * init->pdc, A1i = init->pny * init->pny, A2i = -4.0 * init->a2;
                for (int i = 0; i < m; ++i)
                {
                    const double x = clampT (U[i] * xs, 0.0, 1.0);
                    qPrev[i] = std::max (A0i * (1.0 - x) + A1i * x + A2i * 4.0 * x * (1.0 - x), 1.0e-300);
                }
                haveQ = true;
            }
            double sol[6] = {}, bestSol[6] = {};
            bool any = false;

            for (int iter = 0; iter < 4; ++iter)
            {
                double A[kMaxRows][6], rhs[kMaxRows];
                for (int i = 0; i < m; ++i)
                {
                    const double u = U[i];
                    const double Ti = T[i];
                    for (int k = 0; k < nP; ++k) A[i][k] = basis (k, u);
                    A[i][nP]     = -Ti * (u - 1.0);
                    A[i][nP + 1] = -Ti * (u * u - 1.0);
                    rhs[i] = Ti;
                    double w;
                    if (haveQ) w = WF[i] / (std::max (Ti, tFloor) * std::max (qPrev[i], 1.0e-300));
                    else
                    {
                        double mx = std::abs (rhs[i]);
                        for (int k = 0; k < n; ++k) mx = std::max (mx, std::abs (A[i][k]));
                        w = WF[i] / std::max (mx, 1.0e-300);
                    }
                    for (int k = 0; k < n; ++k) A[i][k] *= w;
                    rhs[i] *= w;
                }
                if (! householderLS (A, rhs, m, n, sol)) break;
                const double q1 = sol[nP], q2 = sol[nP + 1], q0 = 1.0 - q1 - q2;
                bool positive = true;
                double maxErr = 0.0;
                double qNew[kMaxRows];
                for (int i = 0; i < m; ++i)
                {
                    const double u = U[i];
                    const double qv = q0 + q1 * u + q2 * u * u;
                    if (! (qv > 0.0)) { positive = false; break; }
                    double pv = 0.0;
                    for (int k = 0; k < nP; ++k) pv += sol[k] * basis (k, u);
                    if (pv < -1.0e-9 * qv * std::max (T[i], tFloor)) { positive = false; break; }
                    qNew[i] = qv;
                    const double ratio = std::max (pv, 0.0) / qv;
                    if (T[i] > tFloor || ratio > tFloor)
                        maxErr = std::max (maxErr, std::abs (10.0 * std::log10 (std::max (ratio, tFloor * 1e-3) / std::max (T[i], tFloor * 1e-3))));
                }
                if (! positive) break;
                for (int i = 0; i < m; ++i) qPrev[i] = qNew[i];
                haveQ = true;
                if (maxErr < bestErrLS)
                {
                    bestErrLS = maxErr;
                    any = true;
                    for (int k = 0; k < n; ++k) bestSol[k] = sol[k];
                }
            }
            if (! any) return false;

            double p[3] = { 0, 0, 0 };
            if (layout == lsNotch)
            {
                const double c2 = bestSol[0] / (xs * xs);
                p[0] = c2 * xTheta * xTheta; p[1] = -2.0 * c2 * xTheta; p[2] = c2;
            }
            else
                for (int k = 0; k < nP; ++k) p[pPow[k]] = bestSol[k] / std::pow (xs, (double) pPow[k]);
            const double q1s = bestSol[nP], q2s = bestSol[nP + 1];
            const double q0 = 1.0 - q1s - q2s, q1 = q1s / xs, q2 = q2s / (xs * xs);

            double a0c, a1c, a2c, dQ;
            if (! factorQuadratic (q0, q1, q2, a0c, a1c, a2c, dQ)) return false;
            double b0c, b1c, b2c, dP;
            if (layout == lsNotch)
            {
                if (p[2] < 0) return false;
                const double c = std::sqrt (p[2]) * 0.25;
                b0c = c; b1c = -2.0 * c * (1.0 - 2.0 * xTheta); b2c = c; dP = 0.0;
            }
            else
            {
                if (layout == lsHigh) { p[0] = p[1] = 0.0; }
                if (layout == lsBand) p[0] = 0.0;
                if (layout == lsLow)  p[2] = 0.0;
                if (! factorQuadratic (p[0], p[1], p[2], b0c, b1c, b2c, dP)) return false;
            }
            if (a2c / a0c >= 0.9999999999 || dQ <= 0.0) return false;

            const double ia = 1.0 / a0c;
            d.second = true;
            d.a1 = a1c * ia; d.a2 = a2c * ia;
            d.b0 = b0c * ia; d.b1 = b1c * ia; d.b2 = b2c * ia;
            const double Q0 = q0, Q1 = q0 + q1 + q2;
            const double P0 = p[0], P1 = p[0] + p[1] + p[2];
            d.pdc = std::sqrt (std::max (Q0, 0.0)) * ia;
            d.pny = std::sqrt (std::max (Q1, 0.0)) * ia;
            d.omA2 = dQ * ia;
            d.hdc = Q0 > 0 ? std::sqrt (std::max (P0, 0.0) / Q0) : 0.0;
            d.hny = Q1 > 0 ? std::sqrt (std::max (P1, 0.0) / Q1) : 0.0;
            d.bdiff = (b0c - b2c) * ia;
            return std::isfinite (d.a1) && std::isfinite (d.a2) && std::isfinite (d.b0)
                && std::isfinite (d.b1) && std::isfinite (d.b2) && std::isfinite (d.pdc) && std::isfinite (d.pny);
        }

        bool fitSecond (const AnalogSection& s, double w0, DigitalSection& d) noexcept
        {
            const double wp = std::sqrt (s.d0) * w0;                  // pole natural frequency
            const double zetaP = s.d1 / (2.0 * std::sqrt (s.d0));

            FitLayout layout = kGeneral;
            switch (s.kind)
            {
                case AnalogSection::Lowpass:  layout = kLow; break;
                case AnalogSection::Highpass: layout = kHigh; break;
                case AnalogSection::Bandpass: layout = kBand; break;
                case AnalogSection::Notch:    layout = kNotch; break;
                default: break;
            }
            double xTheta = 0.0, wz = wp;
            if (layout == kNotch)
            {
                wz = unwarpW (std::sqrt (s.n0 / s.n2) * w0);   // digital notch position
                if (wz >= 0.995 * kPi) layout = kGeneral;
                else xTheta = xOf (wz);
            }

            int nInterior = 3;
            double centre = wp, span = 1.44 * zetaP;
            switch (layout)
            {
                case kLow: case kHigh: nInterior = 2; span = clampT (1.44 * zetaP, 0.03, 1.0); break;
                case kBand: nInterior = 3; span = clampT (1.44 * zetaP, 0.03, 1.4); break;
                case kNotch: nInterior = 1; break;
                default:
                {
                    nInterior = 3;
                    wz = wp;
                    if (s.n2 > 0.0 && s.n0 > 0.0) wz = std::sqrt (s.n0 / s.n2) * w0;
                    centre = std::sqrt (wp * wz);
                    const double zetaZ = (s.n2 > 0.0 && s.n0 > 0.0) ? s.n1 / (2.0 * std::sqrt (s.n0 * s.n2)) : zetaP;
                    span = std::max (1.44 * std::min (zetaP, std::abs (zetaZ)), 0.5 * std::abs (std::log2 (wz / wp)));
                    span = clampT (span, 0.03, 1.4);
                    break;
                }
            }

            const bool resonant = zetaP < 0.25;
            const double top = resonant ? 0.97 * kPi : 0.9 * kPi;

            DigitalSection best, trial;
            double bestErr = 1.0e30;
            bool any = false;
            const double spanScales[] = { 1.0, 0.5, 1.7, 0.25 };
            const double shifts[] = { 1.0, 1.0, 1.0, 0.7 };
            for (int attempt = 0; attempt < 4; ++attempt)
            {
                double pts[3];
                if (layout == kNotch)
                {
                    const double delta = clampT (1.44 * zetaP * spanScales[attempt], 0.02, 1.2);
                    double pnt = wz * std::pow (2.0, -delta);
                    if (pnt < 0.02 * kPi && wz * std::pow (2.0, delta) < 0.95 * kPi) pnt = wz * std::pow (2.0, delta);
                    pts[0] = clampT (pnt, 1.0e-9, 0.97 * kPi);
                }
                else
                    choosePoints (centre * shifts[attempt], span * spanScales[attempt], nInterior, pts, top);

                if (! fitSecondPts (s, w0, layout, xTheta, pts, nInterior, trial)) continue;
                const double e = sectionError (s, trial, w0, centre);
                if (e < bestErr) { bestErr = e; best = trial; any = true; }
                if (bestErr < 0.02) break;
            }
            if (! any) return false;
            d = best;
            return true;
        }

        bool fitFirst (const AnalogSection& s, double w0, DigitalSection& d) noexcept
        {
            if (s.kind == AnalogSection::Allpass) return false;   // handled exactly elsewhere
            double corner = s.d0 * w0;
            if (s.n1 > 0.0 && s.n0 > 0.0) corner = std::sqrt (corner * (s.n0 / s.n1) * w0);
            double pt;
            choosePoints (corner, 0.0, 1, &pt);
            const double xs = std::max (xOf (pt), 1.0e-30);
            
            const double T0 = s.magSq (0.0), T1 = targetMagSq (s, kPi, w0), Tm = targetMagSq (s, pt, w0);
            // unknowns p0, p1, q0, q1 (P = p0 + p1 x, Q = q0 + q1 x), scaled by xs
            double M[6][7] = {};
            const double uN = 1.0 / xs;
            M[0][0] = 1; M[0][1] = 0;  M[0][2] = -T0; M[0][3] = 0;
            const double sc = 1.0 / (1.0 + uN);
            M[1][0] = sc; M[1][1] = uN * sc; M[1][2] = -T1 * sc; M[1][3] = -T1 * uN * sc;
            M[2][0] = 1; M[2][1] = 1;  M[2][2] = -Tm; M[2][3] = -Tm;
            double v[7] = {};
            if (! nullVector (M, 3, v)) return false;
            double p0 = v[0], p1 = v[1] / xs, q0 = v[2], q1 = v[3] / xs;
            if (q0 + q1 * xs < 0) { p0 = -p0; p1 = -p1; q0 = -q0; q1 = -q1; }
            const double A0 = q0, A1 = q0 + q1, B0 = p0, B1 = p0 + p1;
            if (A0 <= 0 || A1 <= 0 || B0 < -1e-15 || B1 < -1e-15) return false;
            const double sA0 = std::sqrt (A0), sA1 = std::sqrt (A1);
            const double sB0 = std::sqrt (std::max (0.0, B0)), sB1 = std::sqrt (std::max (0.0, B1));
            const double al0 = 0.5 * (sA0 + sA1), al1 = 0.5 * (sA0 - sA1);
            const double ia = 1.0 / al0;
            d.second = false;
            d.a1 = al1 * ia; d.a2 = 0;
            d.b0 = 0.5 * (sB0 + sB1) * ia; d.b1 = 0.5 * (sB0 - sB1) * ia; d.b2 = 0;
            d.pdc = sA0 * ia;
            d.pny = sA1 * ia;
            d.hdc = sB0 / sA0;
            d.hny = sB1 / sA1;
            return std::isfinite (d.a1) && std::abs (d.a1) < 0.999999999;
        }

        void bilinearSection (const AnalogSection& s, double K, DigitalSection& d) noexcept
        {
            if (! s.second)
            {
                const double a0 = 1.0 + s.d0 * K;
                d.second = false;
                d.b0 = (s.n1 + s.n0 * K) / a0;
                d.b1 = (-s.n1 + s.n0 * K) / a0;
                d.b2 = 0.0;
                d.a1 = (-1.0 + s.d0 * K) / a0;
                d.a2 = 0.0;
                d.pdc = 2.0 * s.d0 * K / a0;
                d.pny = 2.0 / a0;
                d.hdc = s.n0 / s.d0;
                d.hny = s.n1;
                return;
            }
            const double K2 = K * K;
            const double a0 = 1.0 + s.d1 * K + s.d0 * K2;
            d.second = true;
            d.b0 = (s.n2 + s.n1 * K + s.n0 * K2) / a0;
            d.b1 = (-2.0 * s.n2 + 2.0 * s.n0 * K2) / a0;
            d.b2 = (s.n2 - s.n1 * K + s.n0 * K2) / a0;
            d.a1 = (-2.0 + 2.0 * s.d0 * K2) / a0;
            d.a2 = (1.0 - s.d1 * K + s.d0 * K2) / a0;
            d.pdc = 4.0 * s.d0 * K2 / a0;
            d.pny = 4.0 / a0;
            d.omA2 = 2.0 * s.d1 * K / a0;
            d.hdc = s.n0 / s.d0;
            d.hny = s.n2;
            d.bdiff = 2.0 * s.n1 * K / a0;
        }
    }

    SvfCoeffs toSvf (const DigitalSection& d) noexcept
    {
        SvfCoeffs c;
        if (! d.second)
        {
            c.second = false;
            c.g  = 0.5 * d.pdc;                   // G = (1 + a1)/2
            c.m0 = d.hny;                         // response at Nyquist
            c.m1 = d.hdc - d.hny;                 // weight of the low pass output
            c.m2 = 0.0;
            c.k = 0.0;
            return c;
        }
        c.second = true;
        const double pdc = std::max (d.pdc, 1.0e-300);
        const double pny = std::max (d.pny, 1.0e-300);
        c.g = std::sqrt (pdc / pny);
        c.k = 2.0 * d.omA2 / std::sqrt (pdc * pny);
        const double n0 = d.hdc;                              // DC gain
        const double n2 = d.hny;                              // Nyquist gain
        const double n1 = 2.0 * c.g * d.bdiff / pdc;
        c.m0 = n2;
        c.m1 = n1 - c.k * n2;
        c.m2 = n0 - n2;
        return c;
    }

    static int gDesignMethod = 0;   // 0 auto, 1 matched only, 2 fit only (testing)
    void setDesignMethodForTesting (int m) noexcept { gDesignMethod = m; }

    int digitalSectionCount (const AnalogFilter& analog) noexcept
    {
        int n = 0;
        for (int i = 0; i < analog.num; ++i)
        {
            if (! analog.bilinear && analog.sec[(size_t) i].pairWithNext && i + 1 < analog.num) ++i;
            ++n;
        }
        return n;
    }

    int analogIndexForDigital (const AnalogFilter& analog, int digitalIndex) noexcept
    {
        int n = 0;
        for (int i = 0; i < analog.num; ++i)
        {
            if (n == digitalIndex) return i;
            if (! analog.bilinear && analog.sec[(size_t) i].pairWithNext && i + 1 < analog.num) ++i;
            ++n;
        }
        return -1;
    }

    bool digitalSectionIsSecond (const AnalogFilter& analog, int digitalIndex) noexcept
    {
        const int ai = analogIndexForDigital (analog, digitalIndex);
        if (ai < 0) return true;
        const auto& s = analog.sec[(size_t) ai];
        return s.second || (! analog.bilinear && s.pairWithNext);
    }

    void designSection (const AnalogFilter& analog, int digitalIndex, double f0, double fs, bool accurate, DigitalSection& d) noexcept
    {
        const int i = analogIndexForDigital (analog, digitalIndex);
        if (i < 0) { d = DigitalSection(); return; }
        const auto& s = analog.sec[(size_t) i];

        if (analog.bilinear)
        {
            const double nyq = 0.5 * fs;
            double fc = f0;
            if (fc >= 0.49 * fs)
            {
                if (! analog.bilinearHighpass && fc >= nyq * 0.995)
                {
                    // high cut above Nyquist: transparent (keeps the section topology)
                    d = DigitalSection();
                    d.second = s.second;
                    d.b0 = 1; d.b1 = d.b2 = d.a1 = d.a2 = 0;
                    d.pdc = d.pny = d.hdc = d.hny = d.omA2 = d.bdiff = 1.0;
                    return;
                }
                fc = 0.49 * fs;
            }
            bilinearSection (s, std::tan (kPi * fc / fs), d);
            return;
        }

        const double w0 = kTwoPi * f0 / fs;
        bool ok = false;
        if (s.kind != AnalogSection::Allpass && gDesignMethod != 1)
        {
            if (s.second && ! accurate)
            {
                matchSection (s, w0, d);
                ok = true;
            }
            else if (s.second)
            {
                DigitalSection dm, dl, de;
                matchSection (s, w0, dm);
                const double centre = clampT (std::sqrt (s.d0) * w0, 1.0e-9, 0.95 * kPi);
                double best = sectionError (s, dm, w0, centre);
                d = dm; ok = true;
                if (gDesignMethod == 0 || gDesignMethod == 3)
                {
                    if (fitSecondLS (s, w0, dl, &dm))
                    {
                        const double e = sectionError (s, dl, w0, centre);
                        if (e < best || gDesignMethod == 3) { best = e; d = dl; }
                    }
                }
                if (gDesignMethod == 2 && fitSecond (s, w0, de))
                {
                    const double e = sectionError (s, de, w0, centre);
                    if (e < best) { best = e; d = de; }
                }
            }
            else ok = fitFirst (s, w0, d);
        }
        if (! ok)
            matchSection (s, w0, d);

        if (s.pairWithNext && i + 1 < analog.num)
        {
            // recombine the overdamped split into one biquad: (b0 + b1 z)(c0 + c1 z) / ((1 + a z)(1 + e z))
            const auto& s2 = analog.sec[(size_t) i + 1];
            DigitalSection d2;
            if (! fitFirst (s2, w0, d2)) matchSection (s2, w0, d2);
            DigitalSection c;
            c.second = true;
            c.b0 = d.b0 * d2.b0;
            c.b1 = d.b0 * d2.b1 + d.b1 * d2.b0;
            c.b2 = d.b1 * d2.b1;
            c.a1 = d.a1 + d2.a1;
            c.a2 = d.a1 * d2.a1;
            c.pdc = d.pdc * d2.pdc;                       // (1 + a)(1 + e)
            c.pny = d.pny * d2.pny;                       // (1 - a)(1 - e)
            c.omA2 = d.pdc - d.a1 * d2.pdc;               // 1 - a e = (1 + a) - a (1 + e)
            c.hdc = d.hdc * d2.hdc;
            c.hny = d.hny * d2.hny;
            c.bdiff = c.b0 - c.b2;
            d = c;
        }
    }

    void designDigital (const AnalogFilter& analog, double f0, double fs, DigitalFilter& out, bool accurate) noexcept
    {
        out.num = 0;
        out.gain = analog.gain;
        const int n = digitalSectionCount (analog);
        for (int k = 0; k < n; ++k)
        {
            designSection (analog, k, f0, fs, accurate, out.sec[(size_t) k]);
            out.svf[(size_t) k] = toSvf (out.sec[(size_t) k]);
        }
        out.num = n;
        if (analog.bilinear && ! analog.bilinearHighpass && f0 >= 0.5 * fs * 0.995) out.gain = 1.0;
    }
}
