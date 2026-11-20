/*
    Claude-Q 4 - floating band panel

    Follows the selected band.  Main row: enable, shape, slope, FREQ / GAIN / Q knobs (the
    outer ring of GAIN sets the dynamic range), stereo placement, solo, split.  The
    expandable dynamics section holds threshold (auto / manual), attack & release (auto =
    program dependent, or manual override), external side chain, free trigger range,
    trigger audition and the spectral dynamics density / tilt.
    With several bands selected every control edits all of them relatively.
*/
#pragma once

#include "EditorModel.h"
#include "Widgets.h"

namespace cq
{
    class BandPanel : public juce::Component
    {
    public:
        explicit BandPanel (EditorModel& m);
        ~BandPanel() override;

        void refresh();                         // once per frame
        bool isExpanded() const noexcept        { return expanded; }
        void setExpanded (bool b);
        int getPreferredWidth() const noexcept  { return juce::roundToInt (536.0f * uiScale); }
        int getPreferredHeight() const noexcept { return juce::roundToInt ((expanded ? 214.0f : 124.0f) * uiScale); }
        void setUiScale (float s)               { uiScale = s; resized(); }
        std::function<void()> onLayoutChanged;
        std::function<bool()> isPianoSnapping;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        std::vector<int> targets() const;
        int focus() const { return model.getFocus(); }
        void beginEdit (BandParam k, const juce::String& name);
        void dragEdit (BandParam k, double delta, std::function<double (double start, double accum)> fn);
        void endEdit();
        void wireKnobs();
        void wireButtons();
        void showShapeMenu();
        void showSlopeMenu();
        void showPlacementMenu();
        void showSplitMenu();
        void stepFocus (int dir);

        EditorModel& model;
        float uiScale = 1.0f;
        bool expanded = false;

        IconButton powerBtn { "Enable / disable band", theme::Icon::Power };
        IconButton dynBtn { "Dynamic EQ on / off", theme::Icon::Dynamic };
        IconButton specBtn { "Spectral dynamics on / off", theme::Icon::Spectral };
        IconButton expandBtn { "Show dynamics settings", theme::Icon::Expand };
        IconButton prevBtn { "Previous band", theme::Icon::ChevronLeft };
        IconButton nextBtn { "Next band", theme::Icon::ChevronRight };
        IconButton closeBtn { "Delete band", theme::Icon::Close };
        IconButton soloBtn { "Solo (hold)", theme::Icon::Headphones };
        IconButton splitBtn { "Split band into L/R or M/S", theme::Icon::Scissors };
        IconButton auditionBtn { "Audition the dynamics trigger (hold)", theme::Icon::Ear };
        FlatButton shapeBtn { "Shape" }, slopeBtn { "Slope" }, placeBtn { "Placement" };
        FlatButton autoThrBtn { "AUTO" }, autoTimesBtn { "AUTO" }, scBtn { "EXT SC" }, freeBtn { "FREE" }, tiltBtn { "TILT" };
        Knob freqKnob { "FREQ" }, gainKnob { "GAIN" }, qKnob { "Q" };
        Knob thrKnob { "THRESHOLD" }, attKnob { "ATTACK" }, relKnob { "RELEASE" }, densityKnob { "DENSITY" };
        DragValue trigLo, trigHi;

        struct Start { int band; double value; };
        std::vector<Start> starts;
        double accum = 0.0;
        bool soloActive = false, auditionActive = false;
        juce::Rectangle<float> headerArea, dynArea, mainArea;
    };
}
