/*
    Claude-Q 4 - editor model
*/
#include "EditorModel.h"

namespace cq
{
    EditorModel::EditorModel (ClaudeQ4AudioProcessor& p) : proc (p) {}

    EditorModel::~EditorModel()
    {
        while (depth > 0) endGesture();
    }

    BandParams EditorModel::band (int b) const
    {
        BandParams bp;
        proc.table.readBand (b, bp);
        return bp;
    }

    int EditorModel::usedCount() const
    {
        int n = 0;
        for (int b = 0; b < kMaxBands; ++b) if (isUsed (b)) ++n;
        return n;
    }

    int EditorModel::freeSlot() const
    {
        for (int b = 0; b < kMaxBands; ++b) if (! isUsed (b)) return b;
        return -1;
    }

    //==========================================================================
    void EditorModel::beginGesture (const juce::String& undoName)
    {
        if (depth++ == 0)
        {
            gestureName = undoName;
            before = proc.captureParameters();
            touched.clear();
        }
    }

    void EditorModel::endGesture()
    {
        if (depth <= 0) return;
        if (! isAlive()) { depth = 0; touched.clear(); before.clear(); return; }
        if (--depth == 0)
        {
            for (auto* p : touched) p->endChangeGesture();
            touched.clear();
            proc.pushUndoStep (std::move (before), gestureName);
            before.clear();
        }
    }

    void EditorModel::touch (juce::RangedAudioParameter* p)
    {
        if (std::find (touched.begin(), touched.end(), p) == touched.end())
        {
            touched.push_back (p);
            p->beginChangeGesture();
        }
        armLearn (p);
    }

    void EditorModel::set (int b, BandParam k, double plain)
    {
        auto* p = param (b, k);
        const bool own = depth == 0;
        if (own) beginGesture ("Edit band " + juce::String (b + 1));
        touch (p);
        const float norm = p->convertTo0to1 ((float) plain);
        if (std::abs (p->getValue() - norm) > 1.0e-7f) p->setValueNotifyingHost (norm);
        if (own) endGesture();
    }

    void EditorModel::setGlobal (GlobalParam k, double plain)
    {
        auto* p = proc.table.global (k);
        const bool own = depth == 0;
        if (own) beginGesture (p->getName (64));
        touch (p);
        const float norm = p->convertTo0to1 ((float) plain);
        if (std::abs (p->getValue() - norm) > 1.0e-7f) p->setValueNotifyingHost (norm);
        if (own) endGesture();
    }

    void EditorModel::toggle (int b, BandParam k, const juce::String& undoName)
    {
        beginGesture (undoName);
        set (b, k, proc.table.bandFlag (b, k) ? 0.0 : 1.0);
        endGesture();
    }

    //==========================================================================
    int EditorModel::createBand (double freq, double gainDb, Shape shape, Slope slope, double q, Placement place)
    {
        const int b = freeSlot();
        if (b < 0) return -1;
        beginGesture ("Create band");
        // reset everything to the defaults first (a slot may contain old settings)
        for (int k = 0; k < kNumBandParams; ++k)
        {
            auto* p = param (b, (BandParam) k);
            set (b, (BandParam) k, p->convertFrom0to1 (p->getDefaultValue()));
        }
        set (b, bpShape, (double) (int) shape);
        set (b, bpSlope, (double) (int) sanitiseSlope (shape, slope));
        set (b, bpFreq, clampT (freq, kMinFreq, kMaxFreq));
        set (b, bpGain, shapeUsesGain (shape) ? clampT (gainDb, kMinGain, kMaxGain) : 0.0);
        set (b, bpQ, clampT (q, kMinQ, kMaxQ));
        set (b, bpPlace, (double) (int) place);
        set (b, bpUsed, 1.0);
        endGesture();
        return b;
    }

    void EditorModel::deleteBand (int b)
    {
        if (b < 0 || b >= kMaxBands || ! isUsed (b)) return;
        beginGesture ("Delete band");
        set (b, bpUsed, 0.0);
        endGesture();
        if (proc.getSoloBand() == b) proc.setSolo (-1, false);
        selected.reset ((size_t) b);
        if (focus == b) focus = -1;
        validateSelection();
    }

    void EditorModel::deleteSelected()
    {
        auto sel = selection();
        if (sel.empty()) return;
        beginGesture (sel.size() > 1 ? "Delete bands" : "Delete band");
        for (int b : sel) set (b, bpUsed, 0.0);
        endGesture();
        if (proc.getSoloBand() >= 0 && ! isUsed (proc.getSoloBand())) proc.setSolo (-1, false);
        clearSelection();
    }

    void EditorModel::copyBand (int from, int to)
    {
        beginGesture ("Copy band");
        for (int k = 0; k < kNumBandParams; ++k)
            set (to, (BandParam) k, value (from, (BandParam) k));
        endGesture();
    }

    int EditorModel::duplicateBand (int b)
    {
        const int to = freeSlot();
        if (to < 0) return -1;
        beginGesture ("Duplicate band");
        copyBand (b, to);
        set (to, bpFreq, clampT (value (b, bpFreq) * std::pow (2.0, 1.0 / 3.0), kMinFreq, kMaxFreq));
        endGesture();
        return to;
    }

    void EditorModel::setShape (int b, Shape s)
    {
        beginGesture ("Change shape");
        const Shape old = band (b).shape;
        set (b, bpShape, (double) (int) s);
        const Slope cur = (Slope) proc.table.bandIndex (b, bpSlope);
        set (b, bpSlope, (double) (int) sanitiseSlope (s, cur));
        // sensible Q when switching between families
        const bool wasCut = old == Shape::LowCut || old == Shape::HighCut;
        const bool isCut = s == Shape::LowCut || s == Shape::HighCut;
        if (wasCut != isCut && (s == Shape::LowCut || s == Shape::HighCut || s == Shape::LowShelf || s == Shape::HighShelf))
            set (b, bpQ, isCut ? 0.71 : 1.0);
        if (! shapeSupportsDynamics (s) && value (b, bpDynOn) > 0.5) set (b, bpDynOn, 0.0);
        endGesture();
    }

    void EditorModel::invertGain (int b)
    {
        beginGesture ("Invert gain");
        set (b, bpGain, -value (b, bpGain));
        if (value (b, bpDynOn) > 0.5) set (b, bpDynRange, -value (b, bpDynRange));
        endGesture();
    }

    int EditorModel::splitBand (int b, bool midSide)
    {
        const int to = freeSlot();
        if (to < 0) return -1;
        beginGesture ("Split band");
        copyBand (b, to);
        set (b, bpPlace, (double) (int) (midSide ? Placement::Mid : Placement::Left));
        set (to, bpPlace, (double) (int) (midSide ? Placement::Side : Placement::Right));
        endGesture();
        return to;
    }

    //==========================================================================
    void EditorModel::select (int b, bool add)
    {
        if (! add) selected.reset();
        if (b >= 0 && b < kMaxBands)
        {
            selected.set ((size_t) b);
            focus = b;
        }
        if (onSelectionChanged) onSelectionChanged();
    }

    void EditorModel::toggleSelect (int b)
    {
        if (b < 0 || b >= kMaxBands) return;
        selected.flip ((size_t) b);
        if (selected.test ((size_t) b)) focus = b;
        else if (focus == b)
        {
            focus = -1;
            for (int k = 0; k < kMaxBands; ++k) if (selected.test ((size_t) k)) { focus = k; break; }
        }
        if (onSelectionChanged) onSelectionChanged();
    }

    void EditorModel::selectAll()
    {
        selected.reset();
        for (int b = 0; b < kMaxBands; ++b) if (isUsed (b)) selected.set ((size_t) b);
        if (focus < 0 || ! isUsed (focus))
        {
            focus = -1;
            for (int b = 0; b < kMaxBands; ++b) if (isUsed (b)) { focus = b; break; }
        }
        if (onSelectionChanged) onSelectionChanged();
    }

    void EditorModel::clearSelection()
    {
        const bool had = selected.any() || focus >= 0;
        selected.reset();
        focus = -1;
        if (had && onSelectionChanged) onSelectionChanged();
    }

    void EditorModel::setSelection (std::bitset<kMaxBands> bits)
    {
        for (int b = 0; b < kMaxBands; ++b) if (! isUsed (b)) bits.reset ((size_t) b);
        if (bits == selected) return;
        selected = bits;
        if (focus < 0 || ! selected.test ((size_t) focus))
        {
            focus = -1;
            for (int b = 0; b < kMaxBands; ++b) if (selected.test ((size_t) b)) { focus = b; break; }
        }
        if (onSelectionChanged) onSelectionChanged();
    }

    std::vector<int> EditorModel::selection() const
    {
        std::vector<int> v;
        for (int b = 0; b < kMaxBands; ++b) if (selected.test ((size_t) b) && isUsed (b)) v.push_back (b);
        return v;
    }

    void EditorModel::setFocus (int b)
    {
        if (b >= 0 && b < kMaxBands && isUsed (b))
        {
            focus = b;
            selected.set ((size_t) b);
        }
        if (onSelectionChanged) onSelectionChanged();
    }

    void EditorModel::validateSelection()
    {
        bool changed = false;
        for (int b = 0; b < kMaxBands; ++b)
            if (selected.test ((size_t) b) && ! isUsed (b)) { selected.reset ((size_t) b); changed = true; }
        if (focus >= 0 && (! isUsed (focus) || ! selected.test ((size_t) focus)))
        {
            focus = -1;
            for (int b = 0; b < kMaxBands; ++b) if (selected.test ((size_t) b)) { focus = b; break; }
            changed = true;
        }
        if (changed && onSelectionChanged) onSelectionChanged();
    }

    void EditorModel::armLearn (juce::RangedAudioParameter* p)
    {
        if (proc.midiLearn.isLearning() && p != nullptr)
            proc.midiLearn.arm (p->getParameterIndex());
    }
}
