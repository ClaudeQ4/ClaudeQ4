/*
    Claude-Q 4 - main EQ display

    Draws the analyzer (pre / post / side chain, tilt, collision detection, peak labels),
    every band's response (from the analog prototypes, i.e. exactly what the linear and
    natural phase engines realise), the live dynamic / spectral gain change areas, the
    composite curves (stereo or per channel L / R / M / S), the band nodes with their hover
    card, and the piano ruler.  Handles all mouse / keyboard editing.
*/
#pragma once

#include "EditorModel.h"
#include "Theme.h"
#include <bitset>

namespace cq
{
    class EQDisplay : public juce::Component
    {
    public:
        EQDisplay (EditorModel& model, bool compact);
        ~EQDisplay() override;

        // once per frame (the owner updates the analyzers first)
        void tick (double now);

        double getRange() const noexcept        { return rangeDb; }
        void setRange (double db);
        bool isPianoVisible() const noexcept    { return pianoVisible; }
        void setPianoVisible (bool b);
        bool isSketchMode() const noexcept      { return sketchMode; }
        void setSketchMode (bool b);

        std::function<void (const juce::String&)> onStatus;
        std::function<bool()> isAlive;          // instance list: the edited processor still exists
        std::function<void()> onSettingsChanged;
        std::function<void()> onSketchModeChanged;

        // geometry
        juce::Rectangle<float> getPlot() const noexcept { return plot; }
        float xForFreq (double f) const noexcept;
        double freqForX (float x) const noexcept;
        float yForDb (double db) const noexcept;
        double dbForY (float y) const noexcept;
        float yForAnalyzer (double db) const noexcept;
        juce::Point<float> nodePosition (int band) const;
        int getHoverBand() const noexcept       { return hoverBand; }
        bool isDraggingNode() const noexcept    { return drag == DragMode::Node; }

        // static composite response (stereo / main channel) at f, in dB
        double staticResponseDb (double f) const;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseEnter (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        bool keyPressed (const juce::KeyPress&) override;

        // menus (also used by the editor)
        void showBandMenu (int band);
        void showBackgroundMenu (double freq, double gainDb);
        static void addAnalyzerMenuItems (juce::PopupMenu& menu, ClaudeQ4AudioProcessor& proc, std::function<void()> changed);

    private:
        enum class DragMode { None, Node, Lasso, Sketch, Range, Piano, CardSolo, AltSolo, CardValue };
        enum Chan { chMain = 0, chLeft, chRight, chMid, chSide, kNumChans };

        struct BandCurve
        {
            BandParams params {};
            double scale = 1.0;
            bool valid = false, active = false, dynamic = false, spectral = false;
            std::vector<float> staticDb, rangeDb, liveDb;
            float liveGain = 0.0f;
            float nodeDb = 0.0f;
        };

        struct DragBand { int band; double freq, gain, q, range; };

        struct Card
        {
            juce::Rectangle<float> r, power, solo, close, shape, menu, freqText, gainText, qText;
        };

        void updateGrid();
        void updateCurves();
        void computeCurve (const BandParams& bp, double scale, double dynDb, float* out, float* nodeDbOut) const;
        void updateCollisions (double now);
        void updatePeaks (double now);
        float nodeDisplayDb (int b) const;
        int hitNode (juce::Point<float> p) const;
        int hitRangeHandle (juce::Point<float> p) const;
        juce::Point<float> rangeHandlePos (int b) const;
        int hitPeak (juce::Point<float> p) const;
        juce::Rectangle<float> peakLabelRect (int idx) const;
        bool nearComposite (juce::Point<float> p, juce::Point<float>& onCurve) const;
        void layoutCard (int b);
        void updateHover();
        double gainDisplayFactor (const BandParams& bp) const;
        double snapFreq (double f) const;

        void startNodeDrag (int b, const juce::MouseEvent& e);
        void dragNodes (const juce::MouseEvent& e);
        void finishSketch();
        void createBandAt (double freq, double gainDb, bool startDrag, const juce::MouseEvent* e);
        void endWheelGesture();
        void status (const juce::String& s) { if (onStatus) onStatus (s); }

        // painting
        void drawGrid (juce::Graphics&);
        void drawAnalyzer (juce::Graphics&);
        void drawBands (juce::Graphics&);
        void drawComposite (juce::Graphics&);
        void drawNodes (juce::Graphics&);
        void drawPeaks (juce::Graphics&);
        void drawPiano (juce::Graphics&);
        void drawLabels (juce::Graphics&);
        void drawCard (juce::Graphics&);
        void drawOverlays (juce::Graphics&);
        juce::Path curvePath (const float* db, float yClampTop, float yClampBottom) const;
        juce::Path areaPath (const float* top, const float* base, float yMin, float yMax) const;

        EditorModel& model;
        ClaudeQ4AudioProcessor& proc;
        const bool compact;

        juce::Rectangle<float> plot, piano, rightCol, rangeButton;
        float uiScale = 1.0f;
        double rangeDb = 12.0;
        bool pianoVisible = false, sketchMode = false;

        int numPts = 0;
        std::vector<double> ptFreq;
        std::vector<float> ptX;
        std::array<BandCurve, kMaxBands> curves;
        std::array<std::vector<float>, kNumChans> composite;
        std::array<bool, kNumChans> chanShown {};

        std::array<float, SpectrumAnalyzer::kPoints> tiltDb {};
        float tiltFor = -1000.0f;
        std::array<float, SpectrumAnalyzer::kPoints> collision {};
        bool collisionActive = false;
        std::vector<int> peaks;
        double lastPeakTime = 0.0;
        AnalyzerSettings ana;

        DragMode drag = DragMode::None;
        int hoverBand = -1, rangeHover = -1, hoverPeak = -1, hoverKey = -1, cardBand = -1;
        juce::Point<float> mouse, dragStart;
        bool mouseInside = false;
        std::vector<DragBand> dragSet;
        int dragPrimary = -1, axisLock = 0;
        bool dragQ = false, dragMoved = false, altClick = false;
        juce::Rectangle<float> lasso;
        std::bitset<kMaxBands> lassoBase;
        std::vector<juce::Point<float>> sketch;
        bool ghostVisible = false;
        juce::Point<float> ghostPos;
        Card card;
        bool cardVisible = false, mouseOverCard = false;
        double lastHoverTime = 0.0, now = 0.0;
        bool wheelGesture = false;
        BandParam cardParam = bpFreq;
        double cardStart = 0.0;
        void copyCurve();
        void pasteCurve();
        int cardValueAt (juce::Point<float> p, BandParam& which) const;
        double wheelGestureEnd = 0.0;
        bool needsRepaint = true;
    };
}
