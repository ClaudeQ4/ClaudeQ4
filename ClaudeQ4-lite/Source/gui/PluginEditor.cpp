/*
    Claude-Q 4 - plug-in editor
*/
#include "PluginEditor.h"

namespace cq
{
    namespace
    {
        // one look and feel shared by all open editors; also the default for pop-up menus
        int& openEditors() { static int n = 0; return n; }
        juce::SharedResourcePointer<ClaudeLookAndFeel>* sharedLnf = nullptr;
    }

    ClaudeQ4Editor::ClaudeQ4Editor (ClaudeQ4AudioProcessor& p)
        : AudioProcessorEditor (p), proc (p), model (p), header (model), bottom (model),
          display (model, false), panel (model), meter (p)
    {
        if (openEditors()++ == 0)
        {
            sharedLnf = new juce::SharedResourcePointer<ClaudeLookAndFeel>();
            juce::LookAndFeel::setDefaultLookAndFeel (&sharedLnf->get());
        }
        setLookAndFeel (&lnf);
        setOpaque (true);
        setWantsKeyboardFocus (true);

        addAndMakeVisible (display);
        addAndMakeVisible (header);
        addAndMakeVisible (bottom);
        addAndMakeVisible (meter);
        addChildComponent (panel);
        for (auto* b : { &pianoBtn, &sketchBtn, &matchBtn })
        {
            addAndMakeVisible (b);
            b->drawFrame = false;
            b->iconScale = 0.72f;
        }

        if ((bool) proc.getUiProperty ("tooltips", true)) tooltips = std::make_unique<juce::TooltipWindow> (this, 700);

        display.onStatus = [this] (const juce::String& s) { setStatus (s); };
        display.onSettingsChanged = [this] { layoutOverlayButtons(); };
        header.onHelp = [this] { openHelp(); };
        header.onInstances = [this] { openInstances(); };
        header.onSettings = [this] { showSettingsMenu(); };
        header.onPresetChanged = [this] { model.clearSelection(); setStatus ("Preset: " + proc.presets.getCurrentName()); };
        bottom.onInstances = [this] { openInstances(); };
        panel.onLayoutChanged = [this] { positionPanel(); };
        panel.isPianoSnapping = [this] { return display.isPianoVisible(); };
        model.onSelectionChanged = [this] { positionPanel(); };

        sketchBtn.getState = [this] { return display.isSketchMode(); };
        sketchBtn.onClick = [this] { display.setSketchMode (! display.isSketchMode()); };
        pianoBtn.getState = [this] { return display.isPianoVisible(); };
        pianoBtn.onClick = [this] { display.setPianoVisible (! display.isPianoVisible()); layoutOverlayButtons(); };
        matchBtn.getState = [this] { return match != nullptr; };
        matchBtn.onClick = [this] { if (match != nullptr) { match.reset(); } else openMatch(); };

        const int w = juce::jlimit (760, 2560, (int) proc.getUiProperty ("width", 1000));
        const int h = juce::jlimit (470, 1600, (int) proc.getUiProperty ("height", 620));
        setResizable (true, true);
        setResizeLimits (760, 470, 2560, 1600);
        setSize (w, h);

        vblank = std::make_unique<juce::VBlankAttachment> (this, [this] { frame (juce::Time::getMillisecondCounterHiRes() * 0.001); });
        startTimerHz (30);   // fallback when no vertical blank callbacks arrive
    }

    ClaudeQ4Editor::~ClaudeQ4Editor()
    {
        stopTimer();
        vblank.reset();
        help.reset();
        instances.reset();
        match.reset();
        tooltips.reset();
        setLookAndFeel (nullptr);
        if (--openEditors() == 0)
        {
            juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
            delete sharedLnf;
            sharedLnf = nullptr;
        }
    }

    void ClaudeQ4Editor::timerCallback()
    {
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        if (now - lastFrame > 0.05)
        {
            frame (now);
            // No vertical blank callbacks (Wine / yabridge, some remote or virtual displays): on Windows
            // JUCE's software renderer only flushes its deferred repaints on vblank, so do it here.
            if (auto* peer = getPeer())
                peer->performAnyPendingRepaintsNow();
        }
    }

    void ClaudeQ4Editor::frame (double now)
    {
        lastFrame = now;
        proc.updateAnalyzers (now);
        display.tick (now);
        model.validateSelection();

        const int f = model.getFocus();
        const bool showPanel = f >= 0 && model.isUsed (f) && instances == nullptr && help == nullptr && ! display.isSketchMode();
        if (panel.isVisible() != showPanel) panel.setVisible (showPanel);
        if (showPanel)
        {
            positionPanel();
            panel.refresh();
        }
        header.refresh();
        bottom.refresh();
        bottom.tickStatus (now);
        meter.update (now, proc.getAnalyzerSettings().range, 0.0f);
        for (auto* b : { &pianoBtn, &sketchBtn, &matchBtn }) b->refresh();
        if (instances != nullptr) instances->tick (now);
        if (match != nullptr) match->tick (now);
    }

    void ClaudeQ4Editor::setStatus (const juce::String& s)
    {
        bottom.setStatus (s, juce::Time::getMillisecondCounterHiRes() * 0.001);
    }

    void ClaudeQ4Editor::positionPanel()
    {
        const int f = model.getFocus();
        if (f < 0 || ! model.isUsed (f)) return;
        const auto plot = display.getPlot().translated ((float) display.getX(), (float) display.getY());
        const int w = std::min (panel.getPreferredWidth(), (int) plot.getWidth() - 12);
        const int h = panel.getPreferredHeight();
        const auto node = display.nodePosition (f).translated ((float) display.getX(), (float) display.getY());
        const float minX = plot.getX() + 36.0f * uiScale;    // keep the tool icons (bottom left) free
        const float tx = juce::jlimit (minX, std::max (minX, plot.getRight() - (float) w - 6.0f), node.x - (float) w * 0.5f);
        panelX = (panelX < 0.0f || panelTarget != f || ! panel.isVisible()) ? tx : panelX + (tx - panelX) * 0.35f;
        if (std::abs (panelX - tx) < 0.5f) panelX = tx;
        panelTarget = f;
        int y = (int) plot.getBottom() - h - juce::roundToInt (22.0f * uiScale);
        // never cover the band that is being edited
        if (node.y > (float) y - 14.0f * uiScale && node.x > panelX - 10.0f && node.x < panelX + (float) w + 10.0f)
            y = (int) plot.getY() + juce::roundToInt (34.0f * uiScale);
        panel.setBounds ((int) panelX, y, w, h);
    }

    void ClaudeQ4Editor::layoutOverlayButtons()
    {
        const auto plot = display.getPlot().translated ((float) display.getX(), (float) display.getY());
        const float s = 24.0f * uiScale;
        float y = plot.getBottom() - s - 18.0f * uiScale;
        for (auto* b : { &pianoBtn, &sketchBtn, &matchBtn })
        {
            b->setBounds (juce::Rectangle<float> (plot.getX() + 4.0f * uiScale, y, s, s).toNearestInt());
            y -= s + 2.0f * uiScale;
        }
    }

    void ClaudeQ4Editor::paint (juce::Graphics& g)
    {
        g.fillAll (theme::bgBottom);
    }

    void ClaudeQ4Editor::resized()
    {
        uiScale = juce::jlimit (0.75f, 2.2f, (float) getWidth() / 1000.0f);
        auto r = getLocalBounds();
        header.setBounds (r.removeFromTop (juce::roundToInt (38.0f * uiScale)));
        header.setUiScale (uiScale);
        bottom.setBounds (r.removeFromBottom (juce::roundToInt (32.0f * uiScale)));
        bottom.setUiScale (uiScale);
        auto meterArea = r.removeFromRight (juce::roundToInt (16.0f * uiScale));
        display.setBounds (r);
        const auto plot = display.getPlot();
        meter.setBounds (meterArea.withY (display.getY() + (int) plot.getY()).withHeight ((int) plot.getHeight()));
        panel.setUiScale (uiScale);
        positionPanel();
        layoutOverlayButtons();
        if (help != nullptr) { help->setBounds (getLocalBounds()); help->setUiScale (uiScale); }
        if (instances != nullptr) { instances->setBounds (display.getBounds()); instances->setUiScale (uiScale); }
        if (match != nullptr)
        {
            match->setUiScale (uiScale);
            match->setBounds (juce::Rectangle<int> (match->getPreferredWidth(), match->getPreferredHeight())
                                  .withCentre ({ display.getX() + (int) plot.getCentreX(), 0 })
                                  .withY (display.getY() + juce::roundToInt (40.0f * uiScale)));
        }
        proc.setUiProperty ("width", getWidth());
        proc.setUiProperty ("height", getHeight());
    }

    bool ClaudeQ4Editor::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)) { proc.undoManager.undo(); return true; }
        if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)
            || key == juce::KeyPress ('y', juce::ModifierKeys::commandModifier, 0)) { proc.undoManager.redo(); return true; }
        if (key == juce::KeyPress::escapeKey && (help != nullptr || instances != nullptr || match != nullptr)) { closeOverlays(); return true; }
        if (instances != nullptr || help != nullptr) return false;
        return display.keyPressed (key);
    }

    void ClaudeQ4Editor::closeOverlays()
    {
        help.reset();
        instances.reset();
        match.reset();
        display.grabKeyboardFocus();
    }

    void ClaudeQ4Editor::openHelp()
    {
        help = std::make_unique<HelpOverlay>();
        juce::Component::SafePointer<ClaudeQ4Editor> safe (this);
        help->onClose = [safe] { juce::MessageManager::callAsync ([safe] { if (safe != nullptr) { safe->help.reset(); } }); };
        addAndMakeVisible (*help);
        help->setUiScale (uiScale);
        help->setBounds (getLocalBounds());
        help->grabKeyboardFocus();
    }

    void ClaudeQ4Editor::openInstances()
    {
        if (instances != nullptr) { instances.reset(); return; }
        instances = std::make_unique<InstancesOverlay> (proc);
        juce::Component::SafePointer<ClaudeQ4Editor> safe (this);
        instances->onClose = [safe] { juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->instances.reset(); }); };
        addAndMakeVisible (*instances);
        instances->setUiScale (uiScale);
        instances->setBounds (display.getBounds());
        instances->tick (juce::Time::getMillisecondCounterHiRes() * 0.001);
    }

    void ClaudeQ4Editor::openMatch()
    {
        match = std::make_unique<MatchPanel> (model, display);
        juce::Component::SafePointer<ClaudeQ4Editor> safe (this);
        match->onClose = [safe] { juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->match.reset(); }); };
        match->onStatus = [this] (const juce::String& s) { setStatus (s); };
        addAndMakeVisible (*match);
        resized();
    }

    void ClaudeQ4Editor::showSettingsMenu()
    {
        juce::PopupMenu m;
        juce::PopupMenu size;
        const std::pair<const char*, juce::Point<int>> sizes[] = { { "Small", { 800, 496 } }, { "Medium", { 1000, 620 } },
                                                                   { "Large", { 1250, 775 } }, { "Extra Large", { 1500, 930 } } };
        for (const auto& [name, sz] : sizes)
            size.addItem (name, true, getWidth() == sz.x && getHeight() == sz.y, [this, target = sz] { setSize (target.x, target.y); });
        size.addSeparator();
        size.addItem ("Fit to screen", [this]
        {
            if (auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds()))
            {
                const auto area = d->userBounds.toNearestInt();
                setSize (juce::jlimit (760, 2560, area.getWidth() - 40), juce::jlimit (470, 1600, area.getHeight() - 90));
            }
        });
        m.addSubMenu ("Interface size", size);
        juce::PopupMenu range;
        for (double rr : { 3.0, 6.0, 12.0, 30.0 })
            range.addItem (juce::String ((int) rr) + " dB", true, display.getRange() == rr, [this, rr] { display.setRange (rr); });
        m.addSubMenu ("Display range", range);
        juce::PopupMenu ana;
        EQDisplay::addAnalyzerMenuItems (ana, proc, {});
        m.addSubMenu ("Analyzer", ana);
        m.addItem ("Piano ruler", true, display.isPianoVisible(), [this] { display.setPianoVisible (! display.isPianoVisible()); layoutOverlayButtons(); });
        m.addItem ("Gain-Q interaction (proportional Q)", true, model.global (gpGainQ) > 0.5, [this] { model.setGlobal (gpGainQ, model.global (gpGainQ) > 0.5 ? 0.0 : 1.0); });
        m.addItem ("Mid/Side output pan", true, model.global (gpPanMS) > 0.5, [this] { model.setGlobal (gpPanMS, model.global (gpPanMS) > 0.5 ? 0.0 : 1.0); });
        m.addItem ("Tooltips", true, tooltips != nullptr, [this]
        {
            if (tooltips != nullptr) tooltips.reset();
            else tooltips = std::make_unique<juce::TooltipWindow> (this, 700);
            proc.setUiProperty ("tooltips", tooltips != nullptr);
        });
        m.addSeparator();
        m.addItem ("Copy EQ curve", model.usedCount() > 0, false, [this]
        {
            juce::SystemClipboard::copyTextToClipboard (proc.presets.bandsToText());
            setStatus ("EQ curve copied");
        });
        m.addItem ("Paste EQ curve", PresetManager::isBandsText (juce::SystemClipboard::getTextFromClipboard()), false, [this]
        {
            if (proc.presets.bandsFromText (juce::SystemClipboard::getTextFromClipboard())) { model.clearSelection(); setStatus ("EQ curve pasted"); }
        });
        m.addSeparator();
        m.addItem ("EQ Match...", [this] { if (match == nullptr) openMatch(); });
        m.addItem ("EQ Sketch", true, display.isSketchMode(), [this] { display.setSketchMode (! display.isSketchMode()); });
        m.addItem ("Instances...", [this] { openInstances(); });
        m.addItem ("Help", [this] { openHelp(); });
        m.addSeparator();
        m.addItem ("About Claude-Q 4", [this]
        {
            juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                .withIconType (juce::MessageBoxIconType::NoIcon)
                .withTitle ("Claude-Q 4  v1.0.1")
                .withMessage ("24-band dynamic and spectral equalizer.\n\n"
                              "Zero Latency, Natural Phase and Linear Phase processing, 10 Hz - 30 kHz, "
                              "6 - 96 dB/oct and brickwall slopes, per band stereo / L / R / M / S.\n\n"
                              "Built with JUCE (AGPLv3 / commercial licence), pffft (BSD-style licence) and the "
                              "Inter typeface (SIL Open Font License).")
                .withButton ("OK")
                .withAssociatedComponent (this), nullptr);
        });
        m.showMenuAsync (juce::PopupMenu::Options().withMousePosition().withDeletionCheck (*this));
    }
}
