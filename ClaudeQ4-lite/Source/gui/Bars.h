/*
    Claude-Q 4 - header bar (logo, undo / redo, A/B, presets, instances, help, settings)
    and bottom bar (MIDI learn, processing mode / resolution, character, instance name,
    analyzer, auto gain, phase, pan, gain scale, output level, bypass)
*/
#pragma once

#include "EditorModel.h"
#include "Widgets.h"

namespace cq
{
    class ABButton : public juce::Button
    {
    public:
        explicit ABButton (ClaudeQ4AudioProcessor& p) : juce::Button ("A/B"), proc (p) {}
        void paintButton (juce::Graphics&, bool over, bool down) override;
        void refresh() { if (proc.isB() != last) { last = proc.isB(); repaint(); } }
    private:
        ClaudeQ4AudioProcessor& proc;
        bool last = false;
    };

    class HeaderBar : public juce::Component
    {
    public:
        explicit HeaderBar (EditorModel& m);
        void refresh();
        void setUiScale (float s) { uiScale = s; resized(); }
        void paint (juce::Graphics&) override;
        void resized() override;

        std::function<void()> onHelp, onInstances, onSettings, onPresetChanged;
        void showPresetMenu();

    private:
        void savePresetAs();
        ClaudeQ4AudioProcessor& proc;
        float uiScale = 1.0f;
        IconButton undoBtn { "Undo (Ctrl+Z)", theme::Icon::Undo }, redoBtn { "Redo (Ctrl+Shift+Z)", theme::Icon::Redo };
        ABButton abBtn;
        FlatButton copyBtn { "Copy" };
        IconButton prevPreset { "Previous preset", theme::Icon::ChevronLeft }, nextPreset { "Next preset", theme::Icon::ChevronRight };
        FlatButton presetBtn { "Preset" };
        FlatButton instancesBtn { "Instances" }, helpBtn { "Help" };
        IconButton settingsBtn { "Settings", theme::Icon::Gear };
        juce::Rectangle<float> logoArea;
        std::unique_ptr<juce::FileChooser> chooser;
        bool lastUndo = false, lastRedo = false;
    };

    class BottomBar : public juce::Component
    {
    public:
        explicit BottomBar (EditorModel& m);
        void refresh();
        void setUiScale (float s) { uiScale = s; resized(); }
        void paint (juce::Graphics&) override;
        void resized() override;
        void setStatus (const juce::String& s, double now) { status = s; statusTime = now; repaint(); }
        void tickStatus (double now) { if (status.isNotEmpty() && now - statusTime > 3.0) { status.clear(); repaint(); } }

        std::function<void()> onInstances, onAnalyzerChanged;

    private:
        void showMidiMenu();
        void showModeMenu();
        void showResolutionMenu();
        void showCharacterMenu();
        void showAnalyzerMenu();
        EditorModel& model;
        ClaudeQ4AudioProcessor& proc;
        float uiScale = 1.0f;
        FlatButton midiBtn { "MIDI Learn" };
        IconButton midiMenuBtn { "MIDI learn options", theme::Icon::ChevronDown };
        FlatButton modeBtn { "Mode" }, resBtn { "Resolution" }, charBtn { "Character" };
        FlatButton instanceBtn { "Instance" };
        FlatButton analyzerBtn { "Analyzer" };
        IconButton autoGainBtn { "Auto gain", theme::Icon::AutoGain };
        IconButton phaseBtn { "Phase invert", theme::Icon::PhaseInvert };
        IconButton bypassBtn { "Bypass", theme::Icon::Power };
        DragValue panVal, scaleVal, outVal;
        juce::String status;
        double statusTime = 0.0;
        juce::Rectangle<float> statusArea;
    };
}
