/*
    Claude-Q 4 - MIDI learn

    Any automatable parameter can be bound to a MIDI CC (per channel or omni).
    Audio thread: CC messages are looked up in a lock-free table and queued.
    Message thread: queued values are applied to the parameters (host notified, with
    gestures), at 60 Hz.  Learn mode: the next CC that arrives binds the armed parameter.
*/
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace cq
{
    class MidiLearn
    {
    public:
        struct Mapping { int channel = 0; int cc = 0; int paramIndex = -1; };   // channel 0 = omni

        MidiLearn();

        // message thread
        void setLearning (bool b) noexcept { learning.store (b); if (! b) armed.store (-1); }
        bool isLearning() const noexcept   { return learning.load(); }
        void arm (int paramIndex) noexcept { armed.store (paramIndex); }
        int  getArmed() const noexcept     { return armed.load(); }
        void clearAll() noexcept;
        void removeParameter (int paramIndex) noexcept;
        std::vector<Mapping> getMappings() const;
        juce::String describe (int paramIndex) const;     // "CC 74 (ch 1)" or empty
        void dispatch (juce::AudioProcessor& proc);       // apply queued values
        int  takeLearnEvent() noexcept { return learnedEvent.exchange (-1); }

        juce::ValueTree toTree() const;
        void fromTree (const juce::ValueTree& t);

        // audio thread
        void process (const juce::MidiBuffer& midi) noexcept;

    private:
        static constexpr int kSlots = 17 * 128;   // [omni, ch1..16] x CC
        std::array<std::atomic<int>, kSlots> map;
        std::atomic<bool> learning { false };
        std::atomic<int> armed { -1 };
        std::atomic<int> learnedEvent { -1 };

        juce::AbstractFifo fifo { 2048 };
        std::array<std::pair<int, float>, 2048> queue {};
    };
}
