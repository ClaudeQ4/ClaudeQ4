/*
    Claude-Q 4 - shared background worker
*/
#include "KernelWorker.h"
#include <algorithm>
#include <chrono>

namespace cq
{
    std::mutex& KernelWorker::registryMutex()
    {
        static std::mutex m;
        return m;
    }

    KernelWorker*& KernelWorker::instance()
    {
        static KernelWorker* w = nullptr;
        return w;
    }

    void KernelWorker::add (BackgroundClient* c)
    {
        std::lock_guard<std::mutex> lg (registryMutex());
        auto*& w = instance();
        if (w == nullptr)
        {
            w = new KernelWorker();
            w->quit.store (false);
            w->thread = std::thread ([w] { w->run(); });
        }
        std::lock_guard<std::mutex> cl (w->clientsMutex);
        w->clients.push_back (c);
    }

    void KernelWorker::remove (BackgroundClient* c)
    {
        std::lock_guard<std::mutex> lg (registryMutex());
        auto*& w = instance();
        if (w == nullptr) return;
        {
            std::lock_guard<std::mutex> cl (w->clientsMutex);
            w->clients.erase (std::remove (w->clients.begin(), w->clients.end(), c), w->clients.end());
        }
        // make sure the worker is not inside this client's work any more
        { std::lock_guard<std::mutex> wl (c->workMutex); }

        bool empty;
        {
            std::lock_guard<std::mutex> cl (w->clientsMutex);
            empty = w->clients.empty();
        }
        if (empty)
        {
            w->quit.store (true);
            w->cv.notify_all();
            if (w->thread.joinable()) w->thread.join();
            delete w;
            w = nullptr;
        }
    }

    void KernelWorker::wake()
    {
        // called from the message thread only (never from the audio thread)
        std::lock_guard<std::mutex> lg (registryMutex());
        if (auto* w = instance())
        {
            w->pending.store (true);
            w->cv.notify_all();
        }
    }

    void KernelWorker::run()
    {
        while (! quit.load())
        {
            std::vector<BackgroundClient*> snapshot;
            {
                std::lock_guard<std::mutex> cl (clientsMutex);
                snapshot = clients;
            }
            for (auto* c : snapshot)
            {
                if (quit.load()) break;
                // the client may have been removed meanwhile: check membership under the lock and
                // take its work mutex before releasing the registry lock
                std::unique_lock<std::mutex> wl;
                {
                    std::lock_guard<std::mutex> cl (clientsMutex);
                    if (std::find (clients.begin(), clients.end(), c) == clients.end()) continue;
                    wl = std::unique_lock<std::mutex> (c->workMutex, std::try_to_lock);
                }
                if (! wl.owns_lock()) continue;
                c->doBackgroundWork();
            }
            std::unique_lock<std::mutex> lk (cvMutex);
            cv.wait_for (lk, std::chrono::milliseconds (4), [this] { return quit.load() || pending.load(); });
            pending.store (false);
        }
    }
}
