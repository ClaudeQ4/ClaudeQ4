/*
    Claude-Q 4 - EQ engine

    Signal flow (per host block, processed in ~0.7 ms control blocks):

      input --+--> [dynamics detectors] (band limited triggers, internal / external side chain)
              |
              +--> Zero Latency : IIR bands (TPT SVF) ............................ latency 0
              |    Natural Phase: IIR bands -> analog phase corrector (FIR) ....... latency ~2.7 ms
              |    Linear Phase : 2x2 partitioned FFT convolution ................. latency L/2 + P
              |
              +--> spectral dynamics (STFT, only while spectral bands exist) ....... + 1 frame
              +--> character (4x oversampled saturation)
              +--> solo / trigger audition (latency aligned)
              +--> auto gain, output gain, pan (L/R or M/S), phase invert
              +--> soft bypass against a latency compensated dry path

    Heavy work (FFT kernels, allocations when the processing mode or the spectral
    resolution changes) runs on the shared background worker; offline renders do it
    synchronously so bounces are deterministic.
*/
#pragma once

#include "IIRBand.h"
#include "Dynamics.h"
#include "LinearPhase.h"
#include "NaturalPhase.h"
#include "Spectral.h"
#include "Character.h"
#include "KernelWorker.h"
#include <memory>
#include <chrono>
#include <cstdint>
#include <limits>

namespace cq
{
    class EQEngine : public BackgroundClient
    {
    public:
        EQEngine();
        ~EQEngine() override;

        // message thread
        void prepare (double sampleRate, int maxBlockSize, int numChannels, const EngineParams& initial);
        void release();

        // audio thread
        void setParams (const EngineParams& p) noexcept;
        // io: numCh channels in place.  sc: side chain channels (may be nullptr / 0)
        void process (float* const* io, int numCh, const float* const* sc, int numSc, int n) noexcept;
        void setNonRealtime (bool b) noexcept { nonRealtime.store (b); }

        int getLatency() const noexcept { return latency.load(); }

        // solo: band index or -1.  audition = listen to the dynamics trigger signal instead
        void setSolo (int band, bool audition, double gainDb = 0.0) noexcept
        {
            soloBand.store (band); soloAudition.store (audition); soloGainDb.store ((float) gainDb);
        }

        // ---- UI feeds (any thread)
        float getDynamicGainDb (int b) const noexcept  { return dynGainUi[(size_t) b].load (std::memory_order_relaxed); }
        float getTriggerLevelDb (int b) const noexcept { return levelUi[(size_t) b].load (std::memory_order_relaxed); }
        float getThresholdDb (int b) const noexcept    { return thresholdUi[(size_t) b].load (std::memory_order_relaxed); }
        float getSpectralGainDb (int b, int point) const noexcept { return spectralUi[(size_t) b][(size_t) point].load (std::memory_order_relaxed); }
        bool  isSpectralActive (int b) const noexcept  { return spectralActiveUi[(size_t) b].load (std::memory_order_relaxed); }
        float getAutoGainDb() const noexcept           { return (float) autoGainDb.load (std::memory_order_relaxed); }
        bool  dynamicsDisabledByResolution() const noexcept { return dynDisabledUi.load (std::memory_order_relaxed); }

        // static estimate of the loudness change of the EQ curve (used by auto gain)
        static double estimateAutoGainDb (const EngineParams& p) noexcept;

        void doBackgroundWork() override;

    private:
        struct Config
        {
            ProcessingMode mode = ProcessingMode::ZeroLatency;
            LPResolution res = LPResolution::Medium;
            bool spectral = false;
            int spectralFrame = 0;
            std::unique_ptr<LinearPhaseConvolver> lp;
            std::unique_ptr<LPKernelBuilder> lpBuilder;
            std::unique_ptr<SpectralEngine> spec;
            int phaseLatency = 0;
            int latency = 0;
            // latency compensated dry path (bypass, solo) and side chain alignment
            std::array<std::vector<float>, 2> dry, scd;
            int dryLen = 1, scdLen = 1, dryPos = 0, scdPos = 0;
            bool lpKernelReady = false;
        };

        struct DesiredConfig
        {
            ProcessingMode mode = ProcessingMode::ZeroLatency;
            LPResolution res = LPResolution::Medium;
            bool spectral = false;
            bool operator== (const DesiredConfig& o) const noexcept { return mode == o.mode && res == o.res && spectral == o.spectral; }
            bool operator!= (const DesiredConfig& o) const noexcept { return ! (*this == o); }
        };

        static DesiredConfig desiredFor (const EngineParams& p) noexcept;
        std::unique_ptr<Config> buildConfig (const DesiredConfig& d, const EngineParams& p, const double* dyn);
        void buildLPKernel (Config& c, const EngineParams& p, const double* dyn, bool force);
        void processChunk (float* L, float* R, const float* scL, const float* scR, int n) noexcept;
        void updateSoloFilters (int band) noexcept;
        void publishNPSnapshot() noexcept;

        double fs = 48000.0;
        int maxBlock = 512, numCh = 2, ctrl = 32;
        bool prepared = false;
        std::atomic<bool> nonRealtime { false };

        EngineParams params {};
        uint32_t lastParamsVersion = 0xffffffffu;
        std::array<IIRBand, kMaxBands> iir;
        std::array<BandDynamics, kMaxBands> dyn;
        std::array<double, kMaxBands> dynGain {};
        NaturalPhaseCorrector np;
        NPKernelBuilder npBuilder;
        CharacterProcessor character;

        // configuration exchange
        Config* active = nullptr;
        std::atomic<Config*> activeForWorker { nullptr };
        std::atomic<Config*> pending { nullptr };
        std::array<std::atomic<Config*>, 8> retired {};
        Config* deferredRetire = nullptr;
        bool retireConfig (Config* c) noexcept;
        std::unique_ptr<Config> ownedActive;      // ownership of the active config (audio thread)
        DesiredConfig desired {};
        std::atomic<int> desiredMode { 0 }, desiredRes { 1 };
        std::atomic<bool> desiredSpectral { false };
        DesiredConfig built {};                    // last config handed over by the worker (worker side)
        double fadeIn = 1.0;

        // worker snapshots
        TripleBuffer<EngineParams> paramsToWorker;
        TripleBuffer<std::array<double, kMaxBands>> dynToWorker;
        TripleBuffer<NPSnapshot> npToWorker;
        EngineParams workerParams {};
        std::array<double, kMaxBands> workerDyn {};
        std::array<double, kMaxBands> workerDynBuilt {};
        uint32_t workerParamsVersion = 0xffffffffu, workerBuiltVersion = 0xfffffffeu;
        int npCounter = 0;
        uint32_t npSerial = 0;
        bool npDirty = true;
        std::chrono::steady_clock::time_point lastLPBuild {};
        // offline renders: the background work runs on the audio thread and is paced by audio
        // time, not by the wall clock, so a bounce is bit-identical however fast it renders
        bool syncWork = false;
        int64_t sampleClock = 0;
        int64_t lastLPBuildSample = std::numeric_limits<int64_t>::min() / 2;

        // solo
        std::atomic<int> soloBand { -1 };
        std::atomic<bool> soloAudition { false };
        std::atomic<float> soloGainDb { 0.0f };
        int soloBandCur = -1;
        double soloMix = 0.0;
        DetectorSvf soloHP[2], soloLP[2], soloHP2[2], soloLP2[2];
        BandParams soloParams {};

        // output stage
        std::atomic<double> autoGainDb { 0.0 };
        double autoGainSmoothed = 0.0, outGainSmoothed = 1.0, panSmoothed = 0.0, bypassMix = 0.0, phaseSign = 1.0;
        std::atomic<int> latency { 0 };

        // scratch (allocated in prepare)
        std::vector<double> dL, dR, inL, inR, dryL, dryR, scL, scR, tmpA, tmpB;
        std::vector<float> fL, fR, fScL, fScR, fDryL, fDryR;

        // UI
        std::array<std::atomic<float>, kMaxBands> dynGainUi, levelUi, thresholdUi;
        std::array<std::atomic<bool>, kMaxBands> spectralActiveUi;
        std::array<std::array<std::atomic<float>, SpectralEngine::kDisplayPoints>, kMaxBands> spectralUi;
        std::atomic<bool> dynDisabledUi { false };
        int budgetStart = 0;
    };
}
