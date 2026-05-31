#include "PluginProcessor.h"
#include "PluginEditor.h"

MistAudioProcessor::MistAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    mistParam   = apvts.getRawParameterValue ("mist");
    sizeParam   = apvts.getRawParameterValue ("size");
    bypassParam = apvts.getRawParameterValue ("bypass");
}

// knob 0..10 -> wet-amount label (the macro is the dry/wet balance).
juce::String MistAudioProcessor::mistToWetString (double knob0to10)
{
    const double pct = juce::jlimit (0.0, 10.0, knob0to10) * 10.0;   // 0..100 %
    return juce::String (juce::roundToInt (pct)) + " %";
}

juce::AudioProcessorValueTreeState::ParameterLayout MistAudioProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // The single macro: Mist 0..10 (dry → drenched). Default a useful mid-wet.
    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "mist", 1 }, "Mist",
        juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), 4.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([](float v, int){ return mistToWetString ((double) v); })));

    // Size selector (scales the FDN + RT60). Default Room (a tight medium room).
    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "size", 1 }, "Size",
        juce::StringArray { "Room", "Hall", "Cathedral" }, 0));

    // A/B compare. When on the plugin outputs the dry input (default off). MistDriver keeps
    // its reverb state warm under bypass, so toggling is click-free.
    layout.add (std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { "bypass", 1 }, "A/B Bypass", false));

    return layout;
}

void MistAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.setSampleRate (sampleRate);
    engine.reset();
    monoR.setSize (1, juce::jmax (1, samplesPerBlock), false, false, true);
}

bool MistAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void MistAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh  = buffer.getNumChannels();
    const int numSmp = buffer.getNumSamples();

    const double mist     = mistParam   ? (double) mistParam->load()                : 4.0;
    const int    sizeMode = sizeParam   ? juce::roundToInt (sizeParam->load())      : 0;
    const bool   bypassed = bypassParam && bypassParam->load() > 0.5f;

    engine.setKnob (mist);
    engine.setSize (sizeMode);
    engine.setBypass (bypassed);

    if (numCh >= 2)
    {
        engine.processBlock (buffer.getWritePointer (0), buffer.getWritePointer (1), numSmp);
    }
    else if (numCh == 1)
    {
        // mono: run the stereo engine with a scratch right channel, fold back to mono
        if (monoR.getNumSamples() < numSmp) monoR.setSize (1, numSmp, false, false, true);
        float* L = buffer.getWritePointer (0);
        float* R = monoR.getWritePointer (0);
        std::memcpy (R, L, sizeof (float) * (size_t) numSmp);
        engine.processBlock (L, R, numSmp);
        for (int i = 0; i < numSmp; ++i) L[i] = 0.5f * (L[i] + R[i]);
    }

    // Front-panel metering LED: keep the running peak of the heard output. The editor
    // drains this each frame and applies its own attack/release ballistics.
    const float blockPeak = buffer.getMagnitude (0, numSmp);
    uiLevel.store (juce::jmax (uiLevel.load (std::memory_order_relaxed), blockPeak),
                   std::memory_order_relaxed);
}

juce::AudioProcessorEditor* MistAudioProcessor::createEditor()
{
    return new MistAudioProcessorEditor (*this);
}

void MistAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
        if (auto xml = state.createXml())
            copyXmlToBinary (*xml, destData);
}

void MistAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

// JUCE entry point
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MistAudioProcessor();
}
