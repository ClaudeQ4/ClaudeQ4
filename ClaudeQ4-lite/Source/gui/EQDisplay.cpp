/*
    Claude-Q 4 - main EQ display
*/
#include "EQDisplay.h"
#include "BandFitter.h"
#include "plugin/InstanceRegistry.h"

namespace cq
{
    namespace
    {
        const double kLogSpan = std::log (kMaxFreq / kMinFreq);

        bool isTiltShape (Shape s) { return s == Shape::TiltShelf || s == Shape::FlatTilt; }

        juce::String modifierName()
        {
           #if JUCE_MAC
            return "Cmd";
           #else
            return "Ctrl";
           #endif
        }

        bool isBlackKey (int note)
        {
            const int k = ((note % 12) + 12) % 12;
            return k == 1 || k == 3 || k == 6 || k == 8 || k == 10;
        }
    }

    EQDisplay::EQDisplay (EditorModel& m, bool c) : model (m), proc (m.proc), compact (c)
    {
        setOpaque (true);
        setWantsKeyboardFocus (! compact);
        setMouseClickGrabsKeyboardFocus (! compact);
        rangeDb = compact ? 12.0 : (double) proc.getUiProperty ("displayRange", 12.0);
        if (rangeDb != 3.0 && rangeDb != 6.0 && rangeDb != 12.0 && rangeDb != 30.0) rangeDb = 12.0;
        pianoVisible = ! compact && (bool) proc.getUiProperty ("piano", false);
        ana = proc.getAnalyzerSettings();
        collision.fill (0.0f);
    }

    EQDisplay::~EQDisplay()
    {
        if (isAlive && ! isAlive()) return;
        if (drag == DragMode::CardSolo || drag == DragMode::AltSolo) proc.setSolo (-1, false);
        if (drag == DragMode::Node || drag == DragMode::Range || drag == DragMode::Piano || drag == DragMode::CardValue) model.endGesture();
        if (wheelGesture) endWheelGesture();
    }

    //==========================================================================
    void EQDisplay::setRange (double db)
    {
        rangeDb = db;
        if (! compact) proc.setUiProperty ("displayRange", db);
        if (onSettingsChanged) onSettingsChanged();
        repaint();
    }

    void EQDisplay::setPianoVisible (bool b)
    {
        pianoVisible = b && ! compact;
        proc.setUiProperty ("piano", pianoVisible);
        resized();
        if (onSettingsChanged) onSettingsChanged();
        repaint();
    }

    void EQDisplay::setSketchMode (bool b)
    {
        if (sketchMode == b) return;
        sketchMode = b;
        sketch.clear();
        setMouseCursor (b ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
        if (b) status ("EQ Sketch: draw the curve you want - Claude-Q 4 creates the bands. Esc to exit.");
        if (onSketchModeChanged) onSketchModeChanged();
        repaint();
    }

    //==========================================================================
    void EQDisplay::resized()
    {
        uiScale = compact ? juce::jlimit (0.7f, 1.4f, (float) getHeight() / 170.0f)
                          : juce::jlimit (0.75f, 2.2f, (float) getWidth() / 940.0f);
        auto r = getLocalBounds().toFloat();
        rightCol = {};
        rangeButton = {};
        if (! compact)
        {
            rightCol = r.removeFromRight (std::round (38.0f * uiScale));
            rangeButton = rightCol.withHeight (22.0f * uiScale).reduced (3.0f * uiScale, 3.0f * uiScale).translated (0.0f, 2.0f * uiScale);
        }
        plot = r;
        piano = {};
        if (pianoVisible && ! compact) piano = plot.removeFromBottom (std::round (20.0f * uiScale));
        updateGrid();
    }

    void EQDisplay::updateGrid()
    {
        numPts = juce::jlimit (160, 1200, (int) (plot.getWidth() / 1.5f));
        ptFreq.resize ((size_t) numPts);
        ptX.resize ((size_t) numPts);
        for (int i = 0; i < numPts; ++i)
        {
            const double t = (double) i / (numPts - 1);
            ptFreq[(size_t) i] = kMinFreq * std::exp (t * kLogSpan);
            ptX[(size_t) i] = plot.getX() + (float) t * plot.getWidth();
        }
        for (auto& c : curves)
        {
            c.staticDb.assign ((size_t) numPts, 0.0f);
            c.rangeDb.assign ((size_t) numPts, 0.0f);
            c.liveDb.assign ((size_t) numPts, 0.0f);
            c.valid = false;
        }
        for (auto& c : composite) c.assign ((size_t) numPts, 0.0f);
        needsRepaint = true;
        updateCurves();   // geometry stays valid between resize and the next frame
    }

    float EQDisplay::xForFreq (double f) const noexcept
    {
        return plot.getX() + (float) (std::log (std::max (f, 1.0) / kMinFreq) / kLogSpan) * plot.getWidth();
    }

    double EQDisplay::freqForX (float x) const noexcept
    {
        return kMinFreq * std::exp ((double) ((x - plot.getX()) / std::max (1.0f, plot.getWidth())) * kLogSpan);
    }

    float EQDisplay::yForDb (double db) const noexcept
    {
        const float half = plot.getHeight() * 0.5f - 12.0f * uiScale;
        return plot.getCentreY() - (float) (db / rangeDb) * half;
    }

    double EQDisplay::dbForY (float y) const noexcept
    {
        const float half = plot.getHeight() * 0.5f - 12.0f * uiScale;
        return (double) ((plot.getCentreY() - y) / std::max (1.0f, half)) * rangeDb;
    }

    float EQDisplay::yForAnalyzer (double db) const noexcept
    {
        return plot.getY() + (float) ((0.0 - db) / std::max (10.0f, ana.range)) * plot.getHeight();
    }

    double EQDisplay::gainDisplayFactor (const BandParams& bp) const
    {
        if (! shapeUsesGain (bp.shape)) return 0.0;
        return model.gainScale() * (isTiltShape (bp.shape) ? 0.5 : 1.0);
    }

    float EQDisplay::nodeDisplayDb (int b) const
    {
        const auto& c = curves[(size_t) b];
        const auto& bp = c.params;
        if (shapeUsesGain (bp.shape)) return (float) (bp.gainDb * gainDisplayFactor (bp));
        if (bp.shape == Shape::LowCut || bp.shape == Shape::HighCut) return c.nodeDb;
        return 0.0f;
    }

    juce::Point<float> EQDisplay::nodePosition (int b) const
    {
        const auto& bp = curves[(size_t) b].params;
        const float m = 7.0f * uiScale;
        return { juce::jlimit (plot.getX() + 2.0f, plot.getRight() - 2.0f, xForFreq (bp.freq)),
                 juce::jlimit (plot.getY() + m, plot.getBottom() - m, yForDb (nodeDisplayDb (b))) };
    }

    juce::Point<float> EQDisplay::rangeHandlePos (int b) const
    {
        const auto& bp = curves[(size_t) b].params;
        const auto np = nodePosition (b);
        const double k = gainDisplayFactor (bp);
        const float y = yForDb ((bp.gainDb + bp.dynRange) * k);
        const float m = 5.0f * uiScale;
        return { np.x, juce::jlimit (plot.getY() + m, plot.getBottom() - m, y) };
    }

    double EQDisplay::snapFreq (double f) const
    {
        return clampT (noteToFreq (std::round (freqToNote (f))), kMinFreq, kMaxFreq);
    }

    double EQDisplay::staticResponseDb (double f) const
    {
        double sum = 0.0;
        const double scale = model.gainScale();
        for (int b = 0; b < kMaxBands; ++b)
        {
            const auto bp = model.band (b);
            if (! bp.isActive()) continue;
            if (bp.place != Placement::Stereo && model.isStereo()) continue;
            AnalogFilter af;
            makeAnalogPrototype (bp.shapeParams (scale), af);
            sum += 10.0 * std::log10 (std::max (af.magSq (f / bp.freq), 1.0e-12));
        }
        return sum;
    }

    //==========================================================================
    void EQDisplay::computeCurve (const BandParams& bp, double scale, double dynDb, float* out, float* nodeDbOut) const
    {
        AnalogFilter af;
        makeAnalogPrototype (bp.shapeParams (scale, dynDb), af);
        const double f0 = bp.freq;
        for (int i = 0; i < numPts; ++i)
            out[i] = (float) (10.0 * std::log10 (std::max (af.magSq (ptFreq[(size_t) i] / f0), 1.0e-12)));
        if (nodeDbOut != nullptr)
            *nodeDbOut = (float) (10.0 * std::log10 (std::max (af.magSq (1.0), 1.0e-12)));
    }

    void EQDisplay::updateCurves()
    {
        if (numPts <= 0) return;
        const double scale = model.gainScale();
        const bool stereoBus = model.isStereo();
        bool any = false;
        for (int b = 0; b < kMaxBands; ++b)
        {
            auto& c = curves[(size_t) b];
            const BandParams bp = model.band (b);
            const bool changed = ! c.valid || ! sameBand (bp, c.params) || scale != c.scale;
            if (! bp.used)
            {
                if (changed) { any = true; c.params = bp; c.scale = scale; c.valid = true; }
                c.active = c.dynamic = c.spectral = false;
                continue;
            }
            if (changed)
            {
                c.params = bp;
                c.scale = scale;
                c.valid = true;
                computeCurve (bp, scale, 0.0, c.staticDb.data(), &c.nodeDb);
                c.dynamic = bp.isDynamic();
                c.spectral = bp.isSpectral();
                if (c.dynamic) computeCurve (bp, scale, bp.dynRange * scale, c.rangeDb.data(), nullptr);
                c.liveGain = 1.0e9f;
                any = true;
            }
            c.active = bp.isActive();
            if (c.dynamic && c.active)
            {
                if (c.spectral && proc.engine.isSpectralActive (b))
                {
                    constexpr int P = SpectralEngine::kDisplayPoints;
                    const double lo = std::log (20.0), span = std::log (1000.0);
                    for (int i = 0; i < numPts; ++i)
                    {
                        const double pos = (std::log (ptFreq[(size_t) i]) - lo) / span * (P - 1);
                        float d = 0.0f;
                        if (pos >= 0.0 && pos <= P - 1)
                        {
                            const int j = std::min (P - 2, (int) pos);
                            const float t = (float) (pos - j);
                            d = proc.engine.getSpectralGainDb (b, j) * (1.0f - t) + proc.engine.getSpectralGainDb (b, j + 1) * t;
                        }
                        c.liveDb[(size_t) i] = c.staticDb[(size_t) i] + d;
                    }
                    c.liveGain = 0.0f;
                    any = true;
                }
                else
                {
                    const float dg = proc.engine.getDynamicGainDb (b);
                    if (std::abs (dg - c.liveGain) > 0.01f)
                    {
                        computeCurve (bp, scale, dg, c.liveDb.data(), nullptr);
                        c.liveGain = dg;
                        any = true;
                    }
                }
            }
        }

        if (! any) return;
        needsRepaint = true;

        bool hasLR = false, hasMS = false;
        if (stereoBus)
            for (const auto& c : curves)
            {
                if (! c.active) continue;
                if (c.params.place == Placement::Left || c.params.place == Placement::Right) hasLR = true;
                if (c.params.place == Placement::Mid || c.params.place == Placement::Side) hasMS = true;
            }
        chanShown[chMain] = ! hasLR && ! hasMS;
        chanShown[chLeft] = chanShown[chRight] = hasLR;
        chanShown[chMid] = chanShown[chSide] = hasMS;

        for (auto& v : composite) std::fill (v.begin(), v.end(), 0.0f);
        for (const auto& c : curves)
        {
            if (! c.active) continue;
            const float* src = c.dynamic ? c.liveDb.data() : c.staticDb.data();
            const Placement pl = stereoBus ? c.params.place : Placement::Stereo;
            for (int ch = 0; ch < kNumChans; ++ch)
            {
                bool use = false;
                switch (ch)
                {
                    case chMain:  use = true; break;
                    case chLeft:  use = pl == Placement::Stereo || pl == Placement::Left; break;
                    case chRight: use = pl == Placement::Stereo || pl == Placement::Right; break;
                    case chMid:   use = pl == Placement::Stereo || pl == Placement::Mid; break;
                    case chSide:  use = pl == Placement::Stereo || pl == Placement::Side; break;
                    default: break;
                }
                if (! use) continue;
                auto* dst = composite[(size_t) ch].data();
                for (int i = 0; i < numPts; ++i) dst[i] += src[i];
            }
        }
    }

    void EQDisplay::updateCollisions (double t)
    {
        std::array<double, SpectrumAnalyzer::kPoints> other {};
        bool anyOther = false;
        InstanceRegistry::get().visit ([&] (ClaudeQ4AudioProcessor& p)
        {
            if (&p == &proc) return;
            p.updateAnalyzers (t);
            if (! p.anaPost.hasSignal()) return;
            const float* d = p.anaPost.getDb();
            for (int i = 0; i < SpectrumAnalyzer::kPoints; ++i) other[(size_t) i] += dbToPower (d[i]);
            anyOther = true;
        });
        // the external side chain (e.g. the vocal feeding this bass' side chain) counts as well
        if (proc.isSidechainActive() && proc.anaSc.hasSignal())
        {
            const float* d = proc.anaSc.getDb();
            for (int i = 0; i < SpectrumAnalyzer::kPoints; ++i) other[(size_t) i] += dbToPower (d[i]);
            anyOther = true;
        }
        collisionActive = anyOther && proc.anaPost.hasSignal();
        if (! collisionActive)
        {
            for (auto& c : collision) c *= 0.8f;
            return;
        }
        const float* mine = proc.anaPost.getDb();
        float mx = -200.0f;
        for (int i = 0; i < SpectrumAnalyzer::kPoints; ++i) mx = std::max (mx, mine[i] + tiltDb[(size_t) i]);
        std::array<float, SpectrumAnalyzer::kPoints> target {};
        for (int i = 0; i < SpectrumAnalyzer::kPoints; ++i)
        {
            const float m = mine[i];
            const float o = (float) powerToDb (other[(size_t) i] + 1.0e-24);
            const float presence = juce::jlimit (0.0f, 1.0f, (m + tiltDb[(size_t) i] - (mx - 45.0f)) / 20.0f);
            const float mask = juce::jlimit (0.0f, 1.0f, (o - m + 3.0f) / 9.0f);
            target[(size_t) i] = presence * mask;
        }
        for (int i = 0; i < SpectrumAnalyzer::kPoints; ++i)
        {
            float s = 0.0f, w = 0.0f;
            for (int k = -4; k <= 4; ++k)
            {
                const int j = i + k;
                if (j < 0 || j >= SpectrumAnalyzer::kPoints) continue;
                const float wk = 1.0f - std::abs ((float) k) / 5.0f;
                s += target[(size_t) j] * wk; w += wk;
            }
            collision[(size_t) i] += (s / w - collision[(size_t) i]) * 0.2f;
        }
    }

    void EQDisplay::updatePeaks (double t)
    {
        const bool want = ! compact && ana.spectrumGrab && mouseInside && drag == DragMode::None && plot.contains (mouse)
                          && hoverBand < 0 && rangeHover < 0 && ! sketchMode && ! mouseOverCard && (ana.post || ana.pre);
        if (! want)
        {
            if (! peaks.empty()) { peaks.clear(); needsRepaint = true; }
            hoverPeak = -1;
            return;
        }
        if (t - lastPeakTime < 0.15 && ! peaks.empty()) return;
        lastPeakTime = t;
        const auto& an = ana.post ? proc.anaPost : proc.anaPre;
        if (! an.hasSignal()) { peaks.clear(); return; }
        std::array<float, SpectrumAnalyzer::kPoints> tmp {};
        float mx = -200.0f;
        for (int i = 0; i < SpectrumAnalyzer::kPoints; ++i)
        {
            tmp[(size_t) i] = an.getDb()[i] + tiltDb[(size_t) i];
            if (SpectrumAnalyzer::pointFreq (i) >= 20.0 && SpectrumAnalyzer::pointFreq (i) <= 20000.0) mx = std::max (mx, tmp[(size_t) i]);
        }
        SpectrumAnalyzer::findPeaks (tmp.data(), SpectrumAnalyzer::kPoints, 6, 5.0f, mx - 48.0f, peaks);
        // only audible range
        peaks.erase (std::remove_if (peaks.begin(), peaks.end(), [] (int i)
        {
            const double f = SpectrumAnalyzer::pointFreq (i);
            return f < 20.0 || f > 20000.0;
        }), peaks.end());
        needsRepaint = true;
    }

    //==========================================================================
    void EQDisplay::tick (double t)
    {
        if (isAlive && ! isAlive()) return;
        now = t;
        const auto prevAna = ana;
        ana = proc.getAnalyzerSettings();
        if (ana.range != prevAna.range || ana.pre != prevAna.pre || ana.post != prevAna.post || ana.sc != prevAna.sc) needsRepaint = true;
        if (ana.tilt != tiltFor)
        {
            tiltFor = ana.tilt;
            for (int i = 0; i < SpectrumAnalyzer::kPoints; ++i)
                tiltDb[(size_t) i] = (float) (ana.tilt * std::log2 (SpectrumAnalyzer::pointFreq (i) / 1000.0));
            needsRepaint = true;
        }
        if (! compact)
        {
            const double r = (double) proc.getUiProperty ("displayRange", rangeDb);
            if (r != rangeDb && (r == 3.0 || r == 6.0 || r == 12.0 || r == 30.0)) { rangeDb = r; needsRepaint = true; }
        }
        updateCurves();
        if (! compact)
        {
            if (ana.collisions && ana.post) updateCollisions (t);
            else { collisionActive = false; collision.fill (0.0f); }
            updatePeaks (t);
        }
        if (wheelGesture && t > wheelGestureEnd) endWheelGesture();
        if (mouseInside && drag == DragMode::None) updateHover();

        // hover card
        if (! compact)
        {
            int want = -1;
            if (drag == DragMode::Node || drag == DragMode::Range) want = dragPrimary;
            else if (hoverBand >= 0) want = hoverBand;
            else if (cardVisible && mouseOverCard) want = cardBand;
            if (want >= 0 && model.isUsed (want))
            {
                if (! cardVisible || cardBand != want) needsRepaint = true;
                cardBand = want;
                cardVisible = true;
                layoutCard (want);
                lastHoverTime = t;
            }
            else if (cardVisible && (t - lastHoverTime > 0.7 || ! model.isUsed (cardBand)))
            {
                cardVisible = false;
                needsRepaint = true;
            }
        }

        const bool animating = (ana.pre && proc.anaPre.hasSignal()) || (ana.post && proc.anaPost.hasSignal())
                            || (ana.sc && proc.anaSc.hasSignal()) || collisionActive;
        if (animating || needsRepaint)
        {
            needsRepaint = false;
            repaint();
        }
    }

    //==========================================================================
    int EQDisplay::hitNode (juce::Point<float> p) const
    {
        int best = -1;
        float bestD = (compact ? 8.0f : 10.0f) * uiScale;
        for (int pass = 0; pass < 2; ++pass)
        {
            for (int b = 0; b < kMaxBands; ++b)
            {
                if (! curves[(size_t) b].params.used || ! curves[(size_t) b].valid) continue;
                if ((pass == 0) != model.isSelected (b)) continue;   // selected bands win ties
                const float d = nodePosition (b).getDistanceFrom (p);
                if (d < bestD) { bestD = d; best = b; }
            }
            if (best >= 0) return best;
        }
        return best;
    }

    int EQDisplay::hitRangeHandle (juce::Point<float> p) const
    {
        if (compact) return -1;
        for (int b = 0; b < kMaxBands; ++b)
        {
            const auto& c = curves[(size_t) b];
            if (! c.params.used || ! c.dynamic) continue;
            if (std::abs (c.params.dynRange * gainDisplayFactor (c.params)) < 0.2) continue;
            if (rangeHandlePos (b).getDistanceFrom (p) < 7.0f * uiScale && nodePosition (b).getDistanceFrom (p) > 6.0f * uiScale) return b;
        }
        return -1;
    }

    juce::Rectangle<float> EQDisplay::peakLabelRect (int idx) const
    {
        const int i = peaks[(size_t) idx];
        const double f = SpectrumAnalyzer::pointFreq (i);
        const auto& an = ana.post ? proc.anaPost : proc.anaPre;
        const float y = yForAnalyzer (an.getDb()[i] + tiltDb[(size_t) i]);
        const auto text = formatFreqShort (f);
        const float w = juce::GlyphArrangement::getStringWidth (theme::font (11.0f * uiScale, 2), text) + 10.0f * uiScale;
        const float h = 16.0f * uiScale;
        auto r = juce::Rectangle<float> (w, h).withCentre ({ xForFreq (f), y - h * 0.5f - 6.0f * uiScale });
        return r.withY (juce::jlimit (plot.getY() + 2.0f, plot.getBottom() - h - 2.0f, r.getY()));
    }

    int EQDisplay::hitPeak (juce::Point<float> p) const
    {
        for (int k = 0; k < (int) peaks.size(); ++k)
            if (peakLabelRect (k).expanded (2.0f).contains (p)) return k;
        return -1;
    }

    bool EQDisplay::nearComposite (juce::Point<float> p, juce::Point<float>& onCurve) const
    {
        if (numPts < 2 || ! plot.contains (p)) return false;
        const float t = (p.x - plot.getX()) / plot.getWidth() * (float) (numPts - 1);
        const int i = juce::jlimit (0, numPts - 2, (int) t);
        const float fr = t - (float) i;
        float bestD = 8.0f * uiScale;
        bool found = false;
        for (int ch = 0; ch < kNumChans; ++ch)
        {
            if (! chanShown[(size_t) ch]) continue;
            const auto& v = composite[(size_t) ch];
            const float db = v[(size_t) i] * (1.0f - fr) + v[(size_t) i + 1] * fr;
            const float y = yForDb (db);
            if (std::abs (y - p.y) < bestD) { bestD = std::abs (y - p.y); onCurve = { p.x, y }; found = true; }
        }
        return found;
    }

    void EQDisplay::layoutCard (int b)
    {
        const auto np = nodePosition (b);
        const float w = 184.0f * uiScale, h = 60.0f * uiScale;
        const float x = juce::jlimit (plot.getX() + 4.0f, plot.getRight() - w - 4.0f, np.x - w * 0.5f);
        float y = np.y - h - 18.0f * uiScale;
        if (y < plot.getY() + 4.0f) y = np.y + 18.0f * uiScale;
        card.r = { x, y, w, h };
        const float row = h / 3.0f;
        const float ic = row * 0.95f;
        card.power = { x + 5.0f * uiScale, y + 2.0f * uiScale, ic, ic };
        card.close = { card.r.getRight() - ic - 5.0f * uiScale, y + 2.0f * uiScale, ic, ic };
        card.solo = card.close.translated (-ic - 2.0f * uiScale, 0.0f);
        card.shape = { x + 5.0f * uiScale, y + 2.0f * row - 1.0f * uiScale, ic * 1.3f, ic };
        card.menu = { card.r.getRight() - ic - 5.0f * uiScale, y + 2.0f * row - 1.0f * uiScale, ic, ic };
        const float tx = card.power.getRight() + 4.0f * uiScale, tw = card.solo.getX() - tx - 4.0f * uiScale;
        card.freqText = { tx, y, tw, row };
        card.gainText = { tx, y + row, tw, row };
        card.qText = { card.shape.getRight() + 2.0f * uiScale, y + 2.0f * row, card.menu.getX() - card.shape.getRight() - 4.0f * uiScale, row };
    }

    int EQDisplay::cardValueAt (juce::Point<float> p, BandParam& which) const
    {
        if (! cardVisible || cardBand < 0) return -1;
        const auto bp = curves[(size_t) cardBand].params;
        if (card.freqText.contains (p)) { which = bpFreq; return cardBand; }
        if (card.gainText.contains (p) && shapeUsesGain (bp.shape)) { which = bpGain; return cardBand; }
        if (card.qText.contains (p) && shapeUsesQ (bp.shape, bp.slope)) { which = bpQ; return cardBand; }
        return -1;
    }

    void EQDisplay::updateHover()
    {
        const int oldBand = hoverBand, oldRange = rangeHover, oldPeak = hoverPeak, oldKey = hoverKey;
        const bool oldGhost = ghostVisible, oldCard = mouseOverCard;
        hoverBand = rangeHover = hoverPeak = hoverKey = -1;
        ghostVisible = false;
        mouseOverCard = cardVisible && card.r.contains (mouse);
        if (mouseInside && ! mouseOverCard)
        {
            if (! piano.isEmpty() && piano.contains (mouse))
                hoverKey = (int) std::lround (freqToNote (freqForX (mouse.x)));
            else if (plot.contains (mouse) && ! sketchMode)
            {
                rangeHover = hitRangeHandle (mouse);
                if (rangeHover < 0) hoverBand = hitNode (mouse);
                if (rangeHover < 0 && hoverBand < 0)
                {
                    hoverPeak = hitPeak (mouse);
                    if (hoverPeak < 0 && ! compact)
                    {
                        juce::Point<float> on;
                        if (nearComposite (mouse, on)) { ghostVisible = true; ghostPos = on; }
                    }
                }
            }
        }
        if (oldBand != hoverBand || oldRange != rangeHover || oldPeak != hoverPeak || oldKey != hoverKey
            || oldGhost != ghostVisible || oldCard != mouseOverCard || ghostVisible)
            needsRepaint = true;
    }

    //==========================================================================
    void EQDisplay::mouseEnter (const juce::MouseEvent& e)
    {
        mouseInside = true;
        mouse = e.position;
        updateHover();
    }

    void EQDisplay::mouseExit (const juce::MouseEvent&)
    {
        mouseInside = false;
        hoverBand = rangeHover = hoverPeak = hoverKey = -1;
        ghostVisible = false;
        mouseOverCard = false;
        peaks.clear();
        needsRepaint = true;
        repaint();
    }

    void EQDisplay::mouseMove (const juce::MouseEvent& e)
    {
        if (isAlive && ! isAlive()) return;
        mouse = e.position;
        mouseInside = true;
        updateHover();
        BandParam cardWhich = bpFreq;
        if (sketchMode) setMouseCursor (juce::MouseCursor::CrosshairCursor);
        else if (cardValueAt (mouse, cardWhich) >= 0) setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        else setMouseCursor (hoverBand >= 0 || rangeHover >= 0 || hoverPeak >= 0 || ghostVisible || hoverKey >= 0
                             || (cardVisible && card.r.contains (mouse)) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }

    void EQDisplay::createBandAt (double freq, double gainDb, bool startDrag, const juce::MouseEvent* e)
    {
        Shape s = Shape::Bell;
        Slope sl = Slope::dB12;
        double q = 1.0;
        if (freq < 28.0)         { s = Shape::LowCut;  sl = Slope::dB24; q = 0.71; gainDb = 0.0; }
        else if (freq > 17000.0) { s = Shape::HighCut; sl = Slope::dB24; q = 0.71; gainDb = 0.0; }
        const double scale = model.gainScale();
        const double g = std::abs (scale) > 1.0e-3 ? gainDb / scale : 0.0;
        model.beginGesture ("Create band");
        const int b = model.createBand (freq, g, s, sl, q, Placement::Stereo);
        if (b < 0)
        {
            model.endGesture();
            status ("All 24 bands are in use");
            return;
        }
        model.selectOnly (b);
        status ("Band " + juce::String (b + 1) + " created (" + shapeName (s) + ", " + formatFreq (freq) + ")");
        if (startDrag && e != nullptr)
        {
            updateCurves();
            dragSet = { { b, model.value (b, bpFreq), model.value (b, bpGain), model.value (b, bpQ), model.value (b, bpDynRange) } };
            dragPrimary = b;
            drag = DragMode::Node;
            dragQ = false;
            altClick = false;
            dragMoved = false;
            dragStart = e->position;
            // the gesture stays open until mouseUp
        }
        else model.endGesture();
        needsRepaint = true;
    }

    void EQDisplay::startNodeDrag (int b, const juce::MouseEvent& e)
    {
        if (! model.isSelected (b))
        {
            if (e.mods.isShiftDown()) model.select (b, true);
            else model.selectOnly (b);
        }
        else model.setFocus (b);
        dragSet.clear();
        for (int s : model.selection())
            dragSet.push_back ({ s, model.value (s, bpFreq), model.value (s, bpGain), model.value (s, bpQ), model.value (s, bpDynRange) });
        dragPrimary = b;
        drag = DragMode::Node;
        dragQ = e.mods.isCommandDown();
        altClick = e.mods.isAltDown() && ! e.mods.isShiftDown();
        dragMoved = false;
        dragStart = e.position;
        model.beginGesture (dragQ ? "Adjust Q" : "Move band");
    }

    void EQDisplay::dragNodes (const juce::MouseEvent& e)
    {
        auto d = e.position - dragStart;
        if (! dragMoved && d.getDistanceFromOrigin() < 2.5f) return;
        dragMoved = true;
        if (e.mods.isAltDown())
        {
            if (axisLock == 0 && d.getDistanceFromOrigin() > 4.0f) axisLock = std::abs (d.x) > std::abs (d.y) ? 1 : 2;
        }
        else axisLock = 0;
        if (axisLock == 1) d.y = 0.0f;
        if (axisLock == 2) d.x = 0.0f;
        const double fine = e.mods.isShiftDown() ? 0.15 : 1.0;
        const double dOct = (double) d.x / std::max (1.0f, plot.getWidth()) * (kLogSpan / kLn2) * fine;
        const double halfH = std::max (1.0, (double) plot.getHeight() * 0.5 - 12.0 * uiScale);
        const double dDb = -(double) d.y / halfH * rangeDb * fine;
        const bool qMode = dragQ || e.mods.isCommandDown();

        const DragBand* primary = nullptr;
        for (const auto& s : dragSet) if (s.band == dragPrimary) primary = &s;
        if (primary == nullptr && ! dragSet.empty()) primary = &dragSet.front();
        if (primary == nullptr) return;

        double ratio = std::exp2 (dOct);
        if (! qMode && pianoVisible && ! e.mods.isShiftDown())
        {
            const double target = clampT (primary->freq * ratio, kMinFreq, kMaxFreq);
            ratio = snapFreq (target) / primary->freq;
        }
        for (const auto& s : dragSet)
        {
            const auto bp = model.band (s.band);
            if (qMode)
            {
                if (shapeUsesQ (bp.shape, bp.slope))
                    model.set (s.band, bpQ, clampT (s.q * std::exp2 (-(double) d.y * fine / 45.0), kMinQ, kMaxQ));
                continue;
            }
            model.set (s.band, bpFreq, clampT (s.freq * ratio, kMinFreq, kMaxFreq));
            if (shapeUsesGain (bp.shape))
            {
                const double k = gainDisplayFactor (bp);
                if (std::abs (k) > 1.0e-3) model.set (s.band, bpGain, clampT (s.gain + dDb / k, kMinGain, kMaxGain));
            }
            else if (dragSet.size() == 1 && shapeUsesQ (bp.shape, bp.slope) && (bp.shape == Shape::LowCut || bp.shape == Shape::HighCut
                                                                                || bp.shape == Shape::Notch || bp.shape == Shape::BandPass))
            {
                model.set (s.band, bpQ, clampT (s.q * std::exp2 (dDb / 6.0), kMinQ, kMaxQ));
            }
        }
    }

    void EQDisplay::mouseDown (const juce::MouseEvent& e)
    {
        if (isAlive && ! isAlive()) return;
        mouse = e.position;
        dragStart = mouse;
        dragMoved = false;
        axisLock = 0;
        if (wheelGesture) endWheelGesture();
        updateHover();

        if (! compact && rangeButton.contains (mouse))
        {
            juce::PopupMenu m;
            m.addSectionHeader ("Display range");
            for (double r : { 3.0, 6.0, 12.0, 30.0 })
                m.addItem (juce::String ((int) r) + " dB", true, rangeDb == r, [this, r] { setRange (r); });
            m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (rangeButton.toNearestInt())).withDeletionCheck (*this));
            return;
        }

        if (cardVisible && card.r.contains (mouse))
        {
            const int b = cardBand;
            if (! model.isUsed (b)) return;
            if (card.power.contains (mouse)) model.toggle (b, bpEnabled, "Enable band");
            else if (card.solo.contains (mouse)) { drag = DragMode::CardSolo; proc.setSolo (b, false); }
            else if (card.close.contains (mouse)) { cardVisible = false; model.deleteBand (b); status ("Band " + juce::String (b + 1) + " deleted"); }
            else if (card.shape.contains (mouse))
            {
                juce::PopupMenu m;
                const auto cur = model.band (b).shape;
                for (int s = 0; s < (int) Shape::Count; ++s)
                    m.addItem (shapeName ((Shape) s), true, cur == (Shape) s, [this, b, s] { if (model.isUsed (b)) model.setShape (b, (Shape) s); });
                m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (card.shape.toNearestInt())).withDeletionCheck (*this));
            }
            else if (card.menu.contains (mouse)) showBandMenu (b);
            else
            {
                BandParam which = bpFreq;
                if (cardValueAt (mouse, which) >= 0)
                {
                    if (e.getNumberOfClicks() >= 2)
                    {
                        if (which == bpGain) model.setOnce (b, bpGain, 0.0, "Reset gain");
                        if (which == bpQ) model.setOnce (b, bpQ, 1.0, "Reset Q");
                    }
                    else
                    {
                        drag = DragMode::CardValue;
                        dragPrimary = b;
                        cardParam = which;
                        cardStart = model.value (b, which);
                        model.beginGesture ("Edit band");
                        e.source.enableUnboundedMouseMovement (true);
                    }
                }
            }
            needsRepaint = true;
            repaint();
            return;
        }

        if (e.mods.isPopupMenu())
        {
            const int b = hitNode (mouse);
            if (b >= 0)
            {
                if (! model.isSelected (b)) model.selectOnly (b);
                showBandMenu (b);
            }
            else if (plot.contains (mouse) && ! compact)
                showBackgroundMenu (freqForX (mouse.x), dbForY (mouse.y));
            return;
        }

        if (! piano.isEmpty() && piano.contains (mouse))
        {
            const int f = model.getFocus();
            if (f >= 0 && hoverKey >= 0)
            {
                model.beginGesture ("Set band to note");
                model.set (f, bpFreq, clampT (noteToFreq (hoverKey), kMinFreq, kMaxFreq));
                drag = DragMode::Piano;
                status ("Band " + juce::String (f + 1) + " -> " + noteName (noteToFreq (hoverKey), false) + "  " + formatFreq (noteToFreq (hoverKey)));
            }
            else status (noteName (noteToFreq (hoverKey), false) + " = " + formatFreq (noteToFreq (hoverKey)) + "  (select a band to tune it to a note)");
            return;
        }
        if (! rightCol.isEmpty() && rightCol.contains (mouse)) return;

        if (sketchMode)
        {
            if (plot.contains (mouse))
            {
                drag = DragMode::Sketch;
                sketch.clear();
                sketch.push_back (mouse);
            }
            return;
        }

        if (rangeHover >= 0)
        {
            const int b = rangeHover;
            drag = DragMode::Range;
            dragPrimary = b;
            dragSet = { { b, model.value (b, bpFreq), model.value (b, bpGain), model.value (b, bpQ), model.value (b, bpDynRange) } };
            model.beginGesture ("Dynamic range");
            return;
        }

        if (hoverBand >= 0)
        {
            const int b = hoverBand;
            if (e.mods.isAltDown() && e.mods.isShiftDown())
            {
                drag = DragMode::AltSolo;
                proc.setSolo (b, false);
                status ("Solo band " + juce::String (b + 1));
                return;
            }
            startNodeDrag (b, e);
            return;
        }

        if (hoverPeak >= 0 && hoverPeak < (int) peaks.size())
        {
            const double f = SpectrumAnalyzer::pointFreq (peaks[(size_t) hoverPeak]);
            createBandAt (f, 0.0, true, &e);
            if (drag == DragMode::Node && dragPrimary >= 0) model.set (dragPrimary, bpQ, 4.0);
            return;
        }

        if (ghostVisible)
        {
            createBandAt (freqForX (ghostPos.x), 0.0, true, &e);
            return;
        }

        if (plot.contains (mouse))
        {
            drag = DragMode::Lasso;
            lasso = { mouse, mouse };
            lassoBase = (e.mods.isShiftDown() || e.mods.isCommandDown()) ? model.getSelectionBits() : std::bitset<kMaxBands>();
        }
    }

    void EQDisplay::mouseDrag (const juce::MouseEvent& e)
    {
        if (isAlive && ! isAlive()) return;
        mouse = e.position;
        switch (drag)
        {
            case DragMode::Node:
                dragNodes (e);
                break;
            case DragMode::Range:
            {
                if (dragSet.empty()) break;
                const auto& s = dragSet.front();
                const auto bp = model.band (s.band);
                const double fine = e.mods.isShiftDown() ? 0.15 : 1.0;
                const double halfH = std::max (1.0, (double) plot.getHeight() * 0.5 - 12.0 * uiScale);
                const double dDb = -(double) (mouse.y - dragStart.y) / halfH * rangeDb * fine;
                const double k = gainDisplayFactor (bp);
                if (std::abs (k) > 1.0e-3) model.set (s.band, bpDynRange, clampT (s.range + dDb / k, -30.0, 30.0));
                break;
            }
            case DragMode::CardValue:
            {
                const double fine = e.mods.isShiftDown() ? 0.15 : 1.0;
                const double dy = (double) (dragStart.y - mouse.y) * fine;   // up = more
                if (cardParam == bpFreq)
                {
                    double f = clampT (cardStart * std::exp2 (dy / 120.0), kMinFreq, kMaxFreq);
                    if (pianoVisible && ! e.mods.isShiftDown()) f = snapFreq (f);
                    model.set (dragPrimary, bpFreq, f);
                }
                else if (cardParam == bpGain) model.set (dragPrimary, bpGain, clampT (cardStart + dy * 0.1, kMinGain, kMaxGain));
                else model.set (dragPrimary, bpQ, clampT (cardStart * std::exp2 (dy / 90.0), kMinQ, kMaxQ));
                break;
            }
            case DragMode::Lasso:
            {
                lasso = juce::Rectangle<float> (dragStart, mouse);
                auto bits = lassoBase;
                for (int b = 0; b < kMaxBands; ++b)
                    if (model.isUsed (b) && lasso.contains (nodePosition (b))) bits.set ((size_t) b);
                model.setSelection (bits);
                break;
            }
            case DragMode::Sketch:
                if (sketch.empty() || sketch.back().getDistanceFrom (mouse) > 1.5f) sketch.push_back (mouse);
                break;
            case DragMode::Piano:
            {
                const int f = model.getFocus();
                if (f >= 0)
                {
                    const int note = (int) std::lround (freqToNote (freqForX (juce::jlimit (plot.getX(), plot.getRight(), mouse.x))));
                    hoverKey = note;
                    model.set (f, bpFreq, clampT (noteToFreq (note), kMinFreq, kMaxFreq));
                }
                break;
            }
            default:
                break;
        }
        needsRepaint = true;
        repaint();
    }

    void EQDisplay::mouseUp (const juce::MouseEvent& e)
    {
        if (isAlive && ! isAlive()) return;
        mouse = e.position;
        switch (drag)
        {
            case DragMode::Node:
                if (! dragMoved && altClick && dragPrimary >= 0 && model.isUsed (dragPrimary))
                {
                    model.set (dragPrimary, bpEnabled, model.value (dragPrimary, bpEnabled) > 0.5 ? 0.0 : 1.0);
                    status (model.value (dragPrimary, bpEnabled) > 0.5 ? "Band enabled" : "Band disabled");
                }
                model.endGesture();
                break;
            case DragMode::Range:
            case DragMode::Piano:
                model.endGesture();
                break;
            case DragMode::CardValue:
                e.source.enableUnboundedMouseMovement (false);
                model.endGesture();
                break;
            case DragMode::Lasso:
                if (lasso.getWidth() < 3.0f && lasso.getHeight() < 3.0f && ! e.mods.isShiftDown() && ! e.mods.isCommandDown())
                    model.clearSelection();
                break;
            case DragMode::Sketch:
                finishSketch();
                break;
            case DragMode::CardSolo:
            case DragMode::AltSolo:
                proc.setSolo (-1, false);
                break;
            default:
                break;
        }
        drag = DragMode::None;
        lasso = {};
        updateHover();
        needsRepaint = true;
        repaint();
    }

    void EQDisplay::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (isAlive && ! isAlive()) return;
        if (sketchMode || e.mods.isPopupMenu()) return;
        if (cardVisible && card.r.contains (e.position)) return;
        if ((! piano.isEmpty() && piano.contains (e.position)) || (! rightCol.isEmpty() && rightCol.contains (e.position))) return;
        const int b = hitNode (e.position);
        if (b >= 0)
        {
            if (drag == DragMode::Node) { model.endGesture(); drag = DragMode::None; }
            model.deleteBand (b);
            status ("Band " + juce::String (b + 1) + " deleted");
            updateHover();
            needsRepaint = true;
            return;
        }
        if (plot.contains (e.position))
        {
            if (drag == DragMode::Node) { model.endGesture(); }
            drag = DragMode::None;
            lasso = {};
            createBandAt (freqForX (e.position.x), dbForY (e.position.y), false, nullptr);
        }
    }

    void EQDisplay::endWheelGesture()
    {
        if (! wheelGesture) return;
        wheelGesture = false;
        model.endGesture();
    }

    void EQDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
    {
        if (isAlive && ! isAlive()) return;
        {
            BandParam which = bpFreq;
            const int cb = cardValueAt (e.position, which);
            if (cb >= 0)
            {
                double d = (double) (w.deltaY != 0.0f ? w.deltaY : w.deltaX) * (w.isReversed ? -1.0 : 1.0);
                if (e.mods.isShiftDown()) d *= 0.2;
                if (! wheelGesture) { model.beginGesture ("Edit band"); wheelGesture = true; }
                wheelGestureEnd = now + 0.6;
                const double v = model.value (cb, which);
                if (which == bpFreq) model.set (cb, bpFreq, clampT (v * std::exp2 (d * 0.25), kMinFreq, kMaxFreq));
                else if (which == bpGain) model.set (cb, bpGain, clampT (v + d * 1.0, kMinGain, kMaxGain));
                else model.set (cb, bpQ, clampT (v * std::exp2 (d * 0.5), kMinQ, kMaxQ));
                needsRepaint = true;
                return;
            }
        }
        const int b = hitNode (e.position);
        if (b < 0) return;
        if (! wheelGesture)
        {
            model.beginGesture ("Adjust band");
            wheelGesture = true;
        }
        wheelGestureEnd = now + 0.6;
        double d = (double) (w.deltaY != 0.0f ? w.deltaY : w.deltaX) * (w.isReversed ? -1.0 : 1.0);
        if (e.mods.isShiftDown()) d *= 0.2;
        const auto bp = model.band (b);
        if (e.mods.isAltDown())
        {
            if (shapeSupportsDynamics (bp.shape))
            {
                if (model.value (b, bpDynOn) < 0.5) model.set (b, bpDynOn, 1.0);
                model.set (b, bpDynRange, clampT (bp.dynRange + d * 6.0, -30.0, 30.0));
            }
        }
        else if (e.mods.isCommandDown())
        {
            if (shapeUsesGain (bp.shape)) model.set (b, bpGain, clampT (bp.gainDb + d * 3.0, kMinGain, kMaxGain));
        }
        else if (shapeUsesQ (bp.shape, bp.slope))
            model.set (b, bpQ, clampT (bp.q * std::exp2 (d * 1.2), kMinQ, kMaxQ));
        needsRepaint = true;
    }

    bool EQDisplay::keyPressed (const juce::KeyPress& key)
    {
        if (compact) return false;
        const auto mods = key.getModifiers();
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        {
            if (! model.selection().empty()) { model.deleteSelected(); status ("Deleted"); return true; }
            return false;
        }
        if (key == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0)) { model.selectAll(); return true; }
        if (key == juce::KeyPress ('c', juce::ModifierKeys::commandModifier, 0)) { copyCurve(); return true; }
        if (key == juce::KeyPress ('v', juce::ModifierKeys::commandModifier, 0)) { pasteCurve(); return true; }
        if (key == juce::KeyPress::escapeKey)
        {
            if (sketchMode) setSketchMode (false);
            else model.clearSelection();
            return true;
        }
        if (key.getKeyCode() == juce::KeyPress::tabKey)
        {
            int f = model.getFocus();
            for (int k = 1; k <= kMaxBands; ++k)
            {
                const int b = ((f < 0 ? (mods.isShiftDown() ? 0 : -1) : f) + (mods.isShiftDown() ? -k : k) + kMaxBands * 2) % kMaxBands;
                if (model.isUsed (b)) { model.selectOnly (b); return true; }
            }
            return true;
        }
        const bool up = key.isKeyCode (juce::KeyPress::upKey), down = key.isKeyCode (juce::KeyPress::downKey);
        const bool left = key.isKeyCode (juce::KeyPress::leftKey), right = key.isKeyCode (juce::KeyPress::rightKey);
        if ((up || down || left || right) && ! model.selection().empty())
        {
            const double fine = mods.isShiftDown() ? 0.2 : 1.0;
            if (! wheelGesture) { model.beginGesture ("Nudge band"); wheelGesture = true; }
            wheelGestureEnd = now + 0.8;
            for (int b : model.selection())
            {
                const auto bp = model.band (b);
                if (left || right)
                {
                    double f = bp.freq * std::exp2 ((right ? 1.0 : -1.0) * fine / 12.0);
                    if (pianoVisible && ! mods.isShiftDown()) f = snapFreq (f);
                    model.set (b, bpFreq, clampT (f, kMinFreq, kMaxFreq));
                }
                else if (mods.isCommandDown() || ! shapeUsesGain (bp.shape))
                {
                    if (shapeUsesQ (bp.shape, bp.slope)) model.set (b, bpQ, clampT (bp.q * std::exp2 ((up ? 0.25 : -0.25) * fine), kMinQ, kMaxQ));
                }
                else model.set (b, bpGain, clampT (bp.gainDb + (up ? 0.5 : -0.5) * fine, kMinGain, kMaxGain));
            }
            return true;
        }
        return false;
    }

    //==========================================================================
    void EQDisplay::copyCurve()
    {
        juce::SystemClipboard::copyTextToClipboard (proc.presets.bandsToText());
        status ("EQ curve copied (" + juce::String (model.usedCount()) + " bands) - paste it into any Claude-Q 4 instance");
    }

    void EQDisplay::pasteCurve()
    {
        const auto text = juce::SystemClipboard::getTextFromClipboard();
        if (proc.presets.bandsFromText (text))
        {
            model.clearSelection();
            status ("EQ curve pasted");
        }
        else status ("The clipboard does not contain a Claude-Q 4 EQ curve");
    }

    void EQDisplay::finishSketch()
    {
        drag = DragMode::None;
        if (sketch.size() < 3) { sketch.clear(); return; }
        auto pts = sketch;
        sketch.clear();
        std::sort (pts.begin(), pts.end(), [] (const juce::Point<float>& a, const juce::Point<float>& b) { return a.x < b.x; });
        const float x0 = pts.front().x, x1 = pts.back().x;
        if (x1 - x0 < 6.0f) return;

        const int n = 160;
        std::vector<double> freqs ((size_t) n), target ((size_t) n), weight ((size_t) n);
        const double f0 = freqForX (x0), f1 = freqForX (x1);
        const double edgeLo = dbForY (pts.front().y) - staticResponseDb (f0);
        const double edgeHi = dbForY (pts.back().y) - staticResponseDb (f1);
        size_t j = 0;
        for (int i = 0; i < n; ++i)
        {
            const double f = 20.0 * std::pow (1000.0, (double) i / (n - 1));
            freqs[(size_t) i] = f;
            if (f >= f0 && f <= f1)
            {
                const float x = xForFreq (f);
                while (j + 1 < pts.size() && pts[j + 1].x < x) ++j;
                const auto& a = pts[j];
                const auto& b = pts[std::min (j + 1, pts.size() - 1)];
                const float t = b.x > a.x ? juce::jlimit (0.0f, 1.0f, (x - a.x) / (b.x - a.x)) : 0.0f;
                const double want = dbForY (a.y + (b.y - a.y) * t);
                target[(size_t) i] = want - staticResponseDb (f);
                weight[(size_t) i] = 1.0;
            }
            else
            {
                // outside the stroke: continue flat from the drawn edge, with little weight
                target[(size_t) i] = f < f0 ? edgeLo : edgeHi;
                weight[(size_t) i] = 0.04;
            }
        }
        const int free = kMaxBands - model.usedCount();
        if (free <= 0) { status ("All 24 bands are in use"); return; }
        const auto fit = fitResponse (freqs, target, weight, std::min (free, 8), 0.3, true, 8.0);
        if (fit.empty()) { status ("Nothing to sketch - the curve already matches"); return; }
        const double scale = model.gainScale();
        model.beginGesture ("EQ Sketch");
        std::bitset<kMaxBands> created;
        for (const auto& fb : fit)
        {
            const int b = model.createBand (fb.freq, std::abs (scale) > 1.0e-3 ? fb.gain / scale : 0.0, fb.shape, Slope::dB12, fb.q);
            if (b >= 0) created.set ((size_t) b);
        }
        model.endGesture();
        model.setSelection (created);
        status ("EQ Sketch: " + juce::String ((int) created.count()) + " band" + (created.count() == 1 ? "" : "s") + " created");
    }

    //==========================================================================
    void EQDisplay::addAnalyzerMenuItems (juce::PopupMenu& m, ClaudeQ4AudioProcessor& p, std::function<void()> changed)
    {
        const auto s = p.getAnalyzerSettings();
        auto act = [&p, changed] (std::function<void (AnalyzerSettings&)> f)
        {
            return [&p, changed, f]
            {
                auto st = p.getAnalyzerSettings();
                f (st);
                p.setAnalyzerSettings (st);
                if (changed) changed();
            };
        };
        m.addItem ("Pre-EQ spectrum (input)", true, s.pre, act ([] (AnalyzerSettings& x) { x.pre = ! x.pre; }));
        m.addItem ("Post-EQ spectrum (output)", true, s.post, act ([] (AnalyzerSettings& x) { x.post = ! x.post; }));
        m.addItem ("External side chain spectrum", true, s.sc, act ([] (AnalyzerSettings& x) { x.sc = ! x.sc; }));
        m.addSeparator();
        juce::PopupMenu range, res, speed, tilt;
        for (float r : { 60.0f, 90.0f, 120.0f })
            range.addItem (juce::String ((int) r) + " dB", true, s.range == r, act ([r] (AnalyzerSettings& x) { x.range = r; }));
        const char* resNames[] = { "Low", "Medium", "High", "Very High", "Maximum" };
        for (int i = 0; i < 5; ++i)
            res.addItem (resNames[i], true, s.resolution == i, act ([i] (AnalyzerSettings& x) { x.resolution = i; }));
        const char* speedNames[] = { "Very Slow", "Slow", "Medium", "Fast", "Very Fast" };
        for (int i = 0; i < 5; ++i)
            speed.addItem (speedNames[i], true, s.speed == i, act ([i] (AnalyzerSettings& x) { x.speed = i; }));
        for (float t : { 0.0f, 1.5f, 3.0f, 4.5f, 6.0f })
            tilt.addItem (t == 0.0f ? juce::String ("Off") : juce::String (t, 1) + " dB/oct", true, s.tilt == t, act ([t] (AnalyzerSettings& x) { x.tilt = t; }));
        m.addSubMenu ("Range", range);
        m.addSubMenu ("Resolution", res);
        m.addSubMenu ("Speed", speed);
        m.addSubMenu ("Tilt", tilt);
        m.addSeparator();
        m.addItem ("Freeze", true, s.freeze, act ([] (AnalyzerSettings& x) { x.freeze = ! x.freeze; }));
        m.addItem ("Collision detection", true, s.collisions, act ([] (AnalyzerSettings& x) { x.collisions = ! x.collisions; }));
        m.addItem ("Spectrum grab (peak labels)", true, s.spectrumGrab, act ([] (AnalyzerSettings& x) { x.spectrumGrab = ! x.spectrumGrab; }));
    }

    void EQDisplay::showBandMenu (int b)
    {
        if (! model.isUsed (b)) return;
        const auto bp = model.band (b);
        auto targets = [this, b]
        {
            std::vector<int> t = model.isSelected (b) ? model.selection() : std::vector<int> { b };
            if (t.empty()) t.push_back (b);
            return t;
        };
        juce::PopupMenu m;
        m.addSectionHeader ("Band " + juce::String (b + 1) + "  -  " + shapeName (bp.shape) + ", " + formatFreq (bp.freq));

        juce::PopupMenu shapes;
        for (int s = 0; s < (int) Shape::Count; ++s)
            shapes.addItem (shapeName ((Shape) s), true, bp.shape == (Shape) s, [this, targets, s]
            {
                model.beginGesture ("Change shape");
                for (int t : targets()) model.setShape (t, (Shape) s);
                model.endGesture();
            });
        m.addSubMenu ("Shape", shapes);

        if (shapeUsesSlope (bp.shape))
        {
            juce::PopupMenu slopes;
            for (int s = 0; s < (int) Slope::Count; ++s)
                if (slopeValidForShape (bp.shape, (Slope) s))
                    slopes.addItem (slopeName ((Slope) s), true, bp.slope == (Slope) s, [this, targets, s]
                    {
                        model.beginGesture ("Change slope");
                        for (int t : targets())
                            model.set (t, bpSlope, (double) (int) sanitiseSlope (model.band (t).shape, (Slope) s));
                        model.endGesture();
                    });
            m.addSubMenu ("Slope", slopes);
        }
        if (model.isStereo())
        {
            juce::PopupMenu pl;
            for (int p = 0; p < (int) Placement::Count; ++p)
                pl.addItem (placementName ((Placement) p), true, bp.place == (Placement) p, [this, targets, p]
                {
                    model.beginGesture ("Stereo placement");
                    for (int t : targets()) model.set (t, bpPlace, (double) p);
                    model.endGesture();
                });
            m.addSubMenu ("Stereo Placement", pl);
        }
        m.addSeparator();
        m.addItem ("Enabled", true, bp.enabled, [this, b] { model.toggle (b, bpEnabled, "Enable band"); });
        if (shapeSupportsDynamics (bp.shape))
        {
            const bool dynOn = model.value (b, bpDynOn) > 0.5;
            m.addItem ("Dynamic EQ", true, dynOn, [this, b, dynOn]
            {
                model.beginGesture ("Dynamic EQ");
                model.set (b, bpDynOn, dynOn ? 0.0 : 1.0);
                if (! dynOn && std::abs (model.value (b, bpDynRange)) < 0.1)
                    model.set (b, bpDynRange, model.value (b, bpGain) > 0.0 ? -std::min (6.0, std::max (3.0, model.value (b, bpGain))) : -6.0);
                model.endGesture();
            });
            m.addItem ("Spectral dynamics", dynOn, bp.spectral, [this, b] { model.toggle (b, bpSpectral, "Spectral dynamics"); });
        }
        m.addItem ("Solo", true, proc.getSoloBand() == b, [this, b] { proc.setSolo (proc.getSoloBand() == b ? -1 : b, false); });
        m.addSeparator();
        if (shapeUsesGain (bp.shape)) m.addItem ("Invert gain", [this, targets]
        {
            model.beginGesture ("Invert gain");
            for (int t : targets()) model.invertGain (t);
            model.endGesture();
        });
        m.addItem ("Duplicate", model.freeSlot() >= 0, false, [this, b] { const int nb = model.duplicateBand (b); if (nb >= 0) model.selectOnly (nb); });
        if (model.isStereo() && bp.place == Placement::Stereo)
        {
            m.addItem ("Split into Left / Right", model.freeSlot() >= 0, false, [this, b] { model.splitBand (b, false); });
            m.addItem ("Split into Mid / Side", model.freeSlot() >= 0, false, [this, b] { model.splitBand (b, true); });
        }
        m.addItem ("Reset band", [this, b]
        {
            const auto cur = model.band (b);
            model.beginGesture ("Reset band");
            for (int k = 0; k < kNumBandParams; ++k)
            {
                if (k == bpUsed || k == bpFreq || k == bpShape) continue;
                auto* p = model.param (b, (BandParam) k);
                model.set (b, (BandParam) k, p->convertFrom0to1 (p->getDefaultValue()));
            }
            model.set (b, bpSlope, (double) (int) sanitiseSlope (cur.shape, Slope::dB12));
            model.endGesture();
        });
        m.addSeparator();
        m.addItem (model.selection().size() > 1 && model.isSelected (b) ? "Delete selected bands" : "Delete band", [this, b]
        {
            if (model.isSelected (b) && model.selection().size() > 1) model.deleteSelected();
            else model.deleteBand (b);
        });
        m.showMenuAsync (juce::PopupMenu::Options().withMousePosition().withDeletionCheck (*this));
    }

    void EQDisplay::showBackgroundMenu (double freq, double gainDb)
    {
        juce::PopupMenu m;
        juce::PopupMenu add;
        for (int s = 0; s < (int) Shape::Count; ++s)
            add.addItem (shapeName ((Shape) s), model.freeSlot() >= 0, false, [this, s, freq, gainDb]
            {
                const auto shape = (Shape) s;
                const double scale = model.gainScale();
                const double g = shapeUsesGain (shape) && std::abs (scale) > 1.0e-3 ? gainDb / scale : 0.0;
                const int b = model.createBand (freq, g, shape, shape == Shape::LowCut || shape == Shape::HighCut ? Slope::dB24 : Slope::dB12,
                                                shape == Shape::LowCut || shape == Shape::HighCut ? 0.71 : 1.0);
                if (b >= 0) model.selectOnly (b);
            });
        m.addSubMenu ("Add band at " + formatFreq (freq), add);
        m.addSeparator();
        juce::PopupMenu range;
        for (double r : { 3.0, 6.0, 12.0, 30.0 })
            range.addItem (juce::String ((int) r) + " dB", true, rangeDb == r, [this, r] { setRange (r); });
        m.addSubMenu ("Display range", range);
        juce::PopupMenu anaMenu;
        addAnalyzerMenuItems (anaMenu, proc, [this] { needsRepaint = true; if (onSettingsChanged) onSettingsChanged(); });
        m.addSubMenu ("Analyzer", anaMenu);
        m.addItem ("Piano ruler", true, pianoVisible, [this] { setPianoVisible (! pianoVisible); });
        m.addItem ("EQ Sketch mode", true, sketchMode, [this] { setSketchMode (! sketchMode); });
        m.addSeparator();
        m.addItem ("Copy EQ curve", model.usedCount() > 0, false, [this] { copyCurve(); });
        m.addItem ("Paste EQ curve", PresetManager::isBandsText (juce::SystemClipboard::getTextFromClipboard()), false, [this] { pasteCurve(); });
        m.addSeparator();
        m.addItem ("Select all bands", model.usedCount() > 0, false, [this] { model.selectAll(); });
        m.addItem ("Delete selected bands", ! model.selection().empty(), false, [this] { model.deleteSelected(); });
        m.addItem ("Delete all bands", model.usedCount() > 0, false, [this]
        {
            model.beginGesture ("Delete all bands");
            for (int b = 0; b < kMaxBands; ++b) if (model.isUsed (b)) model.set (b, bpUsed, 0.0);
            model.endGesture();
            model.clearSelection();
        });
        m.showMenuAsync (juce::PopupMenu::Options().withMousePosition().withDeletionCheck (*this));
    }

    //==========================================================================
    juce::Path EQDisplay::curvePath (const float* db, float yTop, float yBottom) const
    {
        juce::Path p;
        p.preallocateSpace (numPts * 3 + 6);
        for (int i = 0; i < numPts; ++i)
        {
            const float y = juce::jlimit (yTop, yBottom, yForDb (db[i]));
            if (i == 0) p.startNewSubPath (ptX[0], y);
            else p.lineTo (ptX[(size_t) i], y);
        }
        return p;
    }

    juce::Path EQDisplay::areaPath (const float* top, const float* base, float yMin, float yMax) const
    {
        juce::Path p;
        p.preallocateSpace (numPts * 6 + 8);
        for (int i = 0; i < numPts; ++i)
        {
            const float y = juce::jlimit (yMin, yMax, yForDb (top[i]));
            if (i == 0) p.startNewSubPath (ptX[0], y);
            else p.lineTo (ptX[(size_t) i], y);
        }
        for (int i = numPts - 1; i >= 0; --i)
            p.lineTo (ptX[(size_t) i], base != nullptr ? juce::jlimit (yMin, yMax, yForDb (base[i])) : yForDb (0.0));
        p.closeSubPath();
        return p;
    }

    void EQDisplay::paint (juce::Graphics& g)
    {
        juce::ColourGradient bg (theme::bgTop, 0.0f, 0.0f, theme::bgBottom, 0.0f, (float) getHeight(), false);
        g.setGradientFill (bg);
        g.fillAll();
        if (isAlive && ! isAlive()) return;

        drawGrid (g);
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (plot.toNearestInt());
            drawAnalyzer (g);
            drawBands (g);
            drawComposite (g);
            drawOverlays (g);
            drawNodes (g);
            if (! compact) drawPeaks (g);
        }
        if (! compact)
        {
            drawPiano (g);
            drawLabels (g);
            drawCard (g);
        }
        if (proc.getSoloBand() >= 0 && ! compact)
        {
            const auto r = juce::Rectangle<float> (120.0f * uiScale, 20.0f * uiScale).withCentre ({ plot.getCentreX(), plot.getY() + 16.0f * uiScale });
            g.setColour (theme::bandColour (proc.getSoloBand()).withAlpha (0.9f));
            g.fillRoundedRectangle (r, 10.0f * uiScale);
            g.setColour (juce::Colour (0xff111218));
            g.setFont (theme::font (12.0f * uiScale, 2));
            g.drawText ("SOLO  BAND " + juce::String (proc.getSoloBand() + 1), r, juce::Justification::centred, false);
        }
    }

    void EQDisplay::drawGrid (juce::Graphics& g)
    {
        // frequency lines
        for (double decade = 10.0; decade < kMaxFreq; decade *= 10.0)
            for (int k = 1; k <= 9; ++k)
            {
                const double f = decade * k;
                if (f < kMinFreq || f > kMaxFreq) continue;
                const float x = xForFreq (f);
                const bool major = k == 1;
                g.setColour (major ? theme::gridMajor : theme::gridLine);
                g.drawVerticalLine ((int) std::round (x), plot.getY(), plot.getBottom());
            }
        // gain lines
        const double step = rangeDb <= 3.0 ? 1.0 : (rangeDb <= 6.0 ? 2.0 : (rangeDb <= 12.0 ? 3.0 : 6.0));
        for (double db = -rangeDb; db <= rangeDb + 1.0e-6; db += step)
        {
            const float y = yForDb (db);
            g.setColour (std::abs (db) < 1.0e-6 ? theme::zeroLine : theme::gridLine);
            g.drawHorizontalLine ((int) std::round (y), plot.getX(), plot.getRight());
        }
    }

    void EQDisplay::drawAnalyzer (juce::Graphics& g)
    {
        constexpr int P = SpectrumAnalyzer::kPoints;
        auto buildPath = [this] (const float* db, bool closed)
        {
            juce::Path p;
            p.preallocateSpace (P * 3 + 8);
            const float bottom = plot.getBottom() + 2.0f;
            float prev = 0.0f;
            for (int i = 0; i < P; ++i)
            {
                // light smoothing across neighbouring points
                const float a = db[std::max (0, i - 1)], b = db[i], c = db[std::min (P - 1, i + 1)];
                const float v = 0.25f * a + 0.5f * b + 0.25f * c + tiltDb[(size_t) i];
                const float x = plot.getX() + plot.getWidth() * (float) i / (float) (P - 1);
                const float y = juce::jlimit (plot.getY() - 2.0f, bottom, yForAnalyzer (v));
                if (i == 0)
                {
                    if (closed) { p.startNewSubPath (x, bottom); p.lineTo (x, y); }
                    else p.startNewSubPath (x, y);
                }
                else p.lineTo (x, y);
                prev = y;
            }
            juce::ignoreUnused (prev);
            if (closed)
            {
                p.lineTo (plot.getRight(), bottom);
                p.closeSubPath();
            }
            return p;
        };

        if (ana.pre && proc.anaPre.hasSignal())
        {
            const auto fill = buildPath (proc.anaPre.getDb(), true);
            g.setColour (theme::analyzerPre.withAlpha (ana.post ? 0.20f : 0.28f));
            g.fillPath (fill);
            if (! ana.post)
            {
                g.setColour (theme::analyzerPre.withAlpha (0.6f));
                g.strokePath (buildPath (proc.anaPre.getDb(), false), juce::PathStrokeType (1.0f * uiScale));
            }
        }
        if (ana.sc && proc.anaSc.hasSignal())
        {
            g.setColour (theme::analyzerSc.withAlpha (0.08f));
            g.fillPath (buildPath (proc.anaSc.getDb(), true));
            g.setColour (theme::analyzerSc.withAlpha (0.7f));
            g.strokePath (buildPath (proc.anaSc.getDb(), false), juce::PathStrokeType (1.1f * uiScale));
        }
        if (ana.post && proc.anaPost.hasSignal())
        {
            const auto fill = buildPath (proc.anaPost.getDb(), true);
            juce::ColourGradient grad (juce::Colours::white.withAlpha (0.10f), 0.0f, plot.getY(), juce::Colours::white.withAlpha (0.015f), 0.0f, plot.getBottom(), false);
            g.setGradientFill (grad);
            g.fillPath (fill);

            // collisions with other instances: red where this track is masked
            if (collisionActive)
            {
                float mxc = 0.0f;
                for (auto c : collision) mxc = std::max (mxc, c);
                if (mxc > 0.04f)
                {
                    juce::ColourGradient hg (theme::collision.withAlpha (0.0f), plot.getX(), 0.0f, theme::collision.withAlpha (0.0f), plot.getRight(), 0.0f, false);
                    hg.clearColours();
                    juce::ColourGradient top (hg);
                    for (int i = 0; i < P; i += 2)
                    {
                        const float c = juce::jlimit (0.0f, 1.0f, collision[(size_t) i]);
                        hg.addColour ((double) i / (P - 1), theme::collision.withAlpha (0.48f * c));
                        top.addColour ((double) i / (P - 1), theme::collision.withAlpha (0.9f * c));
                    }
                    {
                        juce::Graphics::ScopedSaveState ss (g);
                        g.reduceClipRegion (fill);
                        g.setGradientFill (hg);
                        g.fillRect (plot);
                    }
                    g.setGradientFill (top);
                    g.fillRect (plot.withHeight (3.0f * uiScale));
                }
            }
            g.setColour (theme::analyzerPost.withAlpha (0.62f));
            g.strokePath (buildPath (proc.anaPost.getDb(), false), juce::PathStrokeType (1.1f * uiScale, juce::PathStrokeType::curved));
        }
    }

    void EQDisplay::drawBands (juce::Graphics& g)
    {
        if (numPts < 2) return;
        const float yMin = plot.getY() - 4.0f, yMax = plot.getBottom() + 4.0f;
        const int solo = proc.getSoloBand();

        for (int pass = 0; pass < 2; ++pass)
        {
            for (int b = 0; b < kMaxBands; ++b)
            {
                const auto& c = curves[(size_t) b];
                if (! c.valid || ! c.params.used) continue;
                const bool sel = model.isSelected (b) || b == hoverBand || (drag == DragMode::Node && b == dragPrimary);
                if ((pass == 1) != sel) continue;      // selected bands on top
                const auto col = theme::bandColour (b);
                const float dim = (solo >= 0 && solo != b) ? 0.35f : 1.0f;
                if (! c.active)
                {
                    if (sel && ! compact)
                    {
                        juce::Path p = curvePath (c.staticDb.data(), yMin, yMax);
                        juce::Path dashed;
                        const float dashes[] = { 4.0f, 4.0f };
                        juce::PathStrokeType (1.0f).createDashedStroke (dashed, p, dashes, 2);
                        g.setColour (col.withAlpha (0.45f));
                        g.fillPath (dashed);
                    }
                    continue;
                }

                const auto sh = c.params.shape;
                const bool cutLike = sh == Shape::LowCut || sh == Shape::HighCut || sh == Shape::BandPass || sh == Shape::AllPass;
                if (c.dynamic)
                {
                    // available dynamic range (static -> static + range): faint, dashed edge
                    g.setColour (col.withAlpha (0.07f * dim));
                    g.fillPath (areaPath (c.rangeDb.data(), c.staticDb.data(), yMin, yMax));
                    juce::Path rp = curvePath (c.rangeDb.data(), yMin, yMax);
                    juce::Path dashed;
                    const float dashes[] = { 3.0f * uiScale, 3.0f * uiScale };
                    juce::PathStrokeType (1.0f).createDashedStroke (dashed, rp, dashes, 2);
                    g.setColour (col.withAlpha (0.5f * dim));
                    g.fillPath (dashed);

                    // the band's current response (0 dB -> live curve)
                    g.setColour (col.withAlpha ((sel ? 0.26f : 0.17f) * dim));
                    g.fillPath (areaPath (c.liveDb.data(), nullptr, yMin, yMax));

                    // live gain change (static -> live): what the dynamics are doing right now
                    juce::ColourGradient lg (col.brighter (0.3f).withAlpha (0.66f * dim), 0.0f, plot.getY(),
                                             col.brighter (0.1f).withAlpha (0.50f * dim), 0.0f, plot.getBottom(), false);
                    g.setGradientFill (lg);
                    g.fillPath (areaPath (c.liveDb.data(), c.staticDb.data(), yMin, yMax));
                    g.setColour (col.brighter (0.4f).withAlpha (0.95f * dim));
                    g.strokePath (curvePath (c.liveDb.data(), yMin, yMax), juce::PathStrokeType (1.5f * uiScale, juce::PathStrokeType::curved));
                }
                else if (! cutLike || sel)
                {
                    // area between 0 dB and the band's response (cut filters only while selected)
                    g.setColour (col.withAlpha ((sel ? (cutLike ? 0.12f : 0.22f) : 0.10f) * dim));
                    g.fillPath (areaPath (c.staticDb.data(), nullptr, yMin, yMax));
                }

                g.setColour (col.withAlpha ((sel ? 0.95f : 0.42f) * dim));
                g.strokePath (curvePath (c.staticDb.data(), yMin, yMax), juce::PathStrokeType ((sel ? 1.6f : 1.0f) * uiScale, juce::PathStrokeType::curved));
            }
        }
    }

    void EQDisplay::drawComposite (juce::Graphics& g)
    {
        const float yMin = plot.getY() - 4.0f, yMax = plot.getBottom() + 4.0f;
        bool anyBand = false;
        for (const auto& c : curves) if (c.active) { anyBand = true; break; }
        for (int ch = kNumChans - 1; ch >= 0; --ch)
        {
            if (! chanShown[(size_t) ch]) continue;
            juce::Colour col = theme::accent;
            juce::String tag;
            switch (ch)
            {
                case chLeft:  col = theme::placementColour (Placement::Left);  tag = "L"; break;
                case chRight: col = theme::placementColour (Placement::Right); tag = "R"; break;
                case chMid:   col = theme::placementColour (Placement::Mid);   tag = "M"; break;
                case chSide:  col = theme::placementColour (Placement::Side);  tag = "S"; break;
                default: break;
            }
            const auto p = curvePath (composite[(size_t) ch].data(), yMin, yMax);
            theme::drawGlowPath (g, p, anyBand ? col : col.withAlpha (0.6f), (compact ? 1.5f : 2.1f) * uiScale, anyBand ? 3.0f * uiScale : 0.0f);
            if (tag.isNotEmpty() && ! compact)
            {
                const float y = juce::jlimit (plot.getY() + 8.0f, plot.getBottom() - 8.0f, yForDb (composite[(size_t) ch][(size_t) (numPts - 1)]));
                const auto r = juce::Rectangle<float> (14.0f * uiScale, 14.0f * uiScale).withCentre ({ plot.getRight() - 50.0f * uiScale, y - 10.0f * uiScale });
                g.setColour (col.withAlpha (0.85f));
                g.fillRoundedRectangle (r, 3.0f * uiScale);
                g.setColour (juce::Colour (0xff111218));
                g.setFont (theme::font (10.0f * uiScale, 2));
                g.drawText (tag, r, juce::Justification::centred, false);
            }
        }
    }

    void EQDisplay::drawNodes (juce::Graphics& g)
    {
        const int solo = proc.getSoloBand();
        auto drawOne = [&] (int b)
        {
            const auto& c = curves[(size_t) b];
            const auto& bp = c.params;
            const auto p = nodePosition (b);
            const bool sel = model.isSelected (b);
            const bool hov = b == hoverBand || (drag == DragMode::Node && b == dragPrimary);
            const auto col = theme::bandColour (b);
            float r = (compact ? 4.5f : 6.0f) * uiScale;
            if (hov) r += 1.5f * uiScale;

            // dynamic range handle + live position
            if (c.dynamic && c.active && ! compact)
            {
                const auto h = rangeHandlePos (b);
                g.setColour (col.withAlpha (0.55f));
                g.drawLine ({ p, h }, 1.2f * uiScale);
                const float dir = h.y < p.y ? -1.0f : 1.0f;
                juce::Path tri;
                const float ts = (b == rangeHover ? 6.0f : 4.5f) * uiScale;
                tri.addTriangle (h.x, h.y + dir * ts * 0.9f, h.x - ts, h.y - dir * ts * 0.6f, h.x + ts, h.y - dir * ts * 0.6f);
                g.setColour (col.withAlpha (b == rangeHover ? 1.0f : 0.85f));
                g.fillPath (tri);
                if (! c.spectral)
                {
                    const double k = gainDisplayFactor (bp);
                    const double sc = model.gainScale();
                    const double liveDb = (bp.gainDb * sc + (double) proc.engine.getDynamicGainDb (b)) * (std::abs (sc) > 1.0e-6 ? k / sc : 0.0);
                    const float ly = juce::jlimit (plot.getY() + 3.0f, plot.getBottom() - 3.0f, yForDb (liveDb));
                    g.setColour (juce::Colours::white.withAlpha (0.9f));
                    g.fillEllipse (juce::Rectangle<float> (4.0f * uiScale, 4.0f * uiScale).withCentre ({ p.x, ly }));
                }
            }

            if (sel || hov)
            {
                g.setColour (col.withAlpha (0.22f));
                g.fillEllipse (juce::Rectangle<float> (r * 3.2f, r * 3.2f).withCentre (p));
            }
            const auto circle = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (p);
            if (bp.enabled)
            {
                const float dim = (solo >= 0 && solo != b) ? 0.45f : 1.0f;
                g.setColour (col.withMultipliedAlpha (dim));
                g.fillEllipse (circle);
                g.setColour (juce::Colours::white.withAlpha (0.45f * dim));
                g.drawEllipse (circle.reduced (0.5f), 1.0f);
            }
            else
            {
                g.setColour (theme::bgBottom);
                g.fillEllipse (circle);
                g.setColour (col.withAlpha (0.75f));
                g.drawEllipse (circle.reduced (0.75f), 1.5f * uiScale);
            }
            if (sel)
            {
                g.setColour (juce::Colours::white.withAlpha (0.95f));
                g.drawEllipse (circle.expanded (2.5f * uiScale), 1.5f * uiScale);
            }
            if (bp.place != Placement::Stereo && model.isStereo() && ! compact)
            {
                g.setFont (theme::font (9.5f * uiScale, 2));
                g.setColour (theme::placementColour (bp.place));
                g.drawText (placementShortName (bp.place), juce::Rectangle<float> (14.0f * uiScale, 12.0f * uiScale).withPosition (p.x + r + 1.0f, p.y + r - 4.0f * uiScale),
                            juce::Justification::centredLeft, false);
            }
            if (solo == b)
                theme::drawIcon (g, theme::Icon::Headphones, juce::Rectangle<float> (12.0f * uiScale, 12.0f * uiScale).withCentre ({ p.x + r + 7.0f * uiScale, p.y - r - 6.0f * uiScale }), col, 1.2f);
        };
        for (int pass = 0; pass < 2; ++pass)
            for (int b = 0; b < kMaxBands; ++b)
            {
                const auto& c = curves[(size_t) b];
                if (! c.valid || ! c.params.used) continue;
                const bool front = model.isSelected (b) || b == hoverBand;
                if ((pass == 1) == front) drawOne (b);
            }
    }

    void EQDisplay::drawOverlays (juce::Graphics& g)
    {
        if (drag == DragMode::Lasso && ! lasso.isEmpty())
        {
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRect (lasso);
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.drawRect (lasso, 1.0f);
        }
        if (sketchMode)
        {
            g.setColour (theme::accent.withAlpha (0.06f));
            g.fillRect (plot);
            if (sketch.size() > 1)
            {
                juce::Path p;
                p.startNewSubPath (sketch.front());
                for (size_t i = 1; i < sketch.size(); ++i) p.lineTo (sketch[i]);
                theme::drawGlowPath (g, p, theme::accent.brighter (0.2f), 2.5f * uiScale, 4.0f * uiScale);
            }
            g.setFont (theme::font (13.0f * uiScale, 1));
            g.setColour (theme::accent.withAlpha (0.8f));
            g.drawText ("EQ SKETCH  -  draw the target curve", plot.withHeight (28.0f * uiScale).translated (0.0f, 4.0f * uiScale), juce::Justification::centred, false);
        }
        if (ghostVisible && drag == DragMode::None && ! sketchMode)
        {
            const float r = 6.0f * uiScale;
            g.setColour (theme::accent.withAlpha (0.25f));
            g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (ghostPos));
            g.setColour (theme::accent.withAlpha (0.9f));
            g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (ghostPos), 1.2f);
            theme::drawIcon (g, theme::Icon::Plus, juce::Rectangle<float> (r * 1.6f, r * 1.6f).withCentre (ghostPos), theme::accent, 1.3f);
        }
    }

    void EQDisplay::drawPeaks (juce::Graphics& g)
    {
        if (peaks.empty()) return;
        const auto& an = ana.post ? proc.anaPost : proc.anaPre;
        for (int k = 0; k < (int) peaks.size(); ++k)
        {
            const int i = peaks[(size_t) k];
            const double f = SpectrumAnalyzer::pointFreq (i);
            const float x = xForFreq (f);
            const float y = yForAnalyzer (an.getDb()[i] + tiltDb[(size_t) i]);
            const auto r = peakLabelRect (k);
            const bool hot = k == hoverPeak;
            g.setColour ((hot ? theme::accent.brighter (0.2f) : theme::accent).withAlpha (0.95f));
            g.fillRoundedRectangle (r, 3.0f * uiScale);
            g.drawLine (x, r.getBottom(), x, y, 1.0f);
            g.fillEllipse (juce::Rectangle<float> (5.0f * uiScale, 5.0f * uiScale).withCentre ({ x, y }));
            g.setColour (juce::Colour (0xff17181e));
            g.setFont (theme::font (11.0f * uiScale, 2));
            g.drawText (formatFreqShort (f), r, juce::Justification::centred, false);
        }
    }

    void EQDisplay::drawPiano (juce::Graphics& g)
    {
        if (piano.isEmpty()) return;
        g.setColour (juce::Colour (0xffc9ccd6));
        g.fillRect (piano);
        const int n0 = (int) std::floor (freqToNote (kMinFreq)) - 1, n1 = (int) std::ceil (freqToNote (kMaxFreq)) + 1;
        const float blackH = piano.getHeight() * 0.6f;
        const int focus = model.getFocus();
        const int focusNote = focus >= 0 ? (int) std::lround (freqToNote (model.value (focus, bpFreq))) : -1000;
        for (int n = n0; n <= n1; ++n)
        {
            const float x0 = xForFreq (noteToFreq (n - 0.5)), x1 = xForFreq (noteToFreq (n + 0.5));
            if (x1 < piano.getX() || x0 > piano.getRight()) continue;
            const bool black = isBlackKey (n);
            if (n == focusNote || n == hoverKey)
            {
                const auto col = n == hoverKey ? theme::accent : theme::bandColour (focus);
                g.setColour (col.withAlpha (black ? 0.95f : 0.7f));
                g.fillRect (juce::Rectangle<float> (x0, piano.getY(), x1 - x0, black ? blackH : piano.getHeight()));
            }
            else if (black)
            {
                g.setColour (juce::Colour (0xff2a2c36));
                g.fillRect (juce::Rectangle<float> (x0, piano.getY(), x1 - x0, blackH));
            }
            // white key boundaries
            const int k = ((n % 12) + 12) % 12;
            g.setColour (juce::Colour (0xff8a8d99));
            if (k == 4 || k == 11)
                g.drawVerticalLine ((int) std::round (x1), piano.getY(), piano.getBottom());
            else if (black)
                g.drawVerticalLine ((int) std::round ((x0 + x1) * 0.5f), piano.getY() + blackH, piano.getBottom());
            if (k == 0 && x1 - x0 > 3.0f)
            {
                g.setFont (theme::font (8.5f * uiScale, 1));
                g.setColour (juce::Colour (0xff4a4d5a));
                g.drawText ("C" + juce::String (n / 12 - 1), juce::Rectangle<float> (x0 + 1.0f, piano.getY() + blackH, 24.0f * uiScale, piano.getHeight() - blackH),
                            juce::Justification::centredLeft, false);
            }
        }
        g.setColour (juce::Colour (0xff0b0c10));
        g.drawHorizontalLine ((int) piano.getY(), piano.getX(), piano.getRight());

        if (hoverKey >= 0 && mouseInside)
        {
            const auto text = noteName (noteToFreq (hoverKey), false) + "  " + formatFreq (noteToFreq (hoverKey));
            const auto f = theme::font (12.0f * uiScale, 1);
            const float w = juce::GlyphArrangement::getStringWidth (f, text) + 14.0f * uiScale;
            const float x = juce::jlimit (plot.getX(), plot.getRight() - w, xForFreq (noteToFreq (hoverKey)) - w * 0.5f);
            const auto r = juce::Rectangle<float> (x, piano.getY() - 26.0f * uiScale, w, 21.0f * uiScale);
            theme::drawPanel (g, r, 4.0f * uiScale, false);
            g.setFont (f);
            g.setColour (theme::text);
            g.drawText (text, r, juce::Justification::centred, false);
        }
    }

    void EQDisplay::drawLabels (juce::Graphics& g)
    {
        // frequency labels
        g.setFont (theme::font (11.0f * uiScale));
        g.setColour (theme::textFaint);
        const float ly = plot.getBottom() - 16.0f * uiScale;
        const std::pair<double, const char*> fl[] = { { 20, "20" }, { 50, "50" }, { 100, "100" }, { 200, "200" }, { 500, "500" },
                                                      { 1000, "1k" }, { 2000, "2k" }, { 5000, "5k" }, { 10000, "10k" }, { 20000, "20k" } };
        for (const auto& [f, t] : fl)
            g.drawText (t, juce::Rectangle<float> (40.0f * uiScale, 14.0f * uiScale).withCentre ({ xForFreq (f), ly + 7.0f * uiScale }), juce::Justification::centred, false);

        // gain labels (EQ scale)
        const double step = rangeDb <= 3.0 ? 1.0 : (rangeDb <= 6.0 ? 2.0 : (rangeDb <= 12.0 ? 3.0 : 6.0));
        g.setFont (theme::font (11.0f * uiScale, 1));
        for (double db = -rangeDb + step; db < rangeDb - 1.0e-6; db += step)
        {
            const float y = yForDb (db);
            const juce::String t = std::abs (db) < 1.0e-6 ? juce::String ("0") : (db > 0 ? "+" : "") + juce::String ((int) std::round (db));
            g.setColour (theme::gainLabel.withAlpha (std::abs (db) < 1.0e-6 ? 0.9f : 0.75f));
            g.drawText (t, juce::Rectangle<float> (plot.getRight() - 30.0f * uiScale, y - 7.0f * uiScale, 26.0f * uiScale, 14.0f * uiScale), juce::Justification::centredRight, false);
        }

        // analyzer scale + display range button
        if (! rightCol.isEmpty())
        {
            g.setColour (theme::barBg.withAlpha (0.55f));
            g.fillRect (rightCol);
            g.setColour (theme::barLine);
            g.drawVerticalLine ((int) rightCol.getX(), rightCol.getY(), rightCol.getBottom());
            g.setFont (theme::font (10.5f * uiScale));
            const float stepA = ana.range > 100.0f ? 20.0f : 10.0f;
            for (float db = -stepA; db > -ana.range + 1.0f; db -= stepA)
            {
                const float y = yForAnalyzer (db);
                if (y < rangeButton.getBottom() + 6.0f) continue;
                g.setColour (theme::textFaint);
                g.drawText (juce::String ((int) db), juce::Rectangle<float> (rightCol.getX(), y - 7.0f * uiScale, rightCol.getWidth() - 4.0f * uiScale, 14.0f * uiScale),
                            juce::Justification::centredRight, false);
            }
            const bool hot = mouseInside && rangeButton.contains (mouse);
            g.setColour (hot ? theme::controlHover : theme::controlBg);
            g.fillRoundedRectangle (rangeButton, 3.0f * uiScale);
            g.setColour (theme::accent);
            g.setFont (theme::font (11.0f * uiScale, 2));
            g.drawText (juce::String ((int) rangeDb) + " dB", rangeButton, juce::Justification::centred, false);
        }
    }

    void EQDisplay::drawCard (juce::Graphics& g)
    {
        if (! cardVisible || cardBand < 0 || ! model.isUsed (cardBand)) return;
        const int b = cardBand;
        const auto bp = model.band (b);
        layoutCard (b);
        theme::drawPanel (g, card.r, 6.0f * uiScale, true);
        const auto col = theme::bandColour (b);
        g.setColour (col);
        g.fillRoundedRectangle (card.r.withWidth (3.0f * uiScale).reduced (0.0f, 6.0f * uiScale), 1.5f * uiScale);

        {
            BandParam which = bpFreq;
            if (mouseInside && cardValueAt (mouse, which) >= 0)
            {
                const auto vr = which == bpFreq ? card.freqText : (which == bpGain ? card.gainText : card.qText);
                g.setColour (juce::Colours::white.withAlpha (0.07f));
                g.fillRoundedRectangle (vr.reduced (1.0f), 3.0f * uiScale);
            }
        }
        auto iconCol = [this] (juce::Rectangle<float> r, bool on) { return on ? theme::accent : (mouseInside && r.contains (mouse) ? theme::text : theme::textDim); };
        theme::drawIcon (g, theme::Icon::Power, card.power, iconCol (card.power, bp.enabled), 1.4f * uiScale);
        theme::drawIcon (g, theme::Icon::Headphones, card.solo.reduced (1.5f * uiScale), iconCol (card.solo, proc.getSoloBand() == b), 1.3f * uiScale);
        theme::drawIcon (g, theme::Icon::Close, card.close, iconCol (card.close, false), 1.4f * uiScale);
        theme::drawShapeIcon (g, bp.shape, card.shape.reduced (2.0f * uiScale, 4.0f * uiScale), iconCol (card.shape, false), 1.4f * uiScale);
        theme::drawIcon (g, theme::Icon::ChevronDown, card.menu, iconCol (card.menu, false), 1.4f * uiScale);

        const float row = card.r.getHeight() / 3.0f;
        auto textRow = [&] (int i) { return juce::Rectangle<float> (card.power.getRight() + 4.0f * uiScale, card.r.getY() + row * (float) i,
                                                                     card.solo.getX() - card.power.getRight() - 8.0f * uiScale, row); };
        juce::String freqText = formatFreq (bp.freq);
        if (pianoVisible) freqText << "  " << noteName (bp.freq, true);
        g.setColour (juce::Colours::white);
        g.setFont (theme::font (12.5f * uiScale, 2));
        g.drawFittedText (freqText, textRow (0).toNearestInt(), juce::Justification::centred, 1, 0.8f);

        juce::String mid;
        if (shapeUsesGain (bp.shape))
        {
            mid = formatGain (bp.gainDb);
            if (bp.isDynamic())
            {
                const float dg = proc.engine.getDynamicGainDb (b);
                mid << "   dyn " << (bp.dynRange > 0 ? "+" : "") << juce::String (bp.dynRange, 1);
                if (! bp.spectral && std::abs (dg) > 0.05f) mid << " (" << juce::String (dg, 1) << ")";
            }
        }
        else mid = shapeUsesSlope (bp.shape) ? juce::String (slopeName (bp.slope)) : juce::String();
        g.setColour (theme::text);
        g.setFont (theme::font (12.0f * uiScale, 1));
        g.drawFittedText (mid, textRow (1).toNearestInt(), juce::Justification::centred, 1, 0.8f);

        juce::String qText = shapeUsesQ (bp.shape, bp.slope) ? "Q: " + formatQ (bp.q) : juce::String (shapeName (bp.shape));
        auto r3 = textRow (2);
        g.setFont (theme::font (12.0f * uiScale, 1));
        g.setColour (theme::text);
        g.drawFittedText (qText, r3.toNearestInt(), juce::Justification::centred, 1, 0.8f);
        if (shapeUsesQ (bp.shape, bp.slope))
        {
            g.setFont (theme::font (9.5f * uiScale, 1));
            g.setColour (theme::textFaint);
            g.drawText (modifierName(), r3.withTrimmedLeft (r3.getWidth() * 0.78f), juce::Justification::centredLeft, false);
        }
    }
}
