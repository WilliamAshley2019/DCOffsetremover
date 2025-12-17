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
    // 1st-order filter has infinite impulse response technically, but very short tail
    // 2nd-order filters have ~50-100ms tail depending on cutoff
    return 0.1; // Conservative estimate
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

    filterChain2Pole.prepare(spec);
    analysisFilterChain.prepare(spec);

    // Set initial filter coefficients
    updateFilterCoefficients();
    updateAnalysisFilterCoefficients();
    updateOnePoleCoefficients();

    // Initialize 1st-order DC blocker state (per channel)
    int numChannels = static_cast<int>(spec.numChannels);
    dcXPrev.assign(numChannels, 0.0f);
    dcYPrev.assign(numChannels, 0.0f);

    // Clear FIFO and reset write index
    std::fill(std::begin(visualizerFifo), std::end(visualizerFifo), 0.0f);
    fifoWriteIndex.store(0, std::memory_order_relaxed);

    // Reset all metrics
    dcOffsetPre.store(0.0f, std::memory_order_relaxed);
    rmsPre.store(0.0f, std::memory_order_relaxed);
    peakPre.store(0.0f, std::memory_order_relaxed);
    lowFreqPre.store(0.0f, std::memory_order_relaxed);

    dcOffsetPost.store(0.0f, std::memory_order_relaxed);
    rmsPost.store(0.0f, std::memory_order_relaxed);
    peakPost.store(0.0f, std::memory_order_relaxed);
    lowFreqPost.store(0.0f, std::memory_order_relaxed);

    rmsSumPre = 0.0f;
    rmsSumPost = 0.0f;
    lowFreqSumPre = 0.0f;
    lowFreqSumPost = 0.0f;
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
    // Update 2nd-order filter coefficients based on current mode
    int mode = currentFilterMode.load(std::memory_order_relaxed);
    float cutoff = (mode == MODE_2POLE_10HZ) ? CUTOFF_10HZ : CUTOFF_20HZ;

    auto coefficients = FilterCoefs::makeHighPass(currentSampleRate, cutoff);
    *filterChain2Pole.get<0>().coefficients = *coefficients;
}

void NewProjectAudioProcessor::updateAnalysisFilterCoefficients()
{
    // Create a low-pass filter at the appropriate cutoff
    int mode = currentFilterMode.load(std::memory_order_relaxed);
    float cutoff = CUTOFF_20HZ; // Default

    if (mode == MODE_2POLE_10HZ) {
        cutoff = CUTOFF_10HZ;
    }
    else if (mode == MODE_DC_1POLE) {
        cutoff = CUTOFF_1POLE; // 1st-order filter targets ~5Hz
    }
    // MODE_BYPASS uses the default 20Hz for analysis

    auto coefficients = FilterCoefs::makeLowPass(currentSampleRate, cutoff);
    *analysisFilterChain.get<0>().coefficients = *coefficients;
}

void NewProjectAudioProcessor::updateOnePoleCoefficients()
{
    // CORRECTED: Use exact discrete-time coefficient
    // R = exp(-2π * fc / fs)
    // For fc = 5Hz at 44.1kHz: R = exp(-2π * 5 / 44100) ≈ 0.999285
    // For fc = 5Hz at 48kHz: R = exp(-2π * 5 / 48000) ≈ 0.999345

    float omega = 2.0f * juce::MathConstants<float>::pi * CUTOFF_1POLE / currentSampleRate;
    dcR = std::exp(-omega);
}

void NewProjectAudioProcessor::updatePreFilterMetrics(const juce::AudioBuffer<float>& buffer)
{
    int numSamples = buffer.getNumSamples();

    if (buffer.getNumChannels() > 0)
    {
        auto* channelData = buffer.getReadPointer(0);
        float sum = 0.0f;
        float peak = 0.0f;

        // Calculate DC offset and peak for pre-filter
        for (int i = 0; i < numSamples; ++i)
        {
            float sample = channelData[i];
            sum += sample;
            peak = juce::jmax(peak, std::abs(sample));
            rmsSumPre += sample * sample;
        }

        dcOffsetPre.store(sum / numSamples, std::memory_order_relaxed);
        peakPre.store(peak, std::memory_order_relaxed);

        // Calculate low-frequency energy for pre-filter
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
            lowFreqSumPre += lowFreqData[i] * lowFreqData[i];
        }

        rmsSampleCount += numSamples;

        // Update RMS and low frequency every rmsUpdateInterval samples
        if (rmsSampleCount >= rmsUpdateInterval)
        {
            rmsPre.store(std::sqrt(rmsSumPre / rmsSampleCount), std::memory_order_relaxed);
            lowFreqPre.store(std::sqrt(lowFreqSumPre / rmsSampleCount), std::memory_order_relaxed);
            rmsSumPre = 0.0f;
            lowFreqSumPre = 0.0f;
            rmsSampleCount = 0;
        }
    }
}

void NewProjectAudioProcessor::updatePostFilterMetrics(const juce::AudioBuffer<float>& buffer)
{
    int numSamples = buffer.getNumSamples();

    if (buffer.getNumChannels() > 0)
    {
        auto* channelData = buffer.getReadPointer(0);
        float sum = 0.0f;
        float peak = 0.0f;

        // Calculate DC offset and peak for post-filter
        for (int i = 0; i < numSamples; ++i)
        {
            float sample = channelData[i];
            sum += sample;
            peak = juce::jmax(peak, std::abs(sample));
            rmsSumPost += sample * sample;
        }

        dcOffsetPost.store(sum / numSamples, std::memory_order_relaxed);
        peakPost.store(peak, std::memory_order_relaxed);

        // The low-frequency content should be much lower after filtering
        // Create a temporary buffer for analysis
        juce::AudioBuffer<float> tempBuffer(1, numSamples);
        tempBuffer.copyFrom(0, 0, channelData, numSamples);

        // Reset analysis filter for accurate post-filter measurement
        analysisFilterChain.reset();

        // Apply low-pass filter
        juce::dsp::AudioBlock<float> block(tempBuffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        analysisFilterChain.process(context);

        // Calculate RMS of remaining low-frequency content
        float lowFreqRMS = 0.0f;
        auto* lowFreqData = tempBuffer.getReadPointer(0);
        for (int i = 0; i < numSamples; ++i)
        {
            lowFreqRMS += lowFreqData[i] * lowFreqData[i];
            lowFreqSumPost += lowFreqData[i] * lowFreqData[i];
        }

        // We use the same rmsSampleCount as pre-filter
        if (rmsSampleCount >= rmsUpdateInterval)
        {
            rmsPost.store(std::sqrt(rmsSumPost / rmsSampleCount), std::memory_order_relaxed);
            lowFreqPost.store(std::sqrt(lowFreqSumPost / rmsSampleCount), std::memory_order_relaxed);
            rmsSumPost = 0.0f;
            lowFreqSumPost = 0.0f;
        }
    }
}

void NewProjectAudioProcessor::processOnePoleDCBlocker(juce::AudioBuffer<float>& buffer)
{
    // CORRECTED: Canonical 1st-order DC blocker with persistent state
    // y[n] = x[n] - x[n-1] + R * y[n-1]
    // State persists forever across blocks

    int numChannels = buffer.getNumChannels();
    int numSamples = buffer.getNumSamples();

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* channelData = buffer.getWritePointer(ch);
        float xPrev = dcXPrev[ch];
        float yPrev = dcYPrev[ch];

        for (int i = 0; i < numSamples; ++i)
        {
            float x = channelData[i];

            // CORRECT FORMULA: y[n] = x[n] - x[n-1] + R * y[n-1]
            float y = x - xPrev + dcR * yPrev;

            // Apply filter
            channelData[i] = y;

            // Update state for next sample
            xPrev = x;
            yPrev = y;
        }

        // Store state for next block
        dcXPrev[ch] = xPrev;
        dcYPrev[ch] = yPrev;
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

    // 1. Get PRE-filter metrics (input signal)
    updatePreFilterMetrics(buffer);

    // 2. Get current filter mode from parameters - CORRECTED: Use parameter as-is
    int newFilterMode = static_cast<int>(*apvts.getRawParameterValue("filterMode"));
    int oldFilterMode = currentFilterMode.exchange(newFilterMode, std::memory_order_relaxed);

    // 3. Store a copy of the input buffer for visualizer when in bypass
    juce::AudioBuffer<float> inputBufferCopy;
    bool needVisualizer = visualizerActive.load(std::memory_order_relaxed);

    if (needVisualizer && newFilterMode == MODE_BYPASS)
    {
        inputBufferCopy.makeCopyOf(buffer);
    }

    // 4. Check if we need to update filter coefficients due to mode change
    if (newFilterMode != oldFilterMode)
    {
        if (newFilterMode == MODE_DC_1POLE)
        {
            // Only need to update analysis filter coefficients for 1st-order mode
            updateAnalysisFilterCoefficients();
            // NOTE: Do NOT reset the 1st-order filter state!
            // State persists forever for proper DC blocker operation
        }
        else if (newFilterMode == MODE_2POLE_10HZ || newFilterMode == MODE_2POLE_20HZ)
        {
            // Reset 2nd-order filter state and update coefficients
            filterChain2Pole.reset();
            updateFilterCoefficients();
            updateAnalysisFilterCoefficients();
        }
        else if (newFilterMode == MODE_BYPASS)
        {
            // Bypass mode - update analysis filter to default 20Hz
            updateAnalysisFilterCoefficients();
        }
    }

    // 5. Apply appropriate filter based on mode
    if (newFilterMode == MODE_BYPASS)
    {
        // TRUE BYPASS: Do absolutely nothing to the audio
        // Audio passes through unchanged
    }
    else if (newFilterMode == MODE_DC_1POLE)
    {
        // 1st-order DC blocker with persistent state
        processOnePoleDCBlocker(buffer);
    }
    else if (newFilterMode == MODE_2POLE_10HZ || newFilterMode == MODE_2POLE_20HZ)
    {
        // 2nd-order filter with selectable cutoff
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        filterChain2Pole.process(context);
    }

    // 6. Get POST-filter metrics (output signal - what you actually hear)
    updatePostFilterMetrics(buffer);

    // 7. VISUALIZER LOGIC: Only runs if explicitly enabled
    if (needVisualizer)
    {
        // Determine which buffer to use for visualizer
        auto* channelData = (newFilterMode == MODE_BYPASS) ?
            inputBufferCopy.getReadPointer(0) :
            buffer.getReadPointer(0);
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

    // CORRECTED: 4 modes with proper mapping
    // 0 = Bypass, 1 = 1st-order DC blocker, 2 = 2nd-order 10Hz, 3 = 2nd-order 20Hz
    juce::StringArray filterModes;
    filterModes.add("Bypass");
    filterModes.add("1st-order DC blocker (6dB/oct)");
    filterModes.add("2nd-order 10Hz (12dB/oct)");
    filterModes.add("2nd-order 20Hz (12dB/oct)");

    layout.add(std::make_unique<juce::AudioParameterChoice>("filterMode", "Filter Mode",
        filterModes, 3)); // Default to 20Hz

    // Visualizer state (GUI only, doesn't affect audio processing)
    layout.add(std::make_unique<juce::AudioParameterBool>("visualizer", "Visualizer", false));

    return layout;
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NewProjectAudioProcessor();
}