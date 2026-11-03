#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace loopstation;

LoopStationProcessor::LoopStationProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          // Logic feeds a MIDI-controlled effect from its Side Chain menu.
                          // Depending on the host, that audio may arrive on the main input or
                          // on this bus, so the two are added together.
                          .withInput  ("Side Chain", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createParameterLayout())
{
    loopLevelDb = apvts.getRawParameterValue ("loopLevel");
    dryLevelDb  = apvts.getRawParameterValue ("dryLevel");
    syncParam   = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("sync"));
    gridParam   = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("grid"));
    jassert (syncParam != nullptr && gridParam != nullptr);
}

juce::StringArray LoopStationProcessor::gridChoices()
{
    return { "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars" };
}

juce::AudioProcessorValueTreeState::ParameterLayout LoopStationProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const juce::NormalisableRange<float> dbRange (-60.0f, 6.0f, 0.1f);

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "loopLevel", 1 }, "Loop Level", dbRange, 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "dryLevel", 1 }, "Input Level", dbRange, 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "sync", 1 }, "Sync", juce::StringArray { "Free", "Host tempo" }, 1));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "grid", 1 }, "Grid", gridChoices(), 4)); // default: 1 bar

    return layout;
}

bool LoopStationProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainInputChannelSet() != out)
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto& sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

void LoopStationProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate, maxLoopSeconds); // only allocates when the sample rate changes
}

void LoopStationProcessor::requestAction (LoopAction a) noexcept
{
    pendingUiActions.fetch_or (1u << static_cast<int> (a));
}

LoopStationProcessor::HostClock LoopStationProcessor::readHostClock() const
{
    HostClock clock;

    auto* host = getPlayHead();
    if (host == nullptr)
        return clock;

    const auto pos = host->getPosition();
    if (! pos.hasValue())
        return clock;

    const auto bpm = pos->getBpm();
    if (! bpm.hasValue() || *bpm <= 0.0)
        return clock; // no tempo from the host: behave as free

    clock.bpm = *bpm; // shown in the UI even in Free mode
    if (syncParam->getIndex() != 1)
        return clock;

    int num = 4, den = 4;
    if (const auto ts = pos->getTimeSignature())
    {
        num = juce::jmax (1, ts->numerator);
        den = juce::jmax (1, ts->denominator);
    }

    const double quartersPerBar = num * 4.0 / den;
    static const double gridValues[] = { 0.25, 0.5, 1.0, 2.0, 1.0, 2.0, 4.0 }; // quarters, or bars for 4..6
    const int g = juce::jlimit (0, 6, gridParam->getIndex());

    clock.sync = true;
    clock.gridQuarters = (g >= 4) ? gridValues[g] * quartersPerBar : gridValues[g];
    clock.ppqPerSample = *bpm / (60.0 * getSampleRate());

    if (const auto ppq = pos->getPpqPosition(); pos->getIsPlaying() && ppq.hasValue())
    {
        clock.playing = true;
        clock.ppq = *ppq;

        // Grids up to one bar are counted from the current bar line, so they stay right
        // after a time-signature change. 2 and 4 bars are counted from the project start.
        if (const auto barStart = pos->getPpqPositionOfLastBarStart(); barStart.hasValue() && g <= 4)
            clock.anchorPpq = *barStart;
    }

    return clock;
}

loopstation::SyncContext LoopStationProcessor::syncAt (const HostClock& clock, int sampleInBlock) const noexcept
{
    loopstation::SyncContext c;
    if (! clock.sync)
        return c;

    const double samplesPerQuarter = 1.0 / clock.ppqPerSample;
    c.samplesPerGrid = clock.gridQuarters * samplesPerQuarter;
    c.bpm = clock.bpm;

    if (! clock.playing)
        return c;

    const double ppq = clock.ppq + sampleInBlock * clock.ppqPerSample;
    const double g = clock.gridQuarters;
    const double units = std::floor ((ppq - clock.anchorPpq) / g + 1e-9);

    c.hostPlaying = true;
    c.ppq = ppq;
    c.lastLinePpq = clock.anchorPpq + units * g;
    c.nextLinePpq = c.lastLinePpq + g;
    c.samplesSinceLastLine = juce::jmax (0.0, (ppq - c.lastLinePpq) * samplesPerQuarter);
    c.samplesToNextLine = juce::jmax (0.0, (c.nextLinePpq - ppq) * samplesPerQuarter);
    return c;
}

void LoopStationProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    auto mainBus = getBusBuffer (buffer, false, 0); // main input and output share these channels
    const int numChannels = juce::jmin (mainBus.getNumChannels(), LooperEngine::maxChannels);

    for (auto ch = getMainBusNumInputChannels(); ch < mainBus.getNumChannels(); ++ch)
        mainBus.clear (ch, 0, numSamples);

    // Add the side-chain audio, if any, to the main input.
    if (getBusCount (true) > 1 && getBus (true, 1)->isEnabled())
    {
        const auto sc = getBusBuffer (buffer, true, 1);
        if (sc.getNumChannels() > 0)
            for (int ch = 0; ch < numChannels; ++ch)
                mainBus.addFrom (ch, 0, sc, juce::jmin (ch, sc.getNumChannels() - 1), 0, numSamples);
    }

    const auto clock = readHostClock();
    hostBpm.store (clock.bpm);

    // A loop recorded against the host grid keeps following the host's position.
    if (clock.playing)
        engine.followHost (clock.ppq, clock.bpm);

    // Button presses from the plugin window.
    const auto ui = pendingUiActions.exchange (0);
    for (int a = 0; a < numLoopActions; ++a)
        if (ui & (1u << a))
            engine.trigger (static_cast<LoopAction> (a), syncAt (clock, 0));

    const float dry  = juce::Decibels::decibelsToGain (dryLevelDb->load(), -60.0f);
    const float loop = juce::Decibels::decibelsToGain (loopLevelDb->load(), -60.0f);

    auto* const* channels = mainBus.getArrayOfWritePointers();

    // Sample-accurate MIDI: process up to each event, then apply it.
    int done = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, numSamples, meta.samplePosition);
        if (at > done)
        {
            engine.process (channels, numChannels, done, at - done, dry, loop);
            done = at;
        }

        if (meta.numBytes >= 3)
        {
            const int action = midiMap.handleMessage (meta.data[0], meta.data[1], meta.data[2]);
            if (action >= 0)
                engine.trigger (static_cast<LoopAction> (action), syncAt (clock, at));
        }
    }

    if (done < numSamples)
        engine.process (channels, numChannels, done, numSamples - done, dry, loop);

    midi.clear(); // MIDI is consumed here, not passed on
}

juce::AudioProcessorEditor* LoopStationProcessor::createEditor()
{
    return new LoopStationEditor (*this);
}

void LoopStationProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();

    for (int a = 0; a < numLoopActions; ++a)
        state.setProperty ("midi" + juce::String (a), static_cast<int> (midiMap.get (a).pack()), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void LoopStationProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);

    for (int a = 0; a < numLoopActions; ++a)
    {
        const auto key = juce::Identifier ("midi" + juce::String (a));
        if (state.hasProperty (key))
            midiMap.set (a, MidiBinding::unpack (static_cast<uint32_t> (static_cast<int> (state[key]))));
    }

    apvts.replaceState (state);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LoopStationProcessor();
}
