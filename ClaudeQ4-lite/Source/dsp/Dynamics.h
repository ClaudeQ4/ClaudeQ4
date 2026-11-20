/*
    Claude-Q 4 - per band dynamics (dynamic EQ)

    Trigger path:  band limited detector (band / free range, internal or external side
    chain, stereo placement aware) -> RMS follower -> level in dB
    Threshold:     manual, or automatic (tracks the long term level of the trigger signal,
                   so only events that rise above the program level trigger)
    Gain computer: soft knee, fixed 3:1 slope, smoothly limited to the dynamic range
                   (negative range compresses, positive range expands upwards)
    Ballistics:    program dependent - attack follows the band frequency and the steepness
                   of transients, release grows with sustained gain change (less pumping);
                   "Auto" can be overridden by fixed manual attack / release times
*/
#pragma once

#include "BandParams.h"

namespace cq
{
    // simple TPT SVF used for detection / audition (BLT design, stable under modulation)
    struct DetectorSvf
    {
        double g = 0.1, k = 1.41, a1 = 0, a2 = 0, a3 = 0;
        double s1[2] = { 0, 0 }, s2[2] = { 0, 0 };
        int mode = 0;   // 0 bypass, 1 low pass, 2 high pass, 3 band pass (unity peak)

        void set (int m, double f, double q, double fs) noexcept
        {
            mode = m;
            g = std::tan (kPi * clampT (f, 5.0, 0.49 * fs) / fs);
            k = 1.0 / std::max (q, 0.05);
            a1 = 1.0 / (1.0 + g * (g + k)); a2 = g * a1; a3 = g * a2;
        }
        void reset() noexcept { s1[0] = s1[1] = s2[0] = s2[1] = 0.0; }
        inline double tick (double x, int ch) noexcept
        {
            if (mode == 0) return x;
            const double v3 = x - s2[ch];
            const double v1 = a1 * s1[ch] + a2 * v3;
            const double v2 = s2[ch] + a2 * s1[ch] + a3 * v3;
            s1[ch] = 2.0 * v1 - s1[ch];
            s2[ch] = 2.0 * v2 - s2[ch];
            switch (mode)
            {
                case 1: return v2;
                case 2: return x - k * v1 - v2;
                default: return k * v1;
            }
        }
    };

    class BandDynamics
    {
    public:
        void prepare (double sampleRate, int controlInterval) noexcept;
        void reset() noexcept;

        // once per host block
        void setParams (const BandParams& p, double rangeScale) noexcept;

        // detector input for one control block: a and b are the two detector channels
        // (b == nullptr -> single channel).  Returns the dynamic gain offset (dB).
        double processBlock (const double* a, const double* b, int n) noexcept;

        // filtered trigger signal for audition (writes n samples)
        void auditionBlock (const double* a, const double* b, double* out, int n) noexcept;

        double getGainDb() const noexcept       { return gainDb; }
        double getLevelDb() const noexcept      { return levelDb; }
        double getThresholdDb() const noexcept  { return thresholdDb; }
        bool   isActive() const noexcept        { return active; }

    private:
        void updateFilters() noexcept;

        double fs = 48000.0;
        int ctrl = 32;
        BandParams params {};
        double range = 0.0;
        bool active = false;

        DetectorSvf f1, f2, a1f, a2f;
        double detFreq = 1000.0;
        double rmsCoef = 0.99;
        double power[1] = { 0.0 };
        double levelDb = -120.0, prevLevelDb = -120.0;
        double avgDb = -60.0;
        bool avgInit = false;
        double thresholdDb = -30.0;
        double gainDb = 0.0;
        double sustain = 0.0;
        double riseRate = 0.0;

        // cached filter settings to avoid redundant tan() calls
        int lastMode1 = -1, lastMode2 = -1;
        double lastF1 = -1, lastF2 = -1, lastQ1 = -1;
    };
}
