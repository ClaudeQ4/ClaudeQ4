/*
    Claude-Q 4 - zero latency band processor

    One EQ band realised as a cascade of zero-delay-feedback (TPT) state variable
    sections.  Coefficients are linearly interpolated per sample between control points
    (every ~0.7 ms), which keeps fast automation and dynamic EQ modulation free of zipper
    noise and unconditionally stable.

    - continuous parameters (frequency, gain, Q) are smoothed in the log domain
    - while a band moves, the cheap "matched" design is used; once it settles each section
      is replaced by the accurate least-squares design (a few sections per control block,
      so there are never CPU spikes)
    - dynamic bands look their designs up in a 1 dB gain grid of accurate designs
    - shape changes with an unchanged topology morph the coefficients, other structural
      changes (slope, stereo placement) cross-fade between two filter instances
*/
#pragma once

#include "BandParams.h"

namespace cq
{
    struct SvfRunCoef
    {
        double g = 0.5, k = 1.0, m0 = 1.0, m1 = 0.0, m2 = 0.0;
    };

    class IIRBand
    {
    public:
        static constexpr int kMaxBlock = 256;
        static constexpr int kCacheCells = 32;
        static constexpr int kMaxCacheSections = 8;

        void prepare (double sampleRate, int controlInterval) noexcept;
        void reset() noexcept;

        // once per host block
        void setTarget (const BandParams& p, double gainScale, bool mono) noexcept;

        // once per control block, before process(); n = samples that will follow
        // dynGainDb = dynamic gain offset in dB (already scaled), budget = accurate section designs left
        void control (int n, double dynGainDb, int& budget) noexcept;

        // in place, n <= control interval; R may be nullptr (mono)
        void process (double* L, double* R, int n) noexcept;

        bool isIdle() const noexcept { return idle; }

        // snapshot access (audio thread) for the natural phase corrector / tests
        int  getNumSections() const noexcept           { return slots[(size_t) active].num; }
        bool isSecond (int i) const noexcept           { return slots[(size_t) active].second[(size_t) i]; }
        const SvfRunCoef& getCoef (int i) const noexcept { return slots[(size_t) active].cur[(size_t) i]; }
        Placement getPlacement() const noexcept        { return slots[(size_t) active].place; }
        double getWet() const noexcept                 { return wet; }
        bool isCrossfading() const noexcept            { return xfadeRemaining > 0; }
        bool isSettled() const noexcept;

        // analog prototype parameters matching the coefficients currently running
        BandShapeParams analogShape (double dynGainDb) const noexcept
        {
            const bool dyn = target.isDynamic();
            return currentShape (sGain + (dyn ? dynGainDb : 0.0));
        }

    private:
        struct Slot
        {
            int num = 0;
            Shape shape = Shape::Bell;
            Slope slope = Slope::dB12;
            Placement place = Placement::Stereo;
            std::array<bool, kMaxSections> second {};
            std::array<bool, kMaxSections> accurate {};
            std::array<SvfRunCoef, kMaxSections> cur {}, next {}, tgt {};
            std::array<bool, kMaxSections> moving {};
            std::array<std::array<double, 2>, kMaxSections> st0 {}, st1 {};
            void clearState() noexcept
            {
                for (auto& s : st0) s = { 0.0, 0.0 };
                for (auto& s : st1) s = { 0.0, 0.0 };
            }
        };

        struct CacheCell
        {
            int tag = -100000;
            std::array<SvfRunCoef, kMaxCacheSections> c {};
        };

        void initSlot (Slot& s, const BandParams& p) noexcept;
        void designInto (Slot& s, const AnalogFilter& a, double f0, bool accurateAll, int& budget, bool forceAll,
                         std::array<SvfRunCoef, kMaxSections>& out) noexcept;
        bool cacheLookup (double gainDb, int& budget, std::array<SvfRunCoef, kMaxSections>& out) noexcept;
        void computeCell (int idx, CacheCell& cell) noexcept;
        void runSlot (Slot& s, double* L, double* R, int n) noexcept;
        static void runSection2 (const SvfRunCoef& a, const SvfRunCoef& b, bool interp, std::array<double, 2>& st, double* x, int n) noexcept;
        static void runSection1 (const SvfRunCoef& a, const SvfRunCoef& b, bool interp, std::array<double, 2>& st, double* x, int n) noexcept;
        BandShapeParams currentShape (double gainDb) const noexcept;
        static SvfRunCoef toRun (const SvfCoeffs& c, double gainFold) noexcept;

        double fs = 48000.0;
        int ctrl = 32;
        double smoothAlpha = 0.03;
        int morphBlocksTotal = 30;
        int xfadeTotal = 960;

        BandParams target {};
        double gainScale = 1.0;
        bool mono = false;

        // smoothed continuous parameters
        double sLogF = std::log (1000.0), sGain = 0.0, sLogQ = 0.0;
        double tLogF = std::log (1000.0), tGain = 0.0, tLogQ = 0.0;
        bool smoothing = false;
        bool designDirty = true;
        double lastEffGain = 0.0;
        bool lastWasDynamic = false;

        std::array<Slot, 2> slots {};
        int active = 0;
        int xfadeRemaining = 0;
        bool pendingStructure = false;

        int morphRemaining = 0;

        double wet = 0.0, wetTarget = 0.0, wetStep = 0.0;
        bool idle = true;

        // dynamic gain cache
        std::array<CacheCell, kCacheCells> cache {};
        Shape cacheShape = Shape::Bell; Slope cacheSlope = Slope::dB12;
        double cacheLogF = 0, cacheLogQ = 0; bool cacheGainQ = false; bool cacheValid = false;

        alignas (16) double tmpA[2][kMaxBlock] {};
        alignas (16) double tmpB[2][kMaxBlock] {};
        alignas (16) double dry[2][kMaxBlock] {};
    };
}
