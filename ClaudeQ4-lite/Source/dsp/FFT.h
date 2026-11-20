/*
    Claude-Q 4 - FFT wrapper (pffft, SIMD accelerated) and aligned buffers
*/
#pragma once

#include <cstddef>
#include <cstring>
#include <utility>

extern "C"
{
    #include "../../ThirdParty/pffft/include/pffft.h"
}

namespace cq
{
    class AlignedFloats
    {
    public:
        AlignedFloats() = default;
        explicit AlignedFloats (size_t n) { allocate (n); }
        ~AlignedFloats() { release(); }
        AlignedFloats (const AlignedFloats&) = delete;
        AlignedFloats& operator= (const AlignedFloats&) = delete;
        AlignedFloats (AlignedFloats&& o) noexcept : ptr (o.ptr), count (o.count) { o.ptr = nullptr; o.count = 0; }
        AlignedFloats& operator= (AlignedFloats&& o) noexcept
        {
            if (this != &o) { release(); ptr = o.ptr; count = o.count; o.ptr = nullptr; o.count = 0; }
            return *this;
        }

        void allocate (size_t n)
        {
            release();
            if (n == 0) return;
            ptr = static_cast<float*> (pffft_aligned_malloc (n * sizeof (float)));
            count = ptr != nullptr ? n : 0;
            clear();
        }
        void release() noexcept { if (ptr != nullptr) pffft_aligned_free (ptr); ptr = nullptr; count = 0; }
        void clear() noexcept { if (ptr != nullptr) std::memset (ptr, 0, count * sizeof (float)); }

        float* data() noexcept             { return ptr; }
        const float* data() const noexcept { return ptr; }
        size_t size() const noexcept       { return count; }
        float& operator[] (size_t i) noexcept       { return ptr[i]; }
        const float& operator[] (size_t i) const noexcept { return ptr[i]; }

    private:
        float* ptr = nullptr;
        size_t count = 0;
    };

    // Real FFT of size N (power of two, >= 32)
    class RealFFT
    {
    public:
        RealFFT() = default;
        explicit RealFFT (int n) { init (n); }
        ~RealFFT() { if (setup != nullptr) pffft_destroy_setup (setup); }
        RealFFT (const RealFFT&) = delete;
        RealFFT& operator= (const RealFFT&) = delete;

        void init (int n)
        {
            if (setup != nullptr) { pffft_destroy_setup (setup); setup = nullptr; }
            size = n;
            setup = pffft_new_setup (n, PFFFT_REAL);
            work.allocate ((size_t) n);
        }
        int getSize() const noexcept { return size; }

        // unordered (internal) layout - for fast convolution
        void forward (const float* in, float* out) noexcept  { pffft_transform (setup, in, out, work.data(), PFFFT_FORWARD); }
        void inverse (const float* in, float* out) noexcept  { pffft_transform (setup, in, out, work.data(), PFFFT_BACKWARD); }
        // ordered layout: out[0] = DC, out[1] = Nyquist, then (re, im) pairs for bins 1 .. N/2-1
        void forwardOrdered (const float* in, float* out) noexcept { pffft_transform_ordered (setup, in, out, work.data(), PFFFT_FORWARD); }
        void inverseOrdered (const float* in, float* out) noexcept { pffft_transform_ordered (setup, in, out, work.data(), PFFFT_BACKWARD); }
        // ab += a * b * scale   (unordered layout)
        void convolveAccumulate (const float* a, const float* b, float* ab, float scale) noexcept { pffft_zconvolve_accumulate (setup, a, b, ab, scale); }

    private:
        PFFFT_Setup* setup = nullptr;
        AlignedFloats work;
        int size = 0;
    };
}
