/*
    Claude-Q 4 - plain parameter snapshots shared between the processor, the DSP engine,
    the background kernel builder and the editor.
*/
#pragma once

#include "FilterDesign.h"

namespace cq
{
    enum class ProcessingMode : int { ZeroLatency = 0, NaturalPhase, LinearPhase };
    enum class LPResolution   : int { Low = 0, Medium, High, VeryHigh, Maximum };
    enum class CharacterMode  : int { Clean = 0, Subtle, Warm };

    struct BandParams
    {
        bool used = false;
        bool enabled = true;
        Shape shape = Shape::Bell;
        Slope slope = Slope::dB12;
        double freq = 1000.0;
        double gainDb = 0.0;
        double q = 1.0;
        Placement place = Placement::Stereo;
        bool gainQ = false;

        // dynamics
        double dynRange = 0.0;       // dB, + expands, - compresses, 0 = static band
        bool dynBypass = false;
        bool autoThreshold = true;
        double thresholdDb = -30.0;
        bool autoTimes = true;       // program dependent attack / release
        double attackMs = 10.0;      // manual override (autoTimes == false)
        double releaseMs = 120.0;
        bool extSidechain = false;
        bool triggerFree = false;
        double triggerLo = 20.0;
        double triggerHi = 20000.0;

        // spectral dynamics
        bool spectral = false;
        double density = 0.5;        // 0..1
        bool spectralTilt = true;

        bool isActive() const noexcept        { return used && enabled; }
        bool isDynamic() const noexcept       { return isActive() && shapeSupportsDynamics (shape) && std::abs (dynRange) > 1.0e-3 && ! dynBypass; }
        bool isSpectral() const noexcept      { return isDynamic() && spectral; }

        BandShapeParams shapeParams (double gainScale, double dynGainDb = 0.0) const noexcept
        {
            BandShapeParams s;
            s.shape = shape; s.slope = slope; s.freq = freq; s.q = q; s.gainQ = gainQ;
            s.gainDb = shapeUsesGain (shape) ? gainDb * gainScale + dynGainDb : 0.0;
            return s;
        }
    };

    struct GlobalParams
    {
        ProcessingMode mode = ProcessingMode::ZeroLatency;
        LPResolution resolution = LPResolution::Medium;
        CharacterMode character = CharacterMode::Clean;
        double outputGainDb = 0.0;
        double outputPan = 0.0;      // -1..1
        bool panMidSide = false;
        bool phaseInvert = false;
        bool autoGain = false;
        double gainScale = 1.0;      // -1..2
        bool bypass = false;
    };

    struct EngineParams
    {
        std::array<BandParams, kMaxBands> bands {};
        GlobalParams global {};
        uint32_t version = 0;
    };

    inline int lpKernelLength (LPResolution r, double fs) noexcept
    {
        static const int base[] = { 4096, 8192, 16384, 32768, 131072 };
        int len = base[clampT ((int) r, 0, 4)];
        int mult = 1;
        if (fs > 50000.0)  mult = 2;
        if (fs > 100000.0) mult = 4;
        if (fs > 200000.0) mult = 8;
        return len * mult;
    }

    inline int lpPartitionSize (double fs) noexcept
    {
        int p = 1024;
        if (fs > 50000.0)  p = 2048;
        if (fs > 100000.0) p = 4096;
        if (fs > 200000.0) p = 8192;
        return p;
    }
}
