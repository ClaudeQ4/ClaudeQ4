/*
    Claude-Q 4 - filter design

    Every band is described by an analog prototype (cascade of first/second order
    sections, normalised so that the band frequency is s = j).  The analog prototype is
    the single source of truth:
      - the display and the linear / natural phase engines evaluate it exactly,
      - the zero latency engine derives "magnitude matched" (de-cramped) digital
        sections from it (impulse invariant poles + numerators matched at DC, Nyquist
        and the section frequency), realised as TPT state variable filters.
*/
#pragma once

#include "DspCommon.h"

namespace cq
{
    enum class Shape : int
    {
        Bell = 0, LowShelf, LowCut, HighShelf, HighCut, Notch, BandPass, TiltShelf, FlatTilt, AllPass,
        Count
    };

    enum class Slope : int
    {
        dB6 = 0, dB12, dB18, dB24, dB30, dB36, dB48, dB72, dB96, Brickwall,
        Count
    };

    enum class Placement : int { Stereo = 0, Left, Right, Mid, Side, Count };

    const char* shapeName (Shape s) noexcept;
    const char* slopeName (Slope s) noexcept;
    const char* placementName (Placement p) noexcept;
    const char* placementShortName (Placement p) noexcept;

    int  slopeOrder (Slope s) noexcept;                  // 1..16 (brickwall -> 0)
    bool shapeUsesGain (Shape s) noexcept;               // Bell, shelves, tilt, flat tilt
    bool shapeSupportsDynamics (Shape s) noexcept;       // same as gain shapes
    bool shapeUsesQ (Shape s, Slope slope) noexcept;
    bool shapeUsesSlope (Shape s) noexcept;
    bool slopeValidForShape (Shape s, Slope slope) noexcept;
    Slope sanitiseSlope (Shape s, Slope slope) noexcept; // nearest valid slope
    double gainQInteractionQ (double q, double gainDb) noexcept;
    double gainQInteractionGain (double q, double gainDb) noexcept;

    //==============================================================================
    struct AnalogSection
    {
        enum Kind : uint8_t { General = 0, Lowpass, Highpass, Bandpass, Notch, Allpass };

        // H(s) = (n2 s^2 + n1 s + n0) / (s^2 + d1 s + d0)   (second order)
        // H(s) = (n1 s + n0) / (s + d0)                        (first order, n2 = 0)
        double n2 = 0, n1 = 0, n0 = 1, d1 = 0, d0 = 1;
        bool second = true;
        Kind kind = General;
        bool pairWithNext = false;   // first order section that is recombined with the next one
                                     // into a single digital biquad (overdamped split)

        cplx eval (cplx s) const noexcept
        {
            if (second) return (n2 * s * s + n1 * s + n0) / (s * s + d1 * s + d0);
            return (n1 * s + n0) / (s + d0);
        }
        double magSq (double w) const noexcept  // |H(jw)|^2 at normalised frequency w
        {
            if (second)
            {
                const double w2 = w * w;
                const double nr = n0 - n2 * w2, ni = n1 * w;
                const double dr = d0 - w2,      di = d1 * w;
                return (nr * nr + ni * ni) / (dr * dr + di * di);
            }
            const double nr = n0, ni = n1 * w, dr = d0, di = w;
            return (nr * nr + ni * ni) / (dr * dr + di * di);
        }
    };

    constexpr int kMaxSections = 20;

    struct AnalogFilter
    {
        std::array<AnalogSection, kMaxSections> sec {};
        int num = 0;
        double gain = 1.0;            // overall gain factor
        bool bilinear = false;        // use prewarped bilinear transform (elliptic brickwall)
        bool bilinearHighpass = false;
        bool identity = true;

        void clear() noexcept { num = 0; gain = 1.0; bilinear = false; bilinearHighpass = false; identity = true; }
        void add (const AnalogSection& s) noexcept { if (num < kMaxSections) sec[(size_t) num++] = s; identity = false; }

        cplx eval (double w) const noexcept   // response at normalised frequency w = f / f0
        {
            cplx h (gain, 0.0);
            const cplx s (0.0, w);
            for (int i = 0; i < num; ++i) h *= sec[(size_t) i].eval (s);
            return h;
        }
        double magSq (double w) const noexcept
        {
            double m = gain * gain;
            for (int i = 0; i < num; ++i) m *= sec[(size_t) i].magSq (w);
            return m;
        }
    };

    //==============================================================================
    struct BandShapeParams
    {
        Shape shape = Shape::Bell;
        Slope slope = Slope::dB12;
        double freq = 1000.0;
        double gainDb = 0.0;
        double q = 1.0;
        bool gainQ = false;
    };

    // Builds the analog prototype (normalised to the band frequency).
    void makeAnalogPrototype (const BandShapeParams& p, AnalogFilter& out) noexcept;

    // Flat tilt works on absolute frequencies, so it needs the band frequency.
    // (makeAnalogPrototype handles it using p.freq.)

    //==============================================================================
    struct DigitalSection
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        bool second = true;
        // numerically precise auxiliaries (avoid cancellation for very low frequencies)
        double pdc = 1, pny = 1;     // 1 + a1 + a2,  1 - a1 + a2   (first order: 1 + a1, 1 - a1)
        double hdc = 1, hny = 1;     // response at DC and at Nyquist
        double bdiff = 0;            // b0 - b2
        double omA2 = 1;             // 1 - a2

        cplx eval (double w) const noexcept
        {
            const cplx z1 = std::polar (1.0, -w);
            if (second)
            {
                const cplx z2 = z1 * z1;
                return (b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2);
            }
            return (b0 + b1 * z1) / (1.0 + a1 * z1);
        }
    };

    // Zero-delay-feedback (TPT) state variable realisation of a digital section.
    // second order:  y = m0*x + m1*bp + m2*lp      (g, k)
    // first order :  y = m0*x + m1*lp              (G = g/(1+g) stored in g)
    struct SvfCoeffs
    {
        double g = 0.5, k = 1.0, m0 = 1.0, m1 = 0.0, m2 = 0.0;
        bool second = true;
    };

    struct DigitalFilter
    {
        std::array<DigitalSection, kMaxSections> sec {};
        std::array<SvfCoeffs, kMaxSections> svf {};
        int num = 0;
        double gain = 1.0;

        cplx eval (double w) const noexcept
        {
            cplx h (gain, 0.0);
            for (int i = 0; i < num; ++i) h *= sec[(size_t) i].eval (w);
            return h;
        }
    };

    // Digital design for the zero latency engine.  f0 = band frequency, fs = sample rate.
    // accurate = true : least squares rational fit (~5-10 us per section), within ~0.1 dB of
    //                   the analog prototype across most of the band
    // accurate = false: matched poles + 3 point numerator (~0.2 us per section), used while
    //                   parameters move; exact at low/mid frequencies, approximate near Nyquist
    void designDigital (const AnalogFilter& analog, double f0, double fs, DigitalFilter& out, bool accurate = true) noexcept;

    void setDesignMethodForTesting (int m) noexcept;

    // Per section design (the digital topology depends only on shape and slope):
    int  digitalSectionCount (const AnalogFilter& analog) noexcept;
    bool digitalSectionIsSecond (const AnalogFilter& analog, int digitalIndex) noexcept;
    void designSection (const AnalogFilter& analog, int digitalIndex, double f0, double fs, bool accurate, DigitalSection& out) noexcept;

    // Biquad -> SVF parameters
    SvfCoeffs toSvf (const DigitalSection& d) noexcept;

    // Elliptic low pass prototype (passband edge = 1 rad/s) - cached, thread safe after first call
    struct EllipticPrototype
    {
        int order = 0;
        std::array<cplx, 16> poles {};   // upper half plane poles (+ real pole for odd orders)
        std::array<cplx, 16> zeros {};   // upper half plane zeros (on j axis)
        int numPairs = 0;
        bool hasRealPole = false;
        double realPole = 0;
        double h0 = 1;
    };
    const EllipticPrototype& getBrickwallPrototype() noexcept;
}
