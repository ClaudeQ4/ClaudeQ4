/*
    Claude-Q 4 - instance registry
*/
#include "InstanceRegistry.h"
#include <algorithm>

namespace cq
{
    InstanceRegistry& InstanceRegistry::get()
    {
        static InstanceRegistry r;
        return r;
    }

    int InstanceRegistry::add (ClaudeQ4AudioProcessor* p)
    {
        std::lock_guard<std::mutex> lk (mutex);
        list.push_back (p);
        generation.fetch_add (1);
        return nextId++;
    }

    void InstanceRegistry::remove (ClaudeQ4AudioProcessor* p)
    {
        std::lock_guard<std::mutex> lk (mutex);
        list.erase (std::remove (list.begin(), list.end(), p), list.end());
        generation.fetch_add (1);
    }

    std::vector<ClaudeQ4AudioProcessor*> InstanceRegistry::getAll()
    {
        std::lock_guard<std::mutex> lk (mutex);
        return list;
    }

    void InstanceRegistry::visit (const std::function<void (ClaudeQ4AudioProcessor&)>& fn)
    {
        std::lock_guard<std::mutex> lk (mutex);
        for (auto* p : list) fn (*p);
    }

    bool InstanceRegistry::contains (const ClaudeQ4AudioProcessor* p)
    {
        std::lock_guard<std::mutex> lk (mutex);
        return std::find (list.begin(), list.end(), p) != list.end();
    }
}
