/*
    Claude-Q 4 - common DSP helpers
*/
#pragma once

#include <cmath>
#include <complex>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <atomic>
#include <vector>
#include <limits>

namespace cq
{
    using cplx = std::complex<double>;

    constexpr double kPi     = 3.14159265358979323846;
    constexpr double kTwoPi  = 6.28318530717958647692;
    constexpr double kLn10   = 2.30258509299404568402;
    constexpr double kLn2    = 0.69314718055994530942;

    constexpr int kMaxBands       = 24;
    constexpr double kMinFreq     = 10.0;
    constexpr double kMaxFreq     = 30000.0;
    constexpr double kMinGain     = -30.0;
    constexpr double kMaxGain     = 30.0;
    constexpr double kMinQ        = 0.025;
    constexpr double kMaxQ        = 40.0;

    inline double dbToGain (double db) noexcept         { return std::exp (db * (kLn10 / 20.0)); }
    inline double gainToDb (double g) noexcept          { return 20.0 * std::log10 (std::max (g, 1.0e-30)); }
    inline double powerToDb (double p) noexcept         { return 10.0 * std::log10 (std::max (p, 1.0e-30)); }
    inline double dbToPower (double db) noexcept        { return std::exp (db * (kLn10 / 10.0)); }

    template <typename T>
    inline T clampT (T v, T lo, T hi) noexcept          { return v < lo ? lo : (v > hi ? hi : v); }

    inline double lerp (double a, double b, double t) noexcept { return a + (b - a) * t; }

    // Smooth min(x, 1) for x >= 0 (soft saturation used by the gain computers)
    inline double softUnit (double x) noexcept
    {
        if (x <= 0.0) return 0.0;
        const double x4 = x * x * x * x;
        return x / std::sqrt (std::sqrt (1.0 + x4));
    }

    // Musical helpers --------------------------------------------------------
    inline double noteToFreq (double midiNote) noexcept  { return 440.0 * std::pow (2.0, (midiNote - 69.0) / 12.0); }
    inline double freqToNote (double f) noexcept         { return 69.0 + 12.0 * std::log2 (std::max (f, 1.0e-6) / 440.0); }

    // Simple lock-free single-value exchange of a trivially copyable struct (triple buffer).
    // Writer: one thread. Reader: one thread. Reader always gets the latest complete write.
    template <typename T>
    class TripleBuffer
    {
    public:
        TripleBuffer() { state.store (packState (0, 1, 2, false)); }

        T& writeBuffer() noexcept { return buffers[(size_t) writeIndex()]; }

        void publish() noexcept
        {
            auto s = state.load (std::memory_order_relaxed);
            for (;;)
            {
                const int w = (s >> 0) & 3, m = (s >> 2) & 3, r = (s >> 4) & 3;
                const auto ns = packState (m, w, r, true);
                if (state.compare_exchange_weak (s, ns, std::memory_order_acq_rel)) break;
            }
        }

        // returns true if a new value was fetched
        bool fetch() noexcept
        {
            auto s = state.load (std::memory_order_relaxed);
            for (;;)
            {
                if (((s >> 6) & 1) == 0) return false;
                const int w = (s >> 0) & 3, m = (s >> 2) & 3, r = (s >> 4) & 3;
                const auto ns = packState (w, r, m, false);
                if (state.compare_exchange_weak (s, ns, std::memory_order_acq_rel)) return true;
            }
        }

        const T& readBuffer() const noexcept { return buffers[(size_t) ((state.load (std::memory_order_acquire) >> 4) & 3)]; }

    private:
        static uint32_t packState (int w, int m, int r, bool fresh) noexcept
        {
            return (uint32_t) w | ((uint32_t) m << 2) | ((uint32_t) r << 4) | ((fresh ? 1u : 0u) << 6);
        }
        int writeIndex() const noexcept { return (int) (state.load (std::memory_order_relaxed) & 3); }

        std::array<T, 3> buffers {};
        std::atomic<uint32_t> state;
    };
}
