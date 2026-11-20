/*
    Claude-Q 4 - header and bottom bars
*/
#include "Bars.h"
#include "EQDisplay.h"

namespace cq
{
    void ABButton::paintButton (juce::Graphics& g, bool over, bool)
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        if (over)
        {
            g.setColour (theme::controlHover);
            g.fillRoundedRectangle (r, 4.0f);
        }
        const float h = r.getHeight();
        g.setFont (theme::font (h * 0.52f, 2));
        auto left = r.removeFromLeft (r.getWidth() * 0.42f);
        auto right = r.removeFromRight (r.getWidth() * 0.72f);
        g.setColour (proc.isB() ? theme::textFaint : theme::accent);
        g.drawText ("A", left, juce::Justification::centredRight, false);
        g.setColour (theme::textFaint);
        g.drawText ("/", r, juce::Justification::centred, false);
        g.setColour (proc.isB() ? theme::accent : theme::textFaint);
        g.drawText ("B", right, juce::Justification::centredLeft, false);
    }

    //==========================================================================
    HeaderBar::HeaderBar (EditorModel& m) : proc (m.proc), abBtn (m.proc)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &undoBtn, &redoBtn, &abBtn, &copyBtn, &prevPreset, &nextPreset,
                                                                  &presetBtn, &instancesBtn, &helpBtn, &settingsBtn })
            addAndMakeVisible (c);
        copyBtn.drawFrame = false;
        instancesBtn.drawFrame = helpBtn.drawFrame = false;
        presetBtn.drawFrame = false;
        presetBtn.fontSize = 14.0f;
        abBtn.setTooltip ("A/B comparison: switch between two complete settings");
        copyBtn.setTooltip ("Copy the current settings to the other A/B slot");
        presetBtn.setTooltip ("Presets: factory presets, your own presets, save / load");
        instancesBtn.setTooltip ("See and edit every Claude-Q 4 instance in this session");
        helpBtn.setTooltip ("Help: mouse & keyboard reference and feature guide");

        undoBtn.onClick = [this] { proc.undoManager.undo(); };
        redoBtn.onClick = [this] { proc.undoManager.redo(); };
        abBtn.onClick = [this] { proc.toggleAB(); };
        copyBtn.getText = [this] { return juce::String (proc.isB() ? "Copy B > A" : "Copy A > B"); };
        copyBtn.onClick = [this] { proc.copyToOtherSlot(); };
        prevPreset.onClick = [this] { proc.presets.step (-1); if (onPresetChanged) onPresetChanged(); };
        nextPreset.onClick = [this] { proc.presets.step (1); if (onPresetChanged) onPresetChanged(); };
        presetBtn.getText = [this] { return proc.presets.getCurrentName(); };
        presetBtn.onClick = [this] { showPresetMenu(); };
        instancesBtn.onClick = [this] { if (onInstances) onInstances(); };
        helpBtn.onClick = [this] { if (onHelp) onHelp(); };
        settingsBtn.onClick = [this] { if (onSettings) onSettings(); };
    }

    void HeaderBar::refresh()
    {
        const bool cu = proc.undoManager.canUndo(), cr = proc.undoManager.canRedo();
        if (cu != lastUndo) { lastUndo = cu; undoBtn.setEnabled (cu); }
        if (cr != lastRedo) { lastRedo = cr; redoBtn.setEnabled (cr); }
        abBtn.refresh();
        copyBtn.refresh();
        presetBtn.refresh();
    }

    void HeaderBar::showPresetMenu()
    {
        juce::PopupMenu m;
        const auto& list = PresetManager::factory();
        const int cur = proc.presets.getCurrentFactoryIndex();
        m.addItem ("Default", true, cur == 0, [this] { proc.presets.loadFactory (0); if (onPresetChanged) onPresetChanged(); });
        juce::StringArray cats;
        for (size_t i = 1; i < list.size(); ++i) cats.addIfNotAlreadyThere (list[i].category);
        for (const auto& cat : cats)
        {
            juce::PopupMenu sub;
            for (size_t i = 1; i < list.size(); ++i)
                if (cat == list[i].category)
                    sub.addItem (list[i].name, true, cur == (int) i, [this, i] { proc.presets.loadFactory ((int) i); if (onPresetChanged) onPresetChanged(); });
            m.addSubMenu (cat, sub, true, nullptr, false);
        }
        m.addSeparator();
        juce::PopupMenu user;
        const auto files = proc.presets.getUserPresets();
        for (const auto& f : files)
            user.addItem (f.getFileNameWithoutExtension(), true, proc.presets.getCurrentName() == f.getFileNameWithoutExtension() && cur < 0,
                          [this, f] { proc.presets.loadFile (f); if (onPresetChanged) onPresetChanged(); });
        m.addSubMenu ("User presets", user, ! files.isEmpty());
        m.addItem ("Save preset as...", [this] { savePresetAs(); });
        m.addItem ("Load preset file...", [this]
        {
            chooser = std::make_unique<juce::FileChooser> ("Load Claude-Q 4 preset", PresetManager::getUserFolder(), "*.cq4preset");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
            {
                const auto f = fc.getResult();
                if (f.existsAsFile()) { proc.presets.loadFile (f); if (onPresetChanged) onPresetChanged(); }
            });
        });
        m.addItem ("Show presets folder", [] { auto d = PresetManager::getUserFolder(); d.createDirectory(); d.revealToUser(); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetBtn).withMinimumWidth (presetBtn.getWidth()).withDeletionCheck (*this));
    }

    void HeaderBar::savePresetAs()
    {
        auto folder = PresetManager::getUserFolder();
        folder.createDirectory();
        chooser = std::make_unique<juce::FileChooser> ("Save Claude-Q 4 preset", folder.getChildFile (proc.presets.getCurrentName() + ".cq4preset"), "*.cq4preset");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f != juce::File()) proc.presets.saveFile (f.withFileExtension ("cq4preset"));
            presetBtn.refresh();
        });
    }

    void HeaderBar::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        juce::ColourGradient bg (juce::Colour (0xff15161d), 0.0f, 0.0f, theme::barBg, 0.0f, r.getHeight(), false);
        g.setGradientFill (bg);
        g.fillRect (r);
        g.setColour (theme::barLine);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, r.getWidth());
        theme::drawLogo (g, logoArea);
        // preset field background
        auto pr = juce::Rectangle<float> ((float) prevPreset.getX(), (float) presetBtn.getY(), (float) (nextPreset.getRight() - prevPreset.getX()), (float) presetBtn.getHeight());
        g.setColour (juce::Colour (0xff0d0e13));
        g.fillRoundedRectangle (pr, 4.0f * uiScale);
        g.setColour (theme::barLine);
        g.drawRoundedRectangle (pr.reduced (0.5f), 4.0f * uiScale, 1.0f);
    }

    void HeaderBar::resized()
    {
        const float s = uiScale;
        auto r = getLocalBounds().toFloat().reduced (10.0f * s, 5.0f * s);
        logoArea = r.removeFromLeft (150.0f * s).withSizeKeepingCentre (150.0f * s, 22.0f * s);
        auto place = [] (juce::Component& c, juce::Rectangle<float> a) { c.setBounds (a.toNearestInt()); };
        place (settingsBtn, r.removeFromRight (28.0f * s));
        r.removeFromRight (4.0f * s);
        place (helpBtn, r.removeFromRight (52.0f * s));
        place (instancesBtn, r.removeFromRight (84.0f * s));

        auto c = r.reduced (16.0f * s, 0.0f);
        const float presetW = juce::jlimit (160.0f * s, 300.0f * s, c.getWidth() - 260.0f * s);
        const float total = 28.0f * s * 2.0f + 14.0f * s + 54.0f * s + 86.0f * s + 14.0f * s + 24.0f * s * 2.0f + presetW;
        auto g = c.withSizeKeepingCentre (std::min (total, c.getWidth()), c.getHeight());
        place (undoBtn, g.removeFromLeft (28.0f * s));
        place (redoBtn, g.removeFromLeft (28.0f * s));
        g.removeFromLeft (14.0f * s);
        place (abBtn, g.removeFromLeft (54.0f * s));
        place (copyBtn, g.removeFromLeft (86.0f * s));
        g.removeFromLeft (14.0f * s);
        place (prevPreset, g.removeFromLeft (24.0f * s));
        place (presetBtn, g.removeFromLeft (presetW));
        place (nextPreset, g.removeFromLeft (24.0f * s));
        for (auto* b : { &copyBtn, &presetBtn, &instancesBtn, &helpBtn }) b->fontSize = 13.0f * s;
        presetBtn.fontSize = 14.0f * s;
    }

    //==========================================================================
    BottomBar::BottomBar (EditorModel& m) : model (m), proc (m.proc)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &midiBtn, &midiMenuBtn, &modeBtn, &resBtn, &charBtn, &instanceBtn, &analyzerBtn,
                                                                  &autoGainBtn, &phaseBtn, &bypassBtn, &panVal, &scaleVal, &outVal })
            addAndMakeVisible (c);
        for (auto* b : { &midiBtn, &modeBtn, &resBtn, &charBtn, &instanceBtn, &analyzerBtn }) b->drawFrame = false;
        modeBtn.showChevron = resBtn.showChevron = charBtn.showChevron = analyzerBtn.showChevron = true;
        midiBtn.setTooltip ("MIDI Learn: click, then touch a control and move a MIDI controller");
        modeBtn.setTooltip ("Processing mode: Zero Latency / Natural Phase (analog phase) / Linear Phase");
        resBtn.setTooltip ("Linear phase / spectral resolution (higher = more latency)");
        charBtn.setTooltip ("Character: Clean, Subtle or Warm analog style saturation (4x oversampled)");
        instanceBtn.setTooltip ("This instance - click to see all instances");
        analyzerBtn.setTooltip ("Spectrum analyzer settings");
        autoGainBtn.setTooltip ("Auto gain: compensates the loudness change of the EQ curve");
        phaseBtn.setTooltip ("Invert the output polarity");
        bypassBtn.setTooltip ("Bypass (latency compensated)");
        panVal.setTooltip ("Output pan (Alt+click to type)");
        scaleVal.setTooltip ("Gain scale: scales the gain of all bands (Alt+click to type)");
        outVal.setTooltip ("Output level (Alt+click to type)");

        midiBtn.getState = [this] { return proc.midiLearn.isLearning(); };
        midiBtn.onColour = theme::learn;
        midiBtn.onClick = [this]
        {
            const bool on = ! proc.midiLearn.isLearning();
            proc.midiLearn.setLearning (on);
            setStatus (on ? "MIDI Learn: touch a control, then move a MIDI controller" : "MIDI Learn off", juce::Time::getMillisecondCounterHiRes() * 0.001);
        };
        midiMenuBtn.onClick = [this] { showMidiMenu(); };
        modeBtn.getText = [this] { return modeChoices()[juce::jlimit (0, 2, (int) std::lround (model.global (gpMode)))]; };
        modeBtn.onClick = [this] { showModeMenu(); };
        resBtn.getText = [this] { return resolutionChoices()[juce::jlimit (0, 4, (int) std::lround (model.global (gpResolution)))]; };
        resBtn.onClick = [this] { showResolutionMenu(); };
        charBtn.getText = [this] { return characterChoices()[juce::jlimit (0, 2, (int) std::lround (model.global (gpCharacter)))]; };
        charBtn.onClick = [this] { showCharacterMenu(); };
        instanceBtn.getText = [this] { return proc.getInstanceName(); };
        instanceBtn.onClick = [this] { if (onInstances) onInstances(); };
        analyzerBtn.getText = [this]
        {
            const auto s = proc.getAnalyzerSettings();
            juce::StringArray a;
            if (s.pre) a.add ("Pre");
            if (s.post) a.add ("Post");
            if (s.sc) a.add ("SC");
            return "Analyzer: " + (a.isEmpty() ? juce::String ("Off") : a.joinIntoString ("+")) + (s.freeze ? " (frozen)" : "");
        };
        analyzerBtn.onClick = [this] { showAnalyzerMenu(); };

        auto globalFlag = [this] (GlobalParam p) { return [this, p] { return model.global (p) > 0.5; }; };
        autoGainBtn.getState = globalFlag (gpAutoGain);
        autoGainBtn.onClick = [this] { model.setGlobal (gpAutoGain, model.global (gpAutoGain) > 0.5 ? 0.0 : 1.0); };
        phaseBtn.getState = globalFlag (gpPhaseInvert);
        phaseBtn.onClick = [this] { model.setGlobal (gpPhaseInvert, model.global (gpPhaseInvert) > 0.5 ? 0.0 : 1.0); };
        bypassBtn.getState = [this] { return model.global (gpBypass) < 0.5; };
        bypassBtn.onClick = [this] { model.setGlobal (gpBypass, model.global (gpBypass) > 0.5 ? 0.0 : 1.0); };

        // drag values: shared state for relative dragging
        struct DragState { double start = 0.0, acc = 0.0; };
        auto wire = [this] (DragValue& v, GlobalParam p, double perPixel, double lo, double hi, double def,
                            std::function<juce::String (double)> fmt, const juce::String& caption)
        {
            auto st = std::make_shared<DragState>();
            v.caption = caption;
            v.getText = [this, p, fmt] { return fmt (model.global (p)); };
            v.onDragStart = [this, p, st] { st->start = model.global (p); st->acc = 0.0; model.beginGesture (proc.table.global (p)->getName (64)); };
            v.onDrag = [this, p, st, perPixel, lo, hi] (double d) { st->acc += d * perPixel; model.setGlobal (p, juce::jlimit (lo, hi, st->start + st->acc)); };
            v.onDragEnd = [this] { model.endGesture(); };
            v.onDoubleClick = [this, p, def] { model.setGlobal (p, def); };
            v.onWheel = [this, p, perPixel, lo, hi] (double d) { model.setGlobal (p, juce::jlimit (lo, hi, model.global (p) + d * perPixel)); };
            v.onTextEntered = [this, p, lo, hi] (const juce::String& s)
            {
                auto* par = proc.table.global (p);
                const float norm = par->getValueForText (s);
                model.setGlobal (p, juce::jlimit (lo, hi, (double) par->convertFrom0to1 (norm)));
            };
        };
        wire (panVal, gpOutPan, 1.0 / 150.0, -1.0, 1.0, 0.0, [] (double v) { const int pc = juce::roundToInt (v * 100.0); return pc == 0 ? juce::String ("C") : (pc < 0 ? "L" + juce::String (-pc) : "R" + juce::String (pc)); }, "Pan");
        wire (scaleVal, gpGainScale, 0.5, -100.0, 200.0, 100.0, [] (double v) { return juce::String (juce::roundToInt (v)) + "%"; }, "");
        wire (outVal, gpOutGain, 0.1, -36.0, 36.0, 0.0, [] (double v) { return (v > 0.05 ? "+" : "") + juce::String (v, 1) + " dB"; }, "");
    }

    void BottomBar::refresh()
    {
        for (auto* b : { &midiBtn, &modeBtn, &resBtn, &charBtn, &instanceBtn, &analyzerBtn }) b->refresh();
        for (auto* b : { &autoGainBtn, &phaseBtn, &bypassBtn }) b->refresh();
        panVal.refresh(); scaleVal.refresh(); outVal.refresh();
        const auto col = proc.getInstanceColour();
        if (instanceBtn.dotColour != col) { instanceBtn.dotColour = col; instanceBtn.repaint(); }
        bool spectral = false;
        for (int b = 0; b < kMaxBands; ++b) if (model.band (b).isSpectral()) { spectral = true; break; }
        const bool showRes = (int) std::lround (model.global (gpMode)) == (int) ProcessingMode::LinearPhase || spectral;
        if (resBtn.isVisible() != showRes) resBtn.setVisible (showRes);
        const bool learn = proc.midiLearn.isLearning();
        if (learn)
        {
            const int learned = proc.midiLearn.takeLearnEvent();
            if (learned >= 0 && learned < proc.getParameters().size())
                setStatus ("MIDI: " + proc.getParameters()[learned]->getName (64) + " <- " + proc.midiLearn.describe (learned),
                           juce::Time::getMillisecondCounterHiRes() * 0.001);
        }
        instanceBtn.setVisible (status.isEmpty());
    }

    void BottomBar::showMidiMenu()
    {
        juce::PopupMenu m;
        m.addItem ("MIDI Learn", true, proc.midiLearn.isLearning(), [this] { proc.midiLearn.setLearning (! proc.midiLearn.isLearning()); });
        const auto maps = proc.midiLearn.getMappings();
        m.addItem ("Clear all assignments", ! maps.empty(), false, [this] { proc.midiLearn.clearAll(); });
        if (! maps.empty())
        {
            m.addSeparator();
            m.addSectionHeader ("Assignments (click to remove)");
            const auto& params = proc.getParameters();
            for (const auto& mp : maps)
            {
                if (mp.paramIndex < 0 || mp.paramIndex >= params.size()) continue;
                const int idx = mp.paramIndex;
                m.addItem ("CC " + juce::String (mp.cc) + (mp.channel > 0 ? " ch" + juce::String (mp.channel) : juce::String()) + "  ->  " + params[idx]->getName (64),
                           [this, idx] { proc.midiLearn.removeParameter (idx); });
            }
        }
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (midiBtn).withDeletionCheck (*this));
    }

    void BottomBar::showModeMenu()
    {
        juce::PopupMenu m;
        const int cur = (int) std::lround (model.global (gpMode));
        const double fs = proc.getCurrentSampleRate();
        const auto res = (LPResolution) juce::jlimit (0, 4, (int) std::lround (model.global (gpResolution)));
        const double npMs = 1000.0 * (double) NaturalPhaseCorrector::kernelLengthFor (fs) * 0.5 / fs;
        const double lpMs = 1000.0 * (double) (lpKernelLength (res, fs) / 2 + lpPartitionSize (fs)) / fs;
        const juce::String info[] = { "no latency", juce::String (npMs, 1) + " ms", juce::String (lpMs, 0) + " ms" };
        for (int i = 0; i < 3; ++i)
            m.addItem (modeChoices()[i] + "   (" + info[i] + ")", true, cur == i, [this, i] { model.setGlobal (gpMode, (double) i); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (modeBtn).withDeletionCheck (*this));
    }

    void BottomBar::showResolutionMenu()
    {
        juce::PopupMenu m;
        const int cur = (int) std::lround (model.global (gpResolution));
        const double fs = proc.getCurrentSampleRate();
        for (int i = 0; i < 5; ++i)
        {
            const double ms = 1000.0 * (double) (lpKernelLength ((LPResolution) i, fs) / 2 + lpPartitionSize (fs)) / fs;
            m.addItem (resolutionChoices()[i] + "   (" + juce::String (ms, 0) + " ms)", true, cur == i, [this, i] { model.setGlobal (gpResolution, (double) i); });
        }
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (resBtn).withDeletionCheck (*this));
    }

    void BottomBar::showCharacterMenu()
    {
        juce::PopupMenu m;
        const int cur = (int) std::lround (model.global (gpCharacter));
        for (int i = 0; i < 3; ++i)
            m.addItem (characterChoices()[i], true, cur == i, [this, i] { model.setGlobal (gpCharacter, (double) i); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (charBtn).withDeletionCheck (*this));
    }

    void BottomBar::showAnalyzerMenu()
    {
        juce::PopupMenu m;
        EQDisplay::addAnalyzerMenuItems (m, proc, [this] { analyzerBtn.refresh(); if (onAnalyzerChanged) onAnalyzerChanged(); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (analyzerBtn).withDeletionCheck (*this));
    }

    void BottomBar::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (theme::barBg);
        g.fillRect (r);
        g.setColour (theme::barLine);
        g.drawHorizontalLine (0, 0.0f, r.getWidth());
        if (status.isNotEmpty())
        {
            g.setFont (theme::font (12.5f * uiScale, 1));
            g.setColour (theme::accent);
            g.drawFittedText (status, statusArea.toNearestInt(), juce::Justification::centred, 1, 0.8f);
        }
    }

    void BottomBar::resized()
    {
        const float s = uiScale;
        auto r = getLocalBounds().toFloat().reduced (8.0f * s, 3.0f * s);
        auto place = [] (juce::Component& c, juce::Rectangle<float> a) { c.setBounds (a.toNearestInt()); };
        place (midiBtn, r.removeFromLeft (84.0f * s));
        place (midiMenuBtn, r.removeFromLeft (20.0f * s));
        r.removeFromLeft (14.0f * s);
        place (modeBtn, r.removeFromLeft (130.0f * s));
        place (resBtn, r.removeFromLeft (96.0f * s));
        place (charBtn, r.removeFromLeft (80.0f * s));

        place (bypassBtn, r.removeFromRight (26.0f * s));
        r.removeFromRight (6.0f * s);
        place (outVal, r.removeFromRight (70.0f * s));
        place (scaleVal, r.removeFromRight (54.0f * s));
        place (panVal, r.removeFromRight (70.0f * s));
        place (phaseBtn, r.removeFromRight (26.0f * s));
        place (autoGainBtn, r.removeFromRight (26.0f * s));
        r.removeFromRight (8.0f * s);
        place (analyzerBtn, r.removeFromRight (178.0f * s));
        statusArea = r.reduced (10.0f * s, 0.0f);
        place (instanceBtn, r.withSizeKeepingCentre (std::min (r.getWidth(), 220.0f * s), r.getHeight()));
        for (auto* b : { &midiBtn, &modeBtn, &resBtn, &charBtn, &instanceBtn, &analyzerBtn }) b->fontSize = 12.5f * s;
        for (auto* v : { &panVal, &scaleVal, &outVal }) v->fontSize = 12.5f * s;
    }
}
