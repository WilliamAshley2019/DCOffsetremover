#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// VisualizerComponent Implementation
//==============================================================================

VisualizerComponent::VisualizerComponent(NewProjectAudioProcessor& p)
    : audioProcessor(p)
{
    setVisualizerActive(false);
}

VisualizerComponent::~VisualizerComponent()
{
    stopTimer();
}

void VisualizerComponent::setVisualizerActive(bool active)
{
    visualizerEnabled = active;
    audioProcessor.setVisualizerState(active);

    if (active)
        startTimerHz(30); // Human-scale refresh rate (30 fps)
    else
        stopTimer(); // Complete CPU shutdown when disabled

    repaint();
}

void VisualizerComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);

    if (!visualizerEnabled)
    {
        g.setColour(juce::Colours::grey);
        g.setFont(16.0f);
        g.drawText("Visualizer Disabled", getLocalBounds(), juce::Justification::centred);
        return;
    }

    // Cache background grid - only redraw on resize
    if (backgroundGrid.getWidth() != getWidth() || backgroundGrid.getHeight() != getHeight())
    {
        backgroundGrid = juce::Image(juce::Image::RGB, getWidth(), getHeight(), true);
        juce::Graphics bg(backgroundGrid);
        drawBackgroundGrid(bg);
    }
    g.drawImageAt(backgroundGrid, 0, 0);

    // Draw waveform
    g.setColour(juce::Colours::cyan.withAlpha(0.9f));
    juce::Path waveformPath;

    auto currentWriteIndex = audioProcessor.fifoWriteIndex.load(std::memory_order_relaxed);
    int startIndex = (currentWriteIndex - audioProcessor.fifoSize + audioProcessor.fifoSize) % audioProcessor.fifoSize;

    // Start the path
    float firstSample = audioProcessor.getNextSampleForVisualizer(startIndex);
    float y = juce::jmap(firstSample, -1.0f, 1.0f, (float)getHeight(), 0.0f);
    waveformPath.startNewSubPath(0.0f, y);

    // Draw remaining samples
    for (int i = 1; i < audioProcessor.fifoSize; ++i)
    {
        float sample = audioProcessor.getNextSampleForVisualizer((startIndex + i) % audioProcessor.fifoSize);
        y = juce::jmap(sample, -1.0f, 1.0f, (float)getHeight(), 0.0f);
        float x = juce::jmap((float)i, 0.0f, (float)(audioProcessor.fifoSize - 1), 0.0f, (float)getWidth());
        waveformPath.lineTo(x, y);
    }

    g.strokePath(waveformPath, juce::PathStrokeType(1.5f));
}

void VisualizerComponent::resized()
{
    backgroundGrid = juce::Image(); // Force grid redraw
}

void VisualizerComponent::timerCallback()
{
    repaint();
}

void VisualizerComponent::drawBackgroundGrid(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.darker(0.8f));

    g.setColour(juce::Colours::darkgrey.withAlpha(0.3f));

    // Horizontal reference lines
    float centerY = getHeight() / 2.0f;
    float quarterHeight = getHeight() / 4.0f;

    // Draw reference lines at -1.0, -0.5, 0, +0.5, +1.0
    g.drawHorizontalLine(juce::roundToInt(centerY - quarterHeight * 2), 0.0f, (float)getWidth()); // +1.0
    g.drawHorizontalLine(juce::roundToInt(centerY - quarterHeight), 0.0f, (float)getWidth());     // +0.5
    g.drawHorizontalLine(juce::roundToInt(centerY + quarterHeight), 0.0f, (float)getWidth());     // -0.5
    g.drawHorizontalLine(juce::roundToInt(centerY + quarterHeight * 2), 0.0f, (float)getWidth()); // -1.0

    // Vertical time divisions
    int numDivisions = 8;
    for (int i = 0; i <= numDivisions; ++i)
    {
        float x = juce::jmap((float)i, 0.0f, (float)numDivisions, 0.0f, (float)getWidth());
        g.drawVerticalLine(juce::roundToInt(x), 0.0f, (float)getHeight());
    }

    // Emphasize zero-crossing line (DC offset reference)
    g.setColour(juce::Colours::darkgrey.withAlpha(0.6f));
    g.drawHorizontalLine(juce::roundToInt(centerY), 0.0f, (float)getWidth());
}

//==============================================================================
// PluginEditor Implementation
//==============================================================================

NewProjectAudioProcessorEditor::NewProjectAudioProcessorEditor(NewProjectAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p), visualizer(p)
{
    setSize(600, 300);

    // Setup bypass button
    addAndMakeVisible(bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, "bypass", bypassButton);

    // Setup visualizer toggle
    addAndMakeVisible(visualizerToggleButton);
    visualizerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, "visualizer", visualizerToggleButton);

    visualizerToggleButton.onClick = [this]()
        {
            visualizer.setVisualizerActive(visualizerToggleButton.getToggleState());
        };

    // Initialize visualizer state from saved parameter
    visualizer.setVisualizerActive(visualizerToggleButton.getToggleState());

    addAndMakeVisible(visualizer);
}

NewProjectAudioProcessorEditor::~NewProjectAudioProcessorEditor()
{
}

void NewProjectAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::darkgrey);

    g.setColour(juce::Colours::white);
    g.setFont(18.0f);
    g.drawText("DC Offset Remover", getLocalBounds().removeFromTop(40), juce::Justification::centred);
}

void NewProjectAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced(10);
    auto topArea = bounds.removeFromTop(50);

    // Layout control buttons
    bypassButton.setBounds(topArea.removeFromLeft(bounds.getWidth() / 2).reduced(5));
    visualizerToggleButton.setBounds(topArea.reduced(5));

    // Visualizer takes remaining space
    visualizer.setBounds(bounds.reduced(5));
}