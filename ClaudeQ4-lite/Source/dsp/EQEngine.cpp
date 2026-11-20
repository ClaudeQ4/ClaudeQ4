/*
    Claude-Q 4 - EQ engine
*/
#include "EQEngine.h"
#include <chrono>

namespace cq
{

    EQEngine::EQEngine()
    {
        for (auto& a : dynGainUi) a.store (0.0f);
        for (auto& a : levelUi) a.store (-120.0f);
        for (auto& a : thresholdUi) a.store (-30.0f);
        for (auto& a : spectralActiveUi) a.store (false);
        for (auto& arr : spectralUi) for (auto& v : arr) v.store (0.0f);
        KernelWorker::add (this);
    }

    EQEngine::~EQEngine()
    {
        KernelWorker::remove (this);
        delete pending.exchange (nullptr);
        for (auto& r : retired) delete r.exchange (nullptr);
        delete deferredRetire;
        deferredRetire = nullptr;
        active = nullptr;
        activeForWorker.store (nullptr);
        ownedActive.reset();
    }

    EQEngine::DesiredConfig EQEngine::desiredFor (const EngineParams& p) noexcept
    {
        DesiredConfig d;
        d.mode = p.global.mode;
        d.res = p.global.resolution;
        d.spectral = false;
        for (const auto& b : p.bands) if (b.isSpectral()) { d.spectral = true; break; }
        if (d.mode != ProcessingMode::LinearPhase && ! d.spectral) d.res = LPResolution::Medium;
        return d;
    }

    bool EQEngine::retireConfig (Config* c) noexcept
    {
        for (auto& r : retired)
        {
            Config* expected = nullptr;
            if (r.compare_exchange_strong (expected, c)) return true;
        }
        return false;
    }

    void EQEngine::prepare (double sampleRate, int maxBlockSize, int numChannels, const EngineParams& initial)
    {
        std::lock_guard<std::mutex> lk (workMutex);   // keep the worker out while we rebuild
        prepared = false;
        fs = sampleRate;
        maxBlock = std::max (32, maxBlockSize);
        numCh = std::max (1, std::min (2, numChannels));
        ctrl = fs > 100000.0 ? 128 : (fs > 50000.0 ? 64 : 32);

        for (auto* v : { &dL, &dR, &inL, &inR, &dryL, &dryR, &scL, &scR, &tmpA, &tmpB }) v->assign ((size_t) maxBlock, 0.0);
        for (auto* v : { &fL, &fR, &fScL, &fScR, &fDryL, &fDryR }) v->assign ((size_t) maxBlock, 0.0f);

        params = initial;
        for (int b = 0; b < kMaxBands; ++b)
        {
            iir[(size_t) b].prepare (fs, ctrl);
            dyn[(size_t) b].prepare (fs, ctrl);
            dynGain[(size_t) b] = 0.0;
        }
        np.configure (fs, numCh);
        npBuilder.configure (fs, NaturalPhaseCorrector::kernelLengthFor (fs));
        character.prepare (fs);
        character.setMode (initial.global.character);
        character.reset();

        delete pending.exchange (nullptr);
        for (auto& r : retired) delete r.exchange (nullptr);
        delete deferredRetire;
        deferredRetire = nullptr;
        activeForWorker.store (nullptr);
        desired = desiredFor (initial);
        ownedActive = buildConfig (desired, initial, nullptr);
        active = ownedActive.get();
        activeForWorker.store (active);
        built = desired;
        workerParams = initial;
        workerParamsVersion = initial.version;
        workerBuiltVersion = initial.version;
        workerDyn.fill (0.0);
        workerDynBuilt.fill (0.0);
        sampleClock = 0;
        lastLPBuildSample = std::numeric_limits<int64_t>::min() / 2;

        for (int b = 0; b < kMaxBands; ++b)
        {
            BandParams bp = initial.bands[(size_t) b];
            if (bp.isSpectral() && active->spec != nullptr) bp.used = false;
            iir[(size_t) b].setTarget (bp, initial.global.gainScale, numCh == 1);
            iir[(size_t) b].reset();
            dyn[(size_t) b].setParams (initial.bands[(size_t) b], initial.global.gainScale);
        }
        if (active->spec != nullptr) active->spec->setParams (initial);

        // natural phase kernel for the initial state
        if (desired.mode == ProcessingMode::NaturalPhase)
        {
            publishNPSnapshot();
            npToWorker.fetch();
            if (NPKernelSet* ks = np.beginWriteBack()) { npBuilder.build (npToWorker.readBuffer(), *ks); np.endWriteBack (true); }
        }
        lastParamsVersion = initial.version;
        autoGainDb.store (initial.global.autoGain ? estimateAutoGainDb (initial) : 0.0);
        autoGainSmoothed = autoGainDb.load();
        outGainSmoothed = dbToGain (initial.global.outputGainDb <= -59.9 ? -200.0 : initial.global.outputGainDb);
        panSmoothed = initial.global.outputPan;
        phaseSign = initial.global.phaseInvert ? -1.0 : 1.0;
        bypassMix = initial.global.bypass ? 1.0 : 0.0;
        fadeIn = 1.0;
        soloMix = 0.0;
        soloBandCur = -1;
        latency.store (active->latency);
        prepared = true;
    }

    void EQEngine::release()
    {
        std::lock_guard<std::mutex> lk (workMutex);
        prepared = false;
    }

    std::unique_ptr<EQEngine::Config> EQEngine::buildConfig (const DesiredConfig& d, const EngineParams& p, const double* dynG)
    {
        auto c = std::make_unique<Config>();
        c->mode = d.mode;
        c->res = d.res;
        c->spectral = d.spectral;
        c->phaseLatency = 0;
        if (d.mode == ProcessingMode::NaturalPhase) c->phaseLatency = np.getLatency();
        if (d.mode == ProcessingMode::LinearPhase)
        {
            const int L = lpKernelLength (d.res, fs), P = lpPartitionSize (fs);
            c->lp = std::make_unique<LinearPhaseConvolver>();
            c->lp->configure (L, P, numCh);
            c->lpBuilder = std::make_unique<LPKernelBuilder>();
            c->lpBuilder->configure (fs, L, P);
            c->phaseLatency = c->lp->getLatency();
        }
        if (d.spectral)
        {
            const LPResolution r = d.res > LPResolution::High ? LPResolution::High : d.res;
            c->spectralFrame = SpectralEngine::frameSizeFor (r, fs);
            c->spec = std::make_unique<SpectralEngine>();
            c->spec->configure (fs, c->spectralFrame, numCh);
            c->spec->setParams (p);
        }
        if (c->lp != nullptr) buildLPKernel (*c, p, dynG, true);
        c->latency = c->phaseLatency + (c->spec != nullptr ? c->spec->getLatency() : 0);
        c->dryLen = c->latency + maxBlock + 1;
        for (auto& v : c->dry) v.assign ((size_t) c->dryLen, 0.0f);
        c->scdLen = (d.mode == ProcessingMode::LinearPhase ? c->phaseLatency : 0) + maxBlock + 1;
        for (auto& v : c->scd) v.assign ((size_t) c->scdLen, 0.0f);
        return c;
    }

    void EQEngine::buildLPKernel (Config& c, const EngineParams& p, const double* dynG, bool force)
    {
        (void) force;
        if (c.lp == nullptr) return;
        LPKernelSet* ks = c.lp->beginWriteBack();
        if (ks == nullptr) return;
        const bool dynAllowed = c.res <= LPResolution::High;
        c.lpBuilder->build (p, dynAllowed ? dynG : nullptr, c.spec == nullptr, *ks);
        c.lp->endWriteBack (true);
        c.lpKernelReady = true;
    }

    void EQEngine::setParams (const EngineParams& p) noexcept
    {
        params = p;
        const bool mono = numCh == 1;
        const bool specAvail = active != nullptr && active->spec != nullptr;
        for (int b = 0; b < kMaxBands; ++b)
        {
            BandParams bp = p.bands[(size_t) b];
            const bool spectralHere = bp.isSpectral() && specAvail;
            spectralActiveUi[(size_t) b].store (spectralHere, std::memory_order_relaxed);
            if (spectralHere) bp.used = false;
            iir[(size_t) b].setTarget (bp, p.global.gainScale, mono);
            dyn[(size_t) b].setParams (p.bands[(size_t) b], p.global.gainScale);
        }
        desired = desiredFor (p);
        desiredMode.store ((int) desired.mode);
        desiredRes.store ((int) desired.res);
        desiredSpectral.store (desired.spectral);
        character.setMode (p.global.character);
        if (specAvail) active->spec->setParams (p);
        dynDisabledUi.store (p.global.mode == ProcessingMode::LinearPhase && p.global.resolution > LPResolution::High);

        if (p.version != lastParamsVersion)
        {
            paramsToWorker.writeBuffer() = p;
            paramsToWorker.publish();
            lastParamsVersion = p.version;
            npDirty = true;
            autoGainDb.store (p.global.autoGain ? estimateAutoGainDb (p) : 0.0, std::memory_order_relaxed);
        }
    }

    double EQEngine::estimateAutoGainDb (const EngineParams& p) noexcept
    {
        // loudness weighted average power change of the static EQ curve
        constexpr int nPts = 72;
        double num = 0.0, den = 0.0;
        std::array<AnalogFilter, kMaxBands> af;
        std::array<bool, kMaxBands> use {};
        for (int b = 0; b < kMaxBands; ++b)
        {
            const auto& bp = p.bands[(size_t) b];
            use[(size_t) b] = bp.isActive();
            if (use[(size_t) b]) makeAnalogPrototype (bp.shapeParams (p.global.gainScale), af[(size_t) b]);
        }
        for (int i = 0; i < nPts; ++i)
        {
            const double f = 25.0 * std::pow (16000.0 / 25.0, (i + 0.5) / nPts);
            double m00 = 1, m01 = 0, m10 = 0, m11 = 1;
            for (int b = 0; b < kMaxBands; ++b)
            {
                if (! use[(size_t) b]) continue;
                const auto& bp = p.bands[(size_t) b];
                const double h = std::sqrt (af[(size_t) b].magSq (f / clampT (bp.freq, kMinFreq, kMaxFreq)));
                switch (bp.place)
                {
                    case Placement::Stereo: m00 *= h; m01 *= h; m10 *= h; m11 *= h; break;
                    case Placement::Left:   m00 *= h; m01 *= h; break;
                    case Placement::Right:  m10 *= h; m11 *= h; break;
                    default:
                    {
                        const double pp = 0.5 * (h + 1.0), qq = bp.place == Placement::Mid ? 0.5 * (h - 1.0) : 0.5 * (1.0 - h);
                        const double a00 = pp * m00 + qq * m10, a01 = pp * m01 + qq * m11, a10 = qq * m00 + pp * m10, a11 = qq * m01 + pp * m11;
                        m00 = a00; m01 = a01; m10 = a10; m11 = a11;
                    }
                }
            }
            const double corr = 0.5 * ((m00 + m01) * (m00 + m01) + (m10 + m11) * (m10 + m11));
            const double unc  = 0.5 * (m00 * m00 + m01 * m01 + m10 * m10 + m11 * m11);
            const double pw = 0.5 * (corr + unc);
            // ear weighting: gentle low / high roll-off of importance
            const double w = (f * f / (f * f + 120.0 * 120.0)) / (1.0 + std::pow (f / 7000.0, 2.0));
            num += w * pw;
            den += w;
        }
        const double g = -10.0 * std::log10 (std::max (num / std::max (den, 1e-12), 1e-12));
        return clampT (g, -24.0, 24.0);
    }

    void EQEngine::publishNPSnapshot() noexcept
    {
        NPSnapshot& s = npToWorker.writeBuffer();
        for (int b = 0; b < kMaxBands; ++b)
        {
            const auto& band = iir[(size_t) b];
            auto& o = s.bands[(size_t) b];
            o.active = ! band.isIdle();
            if (! o.active) { o.num = 0; s.wet[b] = 0.0; continue; }
            o.place = band.getPlacement();
            o.shape = band.analogShape (dynGain[(size_t) b]);
            o.num = band.getNumSections();
            for (int k = 0; k < o.num; ++k)
            {
                o.second[(size_t) k] = band.isSecond (k);
                const auto& c = band.getCoef (k);
                o.coefs[(size_t) (5 * k)] = c.g; o.coefs[(size_t) (5 * k + 1)] = c.k; o.coefs[(size_t) (5 * k + 2)] = c.m0;
                o.coefs[(size_t) (5 * k + 3)] = c.m1; o.coefs[(size_t) (5 * k + 4)] = c.m2;
            }
            s.wet[b] = band.getWet();
        }
        s.serial = ++npSerial;
        npToWorker.publish();
    }

    void EQEngine::doBackgroundWork()
    {
        if (! prepared) return;
        if (nonRealtime.load()) return;   // the audio thread does it synchronously

        if (paramsToWorker.fetch()) { workerParams = paramsToWorker.readBuffer(); workerParamsVersion = workerParams.version; }
        if (dynToWorker.fetch()) workerDyn = dynToWorker.readBuffer();

        // 1) processing configuration
        DesiredConfig d;
        d.mode = (ProcessingMode) desiredMode.load();
        d.res = (LPResolution) desiredRes.load();
        d.spectral = desiredSpectral.load();
        if (d != built && pending.load() == nullptr)
        {
            auto c = buildConfig (d, workerParams, workerDyn.data());
            workerBuiltVersion = workerParams.version;
            workerDynBuilt = workerDyn;
            pending.store (c.release());
            built = d;
        }

        // 2) free configurations the audio thread has finished with
        for (auto& r : retired) if (Config* old = r.exchange (nullptr)) delete old;

        // 3) linear phase kernel updates (parameters, dynamic EQ).  The active config can only
        //    be retired by the audio thread and is only ever deleted in step 2 above, so it stays
        //    valid for the rest of this call.
        Config* wc = activeForWorker.load();
        if (wc != nullptr && wc->lp != nullptr && wc->lp->isBackFree())
        {
            const bool paramsChanged = workerParamsVersion != workerBuiltVersion;
            bool dynChanged = false;
            if (wc->res <= LPResolution::High)
                for (int b = 0; b < kMaxBands; ++b)
                    if (workerParams.bands[(size_t) b].isDynamic() && std::abs (workerDyn[(size_t) b] - workerDynBuilt[(size_t) b]) > 0.05)
                        { dynChanged = true; break; }
            // at most one rebuild every 5 ms (parameter edits) / 8 ms (dynamic EQ movement):
            // wall clock time in real time, audio time in offline renders (deterministic bounces)
            const auto now = std::chrono::steady_clock::now();
            const int64_t minGapSamples = (int64_t) (fs * (paramsChanged ? 0.005 : 0.008));
            const bool rateOk = syncWork ? (sampleClock - lastLPBuildSample) >= minGapSamples
                                         : (now - lastLPBuild) > std::chrono::milliseconds (paramsChanged ? 5 : 8);
            if ((paramsChanged || dynChanged) && rateOk)
            {
                buildLPKernel (*wc, workerParams, workerDyn.data(), false);
                workerBuiltVersion = workerParamsVersion;
                workerDynBuilt = workerDyn;
                lastLPBuild = now;
                if (syncWork) lastLPBuildSample = sampleClock;   // (sampleClock belongs to the audio thread)
            }
        }

        // 4) natural phase correction kernel
        if (np.isBackFree() && npToWorker.fetch())
        {
            if (NPKernelSet* ks = np.beginWriteBack())
            {
                npBuilder.build (npToWorker.readBuffer(), *ks);
                np.endWriteBack (true);
            }
        }
    }

    void EQEngine::updateSoloFilters (int band) noexcept
    {
        const auto& bp = params.bands[(size_t) band];
        soloParams = bp;
        int hpMode = 0, lpMode = 0;
        double fLo = 20.0, fHi = 20000.0;
        const double f0 = clampT (bp.freq, kMinFreq, kMaxFreq);
        switch (bp.shape)
        {
            case Shape::Bell: case Shape::Notch: case Shape::BandPass: case Shape::AllPass:
            {
                const double q = clampT (bp.q, 0.1, 40.0);
                const double bw = clampT (2.0 * std::asinh (1.0 / (2.0 * q)) / kLn2, 1.0 / 3.0, 5.0);
                fLo = f0 * std::exp2 (-0.5 * bw); fHi = f0 * std::exp2 (0.5 * bw);
                hpMode = 2; lpMode = 1;
                break;
            }
            case Shape::LowShelf: case Shape::LowCut: lpMode = 1; fHi = f0; break;
            case Shape::HighShelf: case Shape::TiltShelf: case Shape::HighCut: hpMode = 2; fLo = f0; break;
            default: break;
        }
        for (int c = 0; c < 2; ++c)
        {
            soloHP[c].set (hpMode, std::min (fLo, 0.45 * fs), 0.5412, fs);
            soloHP2[c].set (hpMode, std::min (fLo, 0.45 * fs), 1.3066, fs);
            soloLP[c].set (lpMode, std::min (fHi, 0.45 * fs), 0.5412, fs);
            soloLP2[c].set (lpMode, std::min (fHi, 0.45 * fs), 1.3066, fs);
        }
    }

    void EQEngine::process (float* const* io, int nc, const float* const* sc, int numSc, int n) noexcept
    {
        if (! prepared || n <= 0) return;
        if (nonRealtime.load())
        {
            // offline: do the background work synchronously (deterministic bounces)
            std::lock_guard<std::mutex> lk (workMutex);
            nonRealtime.store (false);
            syncWork = true;
            doBackgroundWork();
            syncWork = false;
            nonRealtime.store (true);
        }
        sampleClock += n;
        float* L = io[0];
        float* R = (nc > 1 && numCh > 1) ? io[1] : nullptr;
        const float* sL = (sc != nullptr && numSc > 0) ? sc[0] : nullptr;
        const float* sR = (sc != nullptr && numSc > 1) ? sc[1] : sL;
        for (int pos = 0; pos < n; pos += maxBlock)
        {
            const int m = std::min (maxBlock, n - pos);
            processChunk (L + pos, R != nullptr ? R + pos : nullptr, sL != nullptr ? sL + pos : nullptr, sR != nullptr ? sR + pos : nullptr, m);
        }
        if (nc > 1 && numCh == 1) std::memcpy (io[1], io[0], sizeof (float) * (size_t) n);
    }

    void EQEngine::processChunk (float* L, float* R, const float* sL, const float* sR, int n) noexcept
    {
        // ---- adopt a new processing configuration
        if (deferredRetire != nullptr && retireConfig (deferredRetire)) deferredRetire = nullptr;
        if (deferredRetire == nullptr)
        {
            if (Config* p = pending.exchange (nullptr))
            {
                const ProcessingMode oldMode = active != nullptr ? active->mode : ProcessingMode::ZeroLatency;
                Config* old = ownedActive.release();
                ownedActive.reset (p);
                active = p;
                activeForWorker.store (p);
                if (old != nullptr && ! retireConfig (old)) deferredRetire = old;
                if (oldMode == ProcessingMode::LinearPhase && p->mode != ProcessingMode::LinearPhase)
                    for (auto& b : iir) b.reset();
                if (p->mode == ProcessingMode::NaturalPhase) { np.reset(); npDirty = true; }
                fadeIn = 0.0;
                latency.store (p->latency);
                setParams (params);   // re-route spectral bands
            }
        }
        Config& c = *active;
        const bool stereo = (R != nullptr);
        const bool haveSc = (sL != nullptr);

        // ---- input, latency compensated dry path and side chain alignment
        const int dryDelay = c.latency;
        const int detDelay = (c.mode == ProcessingMode::LinearPhase) ? c.phaseLatency : 0;
        const int scDelay = (c.mode == ProcessingMode::LinearPhase) ? c.phaseLatency : 0;
        for (int i = 0; i < n; ++i)
        {
            const float xl = L[i], xr = stereo ? R[i] : xl;
            inL[(size_t) i] = xl; inR[(size_t) i] = xr;
            c.dry[0][(size_t) c.dryPos] = xl; c.dry[1][(size_t) c.dryPos] = xr;
            int rp = c.dryPos - dryDelay; if (rp < 0) rp += c.dryLen;
            fDryL[(size_t) i] = c.dry[0][(size_t) rp]; fDryR[(size_t) i] = c.dry[1][(size_t) rp];
            int dp = c.dryPos - detDelay; if (dp < 0) dp += c.dryLen;
            dryL[(size_t) i] = c.dry[0][(size_t) dp]; dryR[(size_t) i] = c.dry[1][(size_t) dp];
            if (++c.dryPos >= c.dryLen) c.dryPos = 0;
            if (haveSc)
            {
                c.scd[0][(size_t) c.scdPos] = sL[i]; c.scd[1][(size_t) c.scdPos] = sR[i];
                int sp = c.scdPos - scDelay; if (sp < 0) sp += c.scdLen;
                scL[(size_t) i] = c.scd[0][(size_t) sp]; scR[(size_t) i] = c.scd[1][(size_t) sp];
                fScL[(size_t) i] = (float) scL[(size_t) i]; fScR[(size_t) i] = (float) scR[(size_t) i];
                if (++c.scdPos >= c.scdLen) c.scdPos = 0;
            }
            dL[(size_t) i] = xl; dR[(size_t) i] = xr;
        }

        // ---- control blocks: dynamics + IIR bands
        const bool iirMode = c.mode != ProcessingMode::LinearPhase;
        bool anyDynamic = false;
        for (int pos = 0; pos < n; pos += ctrl)
        {
            const int m = std::min (ctrl, n - pos);
            for (int b = 0; b < kMaxBands; ++b)
            {
                const auto& bp = params.bands[(size_t) b];
                if (! bp.isActive() || ! shapeSupportsDynamics (bp.shape) || std::abs (bp.dynRange) < 1.0e-3) { dynGain[(size_t) b] = 0.0; continue; }
                const bool useSc = bp.extSidechain && haveSc;
                const double* a = (useSc ? scL.data() : dryL.data()) + pos;
                const double* bb = (useSc ? scR.data() : dryR.data()) + pos;
                const double* ch0 = a;
                const double* ch1 = nullptr;
                switch (stereo ? bp.place : Placement::Stereo)
                {
                    case Placement::Stereo: ch0 = a; ch1 = stereo ? bb : nullptr; break;
                    case Placement::Left:   ch0 = a; break;
                    case Placement::Right:  ch0 = bb; break;
                    case Placement::Mid:    for (int i = 0; i < m; ++i) tmpA[(size_t) i] = 0.5 * (a[i] + bb[i]); ch0 = tmpA.data(); break;
                    case Placement::Side:   for (int i = 0; i < m; ++i) tmpA[(size_t) i] = 0.5 * (a[i] - bb[i]); ch0 = tmpA.data(); break;
                    default: break;
                }
                dynGain[(size_t) b] = dyn[(size_t) b].processBlock (ch0, ch1, m);
                if (bp.isDynamic()) anyDynamic = true;
            }
            if (iirMode)
            {
                int budget = 2;
                for (int k = 0; k < kMaxBands; ++k)
                {
                    const int b = (budgetStart + k) % kMaxBands;
                    iir[(size_t) b].control (m, dynGain[(size_t) b], budget);
                }
                for (int b = 0; b < kMaxBands; ++b)
                    iir[(size_t) b].process (dL.data() + pos, stereo ? dR.data() + pos : nullptr, m);
            }
            budgetStart = (budgetStart + 1) % kMaxBands;
        }
        for (int b = 0; b < kMaxBands; ++b)
        {
            dynGainUi[(size_t) b].store ((float) dynGain[(size_t) b], std::memory_order_relaxed);
            levelUi[(size_t) b].store ((float) dyn[(size_t) b].getLevelDb(), std::memory_order_relaxed);
            thresholdUi[(size_t) b].store ((float) dyn[(size_t) b].getThresholdDb(), std::memory_order_relaxed);
        }
        if (anyDynamic && c.mode == ProcessingMode::LinearPhase)
        {
            dynToWorker.writeBuffer() = dynGain;
            dynToWorker.publish();
        }

        // ---- phase stage
        for (int i = 0; i < n; ++i) { fL[(size_t) i] = (float) dL[(size_t) i]; fR[(size_t) i] = (float) dR[(size_t) i]; }
        if (c.mode == ProcessingMode::NaturalPhase)
        {
            // publish the running IIR state for the analog phase corrector (rate limited)
            bool moving = npDirty;
            for (const auto& b : iir) if (! b.isSettled()) { moving = true; break; }
            if (moving)
            {
                npCounter += n;
                const int interval = (int) (0.008 * fs);
                if (npCounter >= interval)
                {
                    npCounter = 0;
                    bool settled = true;
                    for (const auto& b : iir) if (! b.isSettled()) { settled = false; break; }
                    publishNPSnapshot();
                    npDirty = ! settled;
                }
            }
            np.process (fL.data(), stereo ? fR.data() : nullptr, n);
        }
        else if (c.mode == ProcessingMode::LinearPhase)
        {
            c.lp->process (fL.data(), stereo ? fR.data() : nullptr, n);
        }

        // ---- spectral dynamics
        if (c.spec != nullptr)
        {
            c.spec->process (fL.data(), stereo ? fR.data() : nullptr, haveSc ? fScL.data() : nullptr, haveSc ? fScR.data() : nullptr, n);
            for (int b = 0; b < kMaxBands; ++b)
                if (params.bands[(size_t) b].isSpectral())
                    for (int j = 0; j < SpectralEngine::kDisplayPoints; ++j)
                        spectralUi[(size_t) b][(size_t) j].store (c.spec->getDisplayGain (b, j), std::memory_order_relaxed);
        }

        // ---- character (double precision)
        for (int i = 0; i < n; ++i) { dL[(size_t) i] = fL[(size_t) i]; dR[(size_t) i] = stereo ? fR[(size_t) i] : fL[(size_t) i]; }
        character.process (dL.data(), stereo ? dR.data() : nullptr, n);

        // ---- solo / audition
        const int sb = soloBand.load (std::memory_order_relaxed);
        const bool wantSolo = sb >= 0 && sb < kMaxBands && params.bands[(size_t) sb].isActive();
        if (wantSolo && sb != soloBandCur) { soloBandCur = sb; updateSoloFilters (sb); }
        else if (wantSolo)
        {
            const auto& bp = params.bands[(size_t) sb];
            if (bp.freq != soloParams.freq || bp.q != soloParams.q || bp.shape != soloParams.shape) updateSoloFilters (sb);
        }
        if (wantSolo || soloMix > 0.0)
        {
            const double gain = dbToGain ((double) soloGainDb.load (std::memory_order_relaxed));
            const bool audition = soloAudition.load (std::memory_order_relaxed);
            const double step = 1.0 / (0.01 * fs);
            for (int i = 0; i < n; ++i)
            {
                double sl = fDryL[(size_t) i], sr = fDryR[(size_t) i];
                if (soloBandCur >= 0)
                {
                    if (audition)
                    {
                        double mono = 0.5 * (sl + sr), out = 0.0;
                        dyn[(size_t) soloBandCur].auditionBlock (&mono, nullptr, &out, 1);
                        sl = sr = out;
                    }
                    else
                    {
                        sl = soloLP2[0].tick (soloLP[0].tick (soloHP2[0].tick (soloHP[0].tick (sl, 0), 0), 0), 0);
                        sr = soloLP2[1].tick (soloLP[1].tick (soloHP2[1].tick (soloHP[1].tick (sr, 1), 1), 1), 1);
                    }
                }
                soloMix = wantSolo ? std::min (1.0, soloMix + step) : std::max (0.0, soloMix - step);
                dL[(size_t) i] += (sl * gain - dL[(size_t) i]) * soloMix;
                dR[(size_t) i] += (sr * gain - dR[(size_t) i]) * soloMix;
            }
            if (soloMix <= 0.0) soloBandCur = -1;
        }

        // ---- output stage
        const auto& g = params.global;
        const double agTarget = g.autoGain ? autoGainDb.load (std::memory_order_relaxed) : 0.0;
        const double outDb = g.outputGainDb <= -59.9 ? -200.0 : g.outputGainDb;
        const double sm = 1.0 - std::exp (-1.0 / (0.02 * fs));
        const double fadeStep = 1.0 / (0.01 * fs);
        const double bypStep = 1.0 / (0.015 * fs);
        const double phTarget = g.phaseInvert ? -1.0 : 1.0;
        const double gainTarget = dbToGain (outDb);
        for (int i = 0; i < n; ++i)
        {
            autoGainSmoothed += (agTarget - autoGainSmoothed) * sm;
            outGainSmoothed += (gainTarget - outGainSmoothed) * sm;
            panSmoothed += (g.outputPan - panSmoothed) * sm;
            phaseSign += (phTarget - phaseSign) * (sm * 4.0);
            const double gn = outGainSmoothed * std::exp (autoGainSmoothed * (kLn10 / 20.0)) * phaseSign;
            double l = dL[(size_t) i] * gn, r = dR[(size_t) i] * gn;
            if (stereo)
            {
                const double pl = std::min (1.0, 1.0 - panSmoothed), pr = std::min (1.0, 1.0 + panSmoothed);
                if (! g.panMidSide) { l *= pl; r *= pr; }
                else
                {
                    const double mm = 0.5 * (l + r) * pl, ss = 0.5 * (l - r) * pr;
                    l = mm + ss; r = mm - ss;
                }
            }
            if (fadeIn < 1.0) { fadeIn = std::min (1.0, fadeIn + fadeStep); l *= fadeIn; r *= fadeIn; }
            bypassMix = g.bypass ? std::min (1.0, bypassMix + bypStep) : std::max (0.0, bypassMix - bypStep);
            if (bypassMix > 0.0)
            {
                l += (fDryL[(size_t) i] - l) * bypassMix;
                r += (fDryR[(size_t) i] - r) * bypassMix;
            }
            L[i] = (float) l;
            if (stereo) R[i] = (float) r;
        }
    }
}
