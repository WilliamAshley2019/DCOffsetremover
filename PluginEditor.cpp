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

    // Get POST-filter metrics (what you're actually hearing)
    float dcOffsetPost = audioProcessor.getDCOffsetPost();
    float lowFreqPost = audioProcessor.getLowFreqPost();

    // Draw waveform (POST-filter)
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

    // Draw DC offset line (POST-filter - what's actually in the output)
    float dcY = yOffset - (dcOffsetPost * yScale);
    g.setColour(juce::Colours::red.withAlpha(0.7f));
    g.drawHorizontalLine(juce::roundToInt(dcY), 0.0f, (float)getWidth());

    // Draw DC offset value (POST-filter)
    g.setColour(juce::Colours::white);
    g.setFont(12.0f);
    juce::String dcText = "DC Out: " + juce::String(dcOffsetPost * 100.0f, 3) + "%";
    g.drawText(dcText, 10, 10, 100, 20, juce::Justification::left);

    // Draw low-frequency energy meter (POST-filter - what's left after filtering)
    if (lowFreqPost > 0.001f)
    {
        g.setColour(juce::Colours::orange.withAlpha(0.5f));
        float lowFreqHeight = lowFreqPost * getHeight() / 2.0f;
        g.fillRect(0.0f, yOffset - lowFreqHeight, 8.0f, lowFreqHeight * 2.0f);
    }

    // Draw filter mode indicator
    int filterMode = audioProcessor.getFilterMode();
    juce::String modeText;
    juce::Colour modeColor;

    switch (filterMode)
    {
    case 0: // Bypass
        modeText = "BYPASS";
        modeColor = juce::Colours::red;
        break;
    case 1: // 1st-order DC blocker
        modeText = "1st-order DC blocker (~5Hz)";
        modeColor = juce::Colours::yellow;
        break;
    case 2: // 2nd-order 10Hz
        modeText = "2nd-order 10Hz HPF";
        modeColor = juce::Colours::green;
        break;
    case 3: // 2nd-order 20Hz
        modeText = "2nd-order 20Hz HPF";
        modeColor = juce::Colours::cyan;
        break;
    default:
        modeText = "Unknown";
        modeColor = juce::Colours::grey;
    }

    g.setColour(modeColor);
    g.setFont(14.0f);
    g.drawText(modeText, getWidth() / 2 - 150, getHeight() - 30, 300, 20, juce::Justification::centred);

    // Indicate this is POST-filter view
    g.setColour(juce::Colours::lightgrey);
    g.setFont(12.0f);
    g.drawText("Output Signal", 10, getHeight() - 20, 100, 20, juce::Justification::left);

    // Add 1st-order note if applicable
    if (filterMode == 1)
    {
        g.setColour(juce::Colours::yellow.withAlpha(0.7f));
        g.setFont(11.0f);
        g.drawText("Stateful: y[n] = x[n] - x[n-1] + R·y[n-1]", getWidth() - 200, 10, 190, 20, juce::Justification::right);
    }
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
    setSize(600, 450);

    // --- Filter Mode ComboBox ---
    addAndMakeVisible(filterModeComboBox);
    filterModeComboBox.addItem("Bypass (No Filter)", 1);
    filterModeComboBox.addItem("1st-order DC blocker (6dB/oct, ~5Hz)", 2);
    filterModeComboBox.addItem("2nd-order 10Hz HPF (Gentle, 12dB/oct)", 3);
    filterModeComboBox.addItem("2nd-order 20Hz HPF (Standard, 12dB/oct)", 4);
    filterModeComboBox.setSelectedId(4); // Default to 20Hz

    filterModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, "filterMode", filterModeComboBox);

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

    // --- PRE-filter labels (Input) ---
    addAndMakeVisible(preLabel);
    preLabel.setColour(juce::Label::textColourId, juce::Colours::lightblue);
    preLabel.setJustificationType(juce::Justification::centredLeft);
    preLabel.setFont(14.0f);
    preLabel.setText("Input (Pre-filter):", juce::dontSendNotification);

    addAndMakeVisible(dcOffsetLabelPre);
    dcOffsetLabelPre.setColour(juce::Label::textColourId, juce::Colours::white);
    dcOffsetLabelPre.setJustificationType(juce::Justification::centredLeft);
    dcOffsetLabelPre.setFont(12.0f);

    addAndMakeVisible(rmsLabelPre);
    rmsLabelPre.setColour(juce::Label::textColourId, juce::Colours::white);
    rmsLabelPre.setJustificationType(juce::Justification::centredLeft);
    rmsLabelPre.setFont(12.0f);

    addAndMakeVisible(peakLabelPre);
    peakLabelPre.setColour(juce::Label::textColourId, juce::Colours::white);
    peakLabelPre.setJustificationType(juce::Justification::centredLeft);
    peakLabelPre.setFont(12.0f);

    addAndMakeVisible(lowFreqLabelPre);
    lowFreqLabelPre.setColour(juce::Label::textColourId, juce::Colours::lightblue);
    lowFreqLabelPre.setJustificationType(juce::Justification::centredLeft);
    lowFreqLabelPre.setFont(12.0f);

    // --- POST-filter labels (Output - what you hear) ---
    addAndMakeVisible(postLabel);
    postLabel.setColour(juce::Label::textColourId, juce::Colours::lightgreen);
    postLabel.setJustificationType(juce::Justification::centredLeft);
    postLabel.setFont(14.0f);
    postLabel.setText("Output (Post-filter):", juce::dontSendNotification);

    addAndMakeVisible(dcOffsetLabelPost);
    dcOffsetLabelPost.setColour(juce::Label::textColourId, juce::Colours::white);
    dcOffsetLabelPost.setJustificationType(juce::Justification::centredLeft);
    dcOffsetLabelPost.setFont(12.0f);

    addAndMakeVisible(rmsLabelPost);
    rmsLabelPost.setColour(juce::Label::textColourId, juce::Colours::white);
    rmsLabelPost.setJustificationType(juce::Justification::centredLeft);
    rmsLabelPost.setFont(12.0f);

    addAndMakeVisible(peakLabelPost);
    peakLabelPost.setColour(juce::Label::textColourId, juce::Colours::white);
    peakLabelPost.setJustificationType(juce::Justification::centredLeft);
    peakLabelPost.setFont(12.0f);

    addAndMakeVisible(lowFreqLabelPost);
    lowFreqLabelPost.setColour(juce::Label::textColourId, juce::Colours::lightgreen);
    lowFreqLabelPost.setJustificationType(juce::Justification::centredLeft);
    lowFreqLabelPost.setFont(12.0f);

    // --- Filter info label ---
    addAndMakeVisible(filterInfoLabel);
    filterInfoLabel.setColour(juce::Label::textColourId, juce::Colours::yellow);
    filterInfoLabel.setJustificationType(juce::Justification::centred);
    filterInfoLabel.setFont(13.0f);
    filterInfoLabel.setText("DC Offset Remover - Professional", juce::dontSendNotification);

    // --- Info label ---
    addAndMakeVisible(infoLabel);
    infoLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    infoLabel.setJustificationType(juce::Justification::centred);
    infoLabel.setFont(16.0f);
    infoLabel.setText("Professional DC Filter", juce::dontSendNotification);

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
    // PRE-filter values (input)
    float dcOffsetPre = audioProcessor.getDCOffsetPre();
    float rmsPre = audioProcessor.getRMSPre();
    float peakPre = audioProcessor.getPeakPre();
    float lowFreqPre = audioProcessor.getLowFreqPre();

    juce::String dcTextPre = "DC: " + juce::String(dcOffsetPre * 100.0f, 3) + "%";
    juce::String rmsTextPre = "RMS: " + juce::String(rmsPre * 100.0f, 2) + "%";
    juce::String peakTextPre = "Peak: " + juce::String(peakPre * 100.0f, 2) + "%";
    juce::String lowFreqTextPre = "LF: " + juce::String(lowFreqPre * 100.0f, 2) + "%";

    dcOffsetLabelPre.setText(dcTextPre, juce::dontSendNotification);
    rmsLabelPre.setText(rmsTextPre, juce::dontSendNotification);
    peakLabelPre.setText(peakTextPre, juce::dontSendNotification);
    lowFreqLabelPre.setText(lowFreqTextPre, juce::dontSendNotification);

    // POST-filter values (output - what you actually hear)
    float dcOffsetPost = audioProcessor.getDCOffsetPost();
    float rmsPost = audioProcessor.getRMSPost();
    float peakPost = audioProcessor.getPeakPost();
    float lowFreqPost = audioProcessor.getLowFreqPost();

    juce::String dcTextPost = "DC: " + juce::String(dcOffsetPost * 100.0f, 3) + "%";
    juce::String rmsTextPost = "RMS: " + juce::String(rmsPost * 100.0f, 2) + "%";
    juce::String peakTextPost = "Peak: " + juce::String(peakPost * 100.0f, 2) + "%";
    juce::String lowFreqTextPost = "LF: " + juce::String(lowFreqPost * 100.0f, 2) + "%";

    dcOffsetLabelPost.setText(dcTextPost, juce::dontSendNotification);
    rmsLabelPost.setText(rmsTextPost, juce::dontSendNotification);
    peakLabelPost.setText(peakTextPost, juce::dontSendNotification);
    lowFreqLabelPost.setText(lowFreqTextPost, juce::dontSendNotification);

    // Update filter info based on current mode
    int filterMode = audioProcessor.getFilterMode();
    juce::String filterInfo;

    switch (filterMode)
    {
    case 0:
        filterInfo = "BYPASS: True bypass - no processing";
        break;
    case 1:
        filterInfo = "1st-order: Stateful DC blocker (y[n] = x[n] - x[n-1] + R·y[n-1])";
        break;
    case 2:
        filterInfo = "2nd-order: Gentle subsonic filter (10Hz, 12dB/oct)";
        break;
    case 3:
        filterInfo = "2nd-order: Standard DC filter (20Hz, 12dB/oct)";
        break;
    default:
        filterInfo = "Unknown filter mode";
    }

    filterInfoLabel.setText(filterInfo, juce::dontSendNotification);
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
    juce::String versionText = "v2.1 | Professional DC Filter";
    g.drawText(versionText, footerArea, juce::Justification::centred);

    // Draw separator line between pre and post sections
    auto metricsArea = getLocalBounds().reduced(10);
    metricsArea.removeFromTop(40); // Header
    metricsArea.removeFromTop(60); // Controls
    metricsArea.removeFromTop(50); // Pre-filter metrics
    g.setColour(juce::Colours::grey.withAlpha(0.5f));
    g.drawHorizontalLine(metricsArea.getY(), 10.0f, (float)getWidth() - 10.0f);
}

void NewProjectAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced(10);

    // Header area
    auto headerArea = bounds.removeFromTop(40);
    infoLabel.setBounds(headerArea);

    // Filter info area
    auto filterInfoArea = bounds.removeFromTop(25);
    filterInfoLabel.setBounds(filterInfoArea);

    // Control area
    auto controlArea = bounds.removeFromTop(35);
    filterModeComboBox.setBounds(controlArea.removeFromLeft(controlArea.getWidth() * 0.7).reduced(2));
    visualizerToggleButton.setBounds(controlArea.reduced(2));

    // PRE-filter metrics area
    auto preArea = bounds.removeFromTop(25);
    preLabel.setBounds(preArea.removeFromLeft(120).reduced(2));
    int metricWidth = (preArea.getWidth() - 120) / 4;
    dcOffsetLabelPre.setBounds(preArea.removeFromLeft(metricWidth).reduced(2));
    rmsLabelPre.setBounds(preArea.removeFromLeft(metricWidth).reduced(2));
    peakLabelPre.setBounds(preArea.removeFromLeft(metricWidth).reduced(2));
    lowFreqLabelPre.setBounds(preArea.reduced(2));

    // POST-filter metrics area
    auto postArea = bounds.removeFromTop(25);
    postLabel.setBounds(postArea.removeFromLeft(120).reduced(2));
    dcOffsetLabelPost.setBounds(postArea.removeFromLeft(metricWidth).reduced(2));
    rmsLabelPost.setBounds(postArea.removeFromLeft(metricWidth).reduced(2));
    peakLabelPost.setBounds(postArea.removeFromLeft(metricWidth).reduced(2));
    lowFreqLabelPost.setBounds(postArea.reduced(2));

    // Visualizer area (remaining space)
    visualizer.setBounds(bounds.reduced(5));
}