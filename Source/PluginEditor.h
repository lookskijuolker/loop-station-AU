#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class LoopStationEditor final : public juce::AudioProcessorEditor,
                                private juce::Timer
{
public:
    explicit LoopStationEditor (LoopStationProcessor&);
    ~LoopStationEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    struct ActionRow
    {
        juce::TextButton button, learn;
        juce::Label binding;
    };

    LoopStationProcessor& proc;

    juce::Label stateLabel, infoLabel;
    ActionRow rows[loopstation::numLoopActions];

    juce::Slider loopLevel, dryLevel;
    juce::Label loopLevelLabel, dryLevelLabel, syncLabel, gridLabel;
    juce::ComboBox syncBox, gridBox;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> loopLevelAttachment, dryLevelAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> syncAttachment, gridAttachment;

    juce::Rectangle<int> progressArea;
    float progress = 0.0f;
    juce::Colour stateColour;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopStationEditor)
};
