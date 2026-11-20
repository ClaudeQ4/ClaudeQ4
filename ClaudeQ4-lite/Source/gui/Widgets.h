/*
    Claude-Q 4 - widgets

    All widgets use a "pull" model: they ask their owner for the current value through
    lambdas every frame (so they always show the real parameter state, including host
    automation and MIDI), and report relative edits back.
*/
#pragma once

#include "Theme.h"

namespace cq
{
    //==========================================================================
    class Knob : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit Knob (juce::String labelText);

        // value
        std::function<double()> getNormalised;            // 0..1 position of the pointer
        std::function<juce::String()> getText;            // value text
        std::function<bool()> isActive;                   // false -> drawn greyed out
        std::function<void()> onDragStart, onDragEnd, onDoubleClick;
        std::function<void (double deltaNorm)> onDrag;    // relative change (fine already applied)
        std::function<void (double deltaNorm)> onWheel;
        std::function<void (const juce::String&)> onTextEntered;   // right click -> type a value

        // optional outer ring (dynamic range of the gain knob)
        bool hasRing = false;
        std::function<bool()> ringVisible;
        std::function<double()> getRingFrom, getRingTo, getRingLive;   // normalised positions
        std::function<void()> onRingStart, onRingEnd;
        std::function<void (double deltaNorm)> onRingDrag;
        std::function<juce::String()> getRingText;

        juce::Colour colour = theme::accent;
        bool bipolar = false;
        float labelHeight = 15.0f;

        // repaints only if something visible changed
        void refresh();

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { hover = false; ringHover = false; repaint(); }
        void mouseMove (const juce::MouseEvent&) override;

    private:
        juce::Rectangle<float> knobArea() const;
        bool inRingZone (juce::Point<float> p) const;

        juce::String label;
        bool hover = false, ringHover = false, dragging = false, ringDrag = false;
        juce::Point<float> lastPos;
        double lastNorm = -1.0, lastFrom = -1.0, lastTo = -1.0, lastLive = -1.0;
        juce::String lastText;
        bool lastActive = true, lastRingVisible = false;
    };

    //==========================================================================
    class IconButton : public juce::Button
    {
    public:
        IconButton (const juce::String& name, theme::Icon i) : juce::Button (name), icon (i) {}

        theme::Icon icon;
        juce::Colour onColour = theme::accent;
        juce::Colour offColour = theme::textDim;
        bool drawFrame = false;
        float iconScale = 0.62f;
        std::function<bool()> getState;   // optional pull state (drawn as "on")

        void refresh();
        void paintButton (juce::Graphics&, bool over, bool down) override;

    private:
        bool lastState = false;
    };

    //==========================================================================
    class FlatButton : public juce::Button
    {
    public:
        explicit FlatButton (const juce::String& buttonText) : juce::Button (buttonText) { setButtonText (buttonText); }

        std::function<juce::String()> getText;
        std::function<bool()> getState;
        bool showChevron = false;
        bool drawFrame = true;
        juce::Colour onColour = theme::accent;
        float fontSize = 13.0f;
        juce::Justification justification = juce::Justification::centred;
        juce::Colour dotColour = juce::Colours::transparentBlack;

        void refresh();
        void paintButton (juce::Graphics&, bool over, bool down) override;

    private:
        juce::String lastText;
        bool lastState = false;
    };

    //==========================================================================
    class DragValue : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        DragValue() = default;

        std::function<juce::String()> getText;
        std::function<void()> onDragStart, onDragEnd, onDoubleClick;
        std::function<void (double delta)> onDrag;   // units: pixels (fine applied)
        std::function<void (double delta)> onWheel;
        std::function<void (const juce::String&)> onTextEntered;
        juce::String caption;
        float fontSize = 13.0f;

        void refresh();
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { hover = false; repaint(); }

    private:
        void showEditor();
        bool hover = false, dragging = false;
        float lastY = 0.0f;
        juce::String lastText;
        std::unique_ptr<juce::TextEditor> editor;
    };

    // small helper: show a text entry box over a component (async), calls back with the text
    void showInlineTextEntry (juce::Component& over, const juce::String& initial, std::function<void (const juce::String&)> done);
}
