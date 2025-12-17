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

    // Public so visualizer can access the size and write index
    static constexpr int fifoSize = 1024;
    std::atomic<int> fifoWriteIndex{ 0 };

private:
    // Define the filter type: High-pass, 2nd order (12dB/oct)
    using Filter = juce::dsp::IIR::Filter<float>;
    using FilterCoefs = juce::dsp::IIR::Coefficients<float>;

    // ProcessorChain makes it easy to handle multi-channel processing
    juce::dsp::ProcessorChain<Filter> filterChain;

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Visualizer data - lock-free communication between audio and GUI threads
    std::atomic<bool> visualizerActive{ false };
    float visualizerFifo[fifoSize];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NewProjectAudioProcessor)
};