#include "PluginProcessor.h"
#include "PluginEditor.h"

LoopStationGainProcessor::LoopStationGainProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createParameterLayout())
{
    gainDb = apvts.getRawParameterValue (gainParamId);
}

juce::AudioProcessorValueTreeState::ParameterLayout LoopStationGainProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // -60 dB .. +12 dB, default 0 dB (unity = true pass-through).
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { gainParamId, 1 },
        "Gain",
        juce::NormalisableRange<float> (-60.0f, 12.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    return layout;
}

bool LoopStationGainProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

void LoopStationGainProcessor::prepareToPlay (double sampleRate, int)
{
    smoothedGain.reset (sampleRate, 0.02); // 20 ms ramp to avoid zipper noise
    smoothedGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (gainDb->load()));
}

void LoopStationGainProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numIn  = getTotalNumInputChannels();
    const auto numOut = getTotalNumOutputChannels();

    for (auto ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    smoothedGain.setTargetValue (juce::Decibels::decibelsToGain (gainDb->load()));

    if (! smoothedGain.isSmoothing())
    {
        const auto g = smoothedGain.getCurrentValue();
        if (g != 1.0f)
            buffer.applyGain (g);
        return;
    }

    // Ramp sample-by-sample while the knob is moving.
    const auto numSamples = buffer.getNumSamples();
    for (int i = 0; i < numSamples; ++i)
    {
        const auto g = smoothedGain.getNextValue();
        for (int ch = 0; ch < numOut; ++ch)
            buffer.getWritePointer (ch)[i] *= g;
    }
}

juce::AudioProcessorEditor* LoopStationGainProcessor::createEditor()
{
    return new LoopStationGainEditor (*this);
}

void LoopStationGainProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void LoopStationGainProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

// Entry point JUCE's AU wrapper calls to create the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LoopStationGainProcessor();
}
