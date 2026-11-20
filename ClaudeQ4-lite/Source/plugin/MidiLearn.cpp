/*
    Claude-Q 4 - MIDI learn
*/
#include "MidiLearn.h"

namespace cq
{
    MidiLearn::MidiLearn()
    {
        for (auto& m : map) m.store (-1);
    }

    void MidiLearn::clearAll() noexcept
    {
        for (auto& m : map) m.store (-1);
    }

    void MidiLearn::removeParameter (int paramIndex) noexcept
    {
        for (auto& m : map)
        {
            int expected = paramIndex;
            m.compare_exchange_strong (expected, -1);
        }
    }

    std::vector<MidiLearn::Mapping> MidiLearn::getMappings() const
    {
        std::vector<Mapping> out;
        for (int s = 0; s < kSlots; ++s)
        {
            const int p = map[(size_t) s].load();
            if (p >= 0) out.push_back ({ s / 128, s % 128, p });
        }
        return out;
    }

    juce::String MidiLearn::describe (int paramIndex) const
    {
        for (int s = 0; s < kSlots; ++s)
            if (map[(size_t) s].load() == paramIndex)
                return "CC " + juce::String (s % 128) + (s / 128 == 0 ? juce::String() : " (ch " + juce::String (s / 128) + ")");
        return {};
    }

    void MidiLearn::process (const juce::MidiBuffer& midi) noexcept
    {
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (! msg.isController()) continue;
            const int cc = msg.getControllerNumber();
            const int ch = juce::jlimit (1, 16, msg.getChannel());
            const float v = (float) msg.getControllerValue() / 127.0f;

            if (learning.load (std::memory_order_relaxed))
            {
                const int a = armed.load (std::memory_order_relaxed);
                if (a >= 0)
                {
                    // one CC controls one parameter: remove older bindings of this parameter and CC
                    for (auto& m : map) { int e = a; m.compare_exchange_strong (e, -1); }
                    map[(size_t) cc].store (a);      // omni binding
                    for (int c = 1; c <= 16; ++c) map[(size_t) (c * 128 + cc)].store (-1);
                    armed.store (-1);
                    learnedEvent.store (a);
                }
            }

            int p = map[(size_t) (ch * 128 + cc)].load (std::memory_order_relaxed);
            if (p < 0) p = map[(size_t) cc].load (std::memory_order_relaxed);
            if (p < 0) continue;
            int s1, n1, s2, n2;
            fifo.prepareToWrite (1, s1, n1, s2, n2);
            if (n1 > 0) queue[(size_t) s1] = { p, v };
            fifo.finishedWrite (n1);
        }
    }

    void MidiLearn::dispatch (juce::AudioProcessor& proc)
    {
        const auto& params = proc.getParameters();
        int s1, n1, s2, n2;
        const int ready = fifo.getNumReady();
        if (ready <= 0) return;
        fifo.prepareToRead (ready, s1, n1, s2, n2);
        // keep only the latest value per parameter
        std::vector<std::pair<int, float>> latest;
        auto addValue = [&latest] (std::pair<int, float> e)
        {
            for (auto& l : latest) if (l.first == e.first) { l.second = e.second; return; }
            latest.push_back (e);
        };
        for (int i = 0; i < n1; ++i) addValue (queue[(size_t) (s1 + i)]);
        for (int i = 0; i < n2; ++i) addValue (queue[(size_t) (s2 + i)]);
        fifo.finishedRead (n1 + n2);
        for (const auto& [idx, v] : latest)
        {
            if (idx < 0 || idx >= params.size()) continue;
            auto* p = params[idx];
            float value = v;
            // discrete parameters: spread the CC range over the steps
            if (p->isDiscrete() || p->isBoolean())
            {
                const int steps = std::max (2, p->getNumSteps());
                value = (float) juce::roundToInt (v * (float) (steps - 1)) / (float) (steps - 1);
            }
            if (std::abs (p->getValue() - value) < 1.0e-6f) continue;
            p->beginChangeGesture();
            p->setValueNotifyingHost (value);
            p->endChangeGesture();
        }
    }

    juce::ValueTree MidiLearn::toTree() const
    {
        juce::ValueTree t ("MIDI");
        for (const auto& m : getMappings())
        {
            juce::ValueTree c ("MAP");
            c.setProperty ("ch", m.channel, nullptr);
            c.setProperty ("cc", m.cc, nullptr);
            c.setProperty ("param", m.paramIndex, nullptr);
            t.appendChild (c, nullptr);
        }
        return t;
    }

    void MidiLearn::fromTree (const juce::ValueTree& t)
    {
        clearAll();
        if (! t.isValid()) return;
        for (const auto& c : t)
        {
            const int ch = juce::jlimit (0, 16, (int) c.getProperty ("ch", 0));
            const int cc = juce::jlimit (0, 127, (int) c.getProperty ("cc", 0));
            const int p = (int) c.getProperty ("param", -1);
            if (p >= 0) map[(size_t) (ch * 128 + cc)].store (p);
        }
    }
}
