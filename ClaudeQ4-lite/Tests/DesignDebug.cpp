#include "../Source/dsp/FilterDesign.h"
#include <cstdio>
#include <cstdlib>
using namespace cq;

int main (int argc, char** argv)
{
    BandShapeParams p;
    p.shape = (Shape) std::atoi (argv[1]);
    p.slope = (Slope) std::atoi (argv[2]);
    p.freq = std::atof (argv[3]);
    p.q = std::atof (argv[4]);
    p.gainDb = std::atof (argv[5]);
    const double fs = std::atof (argv[6]);
    AnalogFilter a; makeAnalogPrototype (p, a);
    DigitalFilter d; designDigital (a, p.freq, fs, d);
    std::printf ("sections %d gain %g\n", a.num, a.gain);
    for (int i = 0; i < a.num; ++i)
    {
        const auto& s = a.sec[i]; const auto& ds = d.sec[i]; const auto& c = d.svf[i];
        std::printf (" A%d: %s n=[%g %g %g] d=[1 %g %g] kind %d\n", i, s.second ? "2nd" : "1st", s.n2, s.n1, s.n0, s.d1, s.d0, (int) s.kind);
        std::printf ("   D: b=[%g %g %g] a=[1 %g %g] pdc %g pny %g hdc %g hny %g bdiff %g omA2 %g\n", ds.b0, ds.b1, ds.b2, ds.a1, ds.a2, ds.pdc, ds.pny, ds.hdc, ds.hny, ds.bdiff, ds.omA2);
        std::printf ("   S: g %g k %g m %g %g %g\n", c.g, c.k, c.m0, c.m1, c.m2);
    }
    const double fr[] = { 20, 100, 500, 1000, 2000, 5000, 10000, 15000, 18000, 20000, 21000, 22000, 23000, 30000, 40000, 47000, 60000, 90000 };
    for (double f : fr)
    {
        if (f >= fs / 2) break;
        const double ma = gainToDb (std::sqrt (a.magSq (f / p.freq)));
        const double md = gainToDb (std::abs (d.eval (kTwoPi * f / fs)));
        std::printf ("  f=%7.0f analog %9.3f  digital %9.3f  err %8.3f\n", f, ma, md, md - ma);
    }
    return 0;
}
