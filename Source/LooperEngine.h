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

enum class LoopState : int { Empty = 0, Recording, Playing, Overdubbing, Stopped };

enum class LoopAction : int { RecordDub = 0, PlayStop, Clear };
constexpr int numLoopActions = 3;

class LooperEngine
{
public:
    static constexpr int maxChannels = 2;

    // Allocates the loop memory. Keeps the current loop if nothing changed.
    void prepare (double sampleRate, double maxSeconds);

    // samplesPerGrid > 0: sync mode, the loop length is rounded to a whole number of
    // grid units when recording ends. samplesPerGrid <= 0: free-running.
    void trigger (LoopAction action, double samplesPerGrid) noexcept;

    // Processes audio in place: channels[ch][startSample .. startSample + numSamples).
    // Output = input * dryGain + loop * loopGain. Gains are smoothed internally.
    void process (float* const* channels, int numChannels, int startSample, int numSamples,
                  float dryGainTarget, float loopGainTarget) noexcept;

    LoopState getState() const noexcept        { return uiState.load (std::memory_order_relaxed); }
    int64_t   getLength() const noexcept       { return uiLength.load (std::memory_order_relaxed); }
    int64_t   getPosition() const noexcept     { return uiPosition.load (std::memory_order_relaxed); }
    int64_t   getRecordedSamples() const noexcept { return uiRecorded.load (std::memory_order_relaxed); }
    bool      isFinishingToGrid() const noexcept { return uiFinishing.load (std::memory_order_relaxed); }
    int64_t   getMaxLength() const noexcept    { return maxLength; }
    double    getSampleRate() const noexcept   { return sampleRate; }

private:
    void startRecording() noexcept;
    void requestFinish (LoopState next, double samplesPerGrid) noexcept;
    void finishRecording (int64_t newLength, int64_t startPosition, LoopState next) noexcept;
    void applySeamFades() noexcept;
    void publish() noexcept;

    std::vector<float> buffer[maxChannels];
    double  sampleRate = 0.0;
    int64_t maxLength = 0;

    LoopState state = LoopState::Empty;
    int64_t length = 0;          // loop length in samples (0 = no loop)
    int64_t position = 0;        // playback / overdub position
    int64_t recorded = 0;        // samples recorded so far (while Recording)
    int64_t targetLength = 0;    // > 0 while recording on to reach the next grid line
    LoopState afterTarget = LoopState::Playing;
    bool clearAfterFade = false;
    bool rewindAfterFade = false;

    float playEnv = 0.0f, dubEnv = 0.0f, envStep = 1.0f;
    float dryGain = 1.0f, loopGain = 1.0f, gainCoeff = 1.0f;

    std::atomic<LoopState> uiState { LoopState::Empty };
    std::atomic<int64_t> uiLength { 0 }, uiPosition { 0 }, uiRecorded { 0 };
    std::atomic<bool> uiFinishing { false };
};

} // namespace loopstation
