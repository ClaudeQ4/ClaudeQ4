/*
    Claude-Q 4 - audio processor
*/
#include "PluginProcessor.h"
#include "gui/PluginEditor.h"

namespace cq
{
    namespace
    {
        // one undo step = parameter values before / after an edit
        class ParameterSnapshotAction : public juce::UndoableAction
        {
        public:
            ParameterSnapshotAction (ClaudeQ4AudioProcessor& p, std::vector<float> b, std::vector<float> a)
                : proc (p), before (std::move (b)), after (std::move (a)) {}

            bool perform() override { proc.restoreParameters (after); return true; }
            bool undo() override    { proc.restoreParameters (before); return true; }
            int getSizeInUnits() override { return (int) (before.size() + after.size()) * (int) sizeof (float); }

        private:
            ClaudeQ4AudioProcessor& proc;
            std::vector<float> before, after;
        };

        const juce::Colour instancePalette[] =
        {
            juce::Colour (0xffe8a23b), juce::Colour (0xff57c7c2), juce::Colour (0xffb36cf0), juce::Colour (0xff6cb4f0),
            juce::Colour (0xffef6f6c), juce::Colour (0xff8fd16a), juce::Colour (0xfff0d264), juce::Colour (0xffe57ac0)
        };
    }

    ClaudeQ4AudioProcessor::ClaudeQ4AudioProcessor()
        : AudioProcessor (BusesProperties()
                            .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                            .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                            .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
          state (*this, nullptr, "ClaudeQ4", createParameterLayout()),
          presets (*this)
    {
        table.init (state);
        for (auto& m : meterPeak) m.store (0.0f);
        instanceId = InstanceRegistry::get().add (this);
        table.readAll (engineParams);
        engineParams.version = paramVersion;
        startTimerHz (60);
    }

    ClaudeQ4AudioProcessor::~ClaudeQ4AudioProcessor()
    {
        stopTimer();
        InstanceRegistry::get().remove (this);
    }

    //==========================================================================
    void ClaudeQ4AudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        const int nch = juce::jlimit (1, 2, getMainBusNumOutputChannels());
        numChannels.store (nch);
        currentRate.store (sampleRate);
        table.readAll (engineParams);
        engineParams.version = ++paramVersion;
        engine.prepare (sampleRate, std::max (32, samplesPerBlock), nch, engineParams);
        engine.setNonRealtime (isNonRealtime());
        setLatencySamples (engine.getLatency());
        prepared = true;
    }

    void ClaudeQ4AudioProcessor::releaseResources()
    {
        prepared = false;
    }

    bool ClaudeQ4AudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto out = layouts.getMainOutputChannelSet();
        const auto in = layouts.getMainInputChannelSet();
        if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
        if (in != out) return false;
        if (layouts.inputBuses.size() > 1)
        {
            const auto sc = layouts.getChannelSet (true, 1);
            if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo()) return false;
        }
        return true;
    }

    void ClaudeQ4AudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        juce::ScopedNoDenormals noDenormals;
        const int n = buffer.getNumSamples();
        midiLearn.process (midi);
        if (n <= 0 || ! prepared) return;

        auto main = getBusBuffer (buffer, false, 0);
        const int nch = std::min (2, main.getNumChannels());
        if (nch <= 0) return;

        // parameters -> engine snapshot (version bumped only when something changed)
        table.readAll (scratchParams);
        bool changed = ! sameGlobal (scratchParams.global, engineParams.global);
        for (int b = 0; b < kMaxBands && ! changed; ++b)
            if (! sameBand (scratchParams.bands[(size_t) b], engineParams.bands[(size_t) b])) changed = true;
        if (changed)
        {
            scratchParams.version = ++paramVersion;
            engineParams = scratchParams;
        }
        engine.setNonRealtime (isNonRealtime());
        engine.setParams (engineParams);

        // side chain
        const float* sc[2] = { nullptr, nullptr };
        int numSc = 0;
        if (getBusCount (true) > 1)
            if (auto* bus = getBus (true, 1))
                if (bus->isEnabled() && bus->getNumberOfChannels() > 0)
                {
                    auto scBuf = getBusBuffer (buffer, true, 1);
                    numSc = std::min (2, scBuf.getNumChannels());
                    if (numSc > 0)
                    {
                        sc[0] = scBuf.getReadPointer (0);
                        sc[1] = numSc > 1 ? scBuf.getReadPointer (1) : sc[0];
                    }
                }
        scActive.store (numSc > 0, std::memory_order_relaxed);

        float* io[2] = { main.getWritePointer (0), nch > 1 ? main.getWritePointer (1) : nullptr };
        tapPre.push (io[0], io[1], n);
        engine.process (io, nch, numSc > 0 ? sc : nullptr, numSc, n);
        tapPost.push (io[0], io[1], n);
        if (numSc > 0) tapSc.push (sc[0], numSc > 1 ? sc[1] : nullptr, n);

        for (int c = 0; c < 2; ++c)
        {
            const float pk = main.getMagnitude (std::min (c, nch - 1), 0, n);
            float old = meterPeak[(size_t) c].load (std::memory_order_relaxed);
            while (pk > old && ! meterPeak[(size_t) c].compare_exchange_weak (old, pk)) {}
            if (pk > 1.0f) clipFlag.store (true, std::memory_order_relaxed);
        }

        // offline renders: report latency changes immediately
        if (isNonRealtime() && engine.getLatency() != getLatencySamples())
            setLatencySamples (engine.getLatency());
    }

    void ClaudeQ4AudioProcessor::timerCallback()
    {
        midiLearn.dispatch (*this);
        const int lat = engine.getLatency();
        if (lat != getLatencySamples()) setLatencySamples (lat);
    }

    double ClaudeQ4AudioProcessor::getTailLengthSeconds() const
    {
        return (double) engine.getLatency() / std::max (1.0, currentRate.load()) + 0.25;
    }

    //==========================================================================
    // Presets are handled by the plug-in's own browser.  Exposing them as host programs is risky:
    // some hosts restore a "program" after the session state and would overwrite the user's settings.
    int ClaudeQ4AudioProcessor::getNumPrograms()                 { return 1; }
    int ClaudeQ4AudioProcessor::getCurrentProgram()              { return 0; }
    void ClaudeQ4AudioProcessor::setCurrentProgram (int)         {}
    const juce::String ClaudeQ4AudioProcessor::getProgramName (int) { return presets.getCurrentName(); }

    //==========================================================================
    juce::ValueTree ClaudeQ4AudioProcessor::captureParameterState() const
    {
        return const_cast<juce::AudioProcessorValueTreeState&> (state).copyState();
    }

    void ClaudeQ4AudioProcessor::getStateInformation (juce::MemoryBlock& destData)
    {
        juce::ValueTree root ("ClaudeQ4State");
        root.setProperty ("formatVersion", 1, nullptr);
        root.setProperty ("pluginVersion", "1.0.1", nullptr);
        root.appendChild (captureParameterState(), nullptr);
        {
            const juce::ScopedLock sl (uiLock);
            root.appendChild (ui.createCopy(), nullptr);
            juce::ValueTree ab ("AB");
            ab.setProperty ("isB", abIsB, nullptr);
            if (abOther.isValid()) ab.appendChild (abOther.createCopy(), nullptr);
            root.appendChild (ab, nullptr);
            root.setProperty ("preset", presets.getCurrentName(), nullptr);
            root.setProperty ("instanceName", userName, nullptr);
        }
        root.appendChild (midiLearn.toTree(), nullptr);
        if (auto xml = root.createXml()) copyXmlToBinary (*xml, destData);
    }

    void ClaudeQ4AudioProcessor::setStateInformation (const void* data, int sizeInBytes)
    {
        auto xml = getXmlFromBinary (data, sizeInBytes);
        if (xml == nullptr) return;
        auto root = juce::ValueTree::fromXml (*xml);
        if (root.hasType ("ClaudeQ4State"))
        {
            auto params = root.getChildWithName (state.state.getType());
            if (params.isValid()) state.replaceState (params.createCopy());
            {
                const juce::ScopedLock sl (uiLock);
                auto u = root.getChildWithName ("UI");
                if (u.isValid()) ui = u.createCopy();
                auto ab = root.getChildWithName ("AB");
                abIsB = (bool) ab.getProperty ("isB", false);
                abOther = ab.getNumChildren() > 0 ? ab.getChild (0).createCopy() : juce::ValueTree();
                presets.setCurrentName (root.getProperty ("preset", "Default").toString());
                userName = root.getProperty ("instanceName", "").toString();
            }
            midiLearn.fromTree (root.getChildWithName ("MIDI"));
        }
        else if (root.hasType (state.state.getType()))
        {
            state.replaceState (root);
        }
        undoManager.clearUndoHistory();
        InstanceRegistry::get().bump();
    }

    juce::AudioProcessorParameter* ClaudeQ4AudioProcessor::getBypassParameter() const
    {
        return table.global (gpBypass);
    }

    void ClaudeQ4AudioProcessor::updateTrackProperties (const TrackProperties& properties)
    {
        const juce::ScopedLock sl (uiLock);
        trackName = properties.name.value_or (juce::String());
        trackColour = properties.colourARGB;
        InstanceRegistry::get().bump();
    }

    //==========================================================================
    juce::var ClaudeQ4AudioProcessor::getUiProperty (const juce::Identifier& id, const juce::var& fallback) const
    {
        const juce::ScopedLock sl (uiLock);
        return ui.getProperty (id, fallback);
    }

    void ClaudeQ4AudioProcessor::setUiProperty (const juce::Identifier& id, const juce::var& value)
    {
        const juce::ScopedLock sl (uiLock);
        ui.setProperty (id, value, nullptr);
    }

    AnalyzerSettings ClaudeQ4AudioProcessor::getAnalyzerSettings() const
    {
        AnalyzerSettings s;
        const juce::ScopedLock sl (uiLock);
        s.pre = (bool) ui.getProperty ("anaPre", true);
        s.post = (bool) ui.getProperty ("anaPost", true);
        s.sc = (bool) ui.getProperty ("anaSc", false);
        s.resolution = juce::jlimit (0, 4, (int) ui.getProperty ("anaRes", 2));
        s.speed = juce::jlimit (0, 4, (int) ui.getProperty ("anaSpeed", 2));
        s.tilt = (float) (double) ui.getProperty ("anaTilt", 4.5);
        s.range = (float) (double) ui.getProperty ("anaRange", 90.0);
        s.freeze = (bool) ui.getProperty ("anaFreeze", false);
        s.collisions = (bool) ui.getProperty ("anaCollisions", true);
        s.spectrumGrab = (bool) ui.getProperty ("anaGrab", true);
        return s;
    }

    void ClaudeQ4AudioProcessor::setAnalyzerSettings (const AnalyzerSettings& s)
    {
        const juce::ScopedLock sl (uiLock);
        ui.setProperty ("anaPre", s.pre, nullptr);
        ui.setProperty ("anaPost", s.post, nullptr);
        ui.setProperty ("anaSc", s.sc, nullptr);
        ui.setProperty ("anaRes", s.resolution, nullptr);
        ui.setProperty ("anaSpeed", s.speed, nullptr);
        ui.setProperty ("anaTilt", s.tilt, nullptr);
        ui.setProperty ("anaRange", s.range, nullptr);
        ui.setProperty ("anaFreeze", s.freeze, nullptr);
        ui.setProperty ("anaCollisions", s.collisions, nullptr);
        ui.setProperty ("anaGrab", s.spectrumGrab, nullptr);
    }

    void ClaudeQ4AudioProcessor::updateAnalyzers (double now)
    {
        const auto s = getAnalyzerSettings();
        const double fs = currentRate.load();
        anaPre.update (tapPre, fs, now, s.resolution, s.speed, s.freeze);
        anaPost.update (tapPost, fs, now, s.resolution, s.speed, s.freeze);
        anaSc.update (tapSc, fs, now, s.resolution, s.speed, s.freeze);
    }

    //==========================================================================
    juce::String ClaudeQ4AudioProcessor::getInstanceName() const
    {
        const juce::ScopedLock sl (uiLock);
        if (userName.isNotEmpty()) return userName;
        if (trackName.isNotEmpty()) return trackName;
        return "Claude-Q 4 #" + juce::String (instanceId);
    }

    juce::Colour ClaudeQ4AudioProcessor::getInstanceColour() const
    {
        const juce::ScopedLock sl (uiLock);
        if (trackColour.has_value()) return juce::Colour (*trackColour).withAlpha (1.0f);
        return instancePalette[(size_t) ((instanceId - 1) % 8)];
    }

    void ClaudeQ4AudioProcessor::setInstanceName (const juce::String& name)
    {
        {
            const juce::ScopedLock sl (uiLock);
            userName = name.trim();
        }
        InstanceRegistry::get().bump();
    }

    //==========================================================================
    void ClaudeQ4AudioProcessor::toggleAB()
    {
        auto before = captureParameters();
        juce::ValueTree target;
        {
            const juce::ScopedLock sl (uiLock);
            auto current = captureParameterState();
            target = abOther.isValid() ? abOther : current.createCopy();
            abOther = current;
            abIsB = ! abIsB;
        }
        // apply the other slot
        std::vector<std::pair<juce::RangedAudioParameter*, float>> values;
        for (const auto& child : target)
        {
            if (auto* p = state.getParameter (child.getProperty ("id").toString()))
                if (p != table.global (gpBypass))
                    values.push_back ({ p, (float) (double) child.getProperty ("value") });
        }
        for (const auto& [p, v] : values)
        {
            const float norm = p->convertTo0to1 (v);
            if (std::abs (p->getValue() - norm) < 1.0e-7f) continue;
            p->beginChangeGesture();
            p->setValueNotifyingHost (norm);
            p->endChangeGesture();
        }
        pushUndoStep (std::move (before), abIsB ? "Switch to B" : "Switch to A");
    }

    void ClaudeQ4AudioProcessor::copyToOtherSlot()
    {
        auto current = captureParameterState();
        const juce::ScopedLock sl (uiLock);
        abOther = current;
    }

    void ClaudeQ4AudioProcessor::applyPlainValues (const std::vector<std::pair<juce::RangedAudioParameter*, float>>& values, const juce::String& undoName)
    {
        auto before = captureParameters();
        for (const auto& [p, v] : values)
        {
            const float norm = p->convertTo0to1 (v);
            if (std::abs (p->getValue() - norm) < 1.0e-7f) continue;
            p->beginChangeGesture();
            p->setValueNotifyingHost (norm);
            p->endChangeGesture();
        }
        pushUndoStep (std::move (before), undoName);
    }

    int ClaudeQ4AudioProcessor::parameterIndex (const juce::AudioProcessorParameter* p) const
    {
        return p != nullptr ? p->getParameterIndex() : -1;
    }

    std::vector<float> ClaudeQ4AudioProcessor::captureParameters() const
    {
        const auto& ps = getParameters();
        std::vector<float> v ((size_t) ps.size());
        for (int i = 0; i < ps.size(); ++i) v[(size_t) i] = ps[i]->getValue();
        return v;
    }

    void ClaudeQ4AudioProcessor::restoreParameters (const std::vector<float>& values)
    {
        const auto& ps = getParameters();
        auto* bypass = table.global (gpBypass);
        for (int i = 0; i < ps.size() && i < (int) values.size(); ++i)
        {
            auto* p = ps[i];
            if (p == bypass) continue;
            const float v = values[(size_t) i];
            if (std::abs (p->getValue() - v) < 1.0e-7f) continue;
            p->beginChangeGesture();
            p->setValueNotifyingHost (v);
            p->endChangeGesture();
        }
    }

    void ClaudeQ4AudioProcessor::pushUndoStep (std::vector<float> before, const juce::String& name)
    {
        auto after = captureParameters();
        if (after == before) return;
        undoManager.beginNewTransaction (name);
        undoManager.perform (new ParameterSnapshotAction (*this, std::move (before), std::move (after)), name);
        undoManager.beginNewTransaction();
    }

    void ClaudeQ4AudioProcessor::setSolo (int band, bool audition)
    {
        soloBand = band;
        engine.setSolo (band, audition, 0.0);
    }

    juce::AudioProcessorEditor* ClaudeQ4AudioProcessor::createEditor()
    {
        return new ClaudeQ4Editor (*this);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new cq::ClaudeQ4AudioProcessor();
}
