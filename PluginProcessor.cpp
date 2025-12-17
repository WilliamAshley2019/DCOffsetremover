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
    currentSampleRate = sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = static_cast<juce::uint32>(getTotalNumOutputChannels());

    filterChain.prepare(spec);
    analysisFilterChain.prepare(spec);

    // Set initial filter coefficients
    updateFilterCoefficients();
    updateAnalysisFilterCoefficients();

    // Clear FIFO and reset write index
    std::fill(std::begin(visualizerFifo), std::end(visualizerFifo), 0.0f);
    fifoWriteIndex.store(0, std::memory_order_relaxed);

    // Reset metrics
    dcOffset.store(0.0f, std::memory_order_relaxed);
    rmsLevel.store(0.0f, std::memory_order_relaxed);
    peakLevel.store(0.0f, std::memory_order_relaxed);
    lowFreqLevel.store(0.0f, std::memory_order_relaxed);
    rmsSum = 0.0f;
    lowFreqSum = 0.0f;
    rmsSampleCount = 0;
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

void NewProjectAudioProcessor::updateFilterCoefficients()
{
    auto coefficients = FilterCoefs::makeHighPass(currentSampleRate, currentCutoffHz);
    *filterChain.get<0>().coefficients = *coefficients;
}

void NewProjectAudioProcessor::updateAnalysisFilterCoefficients()
{
    // Create a low-pass filter at the same cutoff frequency to analyze what's being removed
    auto coefficients = FilterCoefs::makeLowPass(currentSampleRate, currentCutoffHz);
    *analysisFilterChain.get<0>().coefficients = *coefficients;
}

void NewProjectAudioProcessor::updateAudioMetrics(const juce::AudioBuffer<float>& buffer)
{
    int numSamples = buffer.getNumSamples();

    if (buffer.getNumChannels() > 0)
    {
        auto* channelData = buffer.getReadPointer(0);
        float sum = 0.0f;
        float peak = 0.0f;

        // Calculate DC offset and peak
        for (int i = 0; i < numSamples; ++i)
        {
            float sample = channelData[i];
            sum += sample;
            peak = juce::jmax(peak, std::abs(sample));
        }

        dcOffset.store(sum / numSamples, std::memory_order_relaxed);
        peakLevel.store(peak, std::memory_order_relaxed);

        // Calculate RMS - optimized with running sum
        for (int i = 0; i < numSamples; ++i)
        {
            float sample = channelData[i];
            rmsSum += sample * sample;
            rmsSampleCount++;
        }

        // Update RMS every rmsUpdateInterval samples to reduce CPU
        if (rmsSampleCount >= rmsUpdateInterval)
        {
            rmsLevel.store(std::sqrt(rmsSum / rmsSampleCount), std::memory_order_relaxed);
            rmsSum = 0.0f;
            rmsSampleCount = 0;
        }

        // Calculate low-frequency energy (what's being filtered out)
        // Only do this if visualizer is active to save CPU
        if (visualizerActive.load(std::memory_order_relaxed))
        {
            // Create a temporary buffer for analysis
            juce::AudioBuffer<float> tempBuffer(1, numSamples);
            tempBuffer.copyFrom(0, 0, channelData, numSamples);

            // Apply low-pass filter to isolate frequencies below cutoff
            juce::dsp::AudioBlock<float> block(tempBuffer);
            juce::dsp::ProcessContextReplacing<float> context(block);
            analysisFilterChain.process(context);

            // Calculate RMS of low-frequency content
            float lowFreqRMS = 0.0f;
            auto* lowFreqData = tempBuffer.getReadPointer(0);
            for (int i = 0; i < numSamples; ++i)
            {
                lowFreqRMS += lowFreqData[i] * lowFreqData[i];
            }
            lowFreqRMS = std::sqrt(lowFreqRMS / numSamples);
            lowFreqLevel.store(lowFreqRMS, std::memory_order_relaxed);
        }
    }
}

void NewProjectAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused(midiMessages);

    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Clear any unused output channels
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // Update audio metrics for display (low overhead)
    updateAudioMetrics(buffer);

    // Check filter state
    bool filterActive = *apvts.getRawParameterValue("filterActive") > 0.5f;
    bool wasActive = wasFilterActive.exchange(filterActive, std::memory_order_relaxed);

    // Check cutoff frequency
    bool lowCutoff = *apvts.getRawParameterValue("lowCutoff") > 0.5f;
    float targetCutoff = lowCutoff ? CUTOFF_10HZ : CUTOFF_20HZ;

    if (targetCutoff != currentCutoffHz)
    {
        currentCutoffHz = targetCutoff;
        updateFilterCoefficients();
        updateAnalysisFilterCoefficients();
    }

    // Reset filter state ONLY when transitioning from active to inactive
    if (wasActive && !filterActive)
    {
        filterChain.reset();
        analysisFilterChain.reset();
    }

    if (!filterActive)
    {
        // Filter is OFF - pass through
        if (!visualizerActive.load(std::memory_order_relaxed))
            return;
    }
    else
    {
        // Filter is ACTIVE - process the audio
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        filterChain.process(context);
    }

    // VISUALIZER LOGIC: Only runs if explicitly enabled
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

    // Cutoff frequency toggle (10Hz vs 20Hz)
    // Default to 20Hz for compatibility with most audio systems
    layout.add(std::make_unique<juce::AudioParameterBool>("lowCutoff", "10Hz Mode", false));

    // Visualizer state (GUI only, doesn't affect audio processing)
    layout.add(std::make_unique<juce::AudioParameterBool>("visualizer", "Visualizer", false));

    return layout;
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NewProjectAudioProcessor();
}