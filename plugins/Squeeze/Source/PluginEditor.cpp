#include "PluginEditor.h"

using namespace squeeze::ui;

SqueezeAudioProcessorEditor::SqueezeAudioProcessorEditor (SqueezeAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    setupKnob (threshold, ParamIDs::threshold, "THRESHOLD");
    setupKnob (ratio,     ParamIDs::ratio,     "RATIO");
    setupKnob (knee,      ParamIDs::knee,      "KNEE");
    setupKnob (scHpf,     ParamIDs::scHpf,     "SC HPF");
    setupKnob (attack,    ParamIDs::attack,    "ATTACK");
    setupKnob (release,   ParamIDs::release,   "RELEASE");
    setupKnob (makeup,    ParamIDs::makeup,    "MAKEUP", true);
    setupKnob (mix,       ParamIDs::mix,       "MIX");

    setupToggle (autoMakeupButton, ParamIDs::autoMakeup, autoMakeupAttachment);
    setupToggle (extScButton,      ParamIDs::extSc,      extScAttachment);
    autoMakeupButton.setTooltip ("Automatically compensate level based on threshold and ratio");
    extScButton.setTooltip ("Detect from the plugin's sidechain input instead of the main signal");

    addAndMakeVisible (curve);
    addAndMakeVisible (inMeter);
    addAndMakeVisible (grMeter);
    addAndMakeVisible (outMeter);

    // Set after the children exist so their text boxes are rebuilt with our colours.
    setLookAndFeel (&lnf);

    setSize (defaultWidth, defaultHeight);
    refreshFromProcessor();
    startTimerHz (30);
}

SqueezeAudioProcessorEditor::~SqueezeAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SqueezeAudioProcessorEditor::setupKnob (Knob& k, const char* paramID, const juce::String& title, bool bipolar)
{
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 20);
    k.slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                  juce::MathConstants<float>::pi * 2.75f, true);
    if (bipolar)
        k.slider.getProperties().set ("bipolar", true);

    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processorRef.apvts, paramID, k.slider);

    if (auto* param = processorRef.apvts.getParameter (paramID))
        k.slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));

    k.label.setText (title, juce::dontSendNotification);
    k.label.setFont (font (11.5f, true));
    k.label.setColour (juce::Label::textColourId, Colours::dim);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setInterceptsMouseClicks (false, false);

    addAndMakeVisible (k.slider);
    addAndMakeVisible (k.label);
}

void SqueezeAudioProcessorEditor::setupToggle (juce::ToggleButton& b, const char* paramID,
                                               std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>& att)
{
    att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processorRef.apvts, paramID, b);
    addAndMakeVisible (b);
}

void SqueezeAudioProcessorEditor::refreshFromProcessor()
{
    inMeter.push  (processorRef.meterInputPeak.exchange (0.0f));
    outMeter.push (processorRef.meterOutputPeak.exchange (0.0f));
    grMeter.push  (processorRef.meterGainReductionDb.exchange (0.0f));

    const auto params = processorRef.currentParams();
    curve.setState (params, inMeter.getDisplayed());
    makeup.slider.setEnabled (! params.autoMakeup);
}

void SqueezeAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);

    // Header
    auto header = getLocalBounds().removeFromTop (56).reduced (20, 0);
    g.setColour (Colours::text);
    g.setFont (font (24.0f, true));
    g.drawText ("SQUEEZE", header.removeFromLeft (130), juce::Justification::centredLeft);
    g.setColour (Colours::accent);
    g.setFont (font (11.0f, true));
    g.drawText ("COMPRESSOR", header.removeFromLeft (92).translated (0, 3), juce::Justification::centredLeft);

    g.setColour (Colours::panelEdge.brighter (0.3f));
    g.fillRect (header.getX(), header.getCentreY() - 8, 1, 18);
    header.removeFromLeft (14);
    g.setColour (Colours::dim);
    g.setFont (font (13.0f, true));
    g.drawText ("LBAudio", header.removeFromLeft (100).translated (0, 2), juce::Justification::centredLeft);

    g.setColour (Colours::panelEdge);
    g.drawHorizontalLine (56, 0.0f, (float) getWidth());

    for (auto r : { knobPanel, meterPanel })
    {
        g.setColour (Colours::panel);
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        g.setColour (Colours::panelEdge);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.0f, 1.0f);
    }
}

void SqueezeAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    auto header = area.removeFromTop (56).reduced (20, 14);
    extScButton.setBounds (header.removeFromRight (140));
    header.removeFromRight (10);
    autoMakeupButton.setBounds (header.removeFromRight (116));

    area.reduce (16, 16);

    const int curveSize = area.getHeight();
    curve.setBounds (area.removeFromLeft (curveSize));
    area.removeFromLeft (12);

    meterPanel = area.removeFromRight (132);
    area.removeFromRight (12);
    knobPanel = area;

    auto meters = meterPanel.reduced (8, 10);
    const int meterW = meters.getWidth() / 3;
    inMeter.setBounds  (meters.removeFromLeft (meterW));
    grMeter.setBounds  (meters.removeFromLeft (meterW));
    outMeter.setBounds (meters);

    auto knobs = knobPanel.reduced (8, 10);
    const int rowH = knobs.getHeight() / 2;
    Knob* rows[2][4] = { { &threshold, &ratio, &knee, &scHpf }, { &attack, &release, &makeup, &mix } };

    for (auto& row : rows)
    {
        auto rowArea = knobs.removeFromTop (rowH);
        const int cellW = rowArea.getWidth() / 4;
        for (auto* k : row)
        {
            auto cell = rowArea.removeFromLeft (cellW).reduced (4, 2);
            const int knobSize = juce::jmin (cell.getWidth() - 8, cell.getHeight() - 18 - 22 - 12);
            auto group = cell.withSizeKeepingCentre (cell.getWidth(), 18 + knobSize + 22);
            k->label.setBounds (group.removeFromTop (18));
            k->slider.setBounds (group);
        }
    }
}
