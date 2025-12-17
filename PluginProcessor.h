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

    // Pre-filter metrics (input signal)
    float getDCOffsetPre() const { return dcOffsetPre.load(std::memory_order_relaxed); }
    float getRMSPre() const { return rmsPre.load(std::memory_order_relaxed); }
    float getPeakPre() const { return peakPre.load(std::memory_order_relaxed); }
    float getLowFreqPre() const { return lowFreqPre.load(std::memory_order_relaxed); }

    // Post-filter metrics (output signal - what you actually hear)
    float getDCOffsetPost() const { return dcOffsetPost.load(std::memory_order_relaxed); }
    float getRMSPost() const { return rmsPost.load(std::memory_order_relaxed); }
    float getPeakPost() const { return peakPost.load(std::memory_order_relaxed); }
    float getLowFreqPost() const { return lowFreqPost.load(std::memory_order_relaxed); }

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

    // PRE-filter audio metrics (input signal)
    std::atomic<float> dcOffsetPre{ 0.0f };
    std::atomic<float> rmsPre{ 0.0f };
    std::atomic<float> peakPre{ 0.0f };
    std::atomic<float> lowFreqPre{ 0.0f };

    // POST-filter audio metrics (output signal - what you actually hear)
    std::atomic<float> dcOffsetPost{ 0.0f };
    std::atomic<float> rmsPost{ 0.0f };
    std::atomic<float> peakPost{ 0.0f };
    std::atomic<float> lowFreqPost{ 0.0f };

    // RMS calculation
    float rmsSumPre{ 0.0f };
    float rmsSumPost{ 0.0f };
    float lowFreqSumPre{ 0.0f };
    float lowFreqSumPost{ 0.0f };
    int rmsSampleCount{ 0 };
    const int rmsUpdateInterval = 256; // Update RMS every N samples

    void updateFilterCoefficients();
    void updateAnalysisFilterCoefficients();

    // Separate functions for pre and post analysis
    void updatePreFilterMetrics(const juce::AudioBuffer<float>& buffer);
    void updatePostFilterMetrics(const juce::AudioBuffer<float>& buffer);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NewProjectAudioProcessor)
};