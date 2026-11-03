#pragma once

// Real-time looper engine. Plain C++17 (no JUCE) so it can be unit-tested on any
// machine; see tests/engine_tests.cpp.
//
// Threading: prepare() runs on a non-audio thread and is the only place that
// allocates. trigger() and process() run on the audio thread only. The get*()
// methods may be called from any thread (UI).

#include <atomic>
#include <cstdint>
#include <vector>

namespace loopstation
{

enum class LoopState : int { Empty = 0, Recording, Playing, Overdubbing, Stopped, Armed };

enum class LoopAction : int { RecordDub = 0, PlayStop, Clear };
constexpr int numLoopActions = 3;

// Timing information for one trigger, at the sample the trigger happens.
struct SyncContext
{
    double samplesPerGrid = 0.0;      // > 0: sync mode, loop lengths are whole grid units
    bool   hostPlaying = false;       // host transport running: the fields below are valid
    double ppq = 0.0;                 // host position at the trigger, in quarter notes
    double bpm = 0.0;
    double samplesSinceLastLine = 0.0;
    double samplesToNextLine = 0.0;
    double lastLinePpq = 0.0, nextLinePpq = 0.0;
    double fixedLengthSamples = 0.0;  // > 0 (host playing only): record exactly this long, then overdub
};

class LooperEngine
{
public:
    static constexpr int maxChannels = 2;

    // Allocates the loop memory. Keeps the current loop if nothing changed.
    void prepare (double sampleRate, double maxSeconds);

    // Free-running: default SyncContext. Sync with the host stopped: only samplesPerGrid
    // is set, and the loop length is rounded to whole grid units. Sync with the host
    // playing: recording starts on a grid line too, and the loop locks to the host.
    void trigger (LoopAction action, const SyncContext& sync) noexcept;

    // Call at the start of each block while the host transport is playing. A loop
    // recorded against the host's grid jumps to the host's phase if it has drifted or
    // the host relocated. Nothing happens if the tempo differs from the recording tempo.
    void followHost (double ppqAtBlockStart, double bpm) noexcept;

    // Call once when the host transport stops. A loop recorded against the host grid
    // stops too. A recording is cut at the last full grid unit (or dropped if shorter
    // than one unit), and a recording waiting for the grid is cancelled. Loops recorded
    // free-running aren't affected.
    void hostStopped (double samplesPerGrid) noexcept;

    // Processes audio in place: channels[ch][startSample .. startSample + numSamples).
    // Output = input * dryGain + loop * loopGain. Gains are smoothed internally.
    void process (float* const* channels, int numChannels, int startSample, int numSamples,
                  float dryGainTarget, float loopGainTarget) noexcept;

    LoopState getState() const noexcept        { return uiState.load (std::memory_order_relaxed); }
    int64_t   getLength() const noexcept       { return uiLength.load (std::memory_order_relaxed); }
    int64_t   getPosition() const noexcept     { return uiPosition.load (std::memory_order_relaxed); }
    int64_t   getRecordedSamples() const noexcept { return uiRecorded.load (std::memory_order_relaxed); }
    bool      isFinishingToGrid() const noexcept { return uiFinishing.load (std::memory_order_relaxed); }
    bool      isFixedLength() const noexcept   { return uiFixed.load (std::memory_order_relaxed); }
    int64_t   getTargetLength() const noexcept { return uiTarget.load (std::memory_order_relaxed); }
    int64_t   getMaxLength() const noexcept    { return maxLength; }
    double    getSampleRate() const noexcept   { return sampleRate; }
    bool      isLockedToHost() const noexcept  { return uiLocked.load (std::memory_order_relaxed); }

private:
    void startRecording() noexcept;
    void startSyncedRecording (const SyncContext& sync) noexcept;
    void applyFixedLength (int64_t samples) noexcept;
    int64_t hostPhase (double ppq, double bpm) const noexcept;
    void requestFinish (LoopState next, double samplesPerGrid) noexcept;
    void finishRecording (int64_t newLength, int64_t startPosition, LoopState next) noexcept;
    void applySeamFades() noexcept;
    void publish() noexcept;

    std::vector<float> buffer[maxChannels];
    std::vector<float> preroll[maxChannels];   // the last ~250 ms of input, always recorded
    int64_t prerollLength = 0, prerollWrite = 0;
    double  sampleRate = 0.0;
    int64_t maxLength = 0;

    LoopState state = LoopState::Empty;
    int64_t length = 0;          // loop length in samples (0 = no loop)
    int64_t position = 0;        // playback / overdub position
    int64_t recorded = 0;        // samples recorded so far (while Recording)
    int64_t targetLength = 0;    // > 0 while recording on to reach the next grid line
    LoopState afterTarget = LoopState::Playing;
    int64_t armCountdown = 0;    // samples until recording starts (Armed)
    int64_t armedFixedLength = 0; // fixed length to use once an Armed recording starts
    bool fixedLength = false;     // the current recording stops by itself at targetLength
    bool locked = false;         // recorded against the host grid: follows the host
    double originPpq = 0.0;      // host position of the loop's first sample
    double lockedBpm = 0.0;
    bool clearAfterFade = false;
    bool rewindAfterFade = false;

    float playEnv = 0.0f, dubEnv = 0.0f, envStep = 1.0f;
    float dryGain = 1.0f, loopGain = 1.0f, gainCoeff = 1.0f;

    std::atomic<LoopState> uiState { LoopState::Empty };
    std::atomic<int64_t> uiLength { 0 }, uiPosition { 0 }, uiRecorded { 0 }, uiTarget { 0 };
    std::atomic<bool> uiFinishing { false }, uiLocked { false }, uiFixed { false };
};

} // namespace loopstation
