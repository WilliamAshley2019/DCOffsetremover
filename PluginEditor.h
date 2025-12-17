#pragma once

#include "PluginProcessor.h"

//==============================================================================
// Visualizer Component - shows waveform with DC offset reference
//==============================================================================
class VisualizerComponent : public juce::Component,
    public juce::Timer
{
public:
    VisualizerComponent(NewProjectAudioProcessor& p);
    ~VisualizerComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;

    void setVisualizerActive(bool active);

private:
    NewProjectAudioProcessor& audioProcessor;
    juce::Image backgroundGrid;
    bool visualizerEnabled{ false };

    void drawBackgroundGrid(juce::Graphics& g);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VisualizerComponent)
};

//==============================================================================
// Main Plugin Editor
//==============================================================================
class NewProjectAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    NewProjectAudioProcessorEditor(NewProjectAudioProcessor&);
    ~NewProjectAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    juce::ToggleButton filterActiveButton{ "DC Filter Active" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> filterActiveAttachment;

    juce::ToggleButton visualizerToggleButton{ "Show Visualizer" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> visualizerAttachment;

    VisualizerComponent visualizer;

    NewProjectAudioProcessor& audioProcessor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NewProjectAudioProcessorEditor)
};