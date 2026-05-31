#pragma once

#include <JuceHeader.h>
#include "DSP/MistDriver.h"

// Mist — single-knob reverb (MistDriver). A "Mist" macro (0..10) sweeps the dry→wet
// amount of an original Moorer/FDN reverb; a 3-mode "Size" toggle (Room/Hall/Cathedral)
// scales the network and decay. See DSP/MistDriver.h.
class MistAudioProcessor : public juce::AudioProcessor
{
public:
    MistAudioProcessor();
    ~MistAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Mist"; }
    bool acceptsMidi()  const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    // Reverb tail — hosts must keep flushing after input stops (Cathedral ≈ 3 s decay).
    double getTailLengthSeconds() const override { return 3.6; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // The "Mist" 0..10 knob position shown as a wet-amount percentage. Shared by the
    // parameter's value-display and the editor's knob readout so both agree.
    static juce::String mistToWetString (double knob0to10);

    // Output peak for the front-panel metering LED. processBlock keeps the running max;
    // the editor's timer drains it (exchange -> 0) each frame so no transient is missed
    // between repaints. Lock-free.
    float fetchPeak() noexcept { return uiLevel.exchange (0.0f, std::memory_order_relaxed); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    std::atomic<float> uiLevel { 0.0f };           // output peak hold (drained by the UI)

    MistDriver engine;                             // one stereo reverb (cross-channel decorrelation)
    std::atomic<float>* mistParam   = nullptr;     // 0..10 macro (dry→wet)
    std::atomic<float>* sizeParam   = nullptr;     // choice index 0..2 (Room/Hall/Cathedral)
    std::atomic<float>* bypassParam = nullptr;     // A/B compare (engine stays warm)

    juce::AudioBuffer<float> monoR;                // right-channel scratch for the mono case

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MistAudioProcessor)
};
