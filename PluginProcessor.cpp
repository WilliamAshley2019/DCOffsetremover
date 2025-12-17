#include "PluginProcessor.h"
#include "PluginEditor.h"

NewProjectAudioProcessor::NewProjectAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    // Initialize FIFO buffer
    std::fill(std::begin(visualizerFifo), std::end(visualizerFifo), 0.0f);
}

NewProjectAudioProcessor::~NewProjectAudioProcessor()
{
}

juce::AudioProcessorValueTreeState::ParameterLayout NewProjectAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterBool>(
        "bypass", "Bypass", false));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        "visualizer", "Visualizer", false));

    return layout;
}

void NewProjectAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // Setup DSP processing spec
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = static_cast<juce::uint32>(getTotalNumOutputChannels());

    filterChain.prepare(spec);

    // Configure 20Hz high-pass filter (industry standard for DC offset removal)
    auto coefficients = FilterCoefs::makeHighPass(sampleRate, 20.0f);
    *filterChain.get<0>().coefficients = *coefficients;

    // Clear FIFO buffer
    std::fill(std::begin(visualizerFifo), std::end(visualizerFifo), 0.0f);
    fifoWriteIndex.store(0);
}

void NewProjectAudioProcessor::releaseResources()
{
    // Resources released automatically by JUCE DSP classes
}

void NewProjectAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused(midiMessages);

    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Clear unused output channels
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // Check bypass state - minimal CPU cost when bypassed
    auto isBypassed = *apvts.getRawParameterValue("bypass") > 0.5f;

    if (isBypassed)
    {
        // Reset filter state to prevent clicks when re-enabling
        filterChain.reset();
        return;
    }

    // Block-based processing using JUCE DSP (SIMD optimized)
    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> context(block);
    filterChain.process(context);

    // Visualizer logic - only executes if active (near-zero cost when disabled)
    if (visualizerActive.load(std::memory_order_relaxed))
    {
        auto* channelData = buffer.getReadPointer(0);
        int numSamples = buffer.getNumSamples();

        // Push samples to FIFO (lock-free circular buffer)
        for (int i = 0; i < numSamples; ++i)
        {
            int writePos = fifoWriteIndex.fetch_add(1, std::memory_order_relaxed) % fifoSize;
            visualizerFifo[writePos] = channelData[i];
        }
    }
}

juce::AudioProcessorEditor* NewProjectAudioProcessor::createEditor()
{
    return new NewProjectAudioProcessorEditor(*this);
}

void NewProjectAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void NewProjectAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState != nullptr)
        if (xmlState->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NewProjectAudioProcessor();
}