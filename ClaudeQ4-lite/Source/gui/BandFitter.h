/*
    Claude-Q 4 - band fitter (EQ Sketch and EQ Match)

    Finds a small set of bells / shelves whose summed response approximates a target
    curve: greedy band insertion at the largest weighted residual, followed by a
    Levenberg-Marquardt refinement of all band parameters (log frequency, gain, log Q)
    against the exact analog prototypes used by the DSP.
*/
#pragma once

#include "dsp/FilterDesign.h"
#include <vector>

namespace cq
{
    struct FittedBand
    {
        Shape shape = Shape::Bell;
        double freq = 1000.0, gain = 0.0, q = 1.0;
    };

    std::vector<FittedBand> fitResponse (const std::vector<double>& freqs,
                                         const std::vector<double>& targetDb,
                                         const std::vector<double>& weights,
                                         int maxBands,
                                         double toleranceDb = 0.35,
                                         bool allowShelves = true,
                                         double maxQ = 10.0);
}
