// Numerical verification of the filter designs (no JUCE dependency)
#include "../Source/dsp/FilterDesign.h"
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>

using namespace cq;

struct Case { double err; double errTop; std::string desc; };

static cplx svfResponse (const DigitalFilter& d, double w)
{
    cplx h (d.gain, 0);
    const cplx z1 = std::polar (1.0, -w);
    for (int k = 0; k < d.num; ++k)
    {
        const auto& c = d.svf[(size_t) k];
        if (c.second)
        {
            const cplx s = (1.0 / c.g) * (1.0 - z1) / (1.0 + z1);
            const cplx D = s * s + c.k * s + 1.0;
            h *= c.m0 + c.m1 * s / D + c.m2 / D;
        }
        else
        {
            const cplx lp = c.g * (1.0 + z1) / (1.0 - (1.0 - 2.0 * c.g) * z1);
            h *= c.m0 + c.m1 * lp;
        }
    }
    return h;
}

// error (dB) below 0.76 Nyquist (exact region) and above (approximation region)
static void compare (const BandShapeParams& p, double fs, double& errLow, double& errTop, double& worstF, bool& unstable)
{
    AnalogFilter a; makeAnalogPrototype (p, a);
    DigitalFilter d; designDigital (a, p.freq, fs, d);
    errLow = errTop = 0; worstF = 0; unstable = false;
    for (int k = 0; k < d.num; ++k)
    {
        const auto& s = d.sec[(size_t) k];
        if (s.second) { if (std::abs (s.a2) >= 1.0 || std::abs (s.a1) >= 1.0 + s.a2) unstable = true; }
        else if (std::abs (s.a1) >= 1.0) unstable = true;
        if (! std::isfinite (s.b0) || ! std::isfinite (s.a1)) unstable = true;
    }
    const double nyq = fs * 0.5;
    for (int i = 0; i <= 1500; ++i)
    {
        const double f = 10.0 * std::pow (nyq * 0.999 / 10.0, i / 1500.0);
        const double ma = std::sqrt (a.magSq (f / p.freq));
        const double md = std::abs (svfResponse (d, kTwoPi * f / fs));
        const double da = gainToDb (ma), dd = gainToDb (md);
        if (da < -40.0 && dd < -40.0) continue;
        const double err = std::abs (da - dd);
        if (f < 0.76 * nyq) { if (err > errLow) { errLow = err; worstF = f; } }
        else errTop = std::max (errTop, err);
    }
}

int main (int argc, char** argv)
{
    const bool verbose = argc > 1;
    const double rates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };
    const double freqs[] = { 10, 20, 50, 100, 300, 1000, 3000, 6000, 10000, 14000, 17000, 20000, 25000, 30000 };
    const double qs[] = { 0.025, 0.1, 0.3, 0.7, 1.0, 2.0, 4.0, 10.0, 20.0, 40.0 };
    const double gains[] = { -30, -18, -6, -1, 1, 6, 18, 30 };
    const Shape shapes[] = { Shape::Bell, Shape::LowShelf, Shape::HighShelf, Shape::TiltShelf, Shape::LowCut, Shape::HighCut,
                             Shape::Notch, Shape::BandPass, Shape::AllPass, Shape::FlatTilt };

    {
        const auto& e = getBrickwallPrototype();
        AnalogFilter a; BandShapeParams p; p.shape = Shape::HighCut; p.slope = Slope::Brickwall; p.freq = 1000;
        makeAnalogPrototype (p, a);
        std::printf ("Brickwall: order %d, |H(1)|=%.4f dB, |H(1.05)|=%.1f, |H(1.1)|=%.1f, |H(1.2)|=%.1f, |H(1.5)|=%.1f |H(2)|=%.1f dB\n",
                     e.order, gainToDb (std::sqrt (a.magSq (1))), gainToDb (std::sqrt (a.magSq (1.05))),
                     gainToDb (std::sqrt (a.magSq (1.1))), gainToDb (std::sqrt (a.magSq (1.2))), gainToDb (std::sqrt (a.magSq (1.5))), gainToDb (std::sqrt (a.magSq (2))));
    }

    for (Shape sh : shapes)
    {
        std::vector<Case> typical, extreme;
        int nUnstable = 0;
        for (double fs : rates)
        for (double f : freqs)
        for (double q : qs)
        for (double g : gains)
        for (int sl = 0; sl < (int) Slope::Count; ++sl)
        {
            if (! slopeValidForShape (sh, (Slope) sl)) continue;
            if (sl == (int) Slope::Brickwall) continue;
            if (sh == Shape::FlatTilt && sl != 1) continue;
            if (! shapeUsesGain (sh) && g != 6) continue;
            if (! shapeUsesQ (sh, (Slope) sl) && q != 1.0) continue;
            BandShapeParams p; p.shape = sh; p.slope = (Slope) sl; p.freq = f; p.q = q; p.gainDb = g;
            double eL, eT, wf; bool unst;
            compare (p, fs, eL, eT, wf, unst);
            if (unst) ++nUnstable;
            char buf[256];
            std::snprintf (buf, sizeof (buf), "fs=%g f=%g q=%g g=%g %s (at %.0f Hz) top=%.2f", fs, f, q, g, slopeName ((Slope) sl), wf, eT);
            const bool isTypical = f <= 20000 && q >= 0.1 && q <= 10 && sl <= (int) Slope::dB48 && std::abs (g) <= 18;
            (isTypical ? typical : extreme).push_back ({ eL, eT, buf });
        }
        auto report = [&] (const char* label, std::vector<Case>& v)
        {
            if (v.empty()) return;
            int c01 = 0, c05 = 0, c1 = 0, c3 = 0;
            for (auto& c : v) { c01 += c.err < 0.1; c05 += c.err < 0.5; c1 += c.err < 1.0; c3 += c.err < 3.0; }
            std::sort (v.begin(), v.end(), [] (const Case& a, const Case& b) { return a.err > b.err; });
            const double n = (double) v.size();
            std::printf ("%-10s %-8s n=%5d  <0.1dB %5.1f%%  <0.5dB %5.1f%%  <1dB %5.1f%%  <3dB %5.1f%%  worst %.2f dB [%s]\n",
                         shapeName (sh), label, (int) v.size(), 100 * c01 / n, 100 * c05 / n, 100 * c1 / n, 100 * c3 / n, v[0].err, v[0].desc.c_str());
            if (verbose) for (size_t i = 1; i < std::min<size_t> (8, v.size()); ++i) std::printf ("            %.2f dB [%s]\n", v[i].err, v[i].desc.c_str());
        };
        report ("typical", typical);
        report ("extreme", extreme);
        if (nUnstable) std::printf ("   !!! %d unstable designs\n", nUnstable);
    }
    return 0;
}
