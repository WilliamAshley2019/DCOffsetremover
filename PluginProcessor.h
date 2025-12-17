#pragma once

#include <JuceHeader.h>

class NewProjectAudioProcessor : public juce::AudioProcessor
{
public:
    NewProjectAudioProcessor();
    ~NewProjectAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // Visualizer support - helper methods for GUI thread
    void setVisualizerState(bool active) { visualizerActive.store(active, std::memory_order_relaxed); }
    float getNextSampleForVisualizer(int index) const { return visualizerFifo[index % fifoSize]; }
    float getDCOffset() const { return dcOffset.load(std::memory_order_relaxed); }
    float getRMSLevel() const { return rmsLevel.load(std::memory_order_relaxed); }
    float getPeakLevel() const { return peakLevel.load(std::memory_order_relaxed); }
    float getLowFreqLevel() const { return lowFreqLevel.load(std::memory_order_relaxed); }

    // Public so visualizer can access the size and write index
    static constexpr int fifoSize = 1024;
    std::atomic<int> fifoWriteIndex{ 0 };

private:
    // Define the filter type: High-pass, 2nd order (12dB/oct)
    using Filter = juce::dsp::IIR::Filter<float>;
    using FilterCoefs = juce::dsp::IIR::Coefficients<float>;

    // ProcessorChain makes it easy to handle multi-channel processing
    juce::dsp::ProcessorChain<Filter> filterChain;
    juce::dsp::ProcessorChain<Filter> analysisFilterChain; // For low-frequency analysis

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Filter state management
    std::atomic<bool> wasFilterActive{ false };

    // Cutoff frequency
    static constexpr float CUTOFF_20HZ = 20.0f;
    static constexpr float CUTOFF_10HZ = 10.0f;
    float currentCutoffHz{ CUTOFF_20HZ };

    // Sample rate for filter calculations
    double currentSampleRate{ 44100.0 };

    // Visualizer data - lock-free communication between audio and GUI threads
    std::atomic<bool> visualizerActive{ false };
    float visualizerFifo[fifoSize];

    // Audio metrics - low overhead calculation
    std::atomic<float> dcOffset{ 0.0f };
    std::atomic<float> rmsLevel{ 0.0f };
    std::atomic<float> peakLevel{ 0.0f };
    std::atomic<float> lowFreqLevel{ 0.0f }; // Energy below filter cutoff

    // RMS calculation
    float rmsSum{ 0.0f };
    float lowFreqSum{ 0.0f };
    int rmsSampleCount{ 0 };
    const int rmsUpdateInterval = 256; // Update RMS every N samples

    void updateFilterCoefficients();
    void updateAnalysisFilterCoefficients();
    void updateAudioMetrics(const juce::AudioBuffer<float>& buffer);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NewProjectAudioProcessor)
};