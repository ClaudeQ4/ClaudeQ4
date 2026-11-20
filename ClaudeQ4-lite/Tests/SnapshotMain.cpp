/*
    Claude-Q 4 - GUI / integration test harness

    Drives the real processor and editor: synthetic program material, scripted mouse and
    keyboard interaction, state / undo / MIDI learn / latency checks, PNG snapshots of the
    interface, a paint benchmark and (optionally) loading the built VST3 through JUCE's host.

    usage: ClaudeQ4Tests <outputDir> [path/to/Claude-Q 4.vst3]
*/
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "plugin/PluginProcessor.h"
#include "gui/PluginEditor.h"
#include <random>
#include <cstdio>

using namespace cq;

static int failures = 0, passes = 0;
static void check (bool ok, const juce::String& what, double a = 0.0, double b = 0.0)
{
    (ok ? passes : failures)++;
    std::printf ("%s  %s  (%.5g, %.5g)\n", ok ? "PASS" : "FAIL", what.toRawUTF8(), a, b);
    std::fflush (stdout);
}

//==============================================================================
struct SignalGen
{
    explicit SignalGen (double sampleRate, unsigned seed = 1) : fs (sampleRate), rng (seed) {}
    double fs;
    std::mt19937 rng;
    std::normal_distribution<float> nd { 0.0f, 1.0f };
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    double t = 0.0;
    double ph[32] {};
    float burstGain = 0.0f;
    double fundamental = 82.41;     // E2
    double burstFreq = 700.0;
    float noiseLevel = 0.06f, toneLevel = 0.25f, burstLevel = 0.0f, hissLevel = 0.012f;

    float pink()
    {
        const float white = nd (rng);
        b0 = 0.99886f * b0 + white * 0.0555179f; b1 = 0.99332f * b1 + white * 0.0750759f;
        b2 = 0.96900f * b2 + white * 0.1538520f; b3 = 0.86650f * b3 + white * 0.3104856f;
        b4 = 0.55000f * b4 + white * 0.5329522f; b5 = -0.7616f * b5 - white * 0.0168980f;
        const float out = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f;
        b6 = white * 0.115926f;
        return out * 0.11f;
    }

    void render (float* L, float* R, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const double vib = 1.0 + 0.003 * std::sin (kTwoPi * 5.0 * t);
            float tone = 0.0f;
            for (int h = 1; h <= 24; ++h)
            {
                ph[h] += kTwoPi * fundamental * h * vib / fs;
                if (ph[h] > kTwoPi) ph[h] -= kTwoPi;
                const double amp = (h % 2 == 0 ? 0.6 : 1.0) / (double) h * std::exp (-0.04 * h);
                tone += (float) (amp * std::sin (ph[h]));
            }
            ph[0] += kTwoPi * burstFreq / fs;
            if (ph[0] > kTwoPi) ph[0] -= kTwoPi;
            // repeating loud bursts (drive the dynamic bands)
            const double cyc = std::fmod (t, 0.5);
            const float env = (float) (cyc < 0.18 ? std::min (1.0, cyc / 0.004) : std::exp (-(cyc - 0.18) / 0.05));
            const float burst = burstLevel * env * (float) std::sin (ph[0]);
            const float hiss = hissLevel * nd (rng) * (float) (0.6 + 0.4 * std::sin (kTwoPi * 0.7 * t));
            const float common = noiseLevel * pink() + toneLevel * tone * 0.25f + burst;
            L[i] = common + hiss + 0.01f * nd (rng);
            if (R != nullptr) R[i] = common * 0.92f + 0.6f * hiss + 0.01f * nd (rng);
            t += 1.0 / fs;
        }
    }
};

//==============================================================================
static void setPlain (ClaudeQ4AudioProcessor& p, int band, BandParam k, double v)
{
    auto* par = p.table.band (band, k);
    par->setValueNotifyingHost (par->convertTo0to1 ((float) v));
}

static void setGlobalPlain (ClaudeQ4AudioProcessor& p, GlobalParam k, double v)
{
    auto* par = p.table.global (k);
    par->setValueNotifyingHost (par->convertTo0to1 ((float) v));
}

static void addBand (ClaudeQ4AudioProcessor& p, int b, Shape s, double f, double g, double q, Slope sl = Slope::dB12,
                     Placement pl = Placement::Stereo, double dyn = 0.0, bool spectral = false)
{
    setPlain (p, b, bpShape, (int) s);
    setPlain (p, b, bpSlope, (int) sl);
    setPlain (p, b, bpFreq, f);
    setPlain (p, b, bpGain, g);
    setPlain (p, b, bpQ, q);
    setPlain (p, b, bpPlace, (int) pl);
    setPlain (p, b, bpDynOn, dyn != 0.0 ? 1.0 : 0.0);
    setPlain (p, b, bpDynRange, dyn);
    setPlain (p, b, bpSpectral, spectral ? 1.0 : 0.0);
    setPlain (p, b, bpUsed, 1.0);
}

static void clearBands (ClaudeQ4AudioProcessor& p)
{
    for (int b = 0; b < kMaxBands; ++b) setPlain (p, b, bpUsed, 0.0);
}

struct Rig
{
    explicit Rig (double fs = 48000.0, int block = 512, bool sidechain = false, unsigned seed = 1)
        : sampleRate (fs), blockSize (block), gen (fs, seed), scGen (fs, seed + 99)
    {
        proc = std::make_unique<ClaudeQ4AudioProcessor>();
        if (sidechain)
        {
            auto layout = proc->getBusesLayout();
            layout.inputBuses.getReference (1) = juce::AudioChannelSet::stereo();
            proc->setBusesLayout (layout);
        }
        proc->setRateAndBufferSizeDetails (fs, block);
        proc->prepareToPlay (fs, block);
        hasSc = sidechain;
        scGen.fundamental = 55.0;
        scGen.burstLevel = 0.0f;
    }

    void process (int samples)
    {
        const int nIn = proc->getTotalNumInputChannels(), nOut = proc->getTotalNumOutputChannels();
        juce::AudioBuffer<float> buf (std::max (nIn, nOut), blockSize);
        juce::MidiBuffer midi;
        for (int pos = 0; pos < samples; pos += blockSize)
        {
            const int n = std::min (blockSize, samples - pos);
            buf.setSize (std::max (nIn, nOut), n, false, false, true);
            buf.clear();
            gen.render (buf.getWritePointer (0), buf.getWritePointer (1), n);
            if (hasSc && buf.getNumChannels() >= 4) scGen.render (buf.getWritePointer (2), buf.getWritePointer (3), n);
            proc->processBlock (buf, midi);
        }
    }

    double sampleRate;
    int blockSize;
    SignalGen gen, scGen;
    bool hasSc = false;
    std::unique_ptr<ClaudeQ4AudioProcessor> proc;
};

static double now = 1000.0;

// runs audio and GUI frames in (simulated) real time
static void run (Rig& rig, ClaudeQ4Editor* ed, double seconds, std::vector<Rig*> others = {})
{
    const int frames = (int) std::ceil (seconds * 60.0);
    const int perFrame = (int) (rig.sampleRate / 60.0);
    for (int f = 0; f < frames; ++f)
    {
        rig.process (perFrame);
        for (auto* o : others) o->process (perFrame);
        now += 1.0 / 60.0;
        if (ed != nullptr) ed->frame (now);
        if (f % 6 == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
    }
}

static void snapshot (juce::Component& c, const juce::File& dir, const juce::String& name, float scale = 1.0f)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, scale);
    auto f = dir.getChildFile (name + ".png");
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat png;
    const bool ok = os.openedOk() && png.writeImageToStream (img, os);
    check (ok, "snapshot " + name + ".png");
}

static juce::MouseEvent mouseEvent (juce::Component& c, juce::Point<float> pos, juce::ModifierKeys mods,
                                     int clicks = 1, bool dragged = false, juce::Point<float> downPos = {})
{
    auto src = juce::Desktop::getInstance().getMainMouseSource();
    const auto t = juce::Time::getCurrentTime();
    return juce::MouseEvent (src, pos, mods, juce::MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f, 0.0f,
                             &c, &c, t, downPos.isOrigin() ? pos : downPos, t, clicks, dragged);
}

static void click (juce::Component& c, juce::Point<float> p, juce::ModifierKeys mods = {}, int clicks = 1)
{
    const auto down = mods.withFlags (juce::ModifierKeys::leftButtonModifier);
    c.mouseMove (mouseEvent (c, p, mods));
    c.mouseDown (mouseEvent (c, p, down, clicks));
    c.mouseUp (mouseEvent (c, p, mods, clicks));
}

static void doubleClick (juce::Component& c, juce::Point<float> p, juce::ModifierKeys mods = {})
{
    const auto down = mods.withFlags (juce::ModifierKeys::leftButtonModifier);
    c.mouseMove (mouseEvent (c, p, mods));
    c.mouseDown (mouseEvent (c, p, down, 1));
    c.mouseUp (mouseEvent (c, p, mods, 1));
    c.mouseDown (mouseEvent (c, p, down, 2));
    c.mouseDoubleClick (mouseEvent (c, p, down, 2));
    c.mouseUp (mouseEvent (c, p, mods, 2));
}

static void dragFromTo (juce::Component& c, juce::Point<float> a, juce::Point<float> b, juce::ModifierKeys mods = {}, int steps = 12)
{
    const auto down = mods.withFlags (juce::ModifierKeys::leftButtonModifier);
    c.mouseMove (mouseEvent (c, a, mods));
    c.mouseDown (mouseEvent (c, a, down));
    for (int i = 1; i <= steps; ++i)
    {
        const auto p = a + (b - a) * ((float) i / (float) steps);
        c.mouseDrag (mouseEvent (c, p, down, 1, true, a));
    }
    c.mouseUp (mouseEvent (c, b, mods, 1, true, a));
}

static int usedBands (ClaudeQ4AudioProcessor& p)
{
    int n = 0;
    for (int b = 0; b < kMaxBands; ++b) if (p.table.bandFlag (b, bpUsed)) ++n;
    return n;
}

//==============================================================================
static void buildShowcase (ClaudeQ4AudioProcessor& p)
{
    clearBands (p);
    addBand (p, 0, Shape::LowCut, 28.0, 0.0, 0.71, Slope::dB48);
    addBand (p, 1, Shape::Bell, 72.0, 5.0, 2.2);
    addBand (p, 2, Shape::Notch, 172.0, 0.0, 6.0, Slope::dB12);
    addBand (p, 3, Shape::Bell, 700.0, 9.5, 0.75, Slope::dB12, Placement::Stereo, -6.0);
    addBand (p, 4, Shape::Bell, 2300.0, 0.0, 0.9, Slope::dB12, Placement::Stereo, -9.0, true);
    addBand (p, 5, Shape::HighShelf, 7000.0, 0.5, 0.71, Slope::dB12, Placement::Stereo, 5.0);
    setPlain (p, 3, bpAutoThr, 0.0);
    setPlain (p, 3, bpThreshold, -24.0);
    setPlain (p, 5, bpAutoThr, 0.0);
    setPlain (p, 5, bpThreshold, -60.0);
    setPlain (p, 4, bpDensity, 70.0);
}

//==============================================================================
static void guiTests (const juce::File& outDir)
{
    std::printf ("\n== GUI: showcase snapshots ==\n");
    Rig rig (48000.0, 512, true, 7);
    auto& p = *rig.proc;
    rig.gen.burstLevel = 0.35f;
    rig.gen.burstFreq = 700.0;
    buildShowcase (p);
    {
        auto as = p.getAnalyzerSettings();
        as.pre = true; as.post = true; as.sc = false;
        p.setAnalyzerSettings (as);
    }
    std::unique_ptr<ClaudeQ4Editor> ed (dynamic_cast<ClaudeQ4Editor*> (p.createEditor()));
    check (ed != nullptr, "editor created");
    ed->setSize (1000, 620);
    run (rig, ed.get(), 2.6);
    auto& disp = ed->getDisplay();
    auto& model = ed->getModel();

    snapshot (*ed, outDir, "01_showcase");

    // select the dynamic bell (band 4 = index 3) and hover it -> panel + hover card
    model.selectOnly (3);
    const auto node3 = disp.nodePosition (3);
    disp.mouseEnter (mouseEvent (disp, node3, {}));
    disp.mouseMove (mouseEvent (disp, node3, {}));
    run (rig, ed.get(), 0.3);
    snapshot (*ed, outDir, "02_selected_card");
    check (ed->getPanel().isVisible(), "band panel visible for the selected band");

    // live dynamic gain is moving
    {
        float mn = 1e9f, mx = -1e9f;
        for (int i = 0; i < 60; ++i)
        {
            run (rig, ed.get(), 1.0 / 60.0);
            const float g = p.engine.getDynamicGainDb (3);
            mn = std::min (mn, g); mx = std::max (mx, g);
        }
        check (mx - mn > 1.0 && mn < -1.0, "dynamic band gain moves with the program (dB min / max)", mn, mx);
    }

    // expanded dynamics section on the spectral band
    model.selectOnly (4);
    ed->getPanel().setExpanded (true);
    run (rig, ed.get(), 0.6);
    disp.mouseExit (mouseEvent (disp, { 5.0f, 5.0f }, {}));
    run (rig, ed.get(), 0.8);
    snapshot (*ed, outDir, "03_spectral_expanded");
    bool anySpec = false;
    for (int j = 0; j < SpectralEngine::kDisplayPoints; ++j) if (std::abs (p.engine.getSpectralGainDb (4, j)) > 0.3f) anySpec = true;
    check (p.engine.isSpectralActive (4) && anySpec, "spectral band active with live per-frequency gain");
    ed->getPanel().setExpanded (false);

    // piano ruler + hover note
    disp.setPianoVisible (true);
    ed->resized();
    model.selectOnly (1);
    run (rig, ed.get(), 0.2);
    const auto pianoPt = juce::Point<float> (disp.xForFreq (261.63), disp.getPlot().getBottom() + 8.0f);
    disp.mouseEnter (mouseEvent (disp, pianoPt, {}));
    disp.mouseMove (mouseEvent (disp, pianoPt, {}));
    run (rig, ed.get(), 0.2);
    snapshot (*ed, outDir, "04_piano_ruler");

    // spectrum grab peak labels (mouse over empty display area)
    model.clearSelection();
    disp.setPianoVisible (false);
    ed->resized();
    const auto emptyPt = juce::Point<float> (disp.xForFreq (4000.0), disp.getPlot().getY() + 30.0f);
    disp.mouseMove (mouseEvent (disp, emptyPt, {}));
    run (rig, ed.get(), 0.6);
    disp.mouseMove (mouseEvent (disp, emptyPt, {}));
    run (rig, ed.get(), 0.3);
    snapshot (*ed, outDir, "05_spectrum_grab");

    // help, sizes
    ed->openHelp();
    run (rig, ed.get(), 0.1);
    snapshot (*ed, outDir, "06_help");
    ed->closeOverlays();
    ed->setSize (1500, 930);
    run (rig, ed.get(), 0.4);
    snapshot (*ed, outDir, "07_large_1500x930");
    ed->setSize (800, 496);
    model.selectOnly (5);
    run (rig, ed.get(), 0.4);
    snapshot (*ed, outDir, "08_small_800x496");
    ed->setSize (1000, 620);

    // paint benchmark (software renderer)
    {
        model.selectOnly (3);
        run (rig, ed.get(), 0.2);
        juce::Image img (juce::Image::ARGB, 1000, 620, true);
        const int N = 60;
        double worst = 0.0, total = 0.0;
        for (int i = 0; i < N; ++i)
        {
            rig.process (800);
            now += 1.0 / 60.0;
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            ed->frame (now);
            juce::Graphics g (img);
            ed->paintEntireComponent (g, true);
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            total += ms; worst = std::max (worst, ms);
        }
        std::printf ("BENCH GUI frame (tick + full repaint 1000x620, software renderer): avg %.2f ms, worst %.2f ms\n", total / N, worst);
        check (total / N < 16.6, "full GUI frame under 16.6 ms (60 fps) on the software renderer", total / N, worst);
    }
}

//==============================================================================
static void interactionTests (const juce::File& outDir)
{
    std::printf ("\n== GUI: interaction ==\n");
    Rig rig (48000.0, 512, false, 3);
    auto& p = *rig.proc;
    clearBands (p);
    std::unique_ptr<ClaudeQ4Editor> ed (dynamic_cast<ClaudeQ4Editor*> (p.createEditor()));
    ed->setSize (1000, 620);
    run (rig, ed.get(), 0.5);
    auto& d = ed->getDisplay();
    auto& model = ed->getModel();

    // double-click creates a bell at the mouse position
    const auto pt = juce::Point<float> (d.xForFreq (1000.0), d.yForDb (6.0));
    doubleClick (d, pt);
    run (rig, ed.get(), 0.05);
    int b = model.getFocus();
    check (usedBands (p) == 1 && b >= 0, "double-click creates one band", usedBands (p), b);
    if (b < 0) return;
    check (std::abs (model.value (b, bpFreq) / 1000.0 - 1.0) < 0.02 && std::abs (model.value (b, bpGain) - 6.0) < 0.3
           && model.band (b).shape == Shape::Bell, "new band: bell at 1 kHz / +6 dB", model.value (b, bpFreq), model.value (b, bpGain));

    // double-click near the low edge creates a low cut
    doubleClick (d, { d.xForFreq (16.0), d.yForDb (0.0) });
    run (rig, ed.get(), 0.05);
    const int lc = model.getFocus();
    check (lc >= 0 && lc != b && model.band (lc).shape == Shape::LowCut, "double-click near 10-28 Hz creates a low cut");

    // drag the bell one octave up and 3 dB down
    model.selectOnly (b);
    run (rig, ed.get(), 0.05);
    auto from = d.nodePosition (b);
    auto to = juce::Point<float> (d.xForFreq (2000.0), d.yForDb (3.0));
    dragFromTo (d, from, to);
    run (rig, ed.get(), 0.05);
    check (std::abs (model.value (b, bpFreq) / 2000.0 - 1.0) < 0.02 && std::abs (model.value (b, bpGain) - 3.0) < 0.3,
           "drag node: frequency and gain follow the mouse", model.value (b, bpFreq), model.value (b, bpGain));

    // undo / redo of the drag
    p.undoManager.undo();
    run (rig, ed.get(), 0.05);
    check (std::abs (model.value (b, bpFreq) / 1000.0 - 1.0) < 0.02, "undo restores the frequency", model.value (b, bpFreq));
    p.undoManager.redo();
    run (rig, ed.get(), 0.05);
    check (std::abs (model.value (b, bpFreq) / 2000.0 - 1.0) < 0.02, "redo re-applies it", model.value (b, bpFreq));

    // Ctrl/Cmd drag = Q
    const double q0 = model.value (b, bpQ);
    from = d.nodePosition (b);
    dragFromTo (d, from, from.translated (0.0f, -60.0f), juce::ModifierKeys (juce::ModifierKeys::commandModifier));
    run (rig, ed.get(), 0.05);
    check (model.value (b, bpQ) > q0 * 1.8 && std::abs (model.value (b, bpGain) - 3.0) < 0.05, "Cmd/Ctrl drag up raises Q only", q0, model.value (b, bpQ));

    // piano ruler: dragging snaps to note frequencies
    d.setPianoVisible (true);
    ed->resized();
    run (rig, ed.get(), 0.05);
    from = d.nodePosition (b);
    dragFromTo (d, from, { d.xForFreq (265.0), from.y });
    run (rig, ed.get(), 0.05);
    const double nf = model.value (b, bpFreq);
    const double cents = 100.0 * (freqToNote (nf) - std::round (freqToNote (nf)));
    check (std::abs (cents) < 0.5 && std::abs (nf - 261.63) < 0.3, "piano snapping: band lands on C4 = 261.63 Hz", nf, cents);
    // click a key with the band selected -> tune to that note (A4)
    click (d, { d.xForFreq (440.0), d.getPlot().getBottom() + 10.0f });
    run (rig, ed.get(), 0.05);
    check (std::abs (model.value (b, bpFreq) - 440.0) < 0.2, "click on piano key A4 tunes the band to 440 Hz", model.value (b, bpFreq));
    d.setPianoVisible (false);
    ed->resized();

    // wheel = Q, Alt+wheel = dynamic range
    {
        const auto np = d.nodePosition (b);
        juce::MouseWheelDetails w {};
        w.deltaY = 0.5f;
        const double qa = model.value (b, bpQ);
        d.mouseWheelMove (mouseEvent (d, np, {}), w);

        d.mouseWheelMove (mouseEvent (d, np, juce::ModifierKeys (juce::ModifierKeys::altModifier)), w);
        run (rig, ed.get(), 0.8);
        check (model.value (b, bpQ) > qa * 1.3, "mouse wheel changes Q", qa, model.value (b, bpQ));
        check (model.value (b, bpDynOn) > 0.5 && model.value (b, bpDynRange) > 2.0, "Alt + wheel creates a dynamic range", model.value (b, bpDynRange));
    }

    // hover card values can be dragged (frequency up one octave = 120 px)
    {
        model.selectOnly (b);
        const auto np = d.nodePosition (b);
        d.mouseEnter (mouseEvent (d, np, {}));
        d.mouseMove (mouseEvent (d, np, {}));
        run (rig, ed.get(), 0.05);
        d.mouseMove (mouseEvent (d, np, {}));
        // the card sits above the node: its first row holds the frequency
        const double f0 = model.value (b, bpFreq);
        const auto cardFreq = juce::Point<float> (np.x, np.y - 18.0f - 60.0f + 10.0f);
        dragFromTo (d, cardFreq, cardFreq.translated (0.0f, -120.0f));
        run (rig, ed.get(), 0.05);
        check (std::abs (model.value (b, bpFreq) / (f0 * 2.0) - 1.0) < 0.02, "drag the frequency in the hover card (one octave)", f0, model.value (b, bpFreq));
    }

    // ghost node on the curve: click creates a band there
    {
        const int before = usedBands (p);
        const float x = d.xForFreq (5000.0);
        const auto pt2 = juce::Point<float> (x, d.yForDb (0.0) - 1.0f);
        d.mouseMove (mouseEvent (d, pt2, {}));
        run (rig, ed.get(), 0.05);
        d.mouseMove (mouseEvent (d, pt2, {}));
        click (d, pt2);
        run (rig, ed.get(), 0.05);
        check (usedBands (p) == before + 1 && std::abs (model.value (model.getFocus(), bpFreq) / 5000.0 - 1.0) < 0.03, "clicking the curve (+) adds a band", usedBands (p));
    }

    // lasso selection and multi-band drag
    {
        model.clearSelection();
        const auto r = d.getPlot();
        dragFromTo (d, { r.getX() + 5.0f, r.getY() + 5.0f }, { r.getRight() - 5.0f, r.getBottom() - 5.0f });
        run (rig, ed.get(), 0.05);
        check ((int) model.selection().size() == usedBands (p), "lasso selects every band", (double) model.selection().size(), usedBands (p));
        std::vector<double> fBefore;
        for (int s : model.selection()) fBefore.push_back (model.value (s, bpFreq));
        const int anchor = model.selection().front();
        const auto a = d.nodePosition (anchor);
        dragFromTo (d, a, a.translated (d.getPlot().getWidth() / (float) std::log2 (kMaxFreq / kMinFreq), 0.0f));
        run (rig, ed.get(), 0.05);
        bool allMoved = true;
        size_t k = 0;
        for (int s : model.selection())
        {
            const double expect = clampT (fBefore[k++] * 2.0, kMinFreq, kMaxFreq);
            if (std::abs (model.value (s, bpFreq) / expect - 1.0) > 0.03) allMoved = false;
        }
        check (allMoved, "dragging moves all selected bands together (one octave)");
    }

    // alt-click toggles enable, double-click deletes, Delete key removes selection
    {
        model.selectOnly (b);
        run (rig, ed.get(), 0.05);
        click (d, d.nodePosition (b), juce::ModifierKeys (juce::ModifierKeys::altModifier));
        run (rig, ed.get(), 0.05);
        check (model.value (b, bpEnabled) < 0.5, "Alt+click disables the band");
        click (d, d.nodePosition (b), juce::ModifierKeys (juce::ModifierKeys::altModifier));
        run (rig, ed.get(), 0.05);
        check (model.value (b, bpEnabled) > 0.5, "Alt+click enables it again");
        const int n0 = usedBands (p);
        doubleClick (d, d.nodePosition (b));
        run (rig, ed.get(), 0.05);
        check (usedBands (p) == n0 - 1 && ! model.isUsed (b), "double-click on a dot deletes the band");
        model.selectAll();
        d.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
        run (rig, ed.get(), 0.05);
        check (usedBands (p) == 0, "Delete key removes the selected bands");
        p.undoManager.undo();
        run (rig, ed.get(), 0.05);
        check (usedBands (p) == n0 - 1, "undo brings them back", usedBands (p));
    }

    // band panel knobs edit the band (ring = dynamic range)
    {
        clearBands (p);
        run (rig, ed.get(), 0.05);
        const int nb = model.createBand (500.0, 0.0, Shape::Bell);
        model.selectOnly (nb);
        run (rig, ed.get(), 0.1);
        auto& panel = ed->getPanel();
        check (panel.isVisible(), "panel shows for the new band");
        // find the GAIN knob (second Knob child named GAIN)
        Knob* gain = nullptr;
        for (auto* c : panel.getChildren()) if (auto* k = dynamic_cast<Knob*> (c)) if (k->getText && k->isActive && k->hasRing) gain = k;
        check (gain != nullptr, "gain knob found");
        if (gain != nullptr)
        {
            const auto centre = gain->getLocalBounds().toFloat().withTrimmedBottom (gain->labelHeight).getCentre();
            dragFromTo (*gain, centre, centre.translated (0.0f, -22.0f));
            run (rig, ed.get(), 0.05);
            check (std::abs (model.value (nb, bpGain) - 6.0) < 0.6, "gain knob: 22 px up = +6 dB", model.value (nb, bpGain));
            // outer ring: drag down -> compression range
            const auto ka = gain->getLocalBounds().toFloat().withTrimmedBottom (gain->labelHeight);
            const float R = std::min (ka.getWidth(), ka.getHeight()) * 0.5f;
            const auto ringPt = ka.getCentre().translated (0.0f, -R * 0.97f);
            dragFromTo (*gain, ringPt, ringPt.translated (0.0f, 30.0f));
            run (rig, ed.get(), 0.05);
            check (model.value (nb, bpDynOn) > 0.5 && model.value (nb, bpDynRange) < -5.0, "gain ring sets a compression range", model.value (nb, bpDynRange));
        }
        snapshot (*ed, outDir, "09_interaction_result");
    }
}

//==============================================================================
static void processorTests()
{
    std::printf ("\n== processor ==\n");
    // latency reporting per mode
    {
        Rig rig (48000.0, 512);
        auto& p = *rig.proc;
        clearBands (p);
        addBand (p, 0, Shape::Bell, 1000.0, 6.0, 1.0);
        rig.process (4800);
        check (p.getLatencySamples() == 0, "zero latency mode reports 0 samples", p.getLatencySamples());
        setGlobalPlain (p, gpMode, 2.0);
        for (int i = 0; i < 100 && p.getLatencySamples() != 5120; ++i)
        {
            rig.process (2048);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        }
        check (p.getLatencySamples() == 5120, "linear phase (medium, 48 kHz) reports 5120 samples", p.getLatencySamples());
        setGlobalPlain (p, gpMode, 1.0);
        for (int i = 0; i < 100 && p.getLatencySamples() == 5120; ++i)
        {
            rig.process (2048);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        }
        check (p.getLatencySamples() > 0 && p.getLatencySamples() < 300, "natural phase reports a short latency", p.getLatencySamples());
    }
    // state round trip (parameters + UI + MIDI + A/B)
    {
        Rig a (48000.0, 512);
        buildShowcase (*a.proc);
        setGlobalPlain (*a.proc, gpMode, 1.0);
        setGlobalPlain (*a.proc, gpOutGain, -2.5);
        a.proc->setUiProperty ("displayRange", 6.0);
        a.proc->midiLearn.setLearning (true);
        a.proc->midiLearn.arm (a.proc->table.band (3, bpGain)->getParameterIndex());
        juce::MidiBuffer mb;
        mb.addEvent (juce::MidiMessage::controllerEvent (1, 74, 100), 0);
        juce::AudioBuffer<float> buf (2, 256);
        buf.clear();
        a.proc->processBlock (buf, mb);
        a.proc->midiLearn.setLearning (false);
        check (a.proc->midiLearn.describe (a.proc->table.band (3, bpGain)->getParameterIndex()).contains ("CC 74"), "MIDI learn binds CC 74 to band 4 gain");
        // a CC value then moves the parameter (applied on the message thread)
        juce::MidiBuffer mb2;
        mb2.addEvent (juce::MidiMessage::controllerEvent (1, 74, 127), 0);
        a.proc->processBlock (buf, mb2);
        a.proc->midiLearn.dispatch (*a.proc);
        check (std::abs (a.proc->table.bandValue (3, bpGain) - 30.0f) < 0.01f, "CC 74 = 127 sets the gain to +30 dB", a.proc->table.bandValue (3, bpGain));

        juce::MemoryBlock state;
        a.proc->getStateInformation (state);
        Rig b (48000.0, 512);
        b.proc->setStateInformation (state.getData(), (int) state.getSize());
        bool same = true;
        const auto& pa = a.proc->getParameters();
        const auto& pb = b.proc->getParameters();
        for (int i = 0; i < pa.size(); ++i) if (std::abs (pa[i]->getValue() - pb[i]->getValue()) > 1.0e-6f) same = false;
        check (same && pa.size() == pb.size(), "state round trip restores all " + juce::String (pa.size()) + " parameters");
        check ((double) b.proc->getUiProperty ("displayRange", 0.0) == 6.0, "state round trip restores the UI settings");
        check (b.proc->midiLearn.describe (b.proc->table.band (3, bpGain)->getParameterIndex()).contains ("CC 74"), "state round trip restores MIDI learn");
    }
    // A/B
    {
        Rig r (48000.0, 512);
        auto& p = *r.proc;
        buildShowcase (p);
        const float gA = p.table.bandValue (3, bpGain);
        p.toggleAB();                       // to B (copy of A)
        setPlain (p, 3, bpGain, -4.0);
        p.toggleAB();                       // back to A
        check (std::abs (p.table.bandValue (3, bpGain) - gA) < 1e-4f, "A/B: back to A restores A", p.table.bandValue (3, bpGain), gA);
        p.toggleAB();
        check (std::abs (p.table.bandValue (3, bpGain) + 4.0f) < 1e-4f, "A/B: B kept its own edit", p.table.bandValue (3, bpGain));
    }
    // factory presets all load and process cleanly in every mode
    {
        Rig r (48000.0, 512);
        auto& p = *r.proc;
        const int n = (int) PresetManager::factory().size();
        bool allFinite = true;
        for (int i = 0; i < n; ++i)
        {
            p.presets.loadFactory (i);
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            for (int k = 0; k < 40; ++k)
            {
                r.gen.render (buf.getWritePointer (0), buf.getWritePointer (1), 512);
                p.processBlock (buf, midi);
                for (int c = 0; c < 2; ++c)
                    for (int s = 0; s < 512; ++s)
                        if (! std::isfinite (buf.getSample (c, s)) || std::abs (buf.getSample (c, s)) > 8.0f) allFinite = false;
            }
            juce::MessageManager::getInstance()->runDispatchLoopUntil (2);
        }
        check (allFinite, "all " + juce::String (n) + " factory presets process cleanly");
        check (p.getNumPrograms() == 1, "presets are not exposed as host programs (safe session recall)", p.getNumPrograms());
    }
    // user preset save / load
    {
        Rig r (48000.0, 512);
        auto& p = *r.proc;
        buildShowcase (p);
        auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("cq4test.cq4preset");
        check (p.presets.saveFile (f), "save user preset");
        clearBands (p);
        check (p.presets.loadFile (f) && usedBands (p) == 6 && std::abs (p.table.bandValue (3, bpGain) - 9.5f) < 1e-3f, "load user preset", usedBands (p));
        f.deleteFile();
    }
    // full processor (parameter snapshot + engine) with 24 dynamic bands: CPU per block
    {
        Rig r (48000.0, 512);
        auto& p = *r.proc;
        for (int b = 0; b < kMaxBands; ++b)
        {
            addBand (p, b, Shape::Bell, 40.0 * std::pow (1.3, b), b % 2 ? 3.0 : -3.0, 2.0, b % 3 == 0 ? Slope::dB24 : Slope::dB12,
                     Placement::Stereo, b % 2 ? -6.0 : 6.0);
        }
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        for (int k = 0; k < 100; ++k) { r.gen.render (buf.getWritePointer (0), buf.getWritePointer (1), 512); p.processBlock (buf, midi); }
        double total = 0.0, worst = 0.0;
        const int N = 2000;
        for (int k = 0; k < N; ++k)
        {
            r.gen.render (buf.getWritePointer (0), buf.getWritePointer (1), 512);
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            p.processBlock (buf, midi);
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            total += ms; worst = std::max (worst, ms);
        }
        const double blockMs = 512.0 / 48.0;
        std::printf ("BENCH processor, 24 dynamic bands, 48 kHz stereo: avg %.2f%% CPU, worst block %.1f%% of its deadline\n",
                     100.0 * total / N / blockMs, 100.0 * worst / blockMs);
        check (total / N < blockMs * 0.15, "processor CPU with 24 dynamic bands well below real time", 100.0 * total / N / blockMs);
    }
    // mono layout
    {
        auto p = std::make_unique<ClaudeQ4AudioProcessor>();
        auto layout = p->getBusesLayout();
        layout.inputBuses.getReference (0) = juce::AudioChannelSet::mono();
        layout.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
        check (p->setBusesLayout (layout), "mono layout accepted");
        p->prepareToPlay (44100.0, 256);
        buildShowcase (*p);
        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;
        SignalGen g (44100.0);
        bool ok = true;
        for (int k = 0; k < 100; ++k)
        {
            g.render (buf.getWritePointer (0), nullptr, 256);
            p->processBlock (buf, midi);
            for (int s = 0; s < 256; ++s) if (! std::isfinite (buf.getSample (0, s))) ok = false;
        }
        check (ok, "mono processing");
    }
}

//==============================================================================
static void instanceTests (const juce::File& outDir)
{
    std::printf ("\n== instances, collisions, EQ match, sketch ==\n");
    Rig a (48000.0, 512, true, 11), b (48000.0, 512, false, 12), c (48000.0, 512, false, 13);
    a.proc->setInstanceName ("Lead Vocal");
    b.proc->setInstanceName ("Bass");
    c.proc->setInstanceName ("Guitars");
    buildShowcase (*a.proc);
    clearBands (*b.proc);
    addBand (*b.proc, 0, Shape::LowCut, 30.0, 0.0, 0.71, Slope::dB24);
    addBand (*b.proc, 1, Shape::Bell, 90.0, 4.0, 1.4, Slope::dB12, Placement::Stereo, -5.0);
    addBand (*b.proc, 2, Shape::HighCut, 5000.0, 0.0, 0.71, Slope::dB24);
    clearBands (*c.proc);
    addBand (*c.proc, 0, Shape::LowCut, 110.0, 0.0, 0.71, Slope::dB18);
    addBand (*c.proc, 1, Shape::Bell, 350.0, -3.0, 1.2);
    addBand (*c.proc, 2, Shape::HighShelf, 4000.0, 2.5, 0.71, Slope::dB12, Placement::Side);
    addBand (*c.proc, 3, Shape::Bell, 1200.0, 2.0, 1.0, Slope::dB12, Placement::Mid);
    b.gen.fundamental = 41.2; b.gen.toneLevel = 0.6f; b.gen.noiseLevel = 0.02f;
    c.gen.fundamental = 196.0; c.gen.noiseLevel = 0.05f;
    a.gen.burstLevel = 0.2f;

    std::unique_ptr<ClaudeQ4Editor> ed (dynamic_cast<ClaudeQ4Editor*> (a.proc->createEditor()));
    ed->setSize (1000, 620);
    run (a, ed.get(), 2.0, { &b, &c });
    snapshot (*ed, outDir, "10_collision_detection");

    ed->openInstances();
    run (a, ed.get(), 1.0, { &b, &c });
    snapshot (*ed, outDir, "11_instances");
    ed->closeOverlays();

    // copy the EQ curve of one instance and paste it into another
    {
        std::unique_ptr<ClaudeQ4Editor> eb (dynamic_cast<ClaudeQ4Editor*> (b.proc->createEditor()));
        eb->setSize (1000, 620);
        run (b, eb.get(), 0.1);
        ed->getDisplay().keyPressed (juce::KeyPress ('c', juce::ModifierKeys::commandModifier, 0));
        eb->getDisplay().keyPressed (juce::KeyPress ('v', juce::ModifierKeys::commandModifier, 0));
        run (b, eb.get(), 0.1);
        bool same = true;
        for (int band = 0; band < kMaxBands; ++band)
            for (int k = 0; k < kNumBandParams; ++k)
                if (a.proc->table.bandFlag (band, bpUsed) && std::abs (a.proc->table.bandValue (band, (BandParam) k) - b.proc->table.bandValue (band, (BandParam) k)) > 1.0e-4f)
                    same = false;
        check (same && usedBands (*b.proc) == usedBands (*a.proc), "copy / paste the EQ curve between instances", usedBands (*b.proc));
        b.proc->undoManager.undo();
        check (usedBands (*b.proc) == 3, "undo the paste", usedBands (*b.proc));
    }

    // M/S + L/R curves on the third instance
    {
        std::unique_ptr<ClaudeQ4Editor> ec (dynamic_cast<ClaudeQ4Editor*> (c.proc->createEditor()));
        ec->setSize (1000, 620);
        run (c, ec.get(), 1.2, { &a, &b });
        snapshot (*ec, outDir, "12_mid_side_curves");
    }

    // EQ match against the side chain
    {
        ed->openMatch();
        run (a, ed.get(), 0.1);
        // find the match panel and press Learn, play, then Match
        MatchPanel* mp = nullptr;
        for (auto* ch : ed->getChildren()) if (auto* m = dynamic_cast<MatchPanel*> (ch)) mp = m;
        check (mp != nullptr, "EQ match panel opens");
        if (mp != nullptr)
        {
            FlatButton* learn = nullptr; FlatButton* match = nullptr;
            for (auto* ch : mp->getChildren())
                if (auto* fb = dynamic_cast<FlatButton*> (ch))
                {
                    if (fb->getButtonText() == "Learn") learn = fb;
                    if (fb->getButtonText() == "Match") match = fb;
                }
            check (learn != nullptr && match != nullptr, "match buttons found");
            if (learn != nullptr && match != nullptr)
            {
                learn->triggerClick();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
                run (a, ed.get(), 3.0);
                snapshot (*ed, outDir, "13_eq_match_learning");
                match->triggerClick();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
                run (a, ed.get(), 0.3);
                check (usedBands (*a.proc) >= 3, "EQ match created bands", usedBands (*a.proc));
                snapshot (*ed, outDir, "14_eq_match_result");
            }
        }
        ed->closeOverlays();
    }

    // EQ sketch: draw a smile curve
    {
        clearBands (*a.proc);
        run (a, ed.get(), 0.2);
        auto& d = ed->getDisplay();
        d.setSketchMode (true);
        const auto down = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier);
        std::vector<juce::Point<float>> pts;
        for (int i = 0; i <= 80; ++i)
        {
            const double f = 30.0 * std::pow (16000.0 / 30.0, i / 80.0);
            const double t = std::log2 (f / 700.0);
            const double db = 4.0 * std::tanh (0.6 * t * t - 1.2) + 1.0 + 3.0 / (1.0 + std::exp (-3.0 * std::log2 (f / 5000.0)));
            pts.push_back ({ d.xForFreq (f), d.yForDb (db) });
        }
        d.mouseDown (mouseEvent (d, pts.front(), down));
        for (size_t i = 1; i < pts.size(); ++i) d.mouseDrag (mouseEvent (d, pts[i], down, 1, true, pts.front()));
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        d.mouseUp (mouseEvent (d, pts.back(), {}, 1, true, pts.front()));
        const double fitMs = juce::Time::getMillisecondCounterHiRes() - t0;
        run (a, ed.get(), 0.3);
        // compare the result with the sketch
        double worst = 0.0;
        for (size_t i = 5; i + 5 < pts.size(); i += 3)
        {
            const double f = d.freqForX (pts[i].x);
            worst = std::max (worst, std::abs (d.staticResponseDb (f) - d.dbForY (pts[i].y)));
        }
        check (usedBands (*a.proc) >= 2 && worst < 1.5, "EQ sketch: bands reproduce the drawn curve (bands, worst dB)", usedBands (*a.proc), worst);
        std::printf ("      sketch fit took %.1f ms\n", fitMs);
        d.setSketchMode (false);
        snapshot (*ed, outDir, "15_eq_sketch_result");
    }
}

//==============================================================================
static void vst3Test (const juce::String& path, const juce::File& outDir)
{
    std::printf ("\n== VST3 binary ==\n");
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile (types, path);
    check (types.size() == 1, "VST3 scan finds one plugin", types.size());
    if (types.isEmpty()) return;
    const auto& desc = *types[0];
    check (desc.name == "Claude-Q 4", "plugin name: " + desc.name);
    std::printf ("      manufacturer '%s', category '%s', ins %d outs %d\n", desc.manufacturerName.toRawUTF8(), desc.category.toRawUTF8(), desc.numInputChannels, desc.numOutputChannels);
    juce::String err;
    auto inst = format.createInstanceFromDescription (desc, 48000.0, 512, err);
    check (inst != nullptr, "VST3 instantiates " + err);
    if (inst == nullptr) return;
    inst->prepareToPlay (48000.0, 512);
    check (inst->getParameters().size() >= 539, "VST3 exposes all parameters", inst->getParameters().size());
    // set a band through the host interface and process
    auto& ps = inst->getParameters();
    auto find = [&ps] (const juce::String& name) -> juce::AudioProcessorParameter*
    {
        for (auto* q : ps) if (q->getName (64) == name) return q;
        return nullptr;
    };
    auto* used = find ("Band 1 Used");
    auto* gain = find ("Band 1 Gain");
    auto* freq = find ("Band 1 Frequency");
    check (used != nullptr && gain != nullptr && freq != nullptr, "host sees named band parameters");
    if (used && gain && freq)
    {
        used->setValueNotifyingHost (1.0f);
        gain->setValueNotifyingHost (gain->getValueForText ("+12 dB"));
        freq->setValueNotifyingHost (freq->getValueForText ("1000"));
        check (gain->getText (gain->getValue(), 32).startsWith ("+12"), "host text conversion: " + gain->getText (gain->getValue(), 32));
    }
    const int bs = 512;
    juce::AudioBuffer<float> buf (inst->getTotalNumInputChannels() > 2 ? inst->getTotalNumInputChannels() : 2, bs);
    juce::MidiBuffer midi;
    double rmsIn = 0.0, rmsOut = 0.0;
    for (int k = 0; k < 200; ++k)
    {
        buf.clear();
        for (int s = 0; s < bs; ++s)
        {
            const float v = 0.1f * (float) std::sin (kTwoPi * 1000.0 * (k * bs + s) / 48000.0);
            buf.setSample (0, s, v); buf.setSample (1, s, v);
            if (k > 100) rmsIn += v * v;
        }
        inst->processBlock (buf, midi);
        if (k > 100) for (int s = 0; s < bs; ++s) rmsOut += buf.getSample (0, s) * buf.getSample (0, s);
    }
    const double gainDb = 10.0 * std::log10 (rmsOut / std::max (rmsIn, 1e-30));
    check (std::abs (gainDb - 12.0) < 0.3, "VST3 processes: +12 dB bell at 1 kHz measured", gainDb);
    juce::MemoryBlock st;
    inst->getStateInformation (st);
    check (st.getSize() > 1000, "VST3 state chunk", (double) st.getSize());
    if (auto* ed = inst->createEditorAndMakeActive())
    {
        ed->setSize (1000, 620);
        for (int i = 0; i < 30; ++i) { inst->processBlock (buf, midi); juce::MessageManager::getInstance()->runDispatchLoopUntil (17); }
        snapshot (*ed, outDir, "16_vst3_editor");
        delete ed;
    }
    else check (false, "VST3 editor");
    inst->releaseResources();
}

//==============================================================================
// shows the hosted VST3 editor in a real window (for screen captures of the actual plug-in binary)
static void vst3Show (const juce::String& path, double seconds)
{
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile (types, path);
    if (types.isEmpty()) return;
    juce::String err;
    auto inst = format.createInstanceFromDescription (*types[0], 48000.0, 512, err);
    if (inst == nullptr) return;
    inst->prepareToPlay (48000.0, 512);
    // a few bands so the window shows something real
    auto& ps = inst->getParameters();
    auto set = [&ps] (const juce::String& name, const juce::String& text)
    {
        for (auto* q : ps) if (q->getName (64) == name) { q->setValueNotifyingHost (q->getValueForText (text)); return; }
    };
    set ("Band 1 Used", "On");  set ("Band 1 Shape", "Low Cut"); set ("Band 1 Slope", "24 dB/oct"); set ("Band 1 Frequency", "40");
    set ("Band 2 Used", "On");  set ("Band 2 Frequency", "180"); set ("Band 2 Gain", "-4"); set ("Band 2 Q", "1.6");
    set ("Band 3 Used", "On");  set ("Band 3 Frequency", "2500"); set ("Band 3 Gain", "5"); set ("Band 3 Q", "0.8");
    set ("Band 3 Dynamic Mode", "On"); set ("Band 3 Dynamic Range", "-6");
    set ("Band 4 Used", "On");  set ("Band 4 Shape", "High Shelf"); set ("Band 4 Frequency", "9000"); set ("Band 4 Gain", "3");
    juce::DocumentWindow win ("Claude-Q 4 (hosted VST3)", juce::Colours::black, 0);
    win.setUsingNativeTitleBar (false);
    juce::AudioProcessorEditor* ed = std::getenv ("CQ_NO_EDITOR") != nullptr ? nullptr : inst->createEditorAndMakeActive();
    juce::Label placeholder ("p", "no editor");
    placeholder.setSize (400, 300);
    win.setContentNonOwned (ed != nullptr ? (juce::Component*) ed : (juce::Component*) &placeholder, true);
    win.setTopLeftPosition (0, 0);
    win.setVisible (true);
    SignalGen gen (48000.0, 5);
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    const double end = juce::Time::getMillisecondCounterHiRes() + seconds * 1000.0;
    while (juce::Time::getMillisecondCounterHiRes() < end)
    {
        gen.render (buf.getWritePointer (0), buf.getWritePointer (1), 512);
        inst->processBlock (buf, midi);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    }
    win.clearContentComponent();
    delete ed;
    inst->releaseResources();
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc > 3 && juce::String (argv[3]) == "--show")
    {
        vst3Show (argv[2], argc > 4 ? juce::String (argv[4]).getDoubleValue() : 8.0);
        return 0;
    }
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();

    processorTests();
    interactionTests (outDir);
    guiTests (outDir);
    instanceTests (outDir);
    if (argc > 2) vst3Test (argv[2], outDir);

    std::printf ("\n%d passed, %d failed\n", passes, failures);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    return failures == 0 ? 0 : 1;
}
