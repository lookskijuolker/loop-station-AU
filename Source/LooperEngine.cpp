#include "LooperEngine.h"

#include <algorithm>
#include <cmath>

namespace loopstation
{

namespace
{
    constexpr double fadeSeconds     = 0.005;  // start/stop/overdub ramps
    constexpr double seamFadeSeconds = 0.002;  // fade at the loop's start and end
    constexpr double gainSeconds     = 0.02;   // level knob smoothing
    constexpr double minLoopSeconds  = 0.05;   // shorter "loops" are treated as a mis-tap
    constexpr double prerollSeconds  = 0.25;   // how late a synced Rec press may be
    constexpr double lateFraction    = 0.25;   // ... as a fraction of one grid unit
    constexpr double resyncSeconds   = 0.002;  // drift allowed before snapping to the host
    constexpr double bpmTolerance    = 0.01;

    inline float moveTowards (float value, float target, float step) noexcept
    {
        if (value < target) return std::min (target, value + step);
        if (value > target) return std::max (target, value - step);
        return value;
    }
}

void LooperEngine::prepare (double newSampleRate, double maxSeconds)
{
    const auto newMax = static_cast<int64_t> (std::ceil (newSampleRate * maxSeconds));

    if (newSampleRate == sampleRate && newMax == maxLength)
        return; // keep the current loop

    sampleRate = newSampleRate;
    maxLength = newMax;

    for (auto& b : buffer)
        b.assign (static_cast<size_t> (maxLength), 0.0f);

    prerollLength = std::max<int64_t> (1, static_cast<int64_t> (std::ceil (prerollSeconds * sampleRate)));
    prerollWrite = 0;
    for (auto& b : preroll)
        b.assign (static_cast<size_t> (prerollLength), 0.0f);

    envStep   = static_cast<float> (1.0 / std::max (1.0, fadeSeconds * sampleRate));
    gainCoeff = static_cast<float> (1.0 - std::exp (-1.0 / (gainSeconds * sampleRate)));

    state = LoopState::Empty;
    length = position = recorded = targetLength = armCountdown = 0;
    clearAfterFade = rewindAfterFade = locked = false;
    playEnv = dubEnv = 0.0f;
    publish();
}

void LooperEngine::startRecording() noexcept
{
    length = position = recorded = targetLength = armCountdown = 0;
    clearAfterFade = rewindAfterFade = false;
    playEnv = dubEnv = 0.0f;
    state = LoopState::Recording;
}

void LooperEngine::startSyncedRecording (const SyncContext& sync) noexcept
{
    const double lateLimit = std::min (lateFraction * sync.samplesPerGrid,
                                       static_cast<double> (prerollLength - 1));
    lockedBpm = sync.bpm;
    locked = true;

    if (sync.samplesSinceLastLine <= lateLimit)
    {
        // Pressed just after a grid line: start at that line, taking the audio between
        // the line and the press from the pre-roll buffer.
        startRecording();
        originPpq = sync.lastLinePpq;

        const auto late = static_cast<int64_t> (std::llround (sync.samplesSinceLastLine));
        for (int ch = 0; ch < maxChannels; ++ch)
            for (int64_t i = 0; i < late; ++i)
                buffer[ch][static_cast<size_t> (i)] =
                    preroll[ch][static_cast<size_t> ((prerollWrite + prerollLength - late + i) % prerollLength)];
        recorded = late;
        return;
    }

    // Otherwise wait for the next grid line.
    length = position = recorded = targetLength = 0;
    clearAfterFade = rewindAfterFade = false;
    playEnv = dubEnv = 0.0f;
    originPpq = sync.nextLinePpq;
    armCountdown = std::max<int64_t> (0, static_cast<int64_t> (std::llround (sync.samplesToNextLine)));
    state = LoopState::Armed;
}

int64_t LooperEngine::hostPhase (double ppq, double bpm) const noexcept
{
    const double samplesPerQuarter = sampleRate * 60.0 / bpm;
    double phase = std::fmod ((ppq - originPpq) * samplesPerQuarter, static_cast<double> (length));
    if (phase < 0.0)
        phase += static_cast<double> (length);
    return std::min<int64_t> (length - 1, static_cast<int64_t> (phase));
}

void LooperEngine::followHost (double ppqAtBlockStart, double bpm) noexcept
{
    if (! locked || length <= 0 || bpm <= 0.0 || std::fabs (bpm - lockedBpm) > bpmTolerance)
        return;

    if (state != LoopState::Playing && state != LoopState::Overdubbing && state != LoopState::Stopped)
        return;

    const auto target = hostPhase (ppqAtBlockStart, bpm);
    auto diff = std::llabs (target - position);
    diff = std::min<int64_t> (diff, length - diff);

    if (diff > static_cast<int64_t> (resyncSeconds * sampleRate))
        position = target;

    rewindAfterFade = false; // a locked loop keeps its phase, even when stopped
    publish();
}

void LooperEngine::trigger (LoopAction action, const SyncContext& sync) noexcept
{
    if (maxLength <= 0)
        return;

    const double samplesPerGrid = sync.samplesPerGrid;
    const bool hostLocked = locked && sync.hostPlaying && std::fabs (sync.bpm - lockedBpm) <= bpmTolerance;

    switch (action)
    {
        case LoopAction::RecordDub:
            switch (state)
            {
                case LoopState::Empty:
                    if (samplesPerGrid > 0.0 && sync.hostPlaying && sync.bpm > 0.0)
                        startSyncedRecording (sync);
                    else
                    {
                        locked = false;
                        startRecording();
                    }
                    break;
                case LoopState::Armed:       state = LoopState::Empty; locked = false; break; // cancel
                case LoopState::Recording:   requestFinish (LoopState::Playing, samplesPerGrid); break;
                case LoopState::Playing:     state = LoopState::Overdubbing; break;
                case LoopState::Overdubbing: state = LoopState::Playing; break;
                case LoopState::Stopped:
                    // Start playing (from the top, or in phase with the host) and overdub.
                    if (hostLocked)         position = hostPhase (sync.ppq, sync.bpm);
                    else if (playEnv <= 0.0f) position = 0;
                    rewindAfterFade = false;
                    state = LoopState::Overdubbing;
                    break;
            }
            break;

        case LoopAction::PlayStop:
            switch (state)
            {
                case LoopState::Empty:
                case LoopState::Armed:       break;
                case LoopState::Recording:   requestFinish (LoopState::Stopped, samplesPerGrid); break;
                case LoopState::Playing:
                case LoopState::Overdubbing: state = LoopState::Stopped; rewindAfterFade = true; break;
                case LoopState::Stopped:
                    if (hostLocked)           position = hostPhase (sync.ppq, sync.bpm); // in phase
                    else if (playEnv <= 0.0f) position = 0;  // fully stopped: from the top
                    rewindAfterFade = false;                  // still fading out: carry on
                    state = LoopState::Playing;
                    break;
            }
            break;

        case LoopAction::Clear:
            if (state == LoopState::Recording || state == LoopState::Armed)
            {
                length = position = recorded = targetLength = armCountdown = 0;
                state = LoopState::Empty;
                locked = false;
            }
            else if (state != LoopState::Empty)
            {
                state = LoopState::Empty;
                rewindAfterFade = false;
                locked = false;
                clearAfterFade = true; // fade out, then drop the loop
            }
            break;
    }

    publish();
}

void LooperEngine::requestFinish (LoopState next, double samplesPerGrid) noexcept
{
    if (targetLength > 0)
    {
        afterTarget = next; // already recording on to the grid line; only update what happens then
        return;
    }

    const int64_t len = recorded;

    if (samplesPerGrid > 0.0)
    {
        auto units = static_cast<int64_t> (std::llround (static_cast<double> (len) / samplesPerGrid));
        units = std::max<int64_t> (1, units);

        auto target = static_cast<int64_t> (std::llround (static_cast<double> (units) * samplesPerGrid));
        while (target > maxLength && units > 1)
            target = static_cast<int64_t> (std::llround (static_cast<double> (--units) * samplesPerGrid));
        target = std::min (target, maxLength);

        if (target > len)
        {
            targetLength = target; // keep recording until the grid line
            afterTarget = next;
            return;
        }

        // Stopped a little after the grid line: cut the loop at the line and carry on
        // playing from where we are, so the timing stays continuous.
        finishRecording (target, len - target, next);
        return;
    }

    finishRecording (len, 0, next);
}

void LooperEngine::finishRecording (int64_t newLength, int64_t startPosition, LoopState next) noexcept
{
    targetLength = 0;

    if (newLength < static_cast<int64_t> (minLoopSeconds * sampleRate))
    {
        length = position = recorded = 0;
        state = LoopState::Empty;
        locked = false;
        return;
    }

    length = newLength;
    position = startPosition % length;
    recorded = 0;
    applySeamFades();

    state = next;
    playEnv = (next == LoopState::Playing) ? 1.0f : 0.0f; // continue seamlessly into playback
    dubEnv = 0.0f;
    rewindAfterFade = false;

    if (next == LoopState::Stopped)
        position = 0;
}

void LooperEngine::applySeamFades() noexcept
{
    const auto n = std::min<int64_t> (length / 4, static_cast<int64_t> (seamFadeSeconds * sampleRate));
    if (n <= 0)
        return;

    for (auto& b : buffer)
    {
        for (int64_t i = 0; i < n; ++i)
        {
            const auto g = static_cast<float> (i) / static_cast<float> (n);
            b[static_cast<size_t> (i)] *= g;
            b[static_cast<size_t> (length - 1 - i)] *= g;
        }
    }
}

void LooperEngine::process (float* const* channels, int numChannels, int startSample, int numSamples,
                            float dryGainTarget, float loopGainTarget) noexcept
{
    if (maxLength <= 0 || numSamples <= 0)
        return;

    numChannels = std::min (numChannels, maxChannels);

    for (int i = startSample; i < startSample + numSamples; ++i)
    {
        if (state == LoopState::Armed)
        {
            if (armCountdown <= 0)
                startRecording(); // this sample is on the grid line
            else
                --armCountdown;
        }

        dryGain  += (dryGainTarget  - dryGain)  * gainCoeff;
        loopGain += (loopGainTarget - loopGain) * gainCoeff;

        const bool audible = (state == LoopState::Playing || state == LoopState::Overdubbing);
        playEnv = moveTowards (playEnv, audible ? 1.0f : 0.0f, envStep);
        dubEnv  = moveTowards (dubEnv, state == LoopState::Overdubbing ? 1.0f : 0.0f, envStep);

        const bool reading = length > 0 && playEnv > 0.0f;
        const auto pos = static_cast<size_t> (position);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& loop = buffer[ch];
            const float in = channels[ch][i];
            float out = in * dryGain;

            preroll[ch][static_cast<size_t> (prerollWrite)] = in;

            if (state == LoopState::Recording)
                loop[static_cast<size_t> (recorded)] = in;

            if (reading)
                out += loop[pos] * playEnv * loopGain;

            if (length > 0 && dubEnv > 0.0f)
                loop[pos] += in * dubEnv;

            channels[ch][i] = out;
        }

        prerollWrite = (prerollWrite + 1) % prerollLength;

        if (state == LoopState::Recording)
        {
            ++recorded;

            if (targetLength > 0 && recorded >= targetLength)
                finishRecording (targetLength, 0, afterTarget);
            else if (recorded >= maxLength)
                finishRecording (maxLength, 0, LoopState::Playing);
        }
        else if (reading || audible)
        {
            if (length > 0)
                position = (position + 1) % length;
        }

        if (! audible && playEnv <= 0.0f)
        {
            if (rewindAfterFade) { position = 0; rewindAfterFade = false; }
            if (clearAfterFade)  { length = position = 0; clearAfterFade = false; }
        }
    }

    publish();
}

void LooperEngine::publish() noexcept
{
    uiState.store (state, std::memory_order_relaxed);
    uiLength.store (length, std::memory_order_relaxed);
    uiPosition.store (position, std::memory_order_relaxed);
    uiRecorded.store (recorded, std::memory_order_relaxed);
    uiFinishing.store (targetLength > 0, std::memory_order_relaxed);
    uiLocked.store (locked, std::memory_order_relaxed);
}

} // namespace loopstation
