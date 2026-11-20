/*
    Claude-Q 4 - parameter definitions

    24 bands x 22 parameters + global parameters, all automatable.  The DSP engine gets a
    plain EngineParams snapshot built from the raw parameter atomics once per block.
*/
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/BandParams.h"

namespace cq
{
    enum BandParam : int
    {
        bpUsed = 0, bpEnabled, bpShape, bpSlope, bpFreq, bpGain, bpQ, bpPlace,
        bpDynOn, bpDynRange, bpAutoThr, bpThreshold, bpAutoTimes, bpAttack, bpRelease,
        bpExtSc, bpTrigFree, bpTrigLo, bpTrigHi, bpSpectral, bpDensity, bpSpecTilt,
        kNumBandParams
    };

    enum GlobalParam : int
    {
        gpMode = 0, gpResolution, gpCharacter, gpOutGain, gpOutPan, gpPanMS, gpPhaseInvert,
        gpAutoGain, gpGainScale, gpBypass, gpGainQ,
        kNumGlobalParams
    };

    namespace ParamIDs
    {
        juce::String band (int band, BandParam p);   // e.g. "b1_freq" (bands are 0 based here, 1 based in the ID)
        juce::String global (GlobalParam p);
    }

    juce::StringArray shapeChoices();
    juce::StringArray slopeChoices();
    juce::StringArray placementChoices();
    juce::StringArray modeChoices();
    juce::StringArray resolutionChoices();
    juce::StringArray characterChoices();

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // value <-> text helpers (shared by the host strings and the editor)
    juce::String formatFreq (double hz, bool withUnit = true);
    juce::String formatFreqShort (double hz);           // "235 Hz", "1.6 kHz"
    juce::String formatGain (double db, int decimals = 2);
    juce::String formatQ (double q);
    juce::String formatPan (double pan);
    juce::String formatMs (double ms);
    juce::String noteName (double hz, bool withCents);  // "C4", "A#3 +12 ct"
    double parseFreq (const juce::String& text, double fallback);

    //==========================================================================
    class ParamTable
    {
    public:
        void init (juce::AudioProcessorValueTreeState& state);

        juce::RangedAudioParameter* band (int b, BandParam p) const noexcept   { return bandParams[(size_t) b][(size_t) p]; }
        juce::RangedAudioParameter* global (GlobalParam p) const noexcept      { return globalParams[(size_t) p]; }

        float bandValue (int b, BandParam p) const noexcept   { return bandRaw[(size_t) b][(size_t) p]->load (std::memory_order_relaxed); }
        float globalValue (GlobalParam p) const noexcept      { return globalRaw[(size_t) p]->load (std::memory_order_relaxed); }
        bool  bandFlag (int b, BandParam p) const noexcept    { return bandValue (b, p) > 0.5f; }
        int   bandIndex (int b, BandParam p) const noexcept   { return juce::roundToInt (bandValue (b, p)); }

        void readBand (int b, BandParams& out) const noexcept;
        void readGlobal (GlobalParams& out) const noexcept;
        void readAll (EngineParams& out) const noexcept;    // leaves out.version untouched

        // reverse lookup: which band / band parameter does a parameter belong to (-1 if global)
        bool locate (const juce::AudioProcessorParameter* p, int& bandOut, int& whichOut) const noexcept;

    private:
        std::array<std::array<juce::RangedAudioParameter*, kNumBandParams>, kMaxBands> bandParams {};
        std::array<std::array<std::atomic<float>*, kNumBandParams>, kMaxBands> bandRaw {};
        std::array<juce::RangedAudioParameter*, kNumGlobalParams> globalParams {};
        std::array<std::atomic<float>*, kNumGlobalParams> globalRaw {};
    };

    bool sameBand (const BandParams& a, const BandParams& b) noexcept;
    bool sameGlobal (const GlobalParams& a, const GlobalParams& b) noexcept;
}
