#include "PluginEditor.h"

using namespace loopstation;

namespace
{
    const juce::Colour background (0xff1e1f24);
    const juce::Colour panel      (0xff2a2c33);

    const char* actionNames[numLoopActions] = { "Rec / Dub", "Play / Stop", "Clear" };

    void setUpKnob (juce::Slider& s)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
        s.setTextValueSuffix (" dB");
        s.setDoubleClickReturnValue (true, 0.0);
    }

    void setUpCaption (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
    }
}

LoopStationEditor::LoopStationEditor (LoopStationProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    stateLabel.setFont (juce::Font (22.0f, juce::Font::bold));
    stateLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (stateLabel);

    infoLabel.setJustificationType (juce::Justification::centredRight);
    infoLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
    addAndMakeVisible (infoLabel);

    for (int a = 0; a < numLoopActions; ++a)
    {
        auto& row = rows[a];

        row.button.setButtonText (actionNames[a]);
        row.button.onClick = [this, a] { proc.requestAction (static_cast<LoopAction> (a)); };
        addAndMakeVisible (row.button);

        row.binding.setJustificationType (juce::Justification::centred);
        row.binding.setColour (juce::Label::backgroundColourId, panel);
        addAndMakeVisible (row.binding);

        row.learn.setButtonText ("Learn");
        row.learn.setClickingTogglesState (false);
        row.learn.onClick = [this, a]
        {
            if (proc.midiMap.getLearnTarget() == a)
                proc.midiMap.cancelLearn();
            else
                proc.midiMap.startLearn (a);
        };
        addAndMakeVisible (row.learn);
    }

    setUpKnob (loopLevel);
    setUpKnob (dryLevel);
    addAndMakeVisible (loopLevel);
    addAndMakeVisible (dryLevel);
    setUpCaption (loopLevelLabel, "Loop");
    setUpCaption (dryLevelLabel, "Input");
    setUpCaption (syncLabel, "Sync");
    setUpCaption (gridLabel, "Grid");
    setUpCaption (lengthLabel, "Length");
    for (auto* l : { &loopLevelLabel, &dryLevelLabel, &syncLabel, &gridLabel, &lengthLabel })
        addAndMakeVisible (*l);

    // ComboBox items must exist before the attachments are created.
    syncBox.addItemList ({ "Free", "Host tempo" }, 1);
    gridBox.addItemList (LoopStationProcessor::gridChoices(), 1);
    lengthBox.addItemList (LoopStationProcessor::lengthChoices(), 1);
    addAndMakeVisible (syncBox);
    addAndMakeVisible (gridBox);
    addAndMakeVisible (lengthBox);

    loopLevelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "loopLevel", loopLevel);
    dryLevelAttachment  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "dryLevel", dryLevel);
    syncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "sync", syncBox);
    gridAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "grid", gridBox);
    lengthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "length", lengthBox);

    setSize (560, 330);
    timerCallback();
    startTimerHz (30);
}

LoopStationEditor::~LoopStationEditor()
{
    stopTimer();
    proc.midiMap.cancelLearn();
}

void LoopStationEditor::timerCallback()
{
    const auto& e = proc.engine;
    const double sr = e.getSampleRate() > 0.0 ? e.getSampleRate() : 44100.0;

    juce::String text;
    switch (e.getState())
    {
        case LoopState::Empty:       text = "Empty";       stateColour = juce::Colours::grey; break;
        case LoopState::Recording:   text = e.isFixedLength()     ? "Recording (fixed length)"
                                          : e.isFinishingToGrid() ? "Recording (to grid)" : "Recording";
                                     stateColour = juce::Colours::red; break;
        case LoopState::Playing:     text = "Playing";     stateColour = juce::Colours::limegreen; break;
        case LoopState::Overdubbing: text = "Overdubbing"; stateColour = juce::Colours::orange; break;
        case LoopState::Stopped:     text = "Stopped";     stateColour = juce::Colours::lightblue; break;
        case LoopState::Armed:       text = "Waiting for grid"; stateColour = juce::Colours::indianred; break;
    }

    if (e.isLockedToHost() && e.getLength() > 0)
        text << "  (bar-locked)";
    stateLabel.setText (text, juce::dontSendNotification);
    stateLabel.setColour (juce::Label::textColourId, stateColour);

    const auto length = e.getLength();
    float newProgress = 0.0f;
    juce::String info;

    if (e.getState() == LoopState::Recording)
    {
        const auto target = e.isFixedLength() ? e.getTargetLength() : e.getMaxLength();
        newProgress = static_cast<float> (e.getRecordedSamples()) / static_cast<float> (juce::jmax<int64_t> (1, target));
        info = juce::String (e.getRecordedSamples() / sr, 1) + " s / " + juce::String (target / sr, e.isFixedLength() ? 1 : 0)
             + (e.isFixedLength() ? " s" : " s max");
    }
    else if (length > 0)
    {
        newProgress = static_cast<float> (e.getPosition()) / static_cast<float> (length);
        info = "Loop " + juce::String (length / sr, 2) + " s";
    }

    const auto bpm = proc.getHostBpm();
    info << (info.isEmpty() ? "" : "   ") << (bpm > 0.0 ? "Host " + juce::String (bpm, 1) + " BPM" : "No host tempo");
    infoLabel.setText (info, juce::dontSendNotification);

    const int learning = proc.midiMap.getLearnTarget();
    for (int a = 0; a < numLoopActions; ++a)
    {
        auto& row = rows[a];
        const bool isLearning = (learning == a);
        row.learn.setButtonText (isLearning ? "Press a key..." : "Learn");
        row.learn.setColour (juce::TextButton::buttonColourId, isLearning ? juce::Colours::darkorange : panel);
        row.binding.setText (proc.midiMap.get (a).describe(), juce::dontSendNotification);
    }

    if (newProgress != progress)
    {
        progress = newProgress;
        repaint (progressArea);
    }
}

void LoopStationEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    g.setColour (panel);
    g.fillRoundedRectangle (progressArea.toFloat(), 4.0f);
    g.setColour (stateColour);
    g.fillRoundedRectangle (progressArea.toFloat()
                                .withWidth (progressArea.getWidth() * juce::jlimit (0.0f, 1.0f, progress)), 4.0f);

    g.setColour (juce::Colours::white.withAlpha (0.4f));
    g.setFont (11.0f);
    g.drawText ("LoopStation  -  MIDI: keys/CCs on any channel. Default C1 / D1 / E1.",
                getLocalBounds().removeFromBottom (18).reduced (10, 0), juce::Justification::centredLeft);
}

void LoopStationEditor::resized()
{
    auto area = getLocalBounds().reduced (12);
    area.removeFromBottom (14);

    auto top = area.removeFromTop (32);
    stateLabel.setBounds (top.removeFromLeft (330));
    infoLabel.setBounds (top);

    area.removeFromTop (6);
    progressArea = area.removeFromTop (14);
    area.removeFromTop (14);

    auto right = area.removeFromRight (230);
    area.removeFromRight (12);

    for (auto& row : rows)
    {
        auto r = area.removeFromTop (48);
        r.removeFromBottom (8);
        row.button.setBounds (r.removeFromLeft (120));
        r.removeFromLeft (8);
        row.learn.setBounds (r.removeFromRight (100));
        r.removeFromRight (8);
        row.binding.setBounds (r);
    }

    auto knobs = right.removeFromTop (130);
    auto k1 = knobs.removeFromLeft (knobs.getWidth() / 2);
    loopLevelLabel.setBounds (k1.removeFromTop (18));
    loopLevel.setBounds (k1);
    dryLevelLabel.setBounds (knobs.removeFromTop (18));
    dryLevel.setBounds (knobs);

    right.removeFromTop (6);
    auto s = right.removeFromTop (24);
    syncLabel.setBounds (s.removeFromLeft (50));
    syncBox.setBounds (s);
    right.removeFromTop (6);
    auto gr = right.removeFromTop (24);
    gridLabel.setBounds (gr.removeFromLeft (50));
    gridBox.setBounds (gr);
    right.removeFromTop (6);
    auto len = right.removeFromTop (24);
    lengthLabel.setBounds (len.removeFromLeft (50));
    lengthBox.setBounds (len);
}
