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

static SyncContext freeRunning() { return {}; }

static SyncContext gridOnly (double samplesPerGrid)
{
    SyncContext c;
    c.samplesPerGrid = samplesPerGrid;
    return c;
}

// Host playing at 150 BPM: at 1 kHz that's 400 samples per quarter note. Grid = 1 quarter.
// 'ppq' is the host position at the trigger.
static SyncContext hostAt (double ppq, double bpm = 150.0, double gridQuarters = 1.0)
{
    const double samplesPerQuarter = sr * 60.0 / bpm;
    SyncContext c;
    c.samplesPerGrid = gridQuarters * samplesPerQuarter;
    c.hostPlaying = true;
    c.ppq = ppq;
    c.bpm = bpm;
    c.lastLinePpq = std::floor (ppq / gridQuarters + 1e-9) * gridQuarters;
    c.nextLinePpq = c.lastLinePpq + gridQuarters;
    c.samplesSinceLastLine = (ppq - c.lastLinePpq) * samplesPerQuarter;
    c.samplesToNextLine = (c.nextLinePpq - ppq) * samplesPerQuarter;
    return c;
}

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
    t.e.trigger (LoopAction::RecordDub, freeRunning());
    CHECK (t.e.getState() == LoopState::Recording);
    t.run (500, 0.5f);
    t.e.trigger (LoopAction::RecordDub, freeRunning());
    CHECK (t.e.getState() == LoopState::Playing);
    CHECK (t.e.getLength() == 500);
    // Middle of the loop plays back the recorded value (dry is 0).
    t.run (250, 0.0f);
    CHECK (std::fabs (t.l.back() - 0.5f) < 1e-3f);
}

static void testSyncExtendsToGrid()
{
    Rig t;
    t.e.trigger (LoopAction::RecordDub, freeRunning());
    t.run (380, 0.25f);
    t.e.trigger (LoopAction::RecordDub, gridOnly (100.0)); // grid of 100 samples -> rounds 380 to 400
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
    t.e.trigger (LoopAction::RecordDub, freeRunning());
    t.run (430, 0.25f);
    t.e.trigger (LoopAction::RecordDub, gridOnly (100.0)); // 430 rounds down to 400
    CHECK (t.e.getState() == LoopState::Playing);
    CHECK (t.e.getLength() == 400);
    CHECK (t.e.getPosition() == 30);            // timing continues: we're 30 samples into cycle 2
}

static void testSyncMinimumOneGridUnit()
{
    Rig t;
    t.e.trigger (LoopAction::RecordDub, freeRunning());
    t.run (10, 0.25f);
    t.e.trigger (LoopAction::RecordDub, gridOnly (250.0)); // way under one unit -> one full unit
    t.run (240, 0.25f);
    CHECK (t.e.getState() == LoopState::Playing);
    CHECK (t.e.getLength() == 250);
}

static void testOverdubAdds()
{
    Rig t;
    t.e.trigger (LoopAction::RecordDub, freeRunning());
    t.run (200, 0.25f);
    t.e.trigger (LoopAction::RecordDub, freeRunning()); // -> Playing
    t.e.trigger (LoopAction::RecordDub, freeRunning()); // -> Overdubbing
    CHECK (t.e.getState() == LoopState::Overdubbing);
    t.run (400, 0.25f);                     // two full passes: +0.25 each pass (after the fade-in)
    t.e.trigger (LoopAction::RecordDub, freeRunning()); // -> Playing
    t.run (100, 0.0f);
    CHECK (t.l.back() > 0.6f && t.l.back() < 0.8f); // ~0.75
}

static void testStopRewindsAndPlayRestarts()
{
    Rig t;
    t.e.trigger (LoopAction::RecordDub, freeRunning());
    t.run (300, 0.5f);
    t.e.trigger (LoopAction::RecordDub, freeRunning());
    t.run (120, 0.0f);
    t.e.trigger (LoopAction::PlayStop, freeRunning());
    CHECK (t.e.getState() == LoopState::Stopped);
    t.run (50, 0.0f);                       // fade-out (5 samples at 1 kHz) then rewind
    CHECK (t.e.getPosition() == 0);
    CHECK (std::fabs (t.l.back()) < 1e-6f); // silent
    t.e.trigger (LoopAction::PlayStop, freeRunning());
    CHECK (t.e.getState() == LoopState::Playing);
    t.run (100, 0.0f);
    CHECK (std::fabs (t.l.back() - 0.5f) < 1e-3f);
}

static void testClear()
{
    Rig t;
    t.e.trigger (LoopAction::RecordDub, freeRunning());
    t.run (300, 0.5f);
    t.e.trigger (LoopAction::RecordDub, freeRunning());
    t.e.trigger (LoopAction::Clear, freeRunning());
    CHECK (t.e.getState() == LoopState::Empty);
    t.run (50, 0.0f);
    CHECK (t.e.getLength() == 0);
    CHECK (std::fabs (t.l.back()) < 1e-6f);
}

static void testMistapIsDiscarded()
{
    Rig t;
    t.e.trigger (LoopAction::RecordDub, freeRunning());
    t.run (10, 0.5f);                       // 10 ms < 50 ms minimum
    t.e.trigger (LoopAction::RecordDub, freeRunning());
    CHECK (t.e.getState() == LoopState::Empty);
}

static void testMaxLength()
{
    Rig t;                                   // 10 s at 1 kHz = 10000 samples
    t.e.trigger (LoopAction::RecordDub, freeRunning());
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


static void testHostSyncWaitsForNextLine()
{
    Rig t;
    t.e.trigger (LoopAction::RecordDub, hostAt (2.5));     // halfway through a beat: 200 samples to go
    CHECK (t.e.getState() == LoopState::Armed);
    t.run (200, 0.5f);
    CHECK (t.e.getState() == LoopState::Armed);
    t.run (1, 0.5f);                                       // sample 200 is on the line
    CHECK (t.e.getState() == LoopState::Recording);
    CHECK (t.e.getRecordedSamples() == 1);
    t.run (799, 0.5f);                                     // now 2 beats recorded
    t.e.trigger (LoopAction::RecordDub, hostAt (5.0));
    CHECK (t.e.getState() == LoopState::Playing);
    CHECK (t.e.getLength() == 800);
    CHECK (t.e.isLockedToHost());
}

static void testHostSyncLatePressUsesPreroll()
{
    Rig t;
    t.run (100, 0.7f);                                     // what was played around the line
    t.e.trigger (LoopAction::RecordDub, hostAt (4.0 + 20.0 / 400.0)); // 20 samples late
    CHECK (t.e.getState() == LoopState::Recording);
    CHECK (t.e.getRecordedSamples() == 20);
    t.run (380, 0.1f);
    t.e.trigger (LoopAction::RecordDub, hostAt (5.0));
    CHECK (t.e.getState() == LoopState::Playing);
    CHECK (t.e.getLength() == 400);
    t.run (10, 0.0f);                                      // loop samples 0..9 come from the pre-roll
    CHECK (std::fabs (t.l.back() - 0.7f) < 1e-3f);
    t.run (100, 0.0f);
    CHECK (std::fabs (t.l.back() - 0.1f) < 1e-3f);
}

static void testArmedCanBeCancelled()
{
    Rig t;
    t.e.trigger (LoopAction::RecordDub, hostAt (2.5));
    t.e.trigger (LoopAction::RecordDub, hostAt (2.6));
    CHECK (t.e.getState() == LoopState::Empty);
    t.e.trigger (LoopAction::RecordDub, hostAt (2.5));
    t.e.trigger (LoopAction::Clear, hostAt (2.6));
    CHECK (t.e.getState() == LoopState::Empty);
    t.run (500, 0.5f);
    CHECK (t.e.getState() == LoopState::Empty);
}

static LooperEngine* recordLockedLoop (Rig& t)
{
    // One bar of 4/4 (4 quarters = 1600 samples), starting exactly on beat 8.
    t.e.trigger (LoopAction::RecordDub, hostAt (8.0, 150.0, 4.0));
    t.run (1600, 0.5f);
    t.e.trigger (LoopAction::RecordDub, hostAt (12.0, 150.0, 4.0));
    return &t.e;
}

static void testFollowHostRelocates()
{
    Rig t;
    recordLockedLoop (t);
    CHECK (t.e.getState() == LoopState::Playing);
    CHECK (t.e.getLength() == 1600);
    CHECK (t.e.getPosition() == 0);

    t.e.followHost (13.0, 150.0);                          // host jumps to 1 beat into a bar
    CHECK (t.e.getPosition() == 400);
    t.e.followHost (13.0 + 0.5 / 400.0, 150.0);            // tiny drift: leave it alone
    CHECK (t.e.getPosition() == 400);
    t.e.followHost (7.5, 150.0);                           // before the origin wraps correctly
    CHECK (t.e.getPosition() == 1400);
    t.e.followHost (13.0, 120.0);                          // different tempo: don't follow
    CHECK (t.e.getPosition() == 1400);
}

static void testStoppedLockedLoopRestartsInPhase()
{
    Rig t;
    recordLockedLoop (t);
    t.e.trigger (LoopAction::PlayStop, hostAt (12.5, 150.0, 4.0));
    t.run (50, 0.0f);
    t.e.trigger (LoopAction::PlayStop, hostAt (14.0, 150.0, 4.0)); // two beats into a bar
    CHECK (t.e.getState() == LoopState::Playing);
    CHECK (t.e.getPosition() == 800);

    // Without the host, the same loop restarts from the top.
    t.e.trigger (LoopAction::PlayStop, gridOnly (1600));
    t.run (50, 0.0f);
    t.e.trigger (LoopAction::PlayStop, gridOnly (1600));
    CHECK (t.e.getPosition() == 0);
}

static void testUnlockedLoopIgnoresHost()
{
    Rig t;
    t.e.trigger (LoopAction::RecordDub, gridOnly (400));   // host stopped: starts immediately
    CHECK (t.e.getState() == LoopState::Recording);
    t.run (400, 0.5f);
    t.e.trigger (LoopAction::RecordDub, gridOnly (400));
    CHECK (! t.e.isLockedToHost());
    t.run (100, 0.0f);
    const auto before = t.e.getPosition();
    t.e.followHost (13.0, 150.0);
    CHECK (t.e.getPosition() == before);
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
    testHostSyncWaitsForNextLine();
    testHostSyncLatePressUsesPreroll();
    testArmedCanBeCancelled();
    testFollowHostRelocates();
    testStoppedLockedLoopRestartsInPhase();
    testUnlockedLoopIgnoresHost();

    if (failures == 0)
        std::printf ("All engine tests passed\n");
    return failures == 0 ? 0 : 1;
}
