/*
    Claude-Q 4 - character modes (Clean / Subtle / Warm)

    A gentle analog style saturation stage after the EQ, run at 4x oversampling through
    polyphase IIR halfband filters (elliptic design, ~150 dB image rejection), so no
    audible aliasing is produced.  Low frequencies are pre-emphasised before the
    non-linearity and de-emphasised after it (transformer-like behaviour), so the
    colouring depends on level and frequency - and on the EQ in front of it.
*/
#pragma once

#include "BandParams.h"

namespace cq
{
    class HalfbandPolyphase
    {
    public:
        static constexpr int kMaxCoefs = 12;
        static void design (int numCoefs, double transition, double* coefs) noexcept;

        void setCoefs (const double* c, int n) noexcept;
        void reset() noexcept;
        // up: one input -> two outputs
        void upsample (double x, double& y0, double& y1) noexcept;
        // down: two inputs -> one output
        double downsample (double x0, double x1) noexcept;

    private:
        int num = 0;
        double coef[kMaxCoefs] {};
        double xm[kMaxCoefs] {}, ym[kMaxCoefs] {};
        double oddDelay = 0.0;
    };

    class CharacterProcessor
    {
    public:
        void prepare (double sampleRate) noexcept;
        void reset() noexcept;
        void setMode (CharacterMode m) noexcept { target = m; }
        // in place, ch may be 1 or 2
        void process (double* L, double* R, int n) noexcept;

    private:
        struct Channel
        {
            HalfbandPolyphase up, down, up2a, down2a;   // 2x stage + 4x stage
            double pre = 0, post = 0;     // one pole states for the emphasis shelves
            double dcX = 0, dcY = 0;
        };
        double shape (double x, double drive, double asym) const noexcept;

        double fs = 48000.0;
        CharacterMode target = CharacterMode::Clean, current = CharacterMode::Clean;
        double mix = 0.0;       // cross-fade towards the active character
        double drive = 0.35, asym = 0.08, lowBoost = 1.4;
        double shelfCoef = 0.0, dcCoef = 0.0;
        std::array<Channel, 2> ch;
    };
}
