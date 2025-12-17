#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
NewProjectAudioProcessor::NewProjectAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
    )
#endif
    , apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    // Initialize FIFO to zero
    std::fill(std::begin(visualizerFifo), std::end(visualizerFifo), 0.0f);
}

NewProjectAudioProcessor::~NewProjectAudioProcessor()
{
}

//==============================================================================
const juce::String NewProjectAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool NewProjectAudioProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
    return true;
#else
    return false;
#endif
}

bool NewProjectAudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
    return true;
#else
    return false;
#endif
}

bool NewProjectAudioProcessor::isMidiEffect() const
{
#if JucePlugin_IsMidiEffect
    return true;
#else
    return false;
#endif
}

double NewProjectAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int NewProjectAudioProcessor::getNumPrograms()
{
    return 1;
}

int NewProjectAudioProcessor::getCurrentProgram()
{
    return 0;
}

void NewProjectAudioProcessor::setCurrentProgram(int index)
{
    juce::ignoreUnused(index);
}

const juce::String NewProjectAudioProcessor::getProgramName(int index)
{
    juce::ignoreUnused(index);
    return { "Default" };
}

void NewProjectAudioProcessor::changeProgramName(int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

//==============================================================================
void NewProjectAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = static_cast<juce::uint32>(getTotalNumOutputChannels());

    filterChain.prepare(spec);

    // Set coefficients for 20Hz High Pass
    auto coefficients = FilterCoefs::makeHighPass(sampleRate, 20.0f);
    *filterChain.get<0>().coefficients = *coefficients;

    // Clear FIFO and reset write index
    std::fill(std::begin(visualizerFifo), std::end(visualizerFifo), 0.0f);
    fifoWriteIndex.store(0, std::memory_order_relaxed);
}

void NewProjectAudioProcessor::releaseResources()
{
    // Resources released automatically
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool NewProjectAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused(layouts);
    return true;
#else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

#if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
#endif

    return true;
#endif
}
#endif

void NewProjectAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused(midiMessages);

    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Clear any unused output channels
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // CRITICAL FIX: Check if filter is ACTIVE (not bypassed)
    // The button says "DC Filter Active" so when checked (true), we should FILTER
    bool filterActive = *apvts.getRawParameterValue("filterActive") > 0.5f;

    if (!filterActive)
    {
        // Filter is OFF - do absolutely nothing (full sleep mode)
        // Audio passes through untouched, zero CPU cost
        // Don't even reset the filter state to save CPU

        // If visualizer is also off, this is the absolute minimum processing path
        if (!visualizerActive.load(std::memory_order_relaxed))
        {
            // FULL SLEEP MODE: Both filter and visualizer disabled
            // This should have the absolute lowest CPU usage
            return;
        }
    }
    else
    {
        // Filter is ACTIVE - process the audio
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        filterChain.process(context);
    }

    // VISUALIZER LOGIC: Only runs if explicitly enabled
    // This check is extremely cheap (single atomic load)
    if (visualizerActive.load(std::memory_order_relaxed))
    {
        auto* channelData = buffer.getReadPointer(0);
        int numSamples = buffer.getNumSamples();

        // Push samples to lock-free FIFO
        for (int i = 0; i < numSamples; ++i)
        {
            int writePos = fifoWriteIndex.fetch_add(1, std::memory_order_relaxed) % fifoSize;
            visualizerFifo[writePos] = channelData[i];
        }
    }
}

//==============================================================================
bool NewProjectAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* NewProjectAudioProcessor::createEditor()
{
    return new NewProjectAudioProcessorEditor(*this);
}

//==============================================================================
void NewProjectAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void NewProjectAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout NewProjectAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // RENAMED: "filterActive" instead of "bypass" to avoid confusion
    // When true, the filter is ON. When false, filter is OFF (full bypass/sleep)
    layout.add(std::make_unique<juce::AudioParameterBool>("filterActive", "Filter Active", true));

    // Visualizer state (GUI only, doesn't affect audio processing)
    layout.add(std::make_unique<juce::AudioParameterBool>("visualizer", "Visualizer", false));

    return layout;
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NewProjectAudioProcessor();
}