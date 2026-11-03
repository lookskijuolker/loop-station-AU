#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "LooperEngine.h"
#include "MidiMap.h"

// Step 2: a single stereo loop, controlled by MIDI notes/CCs (with MIDI learn) or by
// the buttons in the plugin window. Loop length is free or rounded to a host-tempo grid.
class LoopStationProcessor final : public juce::AudioProcessor
{
public:
    LoopStationProcessor();
    ~LoopStationProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Called from the UI thread; the action runs at the start of the next audio block.
    void requestAction (loopstation::LoopAction a) noexcept;

    static juce::StringArray gridChoices();
    double getHostBpm() const noexcept { return hostBpm.load(); }

    static constexpr double maxLoopSeconds = 60.0;

    juce::AudioProcessorValueTreeState apvts;
    loopstation::LooperEngine engine;
    loopstation::MidiMap midiMap;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Host timing for the current block.
    struct HostClock
    {
        bool sync = false;          // Sync = Host tempo and the host reports a tempo
        bool playing = false;       // ... and its transport is running with a valid position
        double bpm = 0.0;
        double ppq = 0.0;           // position at the block's first sample, in quarter notes
        double ppqPerSample = 0.0;
        double gridQuarters = 0.0;
        double anchorPpq = 0.0;     // grid lines are anchorPpq + k * gridQuarters
    };

    HostClock readHostClock() const;
    loopstation::SyncContext syncAt (const HostClock&, int sampleInBlock) const noexcept;

    std::atomic<float>* loopLevelDb = nullptr;
    std::atomic<float>* dryLevelDb  = nullptr;
    juce::AudioParameterChoice* syncParam = nullptr;
    juce::AudioParameterChoice* gridParam = nullptr;

    std::atomic<uint32_t> pendingUiActions { 0 };
    std::atomic<double> hostBpm { 0.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopStationProcessor)
};
