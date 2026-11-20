/*
    Claude-Q 4 - presets
*/
#include "Presets.h"
#include "PluginProcessor.h"

namespace cq
{
    namespace
    {
        PresetBand bell (double f, double g, double q)         { PresetBand b; b.shape = Shape::Bell; b.freq = f; b.gain = g; b.q = q; return b; }
        PresetBand lowShelf (double f, double g, double q = 0.71)  { PresetBand b; b.shape = Shape::LowShelf; b.freq = f; b.gain = g; b.q = q; return b; }
        PresetBand highShelf (double f, double g, double q = 0.71) { PresetBand b; b.shape = Shape::HighShelf; b.freq = f; b.gain = g; b.q = q; return b; }
        PresetBand lowCut (double f, Slope s, double q = 0.71)     { PresetBand b; b.shape = Shape::LowCut; b.freq = f; b.slope = s; b.q = q; return b; }
        PresetBand highCut (double f, Slope s, double q = 0.71)    { PresetBand b; b.shape = Shape::HighCut; b.freq = f; b.slope = s; b.q = q; return b; }
        PresetBand notch (double f, double q)                      { PresetBand b; b.shape = Shape::Notch; b.freq = f; b.q = q; return b; }
        PresetBand bandPass (double f, double q)                   { PresetBand b; b.shape = Shape::BandPass; b.freq = f; b.q = q; return b; }
        PresetBand tiltShelf (double f, double g)                  { PresetBand b; b.shape = Shape::TiltShelf; b.freq = f; b.gain = g; b.q = 0.71; return b; }
        PresetBand flatTilt (double f, double g)                   { PresetBand b; b.shape = Shape::FlatTilt; b.freq = f; b.gain = g; return b; }
        PresetBand dyn (PresetBand b, double range)                { b.dynRange = range; return b; }
        PresetBand spec (PresetBand b, double range, double density) { b.dynRange = range; b.spectral = true; b.density = density; return b; }
        PresetBand on (PresetBand b, Placement p)                  { b.place = p; return b; }

        FactoryPreset make (const char* cat, const char* name, std::vector<PresetBand> bands,
                            ProcessingMode mode = ProcessingMode::ZeroLatency, CharacterMode ch = CharacterMode::Clean,
                            LPResolution res = LPResolution::Medium)
        {
            FactoryPreset p;
            p.category = cat; p.name = name; p.bands = std::move (bands); p.mode = mode; p.character = ch; p.res = res;
            return p;
        }
    }

    const std::vector<FactoryPreset>& PresetManager::factory()
    {
        using S = Slope;
        static const std::vector<FactoryPreset> list =
        {
            make ("", "Default", {}),

            make ("Vocals", "Vocal Presence",       { lowCut (80, S::dB24), bell (320, -2.5, 1.2), bell (3200, 2.5, 0.9), highShelf (11000, 2.0) }),
            make ("Vocals", "Vocal De-Esser",       { lowCut (70, S::dB18), dyn (bell (6800, 0.0, 2.6), -9.0) }),
            make ("Vocals", "Vocal Resonance Tamer",{ lowCut (75, S::dB24), spec (bell (2500, 0.0, 0.35), -6.0, 70.0) }),
            make ("Vocals", "Podcast Voice",        { lowCut (70, S::dB18), lowShelf (180, 1.5), dyn (bell (350, -2.0, 1.4), -4.0), bell (4200, 2.0, 1.0), highCut (16000, S::dB12) }),
            make ("Vocals", "Backing Vocal Tuck",   { lowCut (160, S::dB24), bell (2500, -2.5, 0.8), highShelf (9000, 1.5) }),

            make ("Drums", "Kick Punch",            { lowCut (25, S::dB24), bell (58, 3.0, 1.4), bell (360, -4.5, 1.6), bell (3600, 3.0, 2.0) }),
            make ("Drums", "Snare Snap",            { lowCut (90, S::dB18), bell (210, 2.0, 1.5), bell (850, -2.5, 2.0), bell (5200, 3.0, 1.2) }),
            make ("Drums", "Drum Bus Control",      { lowCut (28, S::dB12), dyn (bell (95, 0.0, 1.2), -4.0), dyn (bell (420, -1.0, 1.0), -3.0), dyn (highShelf (8000, 1.5), -3.0) }),
            make ("Drums", "Overheads Air",         { lowCut (180, S::dB24), bell (480, -2.0, 1.0), highShelf (10000, 3.0, 0.6) }),

            make ("Bass", "Bass Tighten",           { lowCut (32, S::dB24), dyn (bell (85, 2.0, 1.2), -4.0), bell (260, -3.0, 1.3), bell (900, 1.5, 1.5) }),
            make ("Bass", "Sub Control",            { lowCut (24, S::dB48), dyn (lowShelf (70, 0.0, 0.8), -6.0) }),
            make ("Bass", "Bass Growl",             { lowCut (35, S::dB24), bell (120, -1.5, 1.0), bell (1200, 3.5, 1.4), highCut (6500, S::dB24) }),

            make ("Guitars & Keys", "Acoustic Guitar Clarity", { lowCut (90, S::dB18), bell (220, -3.0, 1.2), bell (2800, 1.5, 0.9), highShelf (12000, 2.0) }),
            make ("Guitars & Keys", "Electric Guitar Mud Cut", { lowCut (100, S::dB24), dyn (bell (320, -2.0, 1.4), -4.0), bell (3200, 1.5, 1.5), highCut (12000, S::dB18) }),
            make ("Guitars & Keys", "Piano Brilliance",        { lowCut (40, S::dB12), bell (250, -1.5, 0.9), highShelf (6000, 2.5, 0.7) }),
            make ("Guitars & Keys", "Pad Smoother",            { spec (bell (3000, 0.0, 0.3), -5.0, 40.0), highShelf (14000, -1.5) }),

            make ("Mastering", "Master Air & Weight",    { lowCut (18, S::dB24), lowShelf (60, 1.0, 0.6), highShelf (12000, 1.5, 0.55) }, ProcessingMode::NaturalPhase),
            make ("Mastering", "Master Tilt Warm",       { flatTilt (1000, -1.5) }, ProcessingMode::NaturalPhase),
            make ("Mastering", "Master Tilt Bright",     { flatTilt (1000, 1.5) }, ProcessingMode::NaturalPhase),
            make ("Mastering", "Master Mid/Side Width",  { on (lowCut (120, S::dB12), Placement::Side), on (highShelf (6000, 2.0, 0.6), Placement::Side), on (bell (250, -1.0, 0.8), Placement::Mid) }, ProcessingMode::LinearPhase),
            make ("Mastering", "Master Linear Low Cut",  { lowCut (25, S::Brickwall) }, ProcessingMode::LinearPhase, CharacterMode::Clean, LPResolution::High),
            make ("Mastering", "Master Gentle Glue",     { dyn (bell (180, 0.0, 0.8), -2.0), dyn (bell (3000, 0.0, 0.7), -2.0) }, ProcessingMode::NaturalPhase, CharacterMode::Subtle),

            make ("Creative", "Telephone",           { lowCut (420, S::dB48), highCut (3400, S::dB48), bell (1500, 6.0, 1.0) }),
            make ("Creative", "AM Radio",            { lowCut (240, S::dB36), highCut (5000, S::dB36), bell (1800, 4.0, 0.8) }, ProcessingMode::ZeroLatency, CharacterMode::Warm),
            make ("Creative", "Underwater",          { highCut (450, S::dB48, 1.4) }),
            make ("Creative", "Lo-Fi Warmth",        { lowCut (110, S::dB12), highCut (7000, S::dB24), lowShelf (220, 2.0), tiltShelf (1200, -2.0) }, ProcessingMode::ZeroLatency, CharacterMode::Warm),

            make ("Utility", "Rumble Removal",       { lowCut (22, S::dB48) }),
            make ("Utility", "Hum Notch 50 Hz",      { notch (50, 18), notch (100, 18), notch (150, 18), notch (200, 18), notch (250, 18) }),
            make ("Utility", "Hum Notch 60 Hz",      { notch (60, 18), notch (120, 18), notch (180, 18), notch (240, 18), notch (300, 18) }),
            make ("Utility", "Band Pass 1 kHz",      { bandPass (1000, 1.0) }),
        };
        return list;
    }

    PresetManager::PresetManager (ClaudeQ4AudioProcessor& p) : proc (p) {}

    juce::File PresetManager::getUserFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Claude-Q 4").getChildFile ("Presets");
    }

    juce::Array<juce::File> PresetManager::getUserPresets() const
    {
        auto folder = getUserFolder();
        juce::Array<juce::File> files;
        if (folder.isDirectory())
            for (const auto& entry : juce::RangedDirectoryIterator (folder, true, "*.cq4preset", juce::File::findFiles))
                files.add (entry.getFile());
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
        {
            return a.getFullPathName().compareNatural (b.getFullPathName()) < 0;
        });
        return files;
    }

    void PresetManager::loadDefault() { loadFactory (0); }

    void PresetManager::loadFactory (int index)
    {
        const auto& list = factory();
        if (index < 0 || index >= (int) list.size()) return;
        const auto& fp = list[(size_t) index];
        auto& t = proc.table;
        std::vector<std::pair<juce::RangedAudioParameter*, float>> values;
        auto defaultOf = [] (juce::RangedAudioParameter* p) { return p->convertFrom0to1 (p->getDefaultValue()); };

        for (int b = 0; b < kMaxBands; ++b)
        {
            for (int k = 0; k < kNumBandParams; ++k)
            {
                auto* p = t.band (b, (BandParam) k);
                values.push_back ({ p, defaultOf (p) });
            }
            if (b < (int) fp.bands.size())
            {
                const auto& pb = fp.bands[(size_t) b];
                auto set = [&] (BandParam k, double v) { values[(size_t) (b * kNumBandParams + k)].second = (float) v; };
                set (bpUsed, 1.0);
                set (bpShape, (double) (int) pb.shape);
                set (bpSlope, (double) (int) sanitiseSlope (pb.shape, pb.slope));
                set (bpFreq, pb.freq);
                set (bpGain, pb.gain);
                set (bpQ, pb.q);
                set (bpPlace, (double) (int) pb.place);
                if (std::abs (pb.dynRange) > 1.0e-6)
                {
                    set (bpDynOn, 1.0);
                    set (bpDynRange, pb.dynRange);
                    set (bpAutoThr, pb.autoThr ? 1.0 : 0.0);
                    set (bpThreshold, pb.thr);
                    set (bpSpectral, pb.spectral ? 1.0 : 0.0);
                    set (bpDensity, pb.density);
                }
            }
        }
        auto g = [&] (GlobalParam k, double v) { values.push_back ({ t.global (k), (float) v }); };
        g (gpMode, (double) (int) fp.mode);
        g (gpResolution, (double) (int) fp.res);
        g (gpCharacter, (double) (int) fp.character);
        g (gpOutGain, fp.outGain);
        g (gpOutPan, 0.0);
        g (gpPanMS, 0.0);
        g (gpPhaseInvert, 0.0);
        g (gpAutoGain, fp.autoGain ? 1.0 : 0.0);
        g (gpGainScale, 100.0);
        g (gpGainQ, 0.0);
        proc.applyPlainValues (values, "Load preset");
        currentName = fp.name;
        currentFactory = index;
        currentFile = juce::File();
    }

    bool PresetManager::saveFile (const juce::File& f)
    {
        juce::XmlElement root ("ClaudeQ4Preset");
        root.setAttribute ("name", f.getFileNameWithoutExtension());
        root.setAttribute ("version", 1);
        auto& t = proc.table;
        auto plain = [] (juce::RangedAudioParameter* p) { return p->convertFrom0to1 (p->getValue()); };
        for (int b = 0; b < kMaxBands; ++b)
        {
            if (! t.bandFlag (b, bpUsed)) continue;
            for (int k = 0; k < kNumBandParams; ++k)
            {
                auto* p = t.band (b, (BandParam) k);
                auto* e = root.createNewChildElement ("PARAM");
                e->setAttribute ("id", p->paramID);
                e->setAttribute ("value", plain (p));
            }
        }
        for (int k = 0; k < kNumGlobalParams; ++k)
        {
            if (k == gpBypass) continue;
            auto* p = t.global ((GlobalParam) k);
            auto* e = root.createNewChildElement ("PARAM");
            e->setAttribute ("id", p->paramID);
            e->setAttribute ("value", plain (p));
        }
        f.getParentDirectory().createDirectory();
        if (! root.writeTo (f)) return false;
        currentName = f.getFileNameWithoutExtension();
        currentFile = f;
        currentFactory = -1;
        return true;
    }

    bool PresetManager::loadFile (const juce::File& f)
    {
        auto xml = juce::XmlDocument::parse (f);
        if (xml == nullptr || ! xml->hasTagName ("ClaudeQ4Preset")) return false;
        auto& t = proc.table;
        std::vector<std::pair<juce::RangedAudioParameter*, float>> values;
        std::map<juce::String, float> fileValues;
        for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
            fileValues[e->getStringAttribute ("id")] = (float) e->getDoubleAttribute ("value");
        auto add = [&] (juce::RangedAudioParameter* p)
        {
            auto it = fileValues.find (p->paramID);
            values.push_back ({ p, it != fileValues.end() ? it->second : p->convertFrom0to1 (p->getDefaultValue()) });
        };
        for (int b = 0; b < kMaxBands; ++b)
            for (int k = 0; k < kNumBandParams; ++k) add (t.band (b, (BandParam) k));
        for (int k = 0; k < kNumGlobalParams; ++k)
            if (k != gpBypass) add (t.global ((GlobalParam) k));
        proc.applyPlainValues (values, "Load preset");
        currentName = xml->getStringAttribute ("name", f.getFileNameWithoutExtension());
        currentFile = f;
        currentFactory = -1;
        return true;
    }

    juce::String PresetManager::bandsToText() const
    {
        juce::XmlElement root ("ClaudeQ4Bands");
        auto& t = proc.table;
        for (int b = 0; b < kMaxBands; ++b)
        {
            if (! t.bandFlag (b, bpUsed)) continue;
            for (int k = 0; k < kNumBandParams; ++k)
            {
                auto* p = t.band (b, (BandParam) k);
                auto* e = root.createNewChildElement ("PARAM");
                e->setAttribute ("id", p->paramID);
                e->setAttribute ("value", p->convertFrom0to1 (p->getValue()));
            }
        }
        return root.toString();
    }

    bool PresetManager::bandsFromText (const juce::String& text)
    {
        if (! isBandsText (text)) return false;
        auto xml = juce::XmlDocument::parse (text);
        if (xml == nullptr || ! xml->hasTagName ("ClaudeQ4Bands")) return false;
        std::map<juce::String, float> fileValues;
        for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
            fileValues[e->getStringAttribute ("id")] = (float) e->getDoubleAttribute ("value");
        auto& t = proc.table;
        std::vector<std::pair<juce::RangedAudioParameter*, float>> values;
        for (int b = 0; b < kMaxBands; ++b)
            for (int k = 0; k < kNumBandParams; ++k)
            {
                auto* p = t.band (b, (BandParam) k);
                auto it = fileValues.find (p->paramID);
                values.push_back ({ p, it != fileValues.end() ? it->second : p->convertFrom0to1 (p->getDefaultValue()) });
            }
        proc.applyPlainValues (values, "Paste EQ curve");
        return true;
    }

    void PresetManager::step (int direction)
    {
        const int nf = (int) factory().size();
        const auto user = getUserPresets();
        const int total = nf + user.size();
        if (total <= 0) return;
        int cur = currentFactory;
        if (cur < 0)
        {
            const int ui = user.indexOf (currentFile);
            cur = ui >= 0 ? nf + ui : 0;
        }
        const int next = ((cur + direction) % total + total) % total;
        if (next < nf) loadFactory (next);
        else loadFile (user[next - nf]);
    }
}
