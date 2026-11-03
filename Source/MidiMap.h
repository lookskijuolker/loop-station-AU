#pragma once

// Maps incoming MIDI notes / CCs to looper actions, with MIDI learn.
// Plain C++17 (no JUCE) so it can be unit-tested; see tests/engine_tests.cpp.
//
// Threading: handleMessage() runs on the audio thread. get/set/startLearn/
// cancelLearn/getLearnTarget may be called from any thread (all state is atomic,
// except the CC edge detection, which only the audio thread touches).

#include <atomic>
#include <cstdint>
#include <string>

#include "LooperEngine.h"

namespace loopstation
{

struct MidiBinding
{
    enum class Type : uint8_t { None = 0, Note = 1, CC = 2 };

    Type type = Type::None;
    uint8_t number = 0; // note number or CC number, 0..127. Any MIDI channel matches.

    uint32_t pack() const noexcept { return (static_cast<uint32_t> (type) << 8) | number; }
    static MidiBinding unpack (uint32_t v) noexcept;
    bool operator== (const MidiBinding& o) const noexcept { return type == o.type && number == o.number; }

    // "Note C1", "CC 64" or "-". Note names follow Logic: note 60 = C3.
    std::string describe() const;
};

class MidiMap
{
public:
    MidiMap() noexcept;

    // Audio thread. Returns the action to trigger, or -1. While learning, the first
    // note-on (or CC going >= 64) is bound to the learn target and consumed.
    int handleMessage (uint8_t status, uint8_t data1, uint8_t data2) noexcept;

    MidiBinding get (int action) const noexcept;
    void set (int action, MidiBinding binding) noexcept; // also unbinds duplicates

    void startLearn (int action) noexcept  { learnTarget.store (action); }
    void cancelLearn() noexcept            { learnTarget.store (-1); }
    int  getLearnTarget() const noexcept   { return learnTarget.load(); }

    static MidiBinding defaultBinding (int action) noexcept;

private:
    std::atomic<uint32_t> bindings[numLoopActions];
    std::atomic<int> learnTarget { -1 };
    bool ccHigh[numLoopActions] {};
};

} // namespace loopstation
