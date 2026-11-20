/*
    Claude-Q 4 - character modes
*/
#include "Character.h"

namespace cq
{
    namespace
    {
        double ipowp (double x, int n) noexcept
        {
            double z = 1.0;
            while (n != 0)
            {
                if ((n & 1) != 0) z *= x;
                n >>= 1;
                x *= x;
            }
            return z;
        }

        double accNum (double q, int order, int c) noexcept
        {
            int i = 0, j = 1;
            double acc = 0.0, t;
            do
            {
                t = ipowp (q, i * (i + 1)) * std::sin ((i * 2 + 1) * c * kPi / order) * j;
                acc += t;
                j = -j;
                ++i;
            }
            while (std::abs (t) > 1.0e-100 && i < 1000);
            return acc;
        }

        double accDen (double q, int order, int c) noexcept
        {
            int i = 1, j = -1;
            double acc = 0.0, t;
            do
            {
                t = ipowp (q, i * i) * std::cos (i * 2 * c * kPi / order) * j;
                acc += t;
                j = -j;
                ++i;
            }
            while (std::abs (t) > 1.0e-100 && i < 1000);
            return acc;
        }
    }

    // Elliptic polyphase halfband design (after L. de Soras / Valenzuela & Constantinides)
    void HalfbandPolyphase::design (int numCoefs, double transition, double* coefs) noexcept
    {
        double k = std::tan ((1.0 - transition * 2.0) * kPi / 4.0);
        k *= k;
        const double kksqrt = std::pow (1.0 - k * k, 0.25);
        const double e = 0.5 * (1.0 - kksqrt) / (1.0 + kksqrt);
        const double e2 = e * e, e4 = e2 * e2;
        const double q = e * (1.0 + e4 * (2.0 + e4 * (15.0 + 150.0 * e4)));
        const int order = numCoefs * 2 + 1;
        for (int index = 0; index < numCoefs; ++index)
        {
            const int c = index + 1;
            const double num = accNum (q, order, c) * std::pow (q, 0.25);
            const double den = accDen (q, order, c) + 0.5;
            const double ww = num / den;
            const double wwsq = ww * ww;
            const double x = std::sqrt ((1.0 - wwsq * k) * (1.0 - wwsq / k)) / (1.0 + wwsq);
            coefs[index] = (1.0 - x) / (1.0 + x);
        }
    }

    void HalfbandPolyphase::setCoefs (const double* c, int n) noexcept
    {
        num = std::min (n, kMaxCoefs);
        for (int i = 0; i < num; ++i) coef[i] = c[i];
        reset();
    }

    void HalfbandPolyphase::reset() noexcept
    {
        for (int i = 0; i < kMaxCoefs; ++i) xm[i] = ym[i] = 0.0;
        oddDelay = 0.0;
    }

    void HalfbandPolyphase::upsample (double x, double& y0, double& y1) noexcept
    {
        double a = x, b = x;
        for (int i = 0; i < num; i += 2)
        {
            const double y = coef[i] * (a - ym[i]) + xm[i];
            xm[i] = a; ym[i] = y; a = y;
        }
        for (int i = 1; i < num; i += 2)
        {
            const double y = coef[i] * (b - ym[i]) + xm[i];
            xm[i] = b; ym[i] = y; b = y;
        }
        y0 = a; y1 = b;
    }

    double HalfbandPolyphase::downsample (double x0, double x1) noexcept
    {
        double a = x0, b = oddDelay;
        oddDelay = x1;
        for (int i = 0; i < num; i += 2)
        {
            const double y = coef[i] * (a - ym[i]) + xm[i];
            xm[i] = a; ym[i] = y; a = y;
        }
        for (int i = 1; i < num; i += 2)
        {
            const double y = coef[i] * (b - ym[i]) + xm[i];
            xm[i] = b; ym[i] = y; b = y;
        }
        return 0.5 * (a + b);
    }

    //==============================================================================
    void CharacterProcessor::prepare (double sampleRate) noexcept
    {
        fs = sampleRate;
        double c[12], c2[6];
        HalfbandPolyphase::design (12, 0.04, c);
        HalfbandPolyphase::design (6, 0.20, c2);
        for (auto& chn : ch)
        {
            chn.up.setCoefs (c, 12);
            chn.down.setCoefs (c, 12);
            chn.up2a.setCoefs (c2, 6);
            chn.down2a.setCoefs (c2, 6);
        }
        const double g = std::tan (kPi * 150.0 / (4.0 * fs));
        shelfCoef = g / (1.0 + g);
        dcCoef = std::exp (-kTwoPi * 5.0 / fs);
        reset();
    }

    void CharacterProcessor::reset() noexcept
    {
        for (auto& chn : ch)
        {
            chn.up.reset(); chn.down.reset();
            chn.up2a.reset(); chn.down2a.reset();
            chn.pre = chn.post = chn.dcX = chn.dcY = 0.0;
        }
        current = target;
        mix = (current == CharacterMode::Clean) ? 0.0 : 1.0;
        switch (current)
        {
            case CharacterMode::Subtle: drive = 0.30; asym = 0.025; lowBoost = 1.6; break;
            case CharacterMode::Warm:   drive = 0.55; asym = 0.07;  lowBoost = 1.4; break;
            default: break;
        }
    }

    double CharacterProcessor::shape (double x, double d, double a) const noexcept
    {
        return std::tanh (d * (x + a * x * x)) / d;
    }

    void CharacterProcessor::process (double* L, double* R, int n) noexcept
    {
        if (mix <= 0.0 && target == CharacterMode::Clean && current == CharacterMode::Clean) return;

        const double rampStep = 1.0 / (0.03 * fs);
        const double G = shelfCoef;
        for (int i = 0; i < n; ++i)
        {
            // mode switching: fade out, swap the curve, fade back in
            if (target != current)
            {
                mix -= rampStep;
                if (mix <= 0.0)
                {
                    mix = 0.0;
                    current = target;
                    switch (current)
                    {
                        case CharacterMode::Subtle: drive = 0.30; asym = 0.025; lowBoost = 1.6; break;
                        case CharacterMode::Warm:   drive = 0.55; asym = 0.07;  lowBoost = 1.4; break;
                        default: break;
                    }
                }
            }
            else if (current != CharacterMode::Clean && mix < 1.0) mix = std::min (1.0, mix + rampStep);

            const double cc = lowBoost - 1.0;
            for (int k = 0; k < (R != nullptr ? 2 : 1); ++k)
            {
                double* buf = (k == 0) ? L : R;
                auto& chn = ch[(size_t) k];
                const double dry = buf[i];

                double u2[2], u[4];
                chn.up.upsample (dry, u2[0], u2[1]);
                chn.up2a.upsample (u2[0], u[0], u[1]);     // the 2x stream is processed in order
                chn.up2a.upsample (u2[1], u[2], u[3]);
                for (double& s : u)
                {
                    // low frequency emphasis (TPT one pole)
                    const double v = (s - chn.pre) * G;
                    const double lp = v + chn.pre;
                    chn.pre = lp + v;
                    const double e = s + cc * lp;
                    const double sat = shape (e, drive, asym);
                    // exact inverse of the emphasis
                    const double d = (sat - cc * (1.0 - G) * chn.post) / (1.0 + cc * G);
                    const double v2 = (d - chn.post) * G;
                    const double lp2 = v2 + chn.post;
                    chn.post = lp2 + v2;
                    s = d;
                }
                const double d0 = chn.down2a.downsample (u[0], u[1]);
                const double d1 = chn.down2a.downsample (u[2], u[3]);
                const double y = chn.down.downsample (d0, d1);
                // DC blocker (asymmetric curves create a little DC)
                const double hp = y - chn.dcX + dcCoef * chn.dcY;
                chn.dcX = y; chn.dcY = hp;
                buf[i] = dry + (hp - dry) * mix;
            }
        }
    }
}
