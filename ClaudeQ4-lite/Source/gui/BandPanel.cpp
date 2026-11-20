/*
    Claude-Q 4 - floating band panel
*/
#include "BandPanel.h"

namespace cq
{
    namespace
    {
        double normLog (double v, double lo, double hi) { return std::log (std::max (v, lo) / lo) / std::log (hi / lo); }
    }

    BandPanel::BandPanel (EditorModel& m) : model (m)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &powerBtn, &dynBtn, &specBtn, &expandBtn, &prevBtn, &nextBtn, &closeBtn,
                                                                  &soloBtn, &splitBtn, &auditionBtn, &shapeBtn, &slopeBtn, &placeBtn,
                                                                  &autoThrBtn, &autoTimesBtn, &scBtn, &freeBtn, &tiltBtn,
                                                                  &freqKnob, &gainKnob, &qKnob, &thrKnob, &attKnob, &relKnob, &densityKnob,
                                                                  &trigLo, &trigHi })
            addAndMakeVisible (c);
        shapeBtn.showChevron = slopeBtn.showChevron = placeBtn.showChevron = true;
        for (auto* b : { &autoThrBtn, &autoTimesBtn, &scBtn, &freeBtn, &tiltBtn }) b->fontSize = 11.0f;
        gainKnob.bipolar = true;
        gainKnob.hasRing = true;
        trigLo.caption = "LO";
        trigHi.caption = "HI";
        freqKnob.setTooltip ("Frequency - drag, Shift = fine, right click to type a value (e.g. 2.5k or C4)");
        gainKnob.setTooltip ("Gain - drag the outer ring to set the dynamic range (compression / expansion)");
        qKnob.setTooltip ("Q / bandwidth");
        thrKnob.setTooltip ("Threshold - AUTO follows the level of the band's own signal");
        attKnob.setTooltip ("Attack - AUTO = program dependent; drag for a fixed time");
        relKnob.setTooltip ("Release - AUTO = program dependent; drag for a fixed time");
        densityKnob.setTooltip ("Spectral density - low: reacts to wide areas, high: to narrow resonances");
        scBtn.setTooltip ("Trigger the dynamics from the external side chain input");
        freeBtn.setTooltip ("Free trigger range instead of the band's own frequency range");
        tiltBtn.setTooltip ("Spectral tilt: +3 dB/oct detector weighting (natural spectral balance)");
        autoThrBtn.setTooltip ("Automatic threshold");
        autoTimesBtn.setTooltip ("Program dependent attack and release");
        wireKnobs();
        wireButtons();
        expanded = (bool) model.proc.getUiProperty ("panelExpanded", false);
        setExpanded (expanded);
    }

    BandPanel::~BandPanel()
    {
        if (soloActive || auditionActive) model.proc.setSolo (-1, false);
        if (! starts.empty()) model.endGesture();
    }

    std::vector<int> BandPanel::targets() const
    {
        const int f = focus();
        if (f < 0 || ! model.isUsed (f)) return {};
        if (! model.isSelected (f)) return { f };
        auto sel = model.selection();
        std::vector<int> out { f };
        for (int b : sel) if (b != f) out.push_back (b);
        return out;
    }

    void BandPanel::beginEdit (BandParam k, const juce::String& name)
    {
        if (! starts.empty()) endEdit();
        model.beginGesture (name);
        starts.clear();
        accum = 0.0;
        for (int t : targets()) starts.push_back ({ t, model.value (t, k) });
    }

    void BandPanel::dragEdit (BandParam k, double delta, std::function<double (double, double)> fn)
    {
        accum += delta;
        for (const auto& s : starts) model.set (s.band, k, fn (s.value, accum));
    }

    void BandPanel::endEdit()
    {
        if (starts.empty() && ! model.inGesture()) return;
        starts.clear();
        model.endGesture();
    }

    void BandPanel::stepFocus (int dir)
    {
        const int f = focus();
        for (int k = 1; k <= kMaxBands; ++k)
        {
            const int b = (((f < 0 ? 0 : f) + dir * k) % kMaxBands + kMaxBands) % kMaxBands;
            if (model.isUsed (b)) { model.selectOnly (b); return; }
        }
    }

    //==========================================================================
    void BandPanel::wireKnobs()
    {
        auto F = [this] { return focus(); };

        // ---- FREQ
        const double fSpan = std::log (kMaxFreq / kMinFreq);
        freqKnob.getNormalised = [this, F] { const int f = F(); return f < 0 ? 0.5 : normLog (model.value (f, bpFreq), kMinFreq, kMaxFreq); };
        freqKnob.getText = [this, F]
        {
            const int f = F();
            if (f < 0) return juce::String();
            auto t = formatFreq (model.value (f, bpFreq));
            if (isPianoSnapping && isPianoSnapping()) t << " " << noteName (model.value (f, bpFreq), false);
            return t;
        };
        auto freqFn = [this, fSpan] (double start, double acc)
        {
            double fr = clampT (start * std::exp (acc * fSpan), kMinFreq, kMaxFreq);
            if (isPianoSnapping && isPianoSnapping()) fr = clampT (noteToFreq (std::round (freqToNote (fr))), kMinFreq, kMaxFreq);
            return fr;
        };
        freqKnob.onDragStart = [this] { beginEdit (bpFreq, "Frequency"); };
        freqKnob.onDrag = [this, freqFn] (double d) { dragEdit (bpFreq, d, freqFn); };
        freqKnob.onDragEnd = [this] { endEdit(); };
        freqKnob.onWheel = [this, freqFn] (double d) { beginEdit (bpFreq, "Frequency"); dragEdit (bpFreq, d * 0.2, freqFn); endEdit(); };
        freqKnob.onTextEntered = [this] (const juce::String& s)
        {
            const double f = parseFreq (s, -1.0);
            if (f <= 0.0) return;
            model.beginGesture ("Frequency");
            for (int t : targets()) model.set (t, bpFreq, clampT (f, kMinFreq, kMaxFreq));
            model.endGesture();
        };

        // ---- GAIN + dynamic range ring
        auto gNorm = [] (double g) { return clampT ((g + 30.0) / 60.0, 0.0, 1.0); };
        gainKnob.getNormalised = [this, F, gNorm] { const int f = F(); return f < 0 ? 0.5 : gNorm (model.value (f, bpGain)); };
        gainKnob.getText = [this, F]
        {
            const int f = F();
            if (f < 0) return juce::String();
            const auto bp = model.band (f);
            return shapeUsesGain (bp.shape) ? formatGain (bp.gainDb) : juce::String ("-");
        };
        gainKnob.isActive = [this, F] { const int f = F(); return f >= 0 && shapeUsesGain (model.band (f).shape); };
        gainKnob.onDragStart = [this] { beginEdit (bpGain, "Gain"); };
        gainKnob.onDrag = [this] (double d) { dragEdit (bpGain, d, [] (double s, double a) { return clampT (s + a * 60.0, kMinGain, kMaxGain); }); };
        gainKnob.onDragEnd = [this] { endEdit(); };
        gainKnob.onWheel = [this] (double d) { beginEdit (bpGain, "Gain"); dragEdit (bpGain, d * 0.25, [] (double s, double a) { return clampT (s + a * 60.0, kMinGain, kMaxGain); }); endEdit(); };
        gainKnob.onDoubleClick = [this]
        {
            model.beginGesture ("Reset gain");
            for (int t : targets()) model.set (t, bpGain, 0.0);
            model.endGesture();
        };
        gainKnob.onTextEntered = [this] (const juce::String& s)
        {
            const auto txt = s.retainCharacters ("+-.0123456789");
            if (txt.isEmpty()) return;
            model.beginGesture ("Gain");
            for (int t : targets()) model.set (t, bpGain, clampT ((double) txt.getFloatValue(), kMinGain, kMaxGain));
            model.endGesture();
        };
        gainKnob.ringVisible = [this, F]
        {
            const int f = F();
            return f >= 0 && model.value (f, bpDynOn) > 0.5 && shapeSupportsDynamics (model.band (f).shape);
        };
        gainKnob.getRingFrom = [this, F, gNorm] { const int f = F(); return f < 0 ? 0.5 : gNorm (model.value (f, bpGain)); };
        gainKnob.getRingTo = [this, F, gNorm] { const int f = F(); return f < 0 ? 0.5 : gNorm (model.value (f, bpGain) + model.value (f, bpDynRange)); };
        gainKnob.getRingLive = [this, F, gNorm]
        {
            const int f = F();
            if (f < 0) return 0.5;
            const double sc = model.gainScale();
            const double dyn = std::abs (sc) > 1.0e-3 ? (double) model.proc.engine.getDynamicGainDb (f) / sc : 0.0;
            return gNorm (model.value (f, bpGain) + dyn);
        };
        gainKnob.getRingText = [this, F]
        {
            const int f = F();
            if (f < 0) return juce::String();
            const double r = model.value (f, bpDynRange);
            return (r < 0 ? "Comp " : "Exp ") + juce::String (std::abs (r), 1) + " dB";
        };
        gainKnob.onRingStart = [this]
        {
            beginEdit (bpDynRange, "Dynamic range");
            for (const auto& s : starts)
                if (shapeSupportsDynamics (model.band (s.band).shape) && model.value (s.band, bpDynOn) < 0.5)
                    model.set (s.band, bpDynOn, 1.0);
        };
        gainKnob.onRingDrag = [this] (double d) { dragEdit (bpDynRange, d, [] (double s, double a) { return clampT (s + a * 60.0, -30.0, 30.0); }); };
        gainKnob.onRingEnd = [this] { endEdit(); };

        // ---- Q
        const double qSpan = std::log (kMaxQ / kMinQ);
        qKnob.getNormalised = [this, F] { const int f = F(); return f < 0 ? 0.5 : normLog (model.value (f, bpQ), kMinQ, kMaxQ); };
        qKnob.getText = [this, F] { const int f = F(); return f < 0 ? juce::String() : formatQ (model.value (f, bpQ)); };
        qKnob.isActive = [this, F] { const int f = F(); if (f < 0) return false; const auto bp = model.band (f); return shapeUsesQ (bp.shape, bp.slope); };
        auto qFn = [qSpan] (double s, double a) { return clampT (s * std::exp (a * qSpan), kMinQ, kMaxQ); };
        qKnob.onDragStart = [this] { beginEdit (bpQ, "Q"); };
        qKnob.onDrag = [this, qFn] (double d) { dragEdit (bpQ, d, qFn); };
        qKnob.onDragEnd = [this] { endEdit(); };
        qKnob.onWheel = [this, qFn] (double d) { beginEdit (bpQ, "Q"); dragEdit (bpQ, d * 0.2, qFn); endEdit(); };
        qKnob.onDoubleClick = [this]
        {
            model.beginGesture ("Reset Q");
            for (int t : targets())
            {
                const auto s = model.band (t).shape;
                model.set (t, bpQ, (s == Shape::LowCut || s == Shape::HighCut) ? 0.71 : 1.0);
            }
            model.endGesture();
        };
        qKnob.onTextEntered = [this] (const juce::String& s)
        {
            const double q = s.retainCharacters (".0123456789").getDoubleValue();
            if (q <= 0.0) return;
            model.beginGesture ("Q");
            for (int t : targets()) model.set (t, bpQ, clampT (q, kMinQ, kMaxQ));
            model.endGesture();
        };

        // ---- THRESHOLD
        thrKnob.getNormalised = [this, F]
        {
            const int f = F();
            if (f < 0) return 0.5;
            const double t = model.value (f, bpAutoThr) > 0.5 ? (double) model.proc.engine.getThresholdDb (f) : model.value (f, bpThreshold);
            return clampT ((t + 80.0) / 80.0, 0.0, 1.0);
        };
        thrKnob.getText = [this, F]
        {
            const int f = F();
            if (f < 0) return juce::String();
            if (model.value (f, bpAutoThr) > 0.5) return "Auto " + juce::String (juce::jlimit (-80.0f, 0.0f, model.proc.engine.getThresholdDb (f)), 1);
            return juce::String (model.value (f, bpThreshold), 1) + " dB";
        };
        thrKnob.isActive = [this, F] { const int f = F(); return f >= 0 && model.value (f, bpDynOn) > 0.5; };
        thrKnob.onDragStart = [this]
        {
            beginEdit (bpThreshold, "Threshold");
            for (auto& s : starts)
                if (model.value (s.band, bpAutoThr) > 0.5)
                {
                    // manual override starts from where the automatic threshold currently is
                    s.value = clampT ((double) model.proc.engine.getThresholdDb (s.band), -80.0, 0.0);
                    model.set (s.band, bpAutoThr, 0.0);
                    model.set (s.band, bpThreshold, s.value);
                }
        };
        thrKnob.onDrag = [this] (double d) { dragEdit (bpThreshold, d, [] (double s, double a) { return clampT (s + a * 80.0, -80.0, 0.0); }); };
        thrKnob.onDragEnd = [this] { endEdit(); };
        thrKnob.onDoubleClick = [this]
        {
            model.beginGesture ("Auto threshold");
            for (int t : targets()) model.set (t, bpAutoThr, 1.0);
            model.endGesture();
        };

        // ---- ATTACK / RELEASE (auto or manual override)
        auto timeKnob = [this, F] (Knob& k, BandParam p, double lo, double hi, const juce::String& name)
        {
            k.getNormalised = [this, F, p, lo, hi] { const int f = F(); return f < 0 ? 0.5 : (model.value (f, bpAutoTimes) > 0.5 ? 0.5 : normLog (model.value (f, p), lo, hi)); };
            k.getText = [this, F, p] { const int f = F(); if (f < 0) return juce::String(); return model.value (f, bpAutoTimes) > 0.5 ? juce::String ("Auto") : formatMs (model.value (f, p)); };
            k.isActive = [this, F] { const int f = F(); return f >= 0 && model.value (f, bpDynOn) > 0.5 && model.value (f, bpAutoTimes) < 0.5; };
            k.onDragStart = [this, p, name]
            {
                beginEdit (p, name);
                for (const auto& s : starts) if (model.value (s.band, bpAutoTimes) > 0.5) model.set (s.band, bpAutoTimes, 0.0);
            };
            const double span = std::log (hi / lo);
            k.onDrag = [this, p, lo, hi, span] (double d) { dragEdit (p, d, [lo, hi, span] (double s, double a) { return clampT (s * std::exp (a * span), lo, hi); }); };
            k.onDragEnd = [this] { endEdit(); };
            k.onDoubleClick = [this]
            {
                model.beginGesture ("Auto attack / release");
                for (int t : targets()) model.set (t, bpAutoTimes, 1.0);
                model.endGesture();
            };
        };
        timeKnob (attKnob, bpAttack, 0.1, 500.0, "Attack");
        timeKnob (relKnob, bpRelease, 5.0, 5000.0, "Release");

        // ---- DENSITY
        densityKnob.getNormalised = [this, F] { const int f = F(); return f < 0 ? 0.5 : model.value (f, bpDensity) / 100.0; };
        densityKnob.getText = [this, F] { const int f = F(); return f < 0 ? juce::String() : juce::String (juce::roundToInt (model.value (f, bpDensity))) + "%"; };
        densityKnob.isActive = [this, F] { const int f = F(); return f >= 0 && model.value (f, bpSpectral) > 0.5 && model.value (f, bpDynOn) > 0.5; };
        densityKnob.onDragStart = [this] { beginEdit (bpDensity, "Spectral density"); };
        densityKnob.onDrag = [this] (double d) { dragEdit (bpDensity, d, [] (double s, double a) { return clampT (s + a * 100.0, 0.0, 100.0); }); };
        densityKnob.onDragEnd = [this] { endEdit(); };
        densityKnob.onDoubleClick = [this]
        {
            model.beginGesture ("Spectral density");
            for (int t : targets()) model.set (t, bpDensity, 50.0);
            model.endGesture();
        };

        // ---- trigger range
        auto trig = [this, F, fSpan] (DragValue& v, BandParam p, const juce::String& name)
        {
            v.getText = [this, F, p] { const int f = F(); return f < 0 ? juce::String() : formatFreqShort (model.value (f, p)); };
            v.onDragStart = [this, p, name] { beginEdit (p, name); };
            v.onDrag = [this, p, fSpan] (double d) { dragEdit (p, d / 300.0, [fSpan] (double s, double a) { return clampT (s * std::exp (a * fSpan), kMinFreq, kMaxFreq); }); };
            v.onDragEnd = [this] { endEdit(); };
            v.onTextEntered = [this, p] (const juce::String& s)
            {
                const double f = parseFreq (s, -1.0);
                if (f <= 0.0) return;
                model.beginGesture ("Trigger range");
                for (int t : targets()) model.set (t, p, clampT (f, kMinFreq, kMaxFreq));
                model.endGesture();
            };
            v.setTooltip ("Trigger filter - drag, Alt / right click to type");
        };
        trig (trigLo, bpTrigLo, "Trigger low");
        trig (trigHi, bpTrigHi, "Trigger high");
    }

    void BandPanel::wireButtons()
    {
        auto F = [this] { return focus(); };
        auto flag = [this, F] (BandParam p) { return [this, F, p] { const int f = F(); return f >= 0 && model.value (f, p) > 0.5; }; };
        auto toggleAll = [this, F] (BandParam p, const juce::String& name)
        {
            const int f = F();
            if (f < 0) return;
            const double nv = model.value (f, p) > 0.5 ? 0.0 : 1.0;
            model.beginGesture (name);
            for (int t : targets()) model.set (t, p, nv);
            model.endGesture();
        };

        powerBtn.getState = flag (bpEnabled);
        powerBtn.onClick = [toggleAll] { toggleAll (bpEnabled, "Enable band"); };

        dynBtn.getState = flag (bpDynOn);
        dynBtn.onClick = [this, F]
        {
            const int f = F();
            if (f < 0) return;
            const bool on = model.value (f, bpDynOn) < 0.5;
            model.beginGesture ("Dynamic EQ");
            for (int t : targets())
            {
                if (! shapeSupportsDynamics (model.band (t).shape)) continue;
                model.set (t, bpDynOn, on ? 1.0 : 0.0);
                if (on && std::abs (model.value (t, bpDynRange)) < 0.1)
                {
                    const double g = model.value (t, bpGain);
                    model.set (t, bpDynRange, g > 0.5 ? -std::min (6.0, g) : (g < -0.5 ? std::max (-6.0, g) : -6.0));
                }
            }
            model.endGesture();
        };

        specBtn.getState = flag (bpSpectral);
        specBtn.onClick = [this, F]
        {
            const int f = F();
            if (f < 0) return;
            const bool on = model.value (f, bpSpectral) < 0.5;
            model.beginGesture ("Spectral dynamics");
            for (int t : targets())
            {
                if (! shapeSupportsDynamics (model.band (t).shape)) continue;
                model.set (t, bpSpectral, on ? 1.0 : 0.0);
                if (on && model.value (t, bpDynOn) < 0.5)
                {
                    model.set (t, bpDynOn, 1.0);
                    if (std::abs (model.value (t, bpDynRange)) < 0.1) model.set (t, bpDynRange, -6.0);
                }
            }
            model.endGesture();
            if (on && ! expanded) setExpanded (true);
        };

        expandBtn.getState = [this] { return expanded; };
        expandBtn.onClick = [this] { setExpanded (! expanded); };
        prevBtn.onClick = [this] { stepFocus (-1); };
        nextBtn.onClick = [this] { stepFocus (1); };
        closeBtn.onClick = [this, F]
        {
            const int f = F();
            if (f < 0) return;
            if (model.isSelected (f) && model.selection().size() > 1) model.deleteSelected();
            else model.deleteBand (f);
        };

        soloBtn.getState = [this, F] { const int f = F(); return f >= 0 && model.proc.getSoloBand() == f && ! auditionActive; };
        soloBtn.onStateChange = [this, F]
        {
            const bool down = soloBtn.isDown();
            if (down && ! soloActive && F() >= 0) { soloActive = true; model.proc.setSolo (F(), false); }
            else if (! down && soloActive) { soloActive = false; model.proc.setSolo (-1, false); }
        };
        auditionBtn.getState = [this] { return auditionActive; };
        auditionBtn.onStateChange = [this, F]
        {
            const bool down = auditionBtn.isDown();
            if (down && ! auditionActive && F() >= 0) { auditionActive = true; model.proc.setSolo (F(), true); }
            else if (! down && auditionActive) { auditionActive = false; model.proc.setSolo (-1, false); }
        };

        splitBtn.onClick = [this] { showSplitMenu(); };
        shapeBtn.onClick = [this] { showShapeMenu(); };
        slopeBtn.onClick = [this] { showSlopeMenu(); };
        placeBtn.onClick = [this] { showPlacementMenu(); };

        shapeBtn.getText = [this, F]
        {
            const int f = F();
            if (f < 0) return juce::String();
            const auto s = model.band (f).shape;
            for (int t : targets()) if (model.band (t).shape != s) return juce::String ("(multiple)");
            return juce::String (shapeName (s));
        };
        slopeBtn.getText = [this, F]
        {
            const int f = F();
            if (f < 0) return juce::String();
            const auto bp = model.band (f);
            if (! shapeUsesSlope (bp.shape)) return juce::String ("-");
            for (int t : targets()) { const auto o = model.band (t); if (shapeUsesSlope (o.shape) && o.slope != bp.slope) return juce::String ("(multiple)"); }
            return juce::String (slopeName (bp.slope));
        };
        placeBtn.getText = [this, F]
        {
            const int f = F();
            if (f < 0) return juce::String();
            const auto p = model.band (f).place;
            for (int t : targets()) if (model.band (t).place != p) return juce::String ("(multiple)");
            return juce::String (placementName (p));
        };

        autoThrBtn.getState = flag (bpAutoThr);
        autoThrBtn.onClick = [this, F]
        {
            const int f = F();
            if (f < 0) return;
            const bool on = model.value (f, bpAutoThr) < 0.5;
            model.beginGesture ("Auto threshold");
            for (int t : targets())
            {
                if (! on) model.set (t, bpThreshold, clampT ((double) model.proc.engine.getThresholdDb (t), -80.0, 0.0));
                model.set (t, bpAutoThr, on ? 1.0 : 0.0);
            }
            model.endGesture();
        };
        autoTimesBtn.getState = flag (bpAutoTimes);
        autoTimesBtn.onClick = [toggleAll] { toggleAll (bpAutoTimes, "Auto attack / release"); };
        scBtn.getState = flag (bpExtSc);
        scBtn.onClick = [toggleAll] { toggleAll (bpExtSc, "External side chain"); };
        tiltBtn.getState = flag (bpSpecTilt);
        tiltBtn.onClick = [toggleAll] { toggleAll (bpSpecTilt, "Spectral tilt"); };
        freeBtn.getState = flag (bpTrigFree);
        freeBtn.onClick = [this, F]
        {
            const int f = F();
            if (f < 0) return;
            const bool on = model.value (f, bpTrigFree) < 0.5;
            model.beginGesture ("Free trigger range");
            for (int t : targets())
            {
                model.set (t, bpTrigFree, on ? 1.0 : 0.0);
                if (on)
                {
                    const double fr = model.value (t, bpFreq);
                    model.set (t, bpTrigLo, clampT (fr / 2.0, kMinFreq, kMaxFreq));
                    model.set (t, bpTrigHi, clampT (fr * 2.0, kMinFreq, kMaxFreq));
                }
            }
            model.endGesture();
        };
    }

    //==========================================================================
    void BandPanel::showShapeMenu()
    {
        const int f = focus();
        if (f < 0) return;
        const auto cur = model.band (f).shape;
        juce::PopupMenu m;
        for (int s = 0; s < (int) Shape::Count; ++s)
            m.addItem (shapeName ((Shape) s), true, cur == (Shape) s, [this, s]
            {
                model.beginGesture ("Change shape");
                for (int t : targets()) model.setShape (t, (Shape) s);
                model.endGesture();
            });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (shapeBtn).withDeletionCheck (*this));
    }

    void BandPanel::showSlopeMenu()
    {
        const int f = focus();
        if (f < 0) return;
        const auto bp = model.band (f);
        if (! shapeUsesSlope (bp.shape)) return;
        juce::PopupMenu m;
        for (int s = 0; s < (int) Slope::Count; ++s)
            if (slopeValidForShape (bp.shape, (Slope) s))
                m.addItem (slopeName ((Slope) s), true, bp.slope == (Slope) s, [this, s]
                {
                    model.beginGesture ("Change slope");
                    for (int t : targets()) model.set (t, bpSlope, (double) (int) sanitiseSlope (model.band (t).shape, (Slope) s));
                    model.endGesture();
                });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (slopeBtn).withDeletionCheck (*this));
    }

    void BandPanel::showPlacementMenu()
    {
        const int f = focus();
        if (f < 0) return;
        const auto cur = model.band (f).place;
        juce::PopupMenu m;
        for (int p = 0; p < (int) Placement::Count; ++p)
            m.addItem (placementName ((Placement) p), true, cur == (Placement) p, [this, p]
            {
                model.beginGesture ("Stereo placement");
                for (int t : targets()) model.set (t, bpPlace, (double) p);
                model.endGesture();
            });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (placeBtn).withDeletionCheck (*this));
    }

    void BandPanel::showSplitMenu()
    {
        const int f = focus();
        if (f < 0) return;
        const bool stereoBand = model.band (f).place == Placement::Stereo && model.isStereo();
        juce::PopupMenu m;
        m.addItem ("Split into Left / Right", stereoBand && model.freeSlot() >= 0, false, [this, f] { model.splitBand (f, false); });
        m.addItem ("Split into Mid / Side", stereoBand && model.freeSlot() >= 0, false, [this, f] { model.splitBand (f, true); });
        m.addItem ("Duplicate band", model.freeSlot() >= 0, false, [this, f] { const int b = model.duplicateBand (f); if (b >= 0) model.selectOnly (b); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (splitBtn).withDeletionCheck (*this));
    }

    void BandPanel::setExpanded (bool b)
    {
        expanded = b;
        model.proc.setUiProperty ("panelExpanded", b);
        for (auto* c : std::initializer_list<juce::Component*> { &thrKnob, &attKnob, &relKnob, &densityKnob, &autoThrBtn, &autoTimesBtn,
                                                                  &scBtn, &freeBtn, &tiltBtn, &trigLo, &trigHi, &auditionBtn })
            c->setVisible (b);
        expandBtn.icon = b ? theme::Icon::ChevronDown : theme::Icon::Expand;
        expandBtn.repaint();
        if (onLayoutChanged) onLayoutChanged();
        resized();
    }

    //==========================================================================
    void BandPanel::refresh()
    {
        const int f = focus();
        if (f < 0 || ! model.isUsed (f)) return;
        const auto bp = model.band (f);
        const auto col = theme::bandColour (f);
        for (auto* k : { &freqKnob, &gainKnob, &qKnob, &thrKnob, &attKnob, &relKnob, &densityKnob })
            if (k->colour != col) { k->colour = col; k->repaint(); }

        const bool dynCapable = shapeSupportsDynamics (bp.shape);
        const bool dynOn = model.value (f, bpDynOn) > 0.5;
        dynBtn.setEnabled (dynCapable);
        specBtn.setEnabled (dynCapable);
        slopeBtn.setEnabled (shapeUsesSlope (bp.shape));
        placeBtn.setVisible (model.isStereo());
        splitBtn.setEnabled (model.isStereo() || model.freeSlot() >= 0);
        auditionBtn.setEnabled (dynOn);
        scBtn.setEnabled (dynOn);
        freeBtn.setEnabled (dynOn);
        autoThrBtn.setEnabled (dynOn);
        autoTimesBtn.setEnabled (dynOn);
        tiltBtn.setEnabled (dynOn && bp.spectral);
        trigLo.setEnabled (dynOn && bp.triggerFree);
        trigHi.setEnabled (dynOn && bp.triggerFree);
        trigLo.setAlpha (dynOn && bp.triggerFree ? 1.0f : 0.35f);
        trigHi.setAlpha (dynOn && bp.triggerFree ? 1.0f : 0.35f);

        for (auto* k : { &freqKnob, &gainKnob, &qKnob, &thrKnob, &attKnob, &relKnob, &densityKnob }) k->refresh();
        for (auto* b : { &powerBtn, &dynBtn, &specBtn, &expandBtn, &soloBtn, &auditionBtn }) b->refresh();
        for (auto* b : { &shapeBtn, &slopeBtn, &placeBtn, &autoThrBtn, &autoTimesBtn, &scBtn, &freeBtn, &tiltBtn }) b->refresh();
        trigLo.refresh();
        trigHi.refresh();
        repaint (headerArea.toNearestInt());
    }

    void BandPanel::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        theme::drawPanel (g, r, 10.0f * uiScale, true);
        const int f = focus();
        if (f < 0) return;
        const auto col = theme::bandColour (f);
        g.setColour (col.withAlpha (0.9f));
        g.fillRoundedRectangle (juce::Rectangle<float> (r.getX() + 18.0f * uiScale, r.getY() + 1.0f, r.getWidth() - 36.0f * uiScale, 2.5f * uiScale), 1.2f);

        // header: band number / selection count
        const auto sel = model.selection();
        juce::String title = "Band " + juce::String (f + 1);
        if (sel.size() > 1 && model.isSelected (f)) title = juce::String ((int) sel.size()) + " bands";
        g.setFont (theme::font (12.0f * uiScale, 2));
        g.setColour (theme::textDim);
        const auto t = juce::Rectangle<float> ((float) prevBtn.getRight(), headerArea.getY(), (float) (nextBtn.getX() - prevBtn.getRight()), headerArea.getHeight());
        g.drawText (title, t, juce::Justification::centred, false);

        // dynamics section
        if (expanded)
        {
            g.setColour (theme::panelEdge.withAlpha (0.7f));
            g.drawHorizontalLine ((int) dynArea.getBottom(), dynArea.getX() + 10.0f, dynArea.getRight() - 10.0f);
            g.setColour (juce::Colours::black.withAlpha (0.12f));
            g.fillRoundedRectangle (dynArea.reduced (2.0f, 1.0f), 6.0f * uiScale);
            g.setFont (theme::font (9.5f * uiScale, 2));
            g.setColour (theme::textFaint);
            g.drawText ("TRIGGER", juce::Rectangle<float> ((float) scBtn.getX(), dynArea.getY() + 2.0f * uiScale, 120.0f * uiScale, 12.0f * uiScale), juce::Justification::centredLeft, false);
            if (model.proc.engine.dynamicsDisabledByResolution())
            {
                g.setColour (theme::meterRed.withAlpha (0.85f));
                g.drawText ("Dynamics need a linear phase resolution up to High",
                            dynArea.withHeight (12.0f * uiScale).translated (0.0f, dynArea.getHeight() - 13.0f * uiScale), juce::Justification::centred, false);
            }
        }
        g.setColour (theme::panelEdge.withAlpha (0.6f));
        g.drawHorizontalLine ((int) headerArea.getBottom(), r.getX() + 12.0f, r.getRight() - 12.0f);
    }

    void BandPanel::resized()
    {
        const float s = uiScale;
        auto r = getLocalBounds().toFloat().reduced (10.0f * s, 6.0f * s);
        headerArea = r.removeFromTop (24.0f * s);
        auto h = headerArea;
        auto place = [] (juce::Component& c, juce::Rectangle<float> a) { c.setBounds (a.toNearestInt()); };
        place (powerBtn, h.removeFromLeft (24.0f * s));
        place (closeBtn, h.removeFromRight (24.0f * s));
        place (nextBtn, h.removeFromRight (22.0f * s));
        h.removeFromRight (64.0f * s);
        place (prevBtn, h.removeFromRight (22.0f * s));
        {
            auto c = juce::Rectangle<float> (3.0f * 28.0f * s, 24.0f * s).withCentre ({ (float) getWidth() * 0.5f, headerArea.getCentreY() });
            place (dynBtn, c.removeFromLeft (28.0f * s));
            place (specBtn, c.removeFromLeft (28.0f * s));
            place (expandBtn, c.removeFromLeft (28.0f * s));
        }
        if (expanded)
        {
            dynArea = r.removeFromTop (90.0f * s);
            auto d = dynArea.reduced (4.0f * s, 4.0f * s);
            const float kw = 70.0f * s, kh = 64.0f * s;
            auto col1 = d.removeFromLeft (kw);
            place (thrKnob, col1.removeFromTop (kh));
            place (autoThrBtn, col1.removeFromTop (18.0f * s).reduced (10.0f * s, 0.0f));
            d.removeFromLeft (8.0f * s);
            auto col2 = d.removeFromLeft (kw * 2.0f);
            auto kn = col2.removeFromTop (kh);
            place (attKnob, kn.removeFromLeft (kw));
            place (relKnob, kn);
            place (autoTimesBtn, col2.removeFromTop (18.0f * s).reduced (40.0f * s, 0.0f));
            d.removeFromLeft (8.0f * s);
            auto col3 = d.removeFromLeft (kw);
            place (densityKnob, col3.removeFromTop (kh));
            place (tiltBtn, col3.removeFromTop (18.0f * s).reduced (10.0f * s, 0.0f));
            d.removeFromLeft (12.0f * s);
            // trigger block
            d.removeFromTop (14.0f * s);
            auto row1 = d.removeFromTop (22.0f * s);
            place (scBtn, row1.removeFromLeft (64.0f * s));
            row1.removeFromLeft (4.0f * s);
            place (freeBtn, row1.removeFromLeft (52.0f * s));
            row1.removeFromLeft (4.0f * s);
            place (auditionBtn, row1.removeFromLeft (26.0f * s));
            d.removeFromTop (6.0f * s);
            auto row2 = d.removeFromTop (22.0f * s);
            place (trigLo, row2.removeFromLeft (row2.getWidth() * 0.5f).reduced (1.0f, 0.0f));
            place (trigHi, row2.reduced (1.0f, 0.0f));
        }
        else dynArea = {};

        mainArea = r;
        auto m = r.reduced (0.0f, 4.0f * s);
        auto left = m.removeFromLeft (118.0f * s);
        auto right = m.removeFromRight (118.0f * s);
        const float bh = 26.0f * s;
        place (shapeBtn, left.removeFromTop (left.getHeight() * 0.5f).withSizeKeepingCentre (left.getWidth(), bh));
        place (slopeBtn, left.withSizeKeepingCentre (left.getWidth(), bh));
        place (placeBtn, right.removeFromTop (right.getHeight() * 0.5f).withSizeKeepingCentre (right.getWidth(), bh));
        {
            auto row = right.withSizeKeepingCentre (right.getWidth(), bh);
            place (soloBtn, row.removeFromLeft (row.getWidth() * 0.5f).withSizeKeepingCentre (30.0f * s, bh));
            place (splitBtn, row.withSizeKeepingCentre (30.0f * s, bh));
        }
        const float kH = std::min (m.getHeight(), 88.0f * s);
        auto knobs = m.withSizeKeepingCentre (m.getWidth(), kH);
        const float total = 78.0f * s + 96.0f * s + 78.0f * s;
        auto row = knobs.withSizeKeepingCentre (total, kH);
        place (freqKnob, row.removeFromLeft (78.0f * s).reduced (2.0f * s, 6.0f * s));
        place (gainKnob, row.removeFromLeft (96.0f * s));
        place (qKnob, row.reduced (2.0f * s, 6.0f * s));
        for (auto* k : { &freqKnob, &gainKnob, &qKnob, &thrKnob, &attKnob, &relKnob, &densityKnob }) k->labelHeight = 15.0f * s;
    }
}
