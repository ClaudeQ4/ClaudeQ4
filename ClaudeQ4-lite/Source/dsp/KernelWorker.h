/*
    Claude-Q 4 - shared background worker

    One thread for all plug-in instances.  It builds linear phase / natural phase kernels
    and allocates processing configurations, so the audio thread never allocates and never
    spends milliseconds on FFT kernel design.  The thread exists only while at least one
    engine is registered.
*/
#pragma once

#include <mutex>
#include <thread>
#include <condition_variable>
#include <vector>
#include <atomic>

namespace cq
{
    class BackgroundClient
    {
    public:
        virtual ~BackgroundClient() = default;
        virtual void doBackgroundWork() = 0;
        std::mutex workMutex;   // held while the client's background work runs
    };

    class KernelWorker
    {
    public:
        static void add (BackgroundClient* c);
        static void remove (BackgroundClient* c);
        static void wake();

    private:
        KernelWorker() = default;
        void run();

        static std::mutex& registryMutex();
        static KernelWorker*& instance();

        std::thread thread;
        std::vector<BackgroundClient*> clients;
        std::mutex clientsMutex;
        std::condition_variable cv;
        std::mutex cvMutex;
        std::atomic<bool> quit { false };
        std::atomic<bool> pending { false };
    };
}
