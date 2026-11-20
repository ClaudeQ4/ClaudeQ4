/*
    Claude-Q 4 - registry of all plug-in instances loaded in this process.

    Used by the instance list (see and edit every instance from one window) and by the
    analyzer's collision detection (spectra of the other instances).
    All access to other instances happens on the message thread.
*/
#pragma once

#include <juce_core/juce_core.h>
#include <mutex>
#include <vector>
#include <atomic>
#include <functional>

namespace cq
{
    class ClaudeQ4AudioProcessor;

    class InstanceRegistry
    {
    public:
        static InstanceRegistry& get();

        int add (ClaudeQ4AudioProcessor* p);
        void remove (ClaudeQ4AudioProcessor* p);
        std::vector<ClaudeQ4AudioProcessor*> getAll();
        bool contains (const ClaudeQ4AudioProcessor* p);

        // calls fn for every registered instance while holding the registry lock, so no instance
        // can be destroyed meanwhile (its destructor waits in remove())
        void visit (const std::function<void (ClaudeQ4AudioProcessor&)>& fn);

        // changes whenever an instance is added / removed / renamed
        int getGeneration() const noexcept { return generation.load(); }
        void bump() noexcept { generation.fetch_add (1); }

    private:
        InstanceRegistry() = default;
        std::mutex mutex;
        std::vector<ClaudeQ4AudioProcessor*> list;
        std::atomic<int> generation { 0 };
        int nextId = 1;
    };
}
