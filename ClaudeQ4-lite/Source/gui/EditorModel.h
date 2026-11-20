/*
    Claude-Q 4 - editor model

    The editor's view of one plug-in instance: parameter reads, edits grouped into
    gestures (host automation touch + one undo step per gesture), band creation /
    deletion and the band selection.  Message thread only.
*/
#pragma once

#include "plugin/PluginProcessor.h"
#include <bitset>

namespace cq
{
    class EditorModel
    {
    public:
        explicit EditorModel (ClaudeQ4AudioProcessor& p);
        ~EditorModel();

        ClaudeQ4AudioProcessor& proc;
        std::function<bool()> alive;          // instance list: false once the processor is being destroyed
        bool isAlive() const                  { return ! alive || alive(); }

        // ---- reading
        BandParams band (int b) const;
        bool isUsed (int b) const             { return proc.table.bandFlag (b, bpUsed); }
        double value (int b, BandParam k) const { return (double) proc.table.bandValue (b, k); }
        double global (GlobalParam k) const   { return (double) proc.table.globalValue (k); }
        double gainScale() const              { return global (gpGainScale) / 100.0; }
        bool isStereo() const                 { return proc.isStereo(); }
        int usedCount() const;
        int freeSlot() const;

        // ---- editing
        void beginGesture (const juce::String& undoName);
        void endGesture();
        bool inGesture() const noexcept       { return depth > 0; }
        void set (int b, BandParam k, double plain);
        void setGlobal (GlobalParam k, double plain);
        void toggle (int b, BandParam k, const juce::String& undoName);
        void setOnce (int b, BandParam k, double plain, const juce::String& undoName)
        {
            beginGesture (undoName);
            set (b, k, plain);
            endGesture();
        }

        // ---- bands
        int createBand (double freq, double gainDb, Shape shape, Slope slope = Slope::dB12, double q = 1.0,
                        Placement place = Placement::Stereo);
        void deleteBand (int b);
        void deleteSelected();
        void copyBand (int from, int to);
        int duplicateBand (int b);
        void setShape (int b, Shape s);        // also picks a valid slope / sensible Q
        void invertGain (int b);
        int splitBand (int b, bool midSide);   // stereo band -> L/R or M/S pair (returns the new band)

        // ---- selection
        void select (int b, bool add);
        void toggleSelect (int b);
        void selectOnly (int b)               { select (b, false); }
        void selectAll();
        void clearSelection();
        void setSelection (std::bitset<kMaxBands> bits);
        std::bitset<kMaxBands> getSelectionBits() const { return selected; }
        bool isSelected (int b) const         { return b >= 0 && b < kMaxBands && selected.test ((size_t) b); }
        std::vector<int> selection() const;
        int getFocus() const                  { return focus; }
        void setFocus (int b);
        void validateSelection();
        std::function<void()> onSelectionChanged;

        // ---- MIDI learn
        void armLearn (juce::RangedAudioParameter* p);

        juce::RangedAudioParameter* param (int b, BandParam k) const { return proc.table.band (b, k); }

    private:
        void touch (juce::RangedAudioParameter* p);

        int depth = 0;
        juce::String gestureName;
        std::vector<float> before;
        std::vector<juce::RangedAudioParameter*> touched;
        std::bitset<kMaxBands> selected;
        int focus = -1;
    };
}
