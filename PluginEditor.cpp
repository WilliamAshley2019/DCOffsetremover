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

void VisualizerComponent::updateGridCache()
{
    verticalGridLines.clear();
    horizontalGridLines.clear();

    int numVerticalDivisions = 8;
    for (int i = 0; i <= numVerticalDivisions; ++i)
    {
        float x = juce::jmap((float)i, 0.0f, (float)numVerticalDivisions, 0.0f, (float)getWidth());
        verticalGridLines.push_back(x);
    }

    // Horizontal lines at -1.0, -0.5, 0, +0.5, +1.0
    float centerY = getHeight() / 2.0f;
    float quarterHeight = getHeight() / 4.0f;

    horizontalGridLines.push_back(centerY - quarterHeight * 2); // +1.0
    horizontalGridLines.push_back(centerY - quarterHeight);     // +0.5
    horizontalGridLines.push_back(centerY);                     // 0
    horizontalGridLines.push_back(centerY + quarterHeight);     // -0.5
    horizontalGridLines.push_back(centerY + quarterHeight * 2); // -1.0
}

void VisualizerComponent::drawBackgroundGrid(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.darker(0.8f));

    g.setColour(juce::Colours::darkgrey.withAlpha(0.3f));

    // Draw horizontal reference lines
    for (auto y : horizontalGridLines)
    {
        g.drawHorizontalLine(juce::roundToInt(y), 0.0f, (float)getWidth());
    }

    // Draw vertical time divisions
    for (auto x : verticalGridLines)
    {
        g.drawVerticalLine(juce::roundToInt(x), 0.0f, (float)getHeight());
    }

    // Emphasize zero-crossing line (DC offset reference)
    g.setColour(juce::Colours::darkgrey.withAlpha(0.6f));
    if (horizontalGridLines.size() >= 3)
        g.drawHorizontalLine(juce::roundToInt(horizontalGridLines[2]), 0.0f, (float)getWidth());
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

    // Update grid cache if needed
    if (verticalGridLines.empty() || horizontalGridLines.empty() ||
        backgroundGrid.getWidth() != getWidth() || backgroundGrid.getHeight() != getHeight())
    {
        updateGridCache();
        backgroundGrid = juce::Image(juce::Image::RGB, getWidth(), getHeight(), true);
        juce::Graphics bg(backgroundGrid);
        drawBackgroundGrid(bg);
    }
    g.drawImageAt(backgroundGrid, 0, 0);

    // Get current audio metrics
    float dcOffset = audioProcessor.getDCOffset();
    float lowFreq = audioProcessor.getLowFreqLevel();

    // Draw waveform
    g.setColour(juce::Colours::cyan.withAlpha(0.9f));
    juce::Path waveformPath;

    auto currentWriteIndex = audioProcessor.fifoWriteIndex.load(std::memory_order_relaxed);

    // Calculate the oldest sample index correctly
    int startIndex = currentWriteIndex - audioProcessor.fifoSize;
    if (startIndex < 0)
        startIndex += audioProcessor.fifoSize;

    startIndex = startIndex % audioProcessor.fifoSize;

    // Calculate drawing parameters
    float xIncrement = (float)getWidth() / (float)(audioProcessor.fifoSize - 1);
    float yScale = (float)getHeight() / 2.0f;
    float yOffset = (float)getHeight() / 2.0f;

    // Start the path
    float firstSample = audioProcessor.getNextSampleForVisualizer(startIndex);
    float y = yOffset - (firstSample * yScale);
    waveformPath.startNewSubPath(0.0f, y);

    // Draw remaining samples
    for (int i = 1; i < audioProcessor.fifoSize; ++i)
    {
        float sample = audioProcessor.getNextSampleForVisualizer((startIndex + i) % audioProcessor.fifoSize);
        y = yOffset - (sample * yScale);
        float x = i * xIncrement;
        waveformPath.lineTo(x, y);
    }

    g.strokePath(waveformPath, juce::PathStrokeType(1.5f));

    // Draw DC offset line
    if (std::abs(dcOffset) > 0.001f)
    {
        float dcY = yOffset - (dcOffset * yScale);
        g.setColour(juce::Colours::red.withAlpha(0.7f));
        g.drawHorizontalLine(juce::roundToInt(dcY), 0.0f, (float)getWidth());

        // Draw DC offset value
        g.setColour(juce::Colours::white);
        g.setFont(12.0f);
        juce::String dcText = "DC: " + juce::String(dcOffset * 100.0f, 2) + "%";
        g.drawText(dcText, 10, 10, 100, 20, juce::Justification::left);
    }

    // Draw low-frequency energy meter (what's being filtered out)
    if (lowFreq > 0.001f)
    {
        g.setColour(juce::Colours::orange.withAlpha(0.5f));
        float lowFreqHeight = lowFreq * getHeight() / 2.0f;
        g.fillRect(0.0f, yOffset - lowFreqHeight, 8.0f, lowFreqHeight * 2.0f);

        // Draw low-frequency value
        g.setColour(juce::Colours::white);
        g.setFont(12.0f);
        juce::String lowFreqText = "LF: " + juce::String(lowFreq * 100.0f, 2) + "%";
        g.drawText(lowFreqText, getWidth() - 110, 50, 100, 20, juce::Justification::right);
    }

    // Draw cutoff frequency indicator
    g.setColour(juce::Colours::yellow.withAlpha(0.5f));
    juce::String cutoffText = (audioProcessor.apvts.getRawParameterValue("lowCutoff")->load() > 0.5f) ? "10Hz" : "20Hz";
    g.setFont(14.0f);
    g.drawText("Cutoff: " + cutoffText, getWidth() / 2 - 50, getHeight() - 30, 100, 20, juce::Justification::centred);
}

void VisualizerComponent::resized()
{
    backgroundGrid = juce::Image(); // Force grid redraw
    updateGridCache();
}

void VisualizerComponent::timerCallback()
{
    repaint();
}

//==============================================================================
// NewProjectAudioProcessorEditor Implementation
//==============================================================================

NewProjectAudioProcessorEditor::NewProjectAudioProcessorEditor(NewProjectAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p), visualizer(p)
{
    setSize(600, 400);

    // --- Filter Active Button ---
    addAndMakeVisible(filterActiveButton);
    filterActiveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, "filterActive", filterActiveButton);

    // --- Cutoff Frequency Toggle Button ---
    addAndMakeVisible(cutoffToggleButton);
    cutoffAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, "lowCutoff", cutoffToggleButton);

    // --- Visualizer Toggle Button ---
    addAndMakeVisible(visualizerToggleButton);
    visualizerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, "visualizer", visualizerToggleButton);

    visualizerToggleButton.onClick = [this]() {
        visualizer.setVisualizerActive(visualizerToggleButton.getToggleState());
        };

    // Initialize visualizer state from saved parameter
    visualizer.setVisualizerActive(visualizerToggleButton.getToggleState());

    // --- Visualizer Component ---
    addAndMakeVisible(visualizer);

    // --- Info Labels ---
    addAndMakeVisible(dcOffsetLabel);
    dcOffsetLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    dcOffsetLabel.setJustificationType(juce::Justification::centredLeft);
    dcOffsetLabel.setFont(14.0f);

    addAndMakeVisible(rmsLabel);
    rmsLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    rmsLabel.setJustificationType(juce::Justification::centredLeft);
    rmsLabel.setFont(14.0f);

    addAndMakeVisible(peakLabel);
    peakLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    peakLabel.setJustificationType(juce::Justification::centredLeft);
    peakLabel.setFont(14.0f);

    addAndMakeVisible(lowFreqLabel);
    lowFreqLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
    lowFreqLabel.setJustificationType(juce::Justification::centredLeft);
    lowFreqLabel.setFont(14.0f);
    lowFreqLabel.setText("LF Energy: --%", juce::dontSendNotification);

    addAndMakeVisible(infoLabel);
    infoLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    infoLabel.setJustificationType(juce::Justification::centred);
    infoLabel.setFont(16.0f);
    infoLabel.setText("DC Offset Remover", juce::dontSendNotification);

    // Start metrics timer
    metricsTimer.startTimerHz(10); // Update metrics 10 times per second

    updateMetricsDisplay();
}

NewProjectAudioProcessorEditor::~NewProjectAudioProcessorEditor()
{
    metricsTimer.stopTimer();
}

void NewProjectAudioProcessorEditor::updateMetricsDisplay()
{
    float dcOffset = audioProcessor.getDCOffset();
    float rms = audioProcessor.getRMSLevel();
    float peak = audioProcessor.getPeakLevel();
    float lowFreq = audioProcessor.getLowFreqLevel();

    juce::String dcText = "DC: " + juce::String(dcOffset * 100.0f, 3) + "%";
    juce::String rmsText = "RMS: " + juce::String(rms * 100.0f, 2) + "%";
    juce::String peakText = "Peak: " + juce::String(peak * 100.0f, 2) + "%";
    juce::String lowFreqText = "LF: " + juce::String(lowFreq * 100.0f, 2) + "%";

    dcOffsetLabel.setText(dcText, juce::dontSendNotification);
    rmsLabel.setText(rmsText, juce::dontSendNotification);
    peakLabel.setText(peakText, juce::dontSendNotification);
    lowFreqLabel.setText(lowFreqText, juce::dontSendNotification);
}

void NewProjectAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::darkgrey.darker(0.8f));

    // Draw header
    auto headerArea = getLocalBounds().removeFromTop(40);
    g.setGradientFill(juce::ColourGradient(
        juce::Colours::darkblue, 0, 0,
        juce::Colours::black, 0, (float)headerArea.getHeight(), false));
    g.fillRect(headerArea);

    // Draw footer
    auto footerArea = getLocalBounds().removeFromBottom(20);
    g.setColour(juce::Colours::darkgrey);
    g.fillRect(footerArea);
    g.setColour(juce::Colours::lightgrey);
    g.setFont(12.0f);
    juce::String versionText = "v1.0 | " + juce::String(JUCE_MAJOR_VERSION) + "." + juce::String(JUCE_MINOR_VERSION);
    g.drawText(versionText, footerArea, juce::Justification::centred);
}

void NewProjectAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced(10);

    // Header area
    auto headerArea = bounds.removeFromTop(40);
    infoLabel.setBounds(headerArea);

    // Control area
    auto controlArea = bounds.removeFromTop(50);

    int buttonWidth = controlArea.getWidth() / 3;
    filterActiveButton.setBounds(controlArea.removeFromLeft(buttonWidth).reduced(5));
    cutoffToggleButton.setBounds(controlArea.removeFromLeft(buttonWidth).reduced(5));
    visualizerToggleButton.setBounds(controlArea.reduced(5));

    // Metrics area
    auto metricsArea = bounds.removeFromTop(25);
    int metricWidth = metricsArea.getWidth() / 4;
    dcOffsetLabel.setBounds(metricsArea.removeFromLeft(metricWidth).reduced(2));
    rmsLabel.setBounds(metricsArea.removeFromLeft(metricWidth).reduced(2));
    peakLabel.setBounds(metricsArea.removeFromLeft(metricWidth).reduced(2));
    lowFreqLabel.setBounds(metricsArea.reduced(2));

    // Visualizer area (remaining space)
    visualizer.setBounds(bounds.reduced(5));
}