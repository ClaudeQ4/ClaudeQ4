/*
    Claude-Q 4 - plug-in editor
*/
#pragma once

#include "plugin/PluginProcessor.h"
#include "EditorModel.h"
#include "EQDisplay.h"
#include "BandPanel.h"
#include "Bars.h"
#include "Panels.h"

namespace cq
{
    class ClaudeQ4Editor : public juce::AudioProcessorEditor, private juce::Timer
    {
    public:
        explicit ClaudeQ4Editor (ClaudeQ4AudioProcessor& p);
        ~ClaudeQ4Editor() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;

        // one GUI frame (driven by the display's vertical blank, or a timer fallback)
        void frame (double now);

        // test / snapshot access
        EQDisplay& getDisplay() noexcept    { return display; }
        BandPanel& getPanel() noexcept      { return panel; }
        EditorModel& getModel() noexcept    { return model; }
        void openHelp();
        void openInstances();
        void openMatch();
        void closeOverlays();

    private:
        void timerCallback() override;
        void positionPanel();
        void showSettingsMenu();
        void setStatus (const juce::String& s);
        void layoutOverlayButtons();

        ClaudeQ4AudioProcessor& proc;
        juce::SharedResourcePointer<theme::FontCache> fonts;   // first member: alive while anything paints
        ClaudeLookAndFeel lnf;
        EditorModel model;
        HeaderBar header;
        BottomBar bottom;
        EQDisplay display;
        BandPanel panel;
        OutputMeter meter;
        IconButton sketchBtn { "EQ Sketch: draw a curve, the bands are created for you", theme::Icon::Brush };
        IconButton pianoBtn { "Piano ruler: show notes, snap bands to note frequencies", theme::Icon::Piano };
        IconButton matchBtn { "EQ Match: match the spectrum of a reference", theme::Icon::Match };
        std::unique_ptr<HelpOverlay> help;
        std::unique_ptr<InstancesOverlay> instances;
        std::unique_ptr<MatchPanel> match;
        std::unique_ptr<juce::TooltipWindow> tooltips;
        std::unique_ptr<juce::VBlankAttachment> vblank;
        double lastFrame = 0.0;
        float uiScale = 1.0f;
        int panelTarget = -1;
        float panelX = -1.0f;
    };
}
