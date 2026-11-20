/*
    Claude-Q 4 - widgets
*/
#include "Widgets.h"

namespace cq
{
    namespace
    {
        constexpr float kArcStart = -2.35619449f;   // -135 degrees
        constexpr float kArcRange = 4.71238898f;    // 270 degrees

        float angleFor (double norm) { return kArcStart + (float) juce::jlimit (0.0, 1.0, norm) * kArcRange; }

        // self-deleting inline text editor
        class InlineEntry : public juce::TextEditor, private juce::TextEditor::Listener
        {
        public:
            InlineEntry (std::function<void (const juce::String&)> cb) : callback (std::move (cb))
            {
                addListener (this);
                setJustification (juce::Justification::centred);
                setFont (theme::font (13.0f));
                setSelectAllWhenFocused (true);
            }
            void textEditorReturnKeyPressed (juce::TextEditor&) override { finish (true); }
            void textEditorEscapeKeyPressed (juce::TextEditor&) override { finish (false); }
            void textEditorFocusLost (juce::TextEditor&) override         { finish (true); }

        private:
            void finish (bool accept)
            {
                if (done) return;
                done = true;
                auto text = getText();
                auto cb = callback;
                juce::Component::SafePointer<InlineEntry> self (this);
                juce::MessageManager::callAsync ([self, text, cb, accept]
                {
                    if (accept && cb) cb (text);
                    if (self != nullptr) delete self.getComponent();
                });
            }
            std::function<void (const juce::String&)> callback;
            bool done = false;
        };
    }

    void showInlineTextEntry (juce::Component& over, const juce::String& initial, std::function<void (const juce::String&)> done)
    {
        auto* parent = over.getParentComponent() != nullptr ? over.getParentComponent() : &over;
        auto* e = new InlineEntry (std::move (done));
        parent->addAndMakeVisible (e);
        auto r = over.getBounds();
        if (parent == &over) r = over.getLocalBounds();
        e->setBounds (r.withSizeKeepingCentre (std::max (r.getWidth(), 90), std::max (22, std::min (r.getHeight(), 26))));
        e->setText (initial, false);
        e->grabKeyboardFocus();
        e->selectAll();
    }

    //==========================================================================
    Knob::Knob (juce::String labelText) : label (std::move (labelText))
    {
        setRepaintsOnMouseActivity (false);
    }

    juce::Rectangle<float> Knob::knobArea() const
    {
        auto r = getLocalBounds().toFloat();
        r.removeFromBottom (labelHeight);
        const float side = std::min (r.getWidth(), r.getHeight());
        return juce::Rectangle<float> (side, side).withCentre (r.getCentre());
    }

    bool Knob::inRingZone (juce::Point<float> p) const
    {
        if (! hasRing) return false;
        const auto a = knobArea();
        const float R = a.getWidth() * 0.5f;
        const float d = p.getDistanceFrom (a.getCentre());
        return d >= R * 0.80f && d <= R * 1.15f;
    }

    void Knob::refresh()
    {
        const double n = getNormalised ? getNormalised() : 0.0;
        const auto t = getText ? getText() : juce::String();
        const bool act = isActive ? isActive() : true;
        const bool rv = hasRing && ringVisible && ringVisible();
        double f = -1, to = -1, lv = -1;
        if (rv)
        {
            f = getRingFrom ? getRingFrom() : 0.0;
            to = getRingTo ? getRingTo() : 0.0;
            lv = getRingLive ? getRingLive() : f;
        }
        if (std::abs (n - lastNorm) > 1.0e-4 || t != lastText || act != lastActive || rv != lastRingVisible
            || std::abs (f - lastFrom) > 1.0e-4 || std::abs (to - lastTo) > 1.0e-4 || std::abs (lv - lastLive) > 2.0e-3)
        {
            lastNorm = n; lastText = t; lastActive = act; lastRingVisible = rv; lastFrom = f; lastTo = to; lastLive = lv;
            repaint();
        }
    }

    void Knob::paint (juce::Graphics& g)
    {
        const auto a = knobArea();
        const auto c = a.getCentre();
        const float R = a.getWidth() * 0.5f;
        const float rKnob = R * 0.74f;
        const bool act = isActive ? isActive() : true;
        const auto col = act ? colour : theme::textFaint;
        const double norm = getNormalised ? getNormalised() : 0.0;

        // track
        juce::Path track;
        track.addCentredArc (c.x, c.y, R * 0.86f, R * 0.86f, 0.0f, kArcStart, kArcStart + kArcRange, true);
        g.setColour (juce::Colour (0xff2a2d3b));
        g.strokePath (track, juce::PathStrokeType (R * 0.085f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // value arc
        const float from = bipolar ? angleFor (0.5) : kArcStart;
        const float to = angleFor (norm);
        if (std::abs (to - from) > 0.001f)
        {
            juce::Path arc;
            arc.addCentredArc (c.x, c.y, R * 0.86f, R * 0.86f, 0.0f, std::min (from, to), std::max (from, to), true);
            g.setColour (col);
            g.strokePath (arc, juce::PathStrokeType (R * 0.085f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // dynamic range ring
        const bool rv = hasRing && ringVisible && ringVisible();
        if (rv || (hasRing && ringHover))
        {
            const float rr = R * 1.02f;
            if (rv)
            {
                const float af = angleFor (getRingFrom ? getRingFrom() : 0.5), at = angleFor (getRingTo ? getRingTo() : 0.5);
                juce::Path ring;
                ring.addCentredArc (c.x, c.y, rr, rr, 0.0f, std::min (af, at), std::max (af, at), true);
                g.setColour (col.withAlpha (0.55f));
                g.strokePath (ring, juce::PathStrokeType (R * 0.07f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                if (getRingLive)
                {
                    const float al = angleFor (getRingLive());
                    const auto lp = c.getPointOnCircumference (rr, al);
                    g.setColour (juce::Colours::white);
                    g.fillEllipse (juce::Rectangle<float> (R * 0.13f, R * 0.13f).withCentre (lp));
                }
            }
            else
            {
                juce::Path ring;
                ring.addCentredArc (c.x, c.y, rr, rr, 0.0f, kArcStart, kArcStart + kArcRange, true);
                g.setColour (col.withAlpha (0.18f));
                g.strokePath (ring, juce::PathStrokeType (R * 0.05f));
            }
        }

        // knob body
        juce::ColourGradient body (juce::Colour (0xff343848), c.x, c.y - rKnob, juce::Colour (0xff1c1e27), c.x, c.y + rKnob, false);
        g.setGradientFill (body);
        g.fillEllipse (juce::Rectangle<float> (rKnob * 2.0f, rKnob * 2.0f).withCentre (c));
        g.setColour (juce::Colour (0xff0e0f14));
        g.drawEllipse (juce::Rectangle<float> (rKnob * 2.0f, rKnob * 2.0f).withCentre (c), 1.0f);
        g.setColour (juce::Colours::white.withAlpha (hover ? 0.09f : 0.05f));
        g.drawEllipse (juce::Rectangle<float> (rKnob * 2.0f - 2.0f, rKnob * 2.0f - 2.0f).withCentre (c), 1.0f);

        // pointer
        const auto p0 = c.getPointOnCircumference (rKnob * 0.38f, to);
        const auto p1 = c.getPointOnCircumference (rKnob * 0.86f, to);
        g.setColour (act ? juce::Colours::white.withAlpha (0.92f) : theme::textFaint);
        g.drawLine ({ p0, p1 }, std::max (1.6f, R * 0.07f));

        // label / value
        auto lab = getLocalBounds().toFloat().removeFromBottom (labelHeight);
        const bool showValue = dragging || hover;
        juce::String t = label;
        if (showValue)
            t = (ringDrag || ringHover) && getRingText ? getRingText() : (getText ? getText() : label);
        g.setFont (theme::font (std::min (13.0f, labelHeight * 0.82f), showValue ? 1 : 2));
        g.setColour (showValue ? theme::text : theme::textDim);
        g.drawText (t, lab, juce::Justification::centred, false);
    }

    void Knob::mouseMove (const juce::MouseEvent& e)
    {
        const bool rz = inRingZone (e.position);
        if (rz != ringHover) { ringHover = rz; repaint(); }
    }

    void Knob::mouseDown (const juce::MouseEvent& e)
    {
        if (e.mods.isPopupMenu())
        {
            if (onTextEntered)
            {
                auto cb = onTextEntered;
                showInlineTextEntry (*this, getText ? getText() : juce::String(), [cb] (const juce::String& s) { cb (s); });
            }
            return;
        }
        dragging = true;
        ringDrag = inRingZone (e.position);
        lastPos = e.position;
        if (ringDrag) { if (onRingStart) onRingStart(); }
        else if (onDragStart) onDragStart();
        e.source.enableUnboundedMouseMovement (true);
        repaint();
    }

    void Knob::mouseDrag (const juce::MouseEvent& e)
    {
        if (! dragging) return;
        const auto d = e.position - lastPos;
        lastPos = e.position;
        double delta = (double) (d.x - d.y) / 220.0;
        if (e.mods.isShiftDown()) delta *= 0.12;
        if (ringDrag) { if (onRingDrag) onRingDrag (delta); }
        else if (onDrag) onDrag (delta);
        refresh();
    }

    void Knob::mouseUp (const juce::MouseEvent& e)
    {
        if (! dragging) return;
        dragging = false;
        e.source.enableUnboundedMouseMovement (false);
        if (ringDrag) { if (onRingEnd) onRingEnd(); }
        else if (onDragEnd) onDragEnd();
        ringDrag = false;
        repaint();
    }

    void Knob::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (inRingZone (e.position) && onRingStart && onRingDrag && onRingEnd && getRingFrom && getRingTo)
        {
            // reset the dynamic range: move the ring end back onto the static gain
            onRingStart();
            onRingDrag (getRingFrom() - getRingTo());
            onRingEnd();
        }
        else if (onDoubleClick) onDoubleClick();
        refresh();
    }

    void Knob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
    {
        double delta = (double) (w.deltaY != 0.0f ? w.deltaY : w.deltaX) * (w.isReversed ? -1.0 : 1.0) * 0.05;
        if (e.mods.isShiftDown()) delta *= 0.15;
        if (inRingZone (e.position) && onRingStart && onRingDrag && onRingEnd)
        {
            onRingStart(); onRingDrag (delta); onRingEnd();
        }
        else if (onWheel) onWheel (delta);
        refresh();
    }

    //==========================================================================
    void IconButton::refresh()
    {
        const bool s = getState ? getState() : getToggleState();
        if (s != lastState) { lastState = s; repaint(); }
    }

    void IconButton::paintButton (juce::Graphics& g, bool over, bool down)
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const bool on = getState ? getState() : getToggleState();
        if (drawFrame || over || down)
        {
            g.setColour (down ? theme::controlHover.brighter (0.1f) : (over ? theme::controlHover : theme::controlBg));
            g.fillRoundedRectangle (r, 4.0f);
        }
        auto c = on ? onColour : (over ? theme::text : offColour);
        if (! isEnabled()) c = c.withAlpha (0.3f);
        const float side = std::min (r.getWidth(), r.getHeight()) * iconScale;
        theme::drawIcon (g, icon, juce::Rectangle<float> (side, side).withCentre (r.getCentre()), c, std::max (1.2f, side * 0.1f));
    }

    //==========================================================================
    void FlatButton::refresh()
    {
        const auto t = getText ? getText() : getButtonText();
        const bool s = getState ? getState() : getToggleState();
        if (t != lastText || s != lastState) { lastText = t; lastState = s; repaint(); }
    }

    void FlatButton::paintButton (juce::Graphics& g, bool over, bool down)
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const bool on = getState ? getState() : getToggleState();
        if (drawFrame || over || down || on)
        {
            auto bg = on ? onColour.withAlpha (0.16f) : theme::controlBg.withAlpha (drawFrame ? 1.0f : 0.8f);
            if (over) bg = bg.brighter (0.06f);
            if (down) bg = bg.brighter (0.12f);
            g.setColour (bg);
            g.fillRoundedRectangle (r, 4.0f);
            if (drawFrame)
            {
                g.setColour (on ? onColour.withAlpha (0.45f) : theme::panelEdge);
                g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
            }
        }
        const auto t = getText ? getText() : getButtonText();
        auto tr = r.reduced (7.0f, 0.0f);
        if (showChevron)
        {
            auto cr = tr.removeFromRight (10.0f);
            theme::drawIcon (g, theme::Icon::ChevronDown, cr.withSizeKeepingCentre (10.0f, 10.0f), theme::textDim, 1.3f);
            tr.removeFromRight (3.0f);
        }
        if (! dotColour.isTransparent())
        {
            auto dr = tr.removeFromLeft (10.0f);
            g.setColour (dotColour);
            g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (dr.getCentre()));
            tr.removeFromLeft (3.0f);
        }
        g.setFont (theme::font (fontSize, 1));
        g.setColour (! isEnabled() ? theme::textFaint : (on ? onColour.brighter (0.25f) : (over ? juce::Colours::white : theme::text)));
        g.drawFittedText (t, tr.toNearestInt(), justification, 1, 0.8f);
    }

    //==========================================================================
    void DragValue::refresh()
    {
        const auto t = getText ? getText() : juce::String();
        if (t != lastText) { lastText = t; repaint(); }
    }

    void DragValue::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        if (hover || dragging)
        {
            g.setColour (theme::controlHover);
            g.fillRoundedRectangle (r, 4.0f);
        }
        auto tr = r.reduced (6.0f, 0.0f);
        if (caption.isNotEmpty())
        {
            g.setFont (theme::font (fontSize - 1.0f));
            g.setColour (theme::textDim);
            const float w = juce::GlyphArrangement::getStringWidth (theme::font (fontSize - 1.0f), caption) + 6.0f;
            g.drawText (caption, tr.removeFromLeft (w), juce::Justification::centredLeft, false);
        }
        g.setFont (theme::font (fontSize, 1));
        g.setColour (dragging ? juce::Colours::white : theme::text);
        g.drawFittedText (getText ? getText() : juce::String(), tr.toNearestInt(), juce::Justification::centred, 1, 0.8f);
    }

    void DragValue::mouseDown (const juce::MouseEvent& e)
    {
        if (e.mods.isPopupMenu() || e.mods.isAltDown())
        {
            showEditor();
            return;
        }
        dragging = true;
        lastY = e.position.y;
        if (onDragStart) onDragStart();
        e.source.enableUnboundedMouseMovement (true);
    }

    void DragValue::mouseDrag (const juce::MouseEvent& e)
    {
        if (! dragging) return;
        double d = (double) (lastY - e.position.y);
        lastY = e.position.y;
        if (e.mods.isShiftDown()) d *= 0.15;
        if (onDrag) onDrag (d);
        refresh();
    }

    void DragValue::mouseUp (const juce::MouseEvent& e)
    {
        if (! dragging) return;
        dragging = false;
        e.source.enableUnboundedMouseMovement (false);
        if (onDragEnd) onDragEnd();
        repaint();
    }

    void DragValue::mouseDoubleClick (const juce::MouseEvent&)
    {
        if (onDoubleClick) onDoubleClick();
        refresh();
    }

    void DragValue::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
    {
        double d = (double) (w.deltaY != 0.0f ? w.deltaY : w.deltaX) * (w.isReversed ? -1.0 : 1.0) * 40.0;
        if (e.mods.isShiftDown()) d *= 0.15;
        if (onWheel) onWheel (d);
        refresh();
    }

    void DragValue::showEditor()
    {
        if (! onTextEntered) return;
        auto cb = onTextEntered;
        showInlineTextEntry (*this, getText ? getText() : juce::String(), [cb] (const juce::String& s) { cb (s); });
    }
}
