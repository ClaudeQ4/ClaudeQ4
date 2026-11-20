/*
    Claude-Q 4 - parameter definitions
*/
#include "Parameters.h"

namespace cq
{
    namespace
    {
        const char* bandSuffix (BandParam p)
        {
            static const char* s[] = { "used", "enabled", "shape", "slope", "freq", "gain", "q", "place",
                                       "dynOn", "dynRange", "autoThr", "threshold", "autoTimes", "attack", "release",
                                       "extSc", "trigFree", "trigLo", "trigHi", "spectral", "density", "specTilt" };
            static_assert (sizeof (s) / sizeof (s[0]) == kNumBandParams, "band parameter names");
            return s[(int) p];
        }

        const char* bandLongName (BandParam p)
        {
            static const char* s[] = { "Used", "Enabled", "Shape", "Slope", "Frequency", "Gain", "Q", "Stereo Placement",
                                       "Dynamic Mode", "Dynamic Range", "Auto Threshold", "Threshold", "Auto Attack/Release", "Attack", "Release",
                                       "External Side Chain", "Free Trigger Range", "Trigger Low", "Trigger High", "Spectral Mode", "Spectral Density", "Spectral Tilt" };
            static_assert (sizeof (s) / sizeof (s[0]) == kNumBandParams, "band parameter long names");
            return s[(int) p];
        }

        const char* globalSuffix (GlobalParam p)
        {
            static const char* s[] = { "mode", "resolution", "character", "outGain", "outPan", "panMS", "phaseInvert",
                                       "autoGain", "gainScale", "bypass", "gainQ" };
            static_assert (sizeof (s) / sizeof (s[0]) == kNumGlobalParams, "global parameter names");
            return s[(int) p];
        }

        juce::NormalisableRange<float> logRange (float lo, float hi)
        {
            const float ratio = hi / lo;
            juce::NormalisableRange<float> r (lo, hi,
                [lo, ratio] (float, float, float x) { return lo * std::pow (ratio, juce::jlimit (0.0f, 1.0f, x)); },
                [lo, ratio] (float, float, float v) { return juce::jlimit (0.0f, 1.0f, std::log (juce::jmax (v, lo) / lo) / std::log (ratio)); },
                [lo, hi]    (float, float, float v) { return juce::jlimit (lo, hi, v); });
            return r;
        }

        juce::NormalisableRange<float> linRange (float lo, float hi)
        {
            return juce::NormalisableRange<float> (lo, hi);
        }

        juce::String trimNumber (double v, int decimals)
        {
            return juce::String (v, decimals);
        }

        double parseNumberWithSuffix (juce::String t, double fallback)
        {
            t = t.trim().toLowerCase();
            if (t.isEmpty()) return fallback;
            double mult = 1.0;
            if (t.endsWith ("khz")) { mult = 1000.0; t = t.dropLastCharacters (3); }
            else if (t.endsWith ("hz")) { t = t.dropLastCharacters (2); }
            else if (t.endsWith ("k")) { mult = 1000.0; t = t.dropLastCharacters (1); }
            t = t.trim();
            if (! t.containsAnyOf ("0123456789")) return fallback;
            return t.getDoubleValue() * mult;
        }
    }

    juce::String ParamIDs::band (int band, BandParam p)
    {
        return "b" + juce::String (band + 1) + "_" + bandSuffix (p);
    }

    juce::String ParamIDs::global (GlobalParam p)
    {
        return globalSuffix (p);
    }

    juce::StringArray shapeChoices()
    {
        juce::StringArray a;
        for (int i = 0; i < (int) Shape::Count; ++i) a.add (shapeName ((Shape) i));
        return a;
    }

    juce::StringArray slopeChoices()
    {
        juce::StringArray a;
        for (int i = 0; i < (int) Slope::Count; ++i) a.add (slopeName ((Slope) i));
        return a;
    }

    juce::StringArray placementChoices()  { return { "Stereo", "Left", "Right", "Mid", "Side" }; }
    juce::StringArray modeChoices()       { return { "Zero Latency", "Natural Phase", "Linear Phase" }; }
    juce::StringArray resolutionChoices() { return { "Low", "Medium", "High", "Very High", "Maximum" }; }
    juce::StringArray characterChoices()  { return { "Clean", "Subtle", "Warm" }; }

    //==========================================================================
    juce::String formatFreq (double hz, bool withUnit)
    {
        juce::String s;
        if (hz < 100.0)        s = trimNumber (hz, 2);
        else if (hz < 10000.0) s = trimNumber (hz, 1);
        else                   s = trimNumber (hz, 0);
        return withUnit ? s + " Hz" : s;
    }

    juce::String formatFreqShort (double hz)
    {
        if (hz < 1000.0) return juce::String (juce::roundToInt (hz)) + " Hz";
        if (hz < 10000.0) return juce::String (hz / 1000.0, 1) + " kHz";
        return juce::String (hz / 1000.0, hz < 20000.0 ? 1 : 0) + " kHz";
    }

    juce::String formatGain (double db, int decimals)
    {
        if (std::abs (db) < 0.5 * std::pow (10.0, -decimals)) db = 0.0;
        return (db > 0.0 ? "+" : "") + juce::String (db, decimals) + " dB";
    }

    juce::String formatQ (double q)
    {
        if (q < 1.0)  return juce::String (q, 3);
        if (q < 10.0) return juce::String (q, 3);
        return juce::String (q, 2);
    }

    juce::String formatPan (double pan)
    {
        const int pc = juce::roundToInt (pan * 100.0);
        if (pc == 0) return "Center";
        return pc < 0 ? "L" + juce::String (-pc) : "R" + juce::String (pc);
    }

    juce::String formatMs (double ms)
    {
        if (ms < 10.0) return juce::String (ms, 2) + " ms";
        if (ms < 100.0) return juce::String (ms, 1) + " ms";
        if (ms < 1000.0) return juce::String (juce::roundToInt (ms)) + " ms";
        return juce::String (ms / 1000.0, 2) + " s";
    }

    juce::String noteName (double hz, bool withCents)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const double note = freqToNote (hz);
        const int n = (int) std::lround (note);
        const int cents = (int) std::lround ((note - n) * 100.0);
        const int octave = (int) std::floor (n / 12.0) - 1;     // MIDI 60 = C4 = 261.63 Hz
        const int idx = ((n % 12) + 12) % 12;
        juce::String s = juce::String (names[idx]) + juce::String (octave);
        if (withCents && cents != 0) s << (cents > 0 ? " +" : " ") << cents << " ct";
        return s;
    }

    double parseFreq (const juce::String& text, double fallback)
    {
        auto t = text.trim();
        // note names: C4, A#3, Bb2 ...
        if (t.isNotEmpty() && juce::CharacterFunctions::isLetter (t[0]) && ! t.startsWithIgnoreCase ("k"))
        {
            static const int base[] = { 9, 11, 0, 2, 4, 5, 7 };   // a b c d e f g
            const auto c = juce::CharacterFunctions::toLowerCase (t[0]);
            if (c >= 'a' && c <= 'g')
            {
                int semis = base[c - 'a'];
                int pos = 1;
                if (pos < t.length() && t[pos] == '#') { ++semis; ++pos; }
                else if (pos < t.length() && (t[pos] == 'b' || t[pos] == 'B') && pos + 1 < t.length() && (juce::CharacterFunctions::isDigit (t[pos + 1]) || t[pos + 1] == '-')) { --semis; ++pos; }
                const auto rest = t.substring (pos).trim();
                if (rest.isNotEmpty() && (juce::CharacterFunctions::isDigit (rest[0]) || rest[0] == '-'))
                {
                    const int octave = rest.getIntValue();
                    return noteToFreq ((octave + 1) * 12 + semis);
                }
            }
        }
        return parseNumberWithSuffix (t, fallback);
    }

    //==========================================================================
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout layout;

        auto freqAttr = AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return formatFreq (v); })
            .withValueFromStringFunction ([] (const String& t) { return (float) parseFreq (t, 1000.0); })
            .withLabel ("Hz");
        auto gainAttr = AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return formatGain (v); })
            .withValueFromStringFunction ([] (const String& t) { return t.retainCharacters ("+-.0123456789").getFloatValue(); })
            .withLabel ("dB");
        auto qAttr = AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return formatQ (v); })
            .withValueFromStringFunction ([] (const String& t) { return t.retainCharacters (".0123456789").getFloatValue(); });
        auto dbAttr = AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })
            .withValueFromStringFunction ([] (const String& t) { return t.retainCharacters ("+-.0123456789").getFloatValue(); })
            .withLabel ("dB");
        auto msAttr = AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return formatMs (v); })
            .withValueFromStringFunction ([] (const String& t)
            {
                const float v = t.retainCharacters (".0123456789").getFloatValue();
                return t.containsIgnoreCase ("ms") || ! t.containsIgnoreCase ("s") ? v : v * 1000.0f;
            })
            .withLabel ("ms");
        auto pctAttr = AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + "%"; })
            .withValueFromStringFunction ([] (const String& t) { return t.retainCharacters ("+-.0123456789").getFloatValue(); })
            .withLabel ("%");
        auto panAttr = AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return formatPan (v); })
            .withValueFromStringFunction ([] (const String& t)
            {
                const auto s = t.trim().toLowerCase();
                if (s.startsWith ("c")) return 0.0f;
                const float v = s.retainCharacters (".0123456789").getFloatValue() / 100.0f;
                return s.startsWith ("l") || s.startsWith ("-") ? -v : v;
            });

        for (int b = 0; b < kMaxBands; ++b)
        {
            const String pre = "Band " + String (b + 1) + " ";
            auto id = [b] (BandParam p) { return ParameterID { ParamIDs::band (b, p), 1 }; };
            auto nm = [&pre] (BandParam p) { return pre + bandLongName (p); };

            auto group = std::make_unique<AudioProcessorParameterGroup> ("band" + String (b + 1), "Band " + String (b + 1), " | ");
            group->addChild (std::make_unique<AudioParameterBool>   (id (bpUsed), nm (bpUsed), false));
            group->addChild (std::make_unique<AudioParameterBool>   (id (bpEnabled), nm (bpEnabled), true));
            group->addChild (std::make_unique<AudioParameterChoice> (id (bpShape), nm (bpShape), shapeChoices(), 0));
            group->addChild (std::make_unique<AudioParameterChoice> (id (bpSlope), nm (bpSlope), slopeChoices(), 1));
            group->addChild (std::make_unique<AudioParameterFloat>  (id (bpFreq), nm (bpFreq), logRange ((float) kMinFreq, (float) kMaxFreq), 1000.0f, freqAttr));
            group->addChild (std::make_unique<AudioParameterFloat>  (id (bpGain), nm (bpGain), linRange ((float) kMinGain, (float) kMaxGain), 0.0f, gainAttr));
            group->addChild (std::make_unique<AudioParameterFloat>  (id (bpQ), nm (bpQ), logRange ((float) kMinQ, (float) kMaxQ), 1.0f, qAttr));
            group->addChild (std::make_unique<AudioParameterChoice> (id (bpPlace), nm (bpPlace), placementChoices(), 0));
            group->addChild (std::make_unique<AudioParameterBool>   (id (bpDynOn), nm (bpDynOn), false));
            group->addChild (std::make_unique<AudioParameterFloat>  (id (bpDynRange), nm (bpDynRange), linRange (-30.0f, 30.0f), 0.0f, gainAttr));
            group->addChild (std::make_unique<AudioParameterBool>   (id (bpAutoThr), nm (bpAutoThr), true));
            group->addChild (std::make_unique<AudioParameterFloat>  (id (bpThreshold), nm (bpThreshold), linRange (-80.0f, 0.0f), -30.0f, dbAttr));
            group->addChild (std::make_unique<AudioParameterBool>   (id (bpAutoTimes), nm (bpAutoTimes), true));
            group->addChild (std::make_unique<AudioParameterFloat>  (id (bpAttack), nm (bpAttack), logRange (0.1f, 500.0f), 10.0f, msAttr));
            group->addChild (std::make_unique<AudioParameterFloat>  (id (bpRelease), nm (bpRelease), logRange (5.0f, 5000.0f), 120.0f, msAttr));
            group->addChild (std::make_unique<AudioParameterBool>   (id (bpExtSc), nm (bpExtSc), false));
            group->addChild (std::make_unique<AudioParameterBool>   (id (bpTrigFree), nm (bpTrigFree), false));
            group->addChild (std::make_unique<AudioParameterFloat>  (id (bpTrigLo), nm (bpTrigLo), logRange ((float) kMinFreq, (float) kMaxFreq), 20.0f, freqAttr));
            group->addChild (std::make_unique<AudioParameterFloat>  (id (bpTrigHi), nm (bpTrigHi), logRange ((float) kMinFreq, (float) kMaxFreq), 20000.0f, freqAttr));
            group->addChild (std::make_unique<AudioParameterBool>   (id (bpSpectral), nm (bpSpectral), false));
            group->addChild (std::make_unique<AudioParameterFloat>  (id (bpDensity), nm (bpDensity), linRange (0.0f, 100.0f), 50.0f, pctAttr));
            group->addChild (std::make_unique<AudioParameterBool>   (id (bpSpecTilt), nm (bpSpecTilt), true));
            layout.add (std::move (group));
        }

        auto gid = [] (GlobalParam p) { return ParameterID { ParamIDs::global (p), 1 }; };
        auto global = std::make_unique<AudioProcessorParameterGroup> ("global", "Global", " | ");
        global->addChild (std::make_unique<AudioParameterChoice> (gid (gpMode), "Processing Mode", modeChoices(), 0));
        global->addChild (std::make_unique<AudioParameterChoice> (gid (gpResolution), "Processing Resolution", resolutionChoices(), 1));
        global->addChild (std::make_unique<AudioParameterChoice> (gid (gpCharacter), "Character", characterChoices(), 0));
        global->addChild (std::make_unique<AudioParameterFloat>  (gid (gpOutGain), "Output Level", linRange (-36.0f, 36.0f), 0.0f, gainAttr));
        global->addChild (std::make_unique<AudioParameterFloat>  (gid (gpOutPan), "Output Pan", linRange (-1.0f, 1.0f), 0.0f, panAttr));
        global->addChild (std::make_unique<AudioParameterBool>   (gid (gpPanMS), "Mid/Side Pan", false));
        global->addChild (std::make_unique<AudioParameterBool>   (gid (gpPhaseInvert), "Phase Invert", false));
        global->addChild (std::make_unique<AudioParameterBool>   (gid (gpAutoGain), "Auto Gain", false));
        global->addChild (std::make_unique<AudioParameterFloat>  (gid (gpGainScale), "Gain Scale", linRange (-100.0f, 200.0f), 100.0f, pctAttr));
        global->addChild (std::make_unique<AudioParameterBool>   (gid (gpBypass), "Bypass", false));
        global->addChild (std::make_unique<AudioParameterBool>   (gid (gpGainQ), "Gain-Q Interaction", false));
        layout.add (std::move (global));
        return layout;
    }

    //==========================================================================
    void ParamTable::init (juce::AudioProcessorValueTreeState& state)
    {
        for (int b = 0; b < kMaxBands; ++b)
            for (int p = 0; p < kNumBandParams; ++p)
            {
                const auto id = ParamIDs::band (b, (BandParam) p);
                bandParams[(size_t) b][(size_t) p] = state.getParameter (id);
                bandRaw[(size_t) b][(size_t) p] = state.getRawParameterValue (id);
                jassert (bandParams[(size_t) b][(size_t) p] != nullptr && bandRaw[(size_t) b][(size_t) p] != nullptr);
            }
        for (int p = 0; p < kNumGlobalParams; ++p)
        {
            const auto id = ParamIDs::global ((GlobalParam) p);
            globalParams[(size_t) p] = state.getParameter (id);
            globalRaw[(size_t) p] = state.getRawParameterValue (id);
            jassert (globalParams[(size_t) p] != nullptr && globalRaw[(size_t) p] != nullptr);
        }
    }

    void ParamTable::readBand (int b, BandParams& o) const noexcept
    {
        const auto& r = bandRaw[(size_t) b];
        auto v = [&r] (BandParam p) { return (double) r[(size_t) p]->load (std::memory_order_relaxed); };
        auto f = [&r] (BandParam p) { return r[(size_t) p]->load (std::memory_order_relaxed) > 0.5f; };
        auto i = [&r] (BandParam p) { return (int) std::lround (r[(size_t) p]->load (std::memory_order_relaxed)); };
        o.used = f (bpUsed);
        o.enabled = f (bpEnabled);
        o.shape = (Shape) clampT (i (bpShape), 0, (int) Shape::Count - 1);
        o.slope = sanitiseSlope (o.shape, (Slope) clampT (i (bpSlope), 0, (int) Slope::Count - 1));
        o.freq = clampT (v (bpFreq), kMinFreq, kMaxFreq);
        o.gainDb = clampT (v (bpGain), kMinGain, kMaxGain);
        o.q = clampT (v (bpQ), kMinQ, kMaxQ);
        o.place = (Placement) clampT (i (bpPlace), 0, (int) Placement::Count - 1);
        o.dynBypass = ! f (bpDynOn);
        o.dynRange = v (bpDynRange);
        o.autoThreshold = f (bpAutoThr);
        o.thresholdDb = v (bpThreshold);
        o.autoTimes = f (bpAutoTimes);
        o.attackMs = v (bpAttack);
        o.releaseMs = v (bpRelease);
        o.extSidechain = f (bpExtSc);
        o.triggerFree = f (bpTrigFree);
        o.triggerLo = v (bpTrigLo);
        o.triggerHi = v (bpTrigHi);
        o.spectral = f (bpSpectral);
        o.density = clampT (v (bpDensity) / 100.0, 0.0, 1.0);
        o.spectralTilt = f (bpSpecTilt);
        o.gainQ = globalRaw[(size_t) gpGainQ]->load (std::memory_order_relaxed) > 0.5f;
    }

    void ParamTable::readGlobal (GlobalParams& g) const noexcept
    {
        auto v = [this] (GlobalParam p) { return (double) globalRaw[(size_t) p]->load (std::memory_order_relaxed); };
        auto i = [this] (GlobalParam p) { return (int) std::lround (globalRaw[(size_t) p]->load (std::memory_order_relaxed)); };
        g.mode = (ProcessingMode) clampT (i (gpMode), 0, 2);
        g.resolution = (LPResolution) clampT (i (gpResolution), 0, 4);
        g.character = (CharacterMode) clampT (i (gpCharacter), 0, 2);
        g.outputGainDb = v (gpOutGain);
        g.outputPan = clampT (v (gpOutPan), -1.0, 1.0);
        g.panMidSide = v (gpPanMS) > 0.5;
        g.phaseInvert = v (gpPhaseInvert) > 0.5;
        g.autoGain = v (gpAutoGain) > 0.5;
        g.gainScale = v (gpGainScale) / 100.0;
        g.bypass = v (gpBypass) > 0.5;
    }

    void ParamTable::readAll (EngineParams& out) const noexcept
    {
        for (int b = 0; b < kMaxBands; ++b) readBand (b, out.bands[(size_t) b]);
        readGlobal (out.global);
    }

    bool ParamTable::locate (const juce::AudioProcessorParameter* p, int& bandOut, int& whichOut) const noexcept
    {
        for (int b = 0; b < kMaxBands; ++b)
            for (int k = 0; k < kNumBandParams; ++k)
                if (bandParams[(size_t) b][(size_t) k] == p) { bandOut = b; whichOut = k; return true; }
        for (int k = 0; k < kNumGlobalParams; ++k)
            if (globalParams[(size_t) k] == p) { bandOut = -1; whichOut = k; return true; }
        return false;
    }

    namespace
    {
        inline bool eq (double x, double y) noexcept { return juce::exactlyEqual (x, y); }
    }

    bool sameBand (const BandParams& a, const BandParams& b) noexcept
    {
        return a.used == b.used && a.enabled == b.enabled && a.shape == b.shape && a.slope == b.slope
            && eq (a.freq, b.freq) && eq (a.gainDb, b.gainDb) && eq (a.q, b.q) && a.place == b.place && a.gainQ == b.gainQ
            && eq (a.dynRange, b.dynRange) && a.dynBypass == b.dynBypass && a.autoThreshold == b.autoThreshold
            && eq (a.thresholdDb, b.thresholdDb) && a.autoTimes == b.autoTimes && eq (a.attackMs, b.attackMs)
            && eq (a.releaseMs, b.releaseMs) && a.extSidechain == b.extSidechain && a.triggerFree == b.triggerFree
            && eq (a.triggerLo, b.triggerLo) && eq (a.triggerHi, b.triggerHi) && a.spectral == b.spectral
            && eq (a.density, b.density) && a.spectralTilt == b.spectralTilt;
    }

    bool sameGlobal (const GlobalParams& a, const GlobalParams& b) noexcept
    {
        return a.mode == b.mode && a.resolution == b.resolution && a.character == b.character
            && eq (a.outputGainDb, b.outputGainDb) && eq (a.outputPan, b.outputPan) && a.panMidSide == b.panMidSide
            && a.phaseInvert == b.phaseInvert && a.autoGain == b.autoGain && eq (a.gainScale, b.gainScale) && a.bypass == b.bypass;
    }
}
