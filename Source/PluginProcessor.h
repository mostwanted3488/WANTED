#pragma once
#include <JuceHeader.h>
#include "WantedDSP.h"

class WantedProcessor : public juce::AudioProcessor
{
public:
    WantedProcessor();
    ~WantedProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // taille de la fenêtre (hauteur en px, bandeau 4:1) mémorisée avec le projet
    std::atomic<int> uiHeight { 120 };

private:
    void processChunk (juce::AudioBuffer<float>& buffer, int start, int num);

    std::atomic<float>* starsParam = nullptr;

    double fs = 48000.0, osRate = 768000.0;
    int maxBlock = 512, osFactor = 16;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;

    wanted::RatShaper shaper;
    std::array<wanted::WetChannel, 2> wet;
    std::array<wanted::KWeight, 2> kIn, kOut;
    wanted::AutoGain autoGain;
    wanted::OutputGuard guard;

    // réglages lissés (constante de temps 30 ms)
    wanted::StarSettings cur {}, tgt {};
    double wetMix = 0.0, wetTarget = 0.0, lastGbw = -1, lastTone = -1, autoLin = 1.0;

    juce::AudioBuffer<float> dryBuffer;
    std::vector<double> wmBuf;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelay { 1 << 12 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WantedProcessor)
};
