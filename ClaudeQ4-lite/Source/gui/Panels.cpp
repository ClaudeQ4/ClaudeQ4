/*
    Claude-Q 4 - output meter, help page, instance list and EQ Match panel
*/
#include "Panels.h"
#include "BandFitter.h"
#include "plugin/InstanceRegistry.h"

namespace cq
{
    //==========================================================================
    OutputMeter::OutputMeter (ClaudeQ4AudioProcessor& p) : proc (p)
    {
        setTooltip ("Output level - click to reset the peak / clip indicator");
    }

    float OutputMeter::yFor (float db) const
    {
        return top + (0.0f - db) / std::max (10.0f, range) * ((float) getHeight() - top);
    }

    void OutputMeter::update (double now, float analyzerRangeDb, float topOffsetPx)
    {
        const double dt = last > 0.0 ? juce::jlimit (0.0, 0.2, now - last) : 1.0 / 60.0;
        last = now;
        range = analyzerRangeDb;
        top = topOffsetPx;
        for (int c = 0; c < 2; ++c)
        {
            const float db = (float) gainToDb (proc.takePeak (c));
            level[(size_t) c] = std::max (db, level[(size_t) c] - (float) (26.0 * dt));
            if (db >= hold[(size_t) c]) { hold[(size_t) c] = db; holdTime[(size_t) c] = now; }
            else if (now - holdTime[(size_t) c] > 1.6) hold[(size_t) c] = std::max (level[(size_t) c], hold[(size_t) c] - (float) (32.0 * dt));
            maxPeak = std::max (maxPeak, db);
        }
        if (proc.takeClip()) clip = true;
        const float d0 = level[0], d1 = level[1], d2 = hold[0] + hold[1];
        if (std::abs (d0 - lastDrawn[0]) > 0.05f || std::abs (d1 - lastDrawn[1]) > 0.05f || std::abs (d2 - lastDrawn[2]) > 0.05f)
        {
            lastDrawn[0] = d0; lastDrawn[1] = d1; lastDrawn[2] = d2;
            setTooltip ("Output peak " + (maxPeak > -140.0f ? juce::String (maxPeak, 1) + " dBFS" : juce::String ("-inf")) + " - click to reset");
            repaint();
        }
    }

    void OutputMeter::mouseDown (const juce::MouseEvent&)
    {
        clip = false;
        maxPeak = -150.0f;
        hold = { -150.0f, -150.0f };
        repaint();
    }

    void OutputMeter::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (theme::barBg);
        g.fillRect (r);
        const float w = std::max (3.0f, (r.getWidth() - 6.0f) * 0.5f - 1.0f);
        for (int c = 0; c < 2; ++c)
        {
            const float x = r.getX() + 3.0f + (float) c * (w + 2.0f);
            const auto bar = juce::Rectangle<float> (x, top, w, r.getHeight() - top);
            g.setColour (juce::Colour (0xff1b1d26));
            g.fillRect (bar);
            const float y = juce::jlimit (top, r.getBottom(), yFor (level[(size_t) c]));
            if (y < r.getBottom())
            {
                juce::ColourGradient grad (theme::meterRed, 0.0f, yFor (0.0f), theme::meterGreen, 0.0f, yFor (-24.0f), false);
                grad.addColour (0.5, theme::meterYellow);
                g.setGradientFill (grad);
                g.fillRect (juce::Rectangle<float> (x, y, w, r.getBottom() - y));
            }
            const float hy = yFor (hold[(size_t) c]);
            if (hy > top && hy < r.getBottom())
            {
                g.setColour (juce::Colours::white.withAlpha (0.8f));
                g.fillRect (juce::Rectangle<float> (x, hy - 0.5f, w, 1.5f));
            }
        }
        // clip indicator
        g.setColour (clip ? theme::meterRed : juce::Colour (0xff2a2c36));
        g.fillRect (juce::Rectangle<float> (r.getX() + 3.0f, std::max (0.0f, top - 5.0f), r.getWidth() - 6.0f, 3.0f));
    }

    //==========================================================================
    HelpOverlay::HelpOverlay()
    {
        setWantsKeyboardFocus (true);
        addAndMakeVisible (viewport);
        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (closeBtn);
        closeBtn.onClick = [this] { if (onClose) onClose(); };
    }

    void HelpOverlay::setUiScale (float s)
    {
        uiScale = s;
        content.scale = s;
        resized();
    }

    void HelpOverlay::mouseDown (const juce::MouseEvent& e)
    {
        if (! panel.contains (e.position) && onClose) onClose();
    }

    bool HelpOverlay::keyPressed (const juce::KeyPress& k)
    {
        if (k == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
        return false;
    }

    void HelpOverlay::paint (juce::Graphics& g)
    {
        g.fillAll (juce::Colours::black.withAlpha (0.55f));
        theme::drawPanel (g, panel, 10.0f * uiScale, true);
        auto title = panel.reduced (22.0f * uiScale, 14.0f * uiScale).removeFromTop (30.0f * uiScale);
        theme::drawLogo (g, title.removeFromLeft (150.0f * uiScale).withSizeKeepingCentre (150.0f * uiScale, 22.0f * uiScale));
        g.setFont (theme::font (16.0f * uiScale, 2));
        g.setColour (theme::text);
        g.drawText ("Help", title.translated (12.0f * uiScale, 0.0f), juce::Justification::centredLeft, false);
    }

    void HelpOverlay::resized()
    {
        panel = getLocalBounds().toFloat().reduced (std::max (20.0f, (float) getWidth() * 0.09f), std::max (16.0f, (float) getHeight() * 0.06f));
        closeBtn.setBounds (juce::Rectangle<float> (26.0f * uiScale, 26.0f * uiScale)
                                .withPosition (panel.getRight() - 40.0f * uiScale, panel.getY() + 14.0f * uiScale).toNearestInt());
        auto v = panel.reduced (22.0f * uiScale, 14.0f * uiScale);
        v.removeFromTop (40.0f * uiScale);
        viewport.setBounds (v.toNearestInt());
        const int w = viewport.getWidth() - viewport.getScrollBarThickness() - 4;
        content.setSize (w, content.layoutHeight (w));
    }

    void HelpOverlay::Content::build()
    {
        text = juce::AttributedString();
        text.setWordWrap (juce::AttributedString::byWord);
        text.setLineSpacing (3.0f * scale);
        auto head = [this] (const juce::String& s) { text.append ("\n" + s + "\n", theme::font (15.0f * scale, 2), theme::accent); };
        auto item = [this] (const juce::String& key, const juce::String& desc)
        {
            text.append (key, theme::font (13.0f * scale, 2), theme::text);
            text.append ("   " + desc + "\n", theme::font (13.0f * scale, 0), theme::textDim);
        };
        const juce::String mod =
           #if JUCE_MAC
            "Cmd";
           #else
            "Ctrl";
           #endif

        text.append ("Claude-Q 4 is a 24-band equalizer with dynamic and spectral processing, three processing modes "
                     "(Zero Latency, Natural Phase, Linear Phase), per-band stereo placement and a high resolution analyzer.\n",
                     theme::font (13.5f * scale), theme::text);

        head ("Creating and editing bands");
        item ("Double-click", "on an empty spot: create a band there (near the edges: low / high cut)");
        item ("Click the curve (+)", "create a band on the curve and drag it immediately");
        item ("Drag a dot", "frequency and gain (cut, notch, band pass: vertical drag = Q / resonance)");
        item ("Shift + drag", "fine adjustment");
        item ("Alt + drag", "lock to horizontal or vertical movement");
        item (mod + " + drag", "change Q");
        item ("Mouse wheel", "Q;  " + mod + " + wheel: gain;  Alt + wheel: dynamic range");
        item ("Double-click a dot", "delete the band");
        item ("Alt + click a dot", "enable / disable the band");
        item ("Alt + Shift + hold", "solo the band while the mouse button is down");
        item ("Right-click a dot", "band menu: shape, slope, stereo placement, dynamics, spectral, invert, duplicate, split, reset, delete");
        item ("Drag on empty space", "lasso selection (Shift / " + mod + ": add to the selection)");
        item (mod + " + A, Esc, Tab", "select all, deselect, next band");
        item ("Arrow keys", "left / right: frequency in semitones, up / down: gain (" + mod + ": Q), Shift: fine");
        item ("Delete / Backspace", "delete the selected bands");
        item (mod + " + Z / Shift + " + mod + " + Z", "undo / redo");

        head ("Band panel and hover card");
        item ("FREQ / GAIN / Q", "drag, Shift = fine, double-click = reset (gain, Q), right-click = type a value (e.g. 2.5k, -3, C4)");
        item ("Outer ring of GAIN", "drag to set the dynamic range: below the gain = compression, above = expansion");
        item ("Shape / slope", "Bell, Low/High Shelf, Low/High Cut, Notch, Band Pass, Tilt Shelf, Flat Tilt, All Pass;  6 to 96 dB/oct and Brickwall");
        item ("Stereo placement", "Stereo, Left, Right, Mid or Side per band.  The scissors split a stereo band into an L/R or M/S pair");
        item ("Headphones", "hold to solo: you hear the region around the band (band passed)");
        item ("Hover card", "power, solo and delete; shape and band menu; frequency, gain, Q (with the note name when the piano is "
                            "shown) - drag or scroll a value to change it, double-click gain / Q to reset");
        item (mod + " + C / " + mod + " + V", "copy the whole EQ curve / paste it (also into another instance)");

        head ("Dynamic EQ");
        item ("Dynamic button", "turns the band into a dynamic EQ that works only on its own frequency range");
        item ("Range", "-30 dB (compress) to +30 dB (expand).  The shaded area on the curve shows the range, the bright area shows "
                       "the live gain change, reacting to the signal in real time");
        item ("Threshold", "AUTO follows the long term level of the band's signal, so only events that stick out trigger. "
                           "Drag the knob for a manual threshold");
        item ("Attack / Release", "AUTO = program dependent (fast for transients and high bands, slower release for sustained "
                                  "gain change). Drag a knob for fixed times; double-click returns to AUTO");
        item ("EXT SC / FREE", "trigger from the external side chain input;  free trigger range with LO / HI filters");
        item ("Ear", "hold to audition the trigger signal");
        item ("Spectral dynamics", "the dynamics work per frequency inside the band: only the resonances that rise above the "
                                   "threshold are compressed or expanded.  DENSITY: wide areas (low) or narrow resonances (high). "
                                   "TILT: +3 dB/oct detector weighting");

        head ("Processing modes");
        item ("Zero Latency", "minimum phase IIR filters, matched to the analog prototypes (no cramping near Nyquist)");
        item ("Natural Phase", "analog magnitude AND phase response (about 2.7 ms latency)");
        item ("Linear Phase", "no phase shift; resolution Low to Maximum sets the latency / low frequency precision");
        item ("Character", "Clean, Subtle or Warm saturation after the EQ, 4x oversampled (no aliasing)");
        item ("Auto gain / Gain scale", "loudness compensation of the EQ curve;  scale all band gains from -100% to 200%");

        head ("Analyzer");
        item ("Pre / Post / SC", "input, output and external side chain spectra (bottom bar, Analyzer)");
        item ("Tilt", "4.5 dB/oct by default: pink noise and most mixes look flat");
        item ("Spectrum grab", "move the mouse over the display: the strongest peaks get labels. Click a label to create a band "
                               "there and drag it to cut or boost");
        item ("Collision detection", "red areas: frequencies where the external side chain or other Claude-Q 4 instances in the "
                                     "session mask this track");
        item ("Range / resolution / speed / freeze", "in the analyzer menu");

        head ("Piano ruler, EQ Sketch, EQ Match");
        item ("Piano", "keyboard icon (bottom left): shows the notes under the display.  Hover a key to see the note and its "
                       "frequency (C4 = 261.63 Hz).  Dragged bands snap to note frequencies (Shift = no snapping), click a key to "
                       "tune the selected band");
        item ("EQ Sketch", "brush icon: draw the curve you want, Claude-Q 4 creates the bands that reproduce it");
        item ("EQ Match", "match icon: learn the spectrum of the side chain or of another instance and create the bands that "
                          "make this track sound like the reference");

        head ("Instances, presets, A/B, MIDI");
        item ("Instances", "see and edit the EQ of every Claude-Q 4 in the session from one window; click a name to rename it");
        item ("Presets", "factory presets by category, your own presets (Documents/Claude-Q 4/Presets), previous / next arrows");
        item ("A/B", "two complete settings to compare;  Copy copies the current one to the other slot");
        item ("MIDI Learn", "click MIDI Learn, touch a control, move a MIDI controller.  The arrow menu lists / removes assignments");
        item ("Interface size", "drag the bottom right corner, or choose a size in the settings (gear) menu");
    }

    int HelpOverlay::Content::layoutHeight (int width)
    {
        build();
        layout.createLayout (text, (float) std::max (100, width));
        lastWidth = width;
        return (int) std::ceil (layout.getHeight()) + 20;
    }

    void HelpOverlay::Content::paint (juce::Graphics& g)
    {
        layout.draw (g, getLocalBounds().toFloat());
    }

    //==========================================================================
    InstancesOverlay::Row::Row (ClaudeQ4AudioProcessor& p, bool isSelf)
        : proc (p), self (isSelf), model (p), display (model, true)
    {
        auto* pp = &p;
        model.alive = [pp] { return InstanceRegistry::get().contains (pp); };
        display.isAlive = model.alive;
        addAndMakeVisible (display);
        addAndMakeVisible (nameBtn);
        addAndMakeVisible (bypassBtn);
        nameBtn.drawFrame = false;
        nameBtn.justification = juce::Justification::centredLeft;
        nameBtn.getText = [this] { return model.isAlive() ? proc.getInstanceName() : juce::String(); };
        nameBtn.setTooltip ("Click to rename this instance");
        juce::Component::SafePointer<Row> safe (this);
        nameBtn.onClick = [this, safe]
        {
            if (! model.isAlive()) return;
            showInlineTextEntry (nameBtn, proc.getInstanceName(), [safe] (const juce::String& s)
            {
                if (safe != nullptr && safe->model.isAlive()) safe->proc.setInstanceName (s);
            });
        };
        bypassBtn.getState = [this] { return model.isAlive() && model.global (gpBypass) < 0.5; };
        bypassBtn.onClick = [this] { if (model.isAlive()) model.setGlobal (gpBypass, model.global (gpBypass) > 0.5 ? 0.0 : 1.0); };
    }

    InstancesOverlay::Row::~Row() = default;

    void InstancesOverlay::Row::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat().reduced (4.0f);
        g.setColour (theme::panelInner.withAlpha (0.85f));
        g.fillRoundedRectangle (r, 6.0f);
        if (! model.isAlive()) return;
        g.setColour (proc.getInstanceColour());
        g.fillRoundedRectangle (r.withWidth (4.0f), 2.0f);
        auto info = r.withWidth ((float) display.getX() - r.getX() - 8.0f).reduced (14.0f, 8.0f);
        info.removeFromTop ((float) nameBtn.getHeight() + 4.0f);
        g.setFont (theme::font (11.5f));
        g.setColour (theme::textDim);
        const int mode = (int) std::lround (model.global (gpMode));
        juce::String s = modeChoices()[juce::jlimit (0, 2, mode)];
        s << "\n" << model.usedCount() << " band" << (model.usedCount() == 1 ? "" : "s");
        int dyn = 0;
        for (int b = 0; b < kMaxBands; ++b) if (model.band (b).isDynamic()) ++dyn;
        if (dyn > 0) s << ", " << dyn << " dynamic";
        if (self) s << "\n(this instance)";
        g.drawFittedText (s, info.toNearestInt(), juce::Justification::topLeft, 4);
    }

    void InstancesOverlay::Row::resized()
    {
        auto r = getLocalBounds().reduced (8);
        auto left = r.removeFromLeft (190);
        nameBtn.setBounds (left.removeFromTop (26).withTrimmedLeft (8));
        bypassBtn.setBounds (left.removeFromBottom (24).withWidth (28).translated (8, 0));
        display.setBounds (r);
    }

    InstancesOverlay::InstancesOverlay (ClaudeQ4AudioProcessor& s) : self (s)
    {
        addAndMakeVisible (viewport);
        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (closeBtn);
        closeBtn.onClick = [this] { if (onClose) onClose(); };
    }

    InstancesOverlay::~InstancesOverlay()
    {
        rows.clear();
    }

    void InstancesOverlay::rebuild()
    {
        rows.clear();
        auto all = InstanceRegistry::get().getAll();
        std::stable_sort (all.begin(), all.end(), [this] (ClaudeQ4AudioProcessor* a, ClaudeQ4AudioProcessor* b)
        {
            if (a == &self) return b != &self;
            if (b == &self) return false;
            return a->getInstanceId() < b->getInstanceId();
        });
        for (auto* p : all)
        {
            rows.push_back (std::make_unique<Row> (*p, p == &self));
            content.addAndMakeVisible (*rows.back());
        }
        resized();
    }

    void InstancesOverlay::tick (double now)
    {
        const int gen = InstanceRegistry::get().getGeneration();
        if (gen != generation) { generation = gen; rebuild(); }
        for (auto& r : rows)
        {
            if (! r->model.isAlive()) continue;
            r->proc.updateAnalyzers (now);
            r->display.tick (now);
            r->nameBtn.refresh();
            r->bypassBtn.refresh();
        }
    }

    void InstancesOverlay::paint (juce::Graphics& g)
    {
        g.fillAll (theme::bgBottom.withAlpha (0.97f));
        g.setFont (theme::font (14.0f * uiScale, 2));
        g.setColour (theme::text);
        g.drawText ("INSTANCES  (" + juce::String ((int) rows.size()) + ")", getLocalBounds().toFloat().removeFromTop (34.0f * uiScale).reduced (14.0f * uiScale, 0.0f),
                    juce::Justification::centredLeft, false);
        g.setFont (theme::font (12.0f * uiScale));
        g.setColour (theme::textDim);
        g.drawText ("Every Claude-Q 4 in this session - edit any of them right here", getLocalBounds().toFloat().removeFromTop (34.0f * uiScale).reduced (160.0f * uiScale, 0.0f),
                    juce::Justification::centredLeft, false);
    }

    void InstancesOverlay::resized()
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (juce::roundToInt (34.0f * uiScale));
        closeBtn.setBounds (top.removeFromRight (juce::roundToInt (34.0f * uiScale)).reduced (4));
        viewport.setBounds (r.reduced (6, 2));
        const int rowH = juce::roundToInt (160.0f * uiScale);
        const int w = viewport.getWidth() - viewport.getScrollBarThickness() - 2;
        content.setSize (w, std::max (1, (int) rows.size() * rowH));
        for (size_t i = 0; i < rows.size(); ++i) rows[i]->setBounds (0, (int) i * rowH, w, rowH);
    }

    //==========================================================================
    MatchPanel::MatchPanel (EditorModel& m, EQDisplay& d) : model (m), proc (m.proc), display (d)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &sourceBtn, &learnBtn, &matchBtn, &replaceBtn, &closeBtn, &amountVal, &bandsVal })
            addAndMakeVisible (c);
        sourceBtn.showChevron = true;
        sourceBtn.getText = [this]
        {
            if (sourceId == 0) return juce::String ("Reference: side chain input");
            const auto n = referenceName();
            return n.isNotEmpty() ? "Reference: " + n : juce::String ("Reference: (instance closed)");
        };
        sourceBtn.onClick = [this] { showSourceMenu(); };
        learnBtn.getState = [this] { return learning; };
        learnBtn.getText = [this] { return learning ? juce::String ("Learning... ") + juce::String (learnSeconds, 1) + " s" : juce::String ("Learn"); };
        learnBtn.onClick = [this]
        {
            learning = ! learning;
            if (learning) { refAcc.reset(); curAcc.reset(); learnSeconds = 0.0; haveDiff = false; }
            else computeDifference();
        };
        matchBtn.getText = [] { return juce::String ("Match"); };
        matchBtn.onClick = [this] { if (learning) { learning = false; computeDifference(); } applyMatch(); };
        replaceBtn.getState = [this] { return replace; };
        replaceBtn.onClick = [this] { replace = ! replace; };
        replaceBtn.setTooltip ("Replace the existing bands (otherwise the match bands are added)");
        closeBtn.onClick = [this] { if (onClose) onClose(); };

        amountVal.caption = "Amount";
        amountVal.getText = [this] { return juce::String (juce::roundToInt (amount)) + "%"; };
        amountVal.onDrag = [this] (double dl) { amount = juce::jlimit (0.0, 100.0, amount + dl * 0.5); };
        amountVal.onDoubleClick = [this] { amount = 100.0; };
        bandsVal.caption = "Bands";
        bandsVal.getText = [this] { return juce::String (juce::roundToInt (numBands)); };
        bandsVal.onDrag = [this] (double dl) { numBands = juce::jlimit (1.0, 24.0, numBands + dl * 0.08); };
        bandsVal.onDoubleClick = [this] { numBands = 8.0; };
        diff.fill (0.0f);
    }

    MatchPanel::~MatchPanel() = default;

    juce::String MatchPanel::referenceName() const
    {
        juce::String name;
        InstanceRegistry::get().visit ([this, &name] (ClaudeQ4AudioProcessor& p)
        {
            if (p.getInstanceId() == sourceId && &p != &proc) name = p.getInstanceName();
        });
        return name;
    }

    bool MatchPanel::referenceSpectrum (double now, std::array<float, SpectrumAnalyzer::kPoints>& out) const
    {
        if (sourceId == 0)
        {
            if (! proc.isSidechainActive() || ! proc.anaSc.hasSignal()) return false;
            std::copy (proc.anaSc.getDb(), proc.anaSc.getDb() + SpectrumAnalyzer::kPoints, out.begin());
            return true;
        }
        bool found = false;
        InstanceRegistry::get().visit ([&] (ClaudeQ4AudioProcessor& p)
        {
            if (p.getInstanceId() != sourceId || &p == &proc) return;
            p.updateAnalyzers (now);
            if (! p.anaPost.hasSignal()) return;
            std::copy (p.anaPost.getDb(), p.anaPost.getDb() + SpectrumAnalyzer::kPoints, out.begin());
            found = true;
        });
        return found;
    }

    void MatchPanel::showSourceMenu()
    {
        juce::PopupMenu m;
        m.addItem ("Side chain input", true, sourceId == 0, [this] { sourceId = 0; haveDiff = false; });
        std::vector<std::pair<int, juce::String>> others;
        InstanceRegistry::get().visit ([this, &others] (ClaudeQ4AudioProcessor& p)
        {
            if (&p != &proc) others.push_back ({ p.getInstanceId(), p.getInstanceName() });
        });
        for (const auto& [id, name] : others)
        {
            const int sid = id;
            m.addItem ("Instance: " + name, true, sourceId == sid, [this, sid] { sourceId = sid; haveDiff = false; });
        }
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (sourceBtn).withDeletionCheck (*this));
    }

    void MatchPanel::tick (double now)
    {
        const double dt = lastTick > 0.0 ? juce::jlimit (0.0, 0.2, now - lastTick) : 0.0;
        lastTick = now;
        if (learning)
        {
            std::array<float, SpectrumAnalyzer::kPoints> ref {};
            if (referenceSpectrum (now, ref) && proc.anaPre.hasSignal())
            {
                refAcc.add (ref.data());
                curAcc.add (proc.anaPre.getDb());
                learnSeconds += dt;
                if ((int) (learnSeconds * 4.0) != (int) ((learnSeconds - dt) * 4.0)) computeDifference();
            }
        }
        for (auto* b : { &sourceBtn, &learnBtn, &replaceBtn }) b->refresh();
        amountVal.refresh();
        bandsVal.refresh();
        matchBtn.setEnabled (haveDiff);
        repaint (graph.toNearestInt());
    }

    void MatchPanel::computeDifference()
    {
        constexpr int P = SpectrumAnalyzer::kPoints;
        if (refAcc.count < 4 || curAcc.count < 4) { haveDiff = false; return; }
        const double pointsPerOct = (P - 1) / std::log2 (kMaxFreq / kMinFreq);
        // 1) spectral envelopes: power average over +-1/4 octave (removes harmonic / partial structure)
        auto envelope = [pointsPerOct] (const SpectrumAccumulator& acc, std::array<float, P>& out)
        {
            const int half = juce::roundToInt (pointsPerOct * 0.25);
            for (int i = 0; i < P; ++i)
            {
                double s = 0.0; int n = 0;
                for (int k = -half; k <= half; ++k)
                {
                    const int j = i + k;
                    if (j < 0 || j >= P) continue;
                    s += dbToPower (acc.getDb (j)); ++n;
                }
                out[(size_t) i] = (float) powerToDb (s / std::max (1, n) + 1.0e-24);
            }
        };
        std::array<float, P> ref {}, cur {}, raw {}, w {};
        envelope (refAcc, ref);
        envelope (curAcc, cur);
        float refMax = -200.0f, curMax = -200.0f;
        for (int i = 0; i < P; ++i) { refMax = std::max (refMax, ref[(size_t) i]); curMax = std::max (curMax, cur[(size_t) i]); }
        for (int i = 0; i < P; ++i)
        {
            raw[(size_t) i] = juce::jlimit (-12.0f, 12.0f, ref[(size_t) i] - cur[(size_t) i]);
            const double f = SpectrumAnalyzer::pointFreq (i);
            const bool audible = f >= 25.0 && f <= 18000.0;
            w[(size_t) i] = audible && ref[(size_t) i] > refMax - 60.0f && cur[(size_t) i] > curMax - 60.0f ? 1.0f : 0.0f;
        }
        // 2) smooth the difference (+-1/4 octave), 3) remove the overall level offset (match the balance only)
        const int half = juce::roundToInt (pointsPerOct / 4.0);
        std::array<float, P> sm {};
        double mean = 0.0, wsum = 0.0;
        for (int i = 0; i < P; ++i)
        {
            float s = 0.0f, ws = 0.0f;
            for (int k = -half; k <= half; ++k)
            {
                const int j = i + k;
                if (j < 0 || j >= P) continue;
                s += raw[(size_t) j] * w[(size_t) j];
                ws += w[(size_t) j];
            }
            sm[(size_t) i] = ws > 0.0f ? s / ws : 0.0f;
            const double f = SpectrumAnalyzer::pointFreq (i);
            const double ear = (f * f / (f * f + 150.0 * 150.0)) / (1.0 + std::pow (f / 8000.0, 2.0));
            mean += sm[(size_t) i] * w[(size_t) i] * ear;
            wsum += w[(size_t) i] * ear;
        }
        const float offset = wsum > 0.0 ? (float) (mean / wsum) : 0.0f;
        for (int i = 0; i < P; ++i)
            diff[(size_t) i] = juce::jlimit (-9.0f, 9.0f, sm[(size_t) i] - offset);
        haveDiff = true;
    }

    void MatchPanel::applyMatch()
    {
        if (! haveDiff) { if (onStatus) onStatus ("EQ Match: learn the reference first"); return; }
        const int n = 150;
        std::vector<double> freqs ((size_t) n), target ((size_t) n), weight ((size_t) n);
        const double scale = amount / 100.0;
        for (int i = 0; i < n; ++i)
        {
            const double f = 25.0 * std::pow (18000.0 / 25.0, (double) i / (n - 1));
            freqs[(size_t) i] = f;
            const double pos = SpectrumAnalyzer::freqToPoint (f);
            const int j = juce::jlimit (0, SpectrumAnalyzer::kPoints - 2, (int) pos);
            const double t = pos - j;
            target[(size_t) i] = scale * (diff[(size_t) j] * (1.0 - t) + diff[(size_t) j + 1] * t);
            weight[(size_t) i] = 1.0;
        }
        if (! replace)
            for (int i = 0; i < n; ++i) target[(size_t) i] -= display.staticResponseDb (freqs[(size_t) i]);
        const int available = replace ? kMaxBands : kMaxBands - model.usedCount();
        const int want = std::min (available, juce::roundToInt (numBands));
        if (want <= 0) { if (onStatus) onStatus ("EQ Match: no free bands"); return; }
        const auto fit = fitResponse (freqs, target, weight, want, 0.3, true, 3.0);
        const double gs = model.gainScale();
        model.beginGesture ("EQ Match");
        if (replace)
            for (int b = 0; b < kMaxBands; ++b) if (model.isUsed (b)) model.set (b, bpUsed, 0.0);
        std::bitset<kMaxBands> created;
        for (const auto& fb : fit)
        {
            const int b = model.createBand (fb.freq, std::abs (gs) > 1.0e-3 ? fb.gain / gs : 0.0, fb.shape, Slope::dB12, fb.q);
            if (b >= 0) created.set ((size_t) b);
        }
        model.endGesture();
        model.setSelection (created);
        if (onStatus) onStatus ("EQ Match: " + juce::String ((int) created.count()) + " bands created");
        if (onClose) onClose();
    }

    void MatchPanel::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        theme::drawPanel (g, r, 10.0f * uiScale, true);
        g.setFont (theme::font (13.0f * uiScale, 2));
        g.setColour (theme::accent);
        g.drawText ("EQ MATCH", r.reduced (14.0f * uiScale, 8.0f * uiScale).removeFromTop (20.0f * uiScale), juce::Justification::centredLeft, false);

        // mini graph: reference (orange), current (grey), difference (amber)
        g.setColour (juce::Colour (0xff111218));
        g.fillRoundedRectangle (graph, 4.0f * uiScale);
        constexpr int P = SpectrumAnalyzer::kPoints;
        auto gx = [this] (int i) { return graph.getX() + graph.getWidth() * (float) i / (float) (P - 1); };
        if (refAcc.count > 0 && curAcc.count > 0)
        {
            float mx = -200.0f;
            for (int i = 0; i < P; ++i) mx = std::max ({ mx, refAcc.getDb (i), curAcc.getDb (i) });
            auto gy = [this, mx] (float db) { return juce::jlimit (graph.getY(), graph.getBottom(), graph.getY() + (mx - db) / 70.0f * graph.getHeight()); };
            for (int which = 0; which < 2; ++which)
            {
                juce::Path p;
                for (int i = 0; i < P; ++i)
                {
                    const float db = which == 0 ? refAcc.getDb (i) : curAcc.getDb (i);
                    if (i == 0) p.startNewSubPath (gx (i), gy (db)); else p.lineTo (gx (i), gy (db));
                }
                g.setColour (which == 0 ? theme::analyzerSc.withAlpha (0.8f) : theme::analyzerPre.withAlpha (0.8f));
                g.strokePath (p, juce::PathStrokeType (1.0f));
            }
        }
        if (haveDiff)
        {
            juce::Path p;
            for (int i = 0; i < P; ++i)
            {
                const float y = graph.getCentreY() - diff[(size_t) i] * (float) (amount / 100.0) / 12.0f * graph.getHeight() * 0.48f;
                if (i == 0) p.startNewSubPath (gx (i), y); else p.lineTo (gx (i), y);
            }
            theme::drawGlowPath (g, p, theme::accent, 1.6f, 2.0f);
        }
        g.setColour (theme::zeroLine);
        g.drawHorizontalLine ((int) graph.getCentreY(), graph.getX(), graph.getRight());
        if (refAcc.count == 0)
        {
            g.setFont (theme::font (11.5f * uiScale));
            g.setColour (theme::textDim);
            g.drawFittedText ("Choose a reference (side chain or another instance), press Learn and play both signals,\nthen press Match.",
                              graph.reduced (8.0f).toNearestInt(), juce::Justification::centred, 3);
        }
    }

    void MatchPanel::resized()
    {
        const float s = uiScale;
        auto r = getLocalBounds().toFloat().reduced (14.0f * s, 8.0f * s);
        auto top = r.removeFromTop (22.0f * s);
        closeBtn.setBounds (top.removeFromRight (22.0f * s).toNearestInt());
        top.removeFromLeft (90.0f * s);
        sourceBtn.setBounds (top.reduced (4.0f * s, 0.0f).toNearestInt());
        r.removeFromTop (6.0f * s);
        auto bottom = r.removeFromBottom (26.0f * s);
        graph = r.reduced (0.0f, 4.0f * s);
        learnBtn.setBounds (bottom.removeFromLeft (120.0f * s).toNearestInt());
        bottom.removeFromLeft (8.0f * s);
        amountVal.setBounds (bottom.removeFromLeft (100.0f * s).toNearestInt());
        bandsVal.setBounds (bottom.removeFromLeft (80.0f * s).toNearestInt());
        matchBtn.setBounds (bottom.removeFromRight (70.0f * s).toNearestInt());
        bottom.removeFromRight (6.0f * s);
        replaceBtn.setBounds (bottom.removeFromRight (106.0f * s).toNearestInt());
        for (auto* b : { &sourceBtn, &learnBtn, &matchBtn, &replaceBtn }) b->fontSize = 12.0f * s;
        amountVal.fontSize = bandsVal.fontSize = 12.0f * s;
    }
}
