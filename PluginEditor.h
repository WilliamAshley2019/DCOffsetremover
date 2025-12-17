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

    // Cached grid lines for performance
    std::vector<float> verticalGridLines;
    std::vector<float> horizontalGridLines;

    void drawBackgroundGrid(juce::Graphics& g);
    void updateGridCache();

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

    juce::ToggleButton cutoffToggleButton{ "10Hz Cutoff (Gentle)" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> cutoffAttachment;

    juce::ToggleButton visualizerToggleButton{ "Show Visualizer" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> visualizerAttachment;

    VisualizerComponent visualizer;

    // Info labels
    juce::Label dcOffsetLabel;
    juce::Label rmsLabel;
    juce::Label peakLabel;
    juce::Label lowFreqLabel;
    juce::Label infoLabel;

    NewProjectAudioProcessor& audioProcessor;

    // Simple timer for metrics update
    class MetricsTimer : public juce::Timer
    {
    public:
        MetricsTimer(NewProjectAudioProcessorEditor& editor) : owner(editor) {}
        void timerCallback() override { owner.updateMetricsDisplay(); }
    private:
        NewProjectAudioProcessorEditor& owner;
    };

    MetricsTimer metricsTimer{ *this };

    void updateMetricsDisplay();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NewProjectAudioProcessorEditor)
};