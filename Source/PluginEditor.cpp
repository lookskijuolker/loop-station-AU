#include "PluginEditor.h"

LoopStationGainEditor::LoopStationGainEditor (LoopStationGainProcessor& p)
    : AudioProcessorEditor (p),
      gainAttachment (p.apvts, LoopStationGainProcessor::gainParamId, gainKnob)
{
    gainKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    gainKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
    gainKnob.setTextValueSuffix (" dB");
    gainKnob.setDoubleClickReturnValue (true, 0.0);
    addAndMakeVisible (gainKnob);

    gainLabel.setText ("Gain", juce::dontSendNotification);
    gainLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (gainLabel);

    setSize (220, 240);
}

void LoopStationGainEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1f24));
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.setFont (12.0f);
    g.drawFittedText ("LoopStation PoC", getLocalBounds().removeFromBottom (20),
                      juce::Justification::centred, 1);
}

void LoopStationGainEditor::resized()
{
    auto area = getLocalBounds().reduced (10);
    area.removeFromBottom (14);
    gainLabel.setBounds (area.removeFromTop (24));
    gainKnob.setBounds (area);
}
