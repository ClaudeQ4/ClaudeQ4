/*
    Claude-Q 4 - output meter, help page, instance list and EQ Match panel
*/
#pragma once

#include "EQDisplay.h"
#include "Widgets.h"

namespace cq
{
    //==========================================================================
    class OutputMeter : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit OutputMeter (ClaudeQ4AudioProcessor& p);
        void update (double now, float analyzerRangeDb, float topOffsetPx);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;

    private:
        float yFor (float db) const;
        ClaudeQ4AudioProcessor& proc;
        std::array<float, 2> level { -150.0f, -150.0f }, hold { -150.0f, -150.0f };
        std::array<double, 2> holdTime {};
        float maxPeak = -150.0f;
        bool clip = false;
        double last = 0.0;
        float range = 90.0f, top = 0.0f;
        float lastDrawn[3] { 0, 0, 0 };
    };

    //==========================================================================
    class HelpOverlay : public juce::Component
    {
    public:
        HelpOverlay();
        std::function<void()> onClose;
        void setUiScale (float s);
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;

    private:
        class Content : public juce::Component
        {
        public:
            float scale = 1.0f;
            int layoutHeight (int width);
            void paint (juce::Graphics&) override;
        private:
            juce::TextLayout layout;
            juce::AttributedString text;
            int lastWidth = -1;
            void build();
        };
        Content content;
        juce::Viewport viewport;
        IconButton closeBtn { "Close help", theme::Icon::Close };
        juce::Rectangle<float> panel;
        float uiScale = 1.0f;
    };

    //==========================================================================
    class InstancesOverlay : public juce::Component
    {
    public:
        explicit InstancesOverlay (ClaudeQ4AudioProcessor& self);
        ~InstancesOverlay() override;
        void tick (double now);
        std::function<void()> onClose;
        void setUiScale (float s) { uiScale = s; resized(); }
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        struct Row : public juce::Component
        {
            Row (ClaudeQ4AudioProcessor& p, bool isSelf);
            ~Row() override;
            ClaudeQ4AudioProcessor& proc;
            bool self;
            EditorModel model;
            EQDisplay display;
            FlatButton nameBtn { "name" };
            IconButton bypassBtn { "Bypass this instance", theme::Icon::Power };
            void paint (juce::Graphics&) override;
            void resized() override;
        };
        void rebuild();
        ClaudeQ4AudioProcessor& self;
        juce::Viewport viewport;
        juce::Component content;
        std::vector<std::unique_ptr<Row>> rows;
        IconButton closeBtn { "Close instance list", theme::Icon::Close };
        int generation = -1;
        float uiScale = 1.0f;
    };

    //==========================================================================
    class MatchPanel : public juce::Component
    {
    public:
        MatchPanel (EditorModel& m, EQDisplay& d);
        ~MatchPanel() override;
        void tick (double now);
        std::function<void()> onClose;
        std::function<void (const juce::String&)> onStatus;
        void setUiScale (float s) { uiScale = s; resized(); }
        int getPreferredWidth() const  { return juce::roundToInt (460.0f * uiScale); }
        int getPreferredHeight() const { return juce::roundToInt (196.0f * uiScale); }
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void showSourceMenu();
        juce::String referenceName() const;
        bool referenceSpectrum (double now, std::array<float, SpectrumAnalyzer::kPoints>& out) const;
        void computeDifference();
        void applyMatch();

        EditorModel& model;
        ClaudeQ4AudioProcessor& proc;
        EQDisplay& display;
        float uiScale = 1.0f;
        FlatButton sourceBtn { "Reference" }, learnBtn { "Learn" }, matchBtn { "Match" }, replaceBtn { "Replace bands" };
        IconButton closeBtn { "Close EQ Match", theme::Icon::Close };
        DragValue amountVal, bandsVal;
        int sourceId = 0;                  // 0 = side chain, else instance id
        bool learning = false, replace = true;
        double learnSeconds = 0.0, lastTick = 0.0;
        double amount = 100.0, numBands = 8.0;
        SpectrumAccumulator refAcc, curAcc;
        std::array<float, SpectrumAnalyzer::kPoints> diff {};
        bool haveDiff = false;
        juce::Rectangle<float> graph;
    };
}
