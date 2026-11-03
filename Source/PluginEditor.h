#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class LoopStationGainEditor final : public juce::AudioProcessorEditor
{
public:
    explicit LoopStationGainEditor (LoopStationGainProcessor&);
    ~LoopStationGainEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Slider gainKnob;
    juce::Label  gainLabel;
    juce::AudioProcessorValueTreeState::SliderAttachment gainAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopStationGainEditor)
};
