/*
    Claude-Q 4 - presets (factory presets compiled in, user presets as XML files)
*/
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"

namespace cq
{
    class ClaudeQ4AudioProcessor;

    struct PresetBand
    {
        Shape shape = Shape::Bell;
        double freq = 1000.0, gain = 0.0, q = 1.0;
        Slope slope = Slope::dB12;
        Placement place = Placement::Stereo;
        double dynRange = 0.0;        // != 0 -> dynamic band
        bool spectral = false;
        double density = 50.0;
        bool autoThr = true;
        double thr = -30.0;
    };

    struct FactoryPreset
    {
        const char* category;
        const char* name;
        std::vector<PresetBand> bands;
        ProcessingMode mode = ProcessingMode::ZeroLatency;
        LPResolution res = LPResolution::Medium;
        CharacterMode character = CharacterMode::Clean;
        double outGain = 0.0;
        bool autoGain = false;
    };

    class PresetManager
    {
    public:
        explicit PresetManager (ClaudeQ4AudioProcessor& p);

        static const std::vector<FactoryPreset>& factory();

        void loadFactory (int index);
        void loadDefault();
        bool loadFile (const juce::File& f);
        bool saveFile (const juce::File& f);

        static juce::File getUserFolder();
        juce::Array<juce::File> getUserPresets() const;

        // previous / next over factory + user presets
        void step (int direction);

        // EQ curve copy / paste (all bands as XML text)
        juce::String bandsToText() const;
        bool bandsFromText (const juce::String& text);
        static bool isBandsText (const juce::String& text) { return text.trimStart().startsWith ("<?xml") && text.contains ("ClaudeQ4Bands"); }

        juce::String getCurrentName() const   { return currentName; }
        void setCurrentName (const juce::String& n) { currentName = n; }
        int getCurrentFactoryIndex() const    { return currentFactory; }

    private:
        ClaudeQ4AudioProcessor& proc;
        juce::String currentName { "Default" };
        int currentFactory = 0;
        juce::File currentFile;
    };
}
