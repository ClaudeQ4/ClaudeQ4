/*
    Claude-Q 4 - audio processor
*/
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "Analyzer.h"
#include "MidiLearn.h"
#include "Presets.h"
#include "InstanceRegistry.h"
#include "dsp/EQEngine.h"

namespace cq
{
    class ClaudeQ4AudioProcessor : public juce::AudioProcessor,
                                   private juce::Timer
    {
    public:
        ClaudeQ4AudioProcessor();
        ~ClaudeQ4AudioProcessor() override;

        //======================================================================
        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override;
        bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        using juce::AudioProcessor::processBlock;

        juce::AudioProcessorEditor* createEditor() override;
        bool hasEditor() const override               { return true; }

        const juce::String getName() const override   { return "Claude-Q 4"; }
        bool acceptsMidi() const override             { return true; }
        bool producesMidi() const override            { return false; }
        bool isMidiEffect() const override            { return false; }
        double getTailLengthSeconds() const override;

        int getNumPrograms() override;
        int getCurrentProgram() override;
        void setCurrentProgram (int index) override;
        const juce::String getProgramName (int index) override;
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock& destData) override;
        void setStateInformation (const void* data, int sizeInBytes) override;

        juce::AudioProcessorParameter* getBypassParameter() const override;
        void updateTrackProperties (const TrackProperties& properties) override;

        //======================================================================
        // shared with the editor (message thread unless noted)
        juce::UndoManager undoManager { 4000000, 100 };
        juce::AudioProcessorValueTreeState state;
        ParamTable table;
        EQEngine engine;
        AnalyzerTap tapPre, tapPost, tapSc;           // audio thread -> analyzers
        SpectrumAnalyzer anaPre, anaPost, anaSc;
        MidiLearn midiLearn;
        PresetManager presets;

        // editor settings saved with the session
        juce::var getUiProperty (const juce::Identifier& id, const juce::var& fallback) const;
        void setUiProperty (const juce::Identifier& id, const juce::var& value);
        AnalyzerSettings getAnalyzerSettings() const;
        void setAnalyzerSettings (const AnalyzerSettings& s);

        // runs the analyzers (rate limited, safe to call from several editors)
        void updateAnalyzers (double nowSeconds);

        double getCurrentSampleRate() const noexcept   { return currentRate.load(); }
        bool isSidechainActive() const noexcept        { return scActive.load(); }
        bool isStereo() const noexcept                 { return numChannels.load() > 1; }

        // output meter: peak (linear) since the last call
        float takePeak (int ch) noexcept               { return meterPeak[(size_t) ch].exchange (0.0f); }
        bool takeClip() noexcept                       { return clipFlag.exchange (false); }

        // instance information (instance list, collision detection)
        int getInstanceId() const noexcept             { return instanceId; }
        juce::String getInstanceName() const;
        juce::Colour getInstanceColour() const;
        void setInstanceName (const juce::String& name);

        // A / B comparison
        bool isB() const noexcept                      { return abIsB; }
        void toggleAB();
        void copyToOtherSlot();

        // parameter helpers
        void applyPlainValues (const std::vector<std::pair<juce::RangedAudioParameter*, float>>& values, const juce::String& undoName);
        int parameterIndex (const juce::AudioProcessorParameter* p) const;

        // undo: whole-state snapshots of the (normalised) parameter values
        std::vector<float> captureParameters() const;
        void restoreParameters (const std::vector<float>& values);
        void pushUndoStep (std::vector<float> before, const juce::String& name);

        // solo: band index or -1, audition = listen to the dynamics trigger
        void setSolo (int band, bool audition);
        int getSoloBand() const noexcept               { return soloBand; }

    private:
        void timerCallback() override;
        juce::ValueTree captureParameterState() const;

        int instanceId = 0;
        EngineParams engineParams {}, scratchParams {};
        uint32_t paramVersion = 1;
        bool prepared = false;

        std::atomic<double> currentRate { 48000.0 };
        std::atomic<int> numChannels { 2 };
        std::atomic<bool> scActive { false };
        std::array<std::atomic<float>, 2> meterPeak {};
        std::atomic<bool> clipFlag { false };

        mutable juce::CriticalSection uiLock;
        juce::ValueTree ui { "UI" };

        juce::String trackName;
        std::optional<juce::uint32> trackColour;
        juce::String userName;

        bool abIsB = false;
        juce::ValueTree abOther;

        int soloBand = -1;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClaudeQ4AudioProcessor)
    };
}
