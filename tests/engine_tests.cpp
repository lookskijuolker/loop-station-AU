// Unit tests for the looper engine and MIDI map. No JUCE, no test framework:
//   c++ -std=c++17 -I Source Source/LooperEngine.cpp Source/MidiMap.cpp tests/engine_tests.cpp -o engine_tests && ./engine_tests
#include "LooperEngine.h"
#include "MidiMap.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace loopstation;

static int failures = 0;
#define CHECK(cond) do { if (! (cond)) { std::printf ("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

constexpr double sr = 1000.0; // small sample rate keeps the numbers readable

struct Rig
{
    LooperEngine e;
    std::vector<float> l, r;
    Rig() { e.prepare (sr, 10.0); }

    // Runs n samples with a constant input value; returns the left output of the last sample.
    float run (int n, float in, float dry = 0.0f, float loop = 1.0f)
    {
        l.assign (static_cast<size_t> (n), in);
        r.assign (static_cast<size_t> (n), in);
        float* ch[] = { l.data(), r.data() };
        e.process (ch, 2, 0, n, dry, loop);
        return l.back();
    }
};

static void testFreeRecordAndPlay()
{
    Rig t;
    t.run (100, 0.0f);                 // let the gain smoothing settle
    t.e.trigger (LoopAction::RecordDub, 0);
    CHECK (t.e.getState() == LoopState::Recording);
    t.run (500, 0.5f);
    t.e.trigger (LoopAction::RecordDub, 0);
    CHECK (t.e.getState() == LoopState::Playing);
    CHECK (t.e.getLength() == 500);
    // Middle of the loop plays back the recorded value (dry is 0).
    t.run (250, 0.0f);
    CHECK (std::fabs (t.l.back() - 0.5f) < 1e-3f);
}

static void testSyncExtendsToGrid()
{
    Rig t;
    t.e.trigger (LoopAction::RecordDub, 0);
    t.run (380, 0.25f);
    t.e.trigger (LoopAction::RecordDub, 100.0); // grid of 100 samples -> rounds 380 to 400
    CHECK (t.e.getState() == LoopState::Recording);
    CHECK (t.e.isFinishingToGrid());
    t.run (19, 0.25f);
    CHECK (t.e.getState() == LoopState::Recording);
    t.run (1, 0.25f);
    CHECK (t.e.getState() == LoopState::Playing);
    CHECK (t.e.getLength() == 400);
    CHECK (t.e.getPosition() == 0);
}

static void testSyncTruncatesToGrid()
{
    Rig t;
    t.e.trigger (LoopAction::RecordDub, 0);
    t.run (430, 0.25f);
    t.e.trigger (LoopAction::RecordDub, 100.0); // 430 rounds down to 400
    CHECK (t.e.getState() == LoopState::Playing);
    CHECK (t.e.getLength() == 400);
    CHECK (t.e.getPosition() == 30);            // timing continues: we're 30 samples into cycle 2
}

static void testSyncMinimumOneGridUnit()
{
    Rig t;
    t.e.trigger (LoopAction::RecordDub, 0);
    t.run (10, 0.25f);
    t.e.trigger (LoopAction::RecordDub, 250.0); // way under one unit -> one full unit
    t.run (240, 0.25f);
    CHECK (t.e.getState() == LoopState::Playing);
    CHECK (t.e.getLength() == 250);
}

static void testOverdubAdds()
{
    Rig t;
    t.e.trigger (LoopAction::RecordDub, 0);
    t.run (200, 0.25f);
    t.e.trigger (LoopAction::RecordDub, 0); // -> Playing
    t.e.trigger (LoopAction::RecordDub, 0); // -> Overdubbing
    CHECK (t.e.getState() == LoopState::Overdubbing);
    t.run (400, 0.25f);                     // two full passes: +0.25 each pass (after the fade-in)
    t.e.trigger (LoopAction::RecordDub, 0); // -> Playing
    t.run (100, 0.0f);
    CHECK (t.l.back() > 0.6f && t.l.back() < 0.8f); // ~0.75
}

static void testStopRewindsAndPlayRestarts()
{
    Rig t;
    t.e.trigger (LoopAction::RecordDub, 0);
    t.run (300, 0.5f);
    t.e.trigger (LoopAction::RecordDub, 0);
    t.run (120, 0.0f);
    t.e.trigger (LoopAction::PlayStop, 0);
    CHECK (t.e.getState() == LoopState::Stopped);
    t.run (50, 0.0f);                       // fade-out (5 samples at 1 kHz) then rewind
    CHECK (t.e.getPosition() == 0);
    CHECK (std::fabs (t.l.back()) < 1e-6f); // silent
    t.e.trigger (LoopAction::PlayStop, 0);
    CHECK (t.e.getState() == LoopState::Playing);
    t.run (100, 0.0f);
    CHECK (std::fabs (t.l.back() - 0.5f) < 1e-3f);
}

static void testClear()
{
    Rig t;
    t.e.trigger (LoopAction::RecordDub, 0);
    t.run (300, 0.5f);
    t.e.trigger (LoopAction::RecordDub, 0);
    t.e.trigger (LoopAction::Clear, 0);
    CHECK (t.e.getState() == LoopState::Empty);
    t.run (50, 0.0f);
    CHECK (t.e.getLength() == 0);
    CHECK (std::fabs (t.l.back()) < 1e-6f);
}

static void testMistapIsDiscarded()
{
    Rig t;
    t.e.trigger (LoopAction::RecordDub, 0);
    t.run (10, 0.5f);                       // 10 ms < 50 ms minimum
    t.e.trigger (LoopAction::RecordDub, 0);
    CHECK (t.e.getState() == LoopState::Empty);
}

static void testMaxLength()
{
    Rig t;                                   // 10 s at 1 kHz = 10000 samples
    t.e.trigger (LoopAction::RecordDub, 0);
    t.run (10000, 0.5f);
    CHECK (t.e.getState() == LoopState::Playing);
    CHECK (t.e.getLength() == 10000);
}

static void testDryPassesThrough()
{
    Rig t;
    t.run (2000, 0.3f, 1.0f, 1.0f);
    CHECK (std::fabs (t.l.back() - 0.3f) < 1e-3f);
}

static void testMidiDefaultsAndLearn()
{
    MidiMap m;
    CHECK (m.handleMessage (0x90, 36, 100) == 0);   // C1 -> Rec/Dub
    CHECK (m.handleMessage (0x91, 38, 100) == 1);   // any channel
    CHECK (m.handleMessage (0x90, 38, 0) == -1);    // note-on velocity 0 is a note-off
    CHECK (m.handleMessage (0x80, 40, 0) == -1);    // note-off

    m.startLearn (2);
    CHECK (m.handleMessage (0xb0, 64, 127) == -1);  // learned (sustain pedal), consumed
    CHECK (m.getLearnTarget() == -1);
    CHECK (m.get (2).type == MidiBinding::Type::CC && m.get (2).number == 64);
    CHECK (m.handleMessage (0xb0, 64, 0) == -1);    // release
    CHECK (m.handleMessage (0xb0, 64, 127) == 2);   // press -> Clear
    CHECK (m.handleMessage (0xb0, 64, 127) == -1);  // held: no repeat
    CHECK (m.handleMessage (0xb0, 64, 0) == -1);

    m.startLearn (1);
    m.handleMessage (0x90, 36, 90);                 // steal C1 from Rec/Dub
    CHECK (m.get (1).number == 36);
    CHECK (m.get (0).type == MidiBinding::Type::None);
    CHECK (m.handleMessage (0x90, 36, 90) == 1);

    CHECK (MidiMap::defaultBinding (0).describe() == "Note C1");
    MidiBinding b; b.type = MidiBinding::Type::Note; b.number = 60;
    CHECK (b.describe() == "Note C3");
    CHECK (MidiBinding::unpack (b.pack()) == b);
}

int main()
{
    testFreeRecordAndPlay();
    testSyncExtendsToGrid();
    testSyncTruncatesToGrid();
    testSyncMinimumOneGridUnit();
    testOverdubAdds();
    testStopRewindsAndPlayRestarts();
    testClear();
    testMistapIsDiscarded();
    testMaxLength();
    testDryPassesThrough();
    testMidiDefaultsAndLearn();

    if (failures == 0)
        std::printf ("All engine tests passed\n");
    return failures == 0 ? 0 : 1;
}
