// Natural phase test: IIR chain + correction FIR must reproduce analog magnitude and phase
#include "../Source/dsp/IIRBand.h"
#include "../Source/dsp/NaturalPhase.h"
#include <cstdio>
#include <vector>

using namespace cq;

int main()
{
    int failures = 0;
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        const int ctrl = fs > 50000 ? 64 : 32;
        struct B { Shape sh; Slope sl; double f, g, q; Placement pl; };
        const B bands[] = {
            { Shape::Bell, Slope::dB12, 12000, 6, 1, Placement::Stereo },
            { Shape::HighCut, Slope::dB24, 17000, 0, 1, Placement::Stereo },
            { Shape::LowShelf, Slope::dB12, 150, 4, 1, Placement::Mid },
            { Shape::Bell, Slope::dB24, 3000, -5, 2, Placement::Side },
            { Shape::HighShelf, Slope::dB12, 10000, 3, 1, Placement::Left },
        };
        std::vector<IIRBand> iir (5);
        NPSnapshot snap;
        for (int i = 0; i < 5; ++i)
        {
            iir[(size_t) i].prepare (fs, ctrl);
            BandParams p; p.used = true; p.shape = bands[i].sh; p.slope = bands[i].sl; p.freq = bands[i].f; p.gainDb = bands[i].g; p.q = bands[i].q; p.place = bands[i].pl;
            iir[(size_t) i].setTarget (p, 1.0, false);
        }
        auto runChain = [&] (std::vector<double>& L, std::vector<double>& R)
        {
            for (size_t pos = 0; pos < L.size(); pos += (size_t) ctrl)
            {
                const int n = (int) std::min ((size_t) ctrl, L.size() - pos);
                for (auto& b : iir) { int budget = 8; b.control (n, 0.0, budget); b.process (L.data() + pos, R.data() + pos, n); }
            }
        };
        std::vector<double> sL ((size_t) fs / 2, 0.0), sR ((size_t) fs / 2, 0.0);
        runChain (sL, sR);   // settle
        for (int i = 0; i < 5; ++i)
        {
            auto& bs = snap.bands[(size_t) i];
            bs.active = true; bs.place = bands[i].pl;
            bs.shape.shape = bands[i].sh; bs.shape.slope = bands[i].sl; bs.shape.freq = bands[i].f; bs.shape.gainDb = bands[i].g; bs.shape.q = bands[i].q;
            bs.num = iir[(size_t) i].getNumSections();
            for (int s = 0; s < bs.num; ++s)
            {
                bs.second[(size_t) s] = iir[(size_t) i].isSecond (s);
                const auto& c = iir[(size_t) i].getCoef (s);
                bs.coefs[(size_t) (5 * s)] = c.g; bs.coefs[(size_t) (5 * s + 1)] = c.k; bs.coefs[(size_t) (5 * s + 2)] = c.m0;
                bs.coefs[(size_t) (5 * s + 3)] = c.m1; bs.coefs[(size_t) (5 * s + 4)] = c.m2;
            }
            snap.wet[i] = 1.0;
        }
        NaturalPhaseCorrector np; np.configure (fs, 2);
        NPKernelBuilder kb; kb.configure (fs, NaturalPhaseCorrector::kernelLengthFor (fs));
        NPKernelSet* ks = np.beginWriteBack(); kb.build (snap, *ks); np.endWriteBack (true);

        // impulse into L and R separately to recover the 2x2 response
        const int len = 1 << 15;
        double worstMag = 0, worstPh = 0;
        for (int inCh = 0; inCh < 2; ++inCh)
        {
            std::vector<double> L ((size_t) len, 0.0), R ((size_t) len, 0.0);
            // let the kernel cross-fade complete first
            std::vector<float> fl (4096, 0.0f), fr (4096, 0.0f);
            np.process (fl.data(), fr.data(), 4096);
            (inCh == 0 ? L : R)[0] = 1.0;
            runChain (L, R);
            std::vector<float> Lf ((size_t) len), Rf ((size_t) len);
            for (int i = 0; i < len; ++i) { Lf[(size_t) i] = (float) L[(size_t) i]; Rf[(size_t) i] = (float) R[(size_t) i]; }
            np.process (Lf.data(), Rf.data(), len);
            const int D = np.getLatency();
            for (double f : { 100.0, 1000.0, 3000.0, 6000.0, 10000.0, 14000.0, 17000.0, 19000.0 })
            {
                // expected: analog 2x2 response column inCh, times delay D
                cplx m00 (1, 0), m01 (0, 0), m10 (0, 0), m11 (1, 0);
                for (int i = 0; i < 5; ++i)
                {
                    BandShapeParams p; p.shape = bands[i].sh; p.slope = bands[i].sl; p.freq = bands[i].f; p.gainDb = bands[i].g; p.q = bands[i].q;
                    AnalogFilter a; makeAnalogPrototype (p, a);
                    const cplx h = a.eval (f / p.freq);
                    switch (bands[i].pl)
                    {
                        case Placement::Stereo: m00 *= h; m01 *= h; m10 *= h; m11 *= h; break;
                        case Placement::Left: m00 *= h; m01 *= h; break;
                        case Placement::Right: m10 *= h; m11 *= h; break;
                        default:
                        {
                            const cplx pp = 0.5 * (h + 1.0), qq = bands[i].pl == Placement::Mid ? 0.5 * (h - 1.0) : 0.5 * (1.0 - h);
                            const cplx a00 = pp * m00 + qq * m10, a01 = pp * m01 + qq * m11, a10 = qq * m00 + pp * m10, a11 = qq * m01 + pp * m11;
                            m00 = a00; m01 = a01; m10 = a10; m11 = a11;
                        }
                    }
                }
                for (int outCh = 0; outCh < 2; ++outCh)
                {
                    const cplx expect = (outCh == 0 ? (inCh == 0 ? m00 : m01) : (inCh == 0 ? m10 : m11)) * std::polar (1.0, -kTwoPi * f * D / fs);
                    if (std::abs (expect) < 0.05) continue;
                    cplx meas (0, 0);
                    const auto& y = outCh == 0 ? Lf : Rf;
                    for (int n = 0; n < len; ++n) meas += (double) y[(size_t) n] * std::polar (1.0, -kTwoPi * f * n / fs);
                    worstMag = std::max (worstMag, std::abs (gainToDb (std::abs (meas)) - gainToDb (std::abs (expect))));
                    worstPh = std::max (worstPh, std::abs (std::arg (meas / expect)) * 180.0 / kPi);
                }
            }
        }
        const bool ok = worstMag < 0.1 && worstPh < 1.0;
        if (! ok) ++failures;
        std::printf ("%s fs=%g: natural phase chain vs analog: max %.4f dB, %.3f deg (latency %d samples = %.2f ms)\n",
                     ok ? "PASS" : "FAIL", fs, worstMag, worstPh, np.getLatency(), 1000.0 * np.getLatency() / fs);
    }
    std::printf ("%d failures\n", failures);
    return failures;
}
