#include "VoidFurnacePlugin.h"

#include <yup_audio_processors/yup_audio_processors.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>

namespace
{
constexpr int numChannels = 2;
constexpr int blockSamples = 2048;

class PluginHarness
{
public:
    PluginHarness()
        : audio (numChannels, blockSamples)
        , context { audio, midi, automation, nullptr, {}, {} }
    {
        plugin.prepareToPlay (yup::AudioSpec (48000.0f, blockSamples, numChannels));
    }

    float processSilence()
    {
        audio.clear();
        plugin.processBlock (context);
        return peak();
    }

    float processConstant (float value)
    {
        for (int channel = 0; channel < audio.getNumChannels(); ++channel)
            for (int sample = 0; sample < audio.getNumSamples(); ++sample)
                audio.getWritePointer (channel)[sample] = value;
        plugin.processBlock (context);
        return peak();
    }

    float processConstantBlocks (float value, int blocks)
    {
        float result = 0.0f;
        for (int block = 0; block < blocks; ++block)
            result = std::max (result, processConstant (value));
        return result;
    }

    float peak() const
    {
        float result = 0.0f;
        for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        {
            const auto* samples = audio.getReadPointer (channel);
            for (int sample = 0; sample < audio.getNumSamples(); ++sample)
                result = std::max (result, std::fabs (samples[sample]));
        }
        return result;
    }

    voidfurnace::plugin::VoidFurnacePlugin plugin;

private:
    yup::AudioBuffer<float> audio;
    yup::MidiBuffer midi;
    yup::ParameterChangeBuffer automation;
    yup::AudioProcessContext<float> context;
};

void testHostedSilencePreserved()
{
    PluginHarness harness;
    for (int i = 0; i < 4; ++i)
        assert (harness.processSilence() <= 1.0e-7f);
}

void testHostedDoesNotAcceptMidi()
{
    PluginHarness harness;
    assert (! harness.plugin.acceptsMidi());
    assert (! harness.plugin.producesMidi());
}

void testParameterResponse()
{
    PluginHarness soft;
    const auto softParameters = soft.plugin.getParameters();
    assert (softParameters.size() == 7);
    softParameters[2]->setValue (0.0f);
    softParameters[5]->setValue (0.0f);
    softParameters[6]->setValue (0.35f);
    const auto softPeak = soft.processConstantBlocks (0.5f, 10);

    PluginHarness hard;
    const auto hardParameters = hard.plugin.getParameters();
    hardParameters[2]->setValue (1.0f);
    hardParameters[5]->setValue (1.0f);
    hardParameters[6]->setValue (1.0f);
    const auto hardPeak = hard.processConstantBlocks (0.5f, 10);

    assert (std::fabs (hardPeak - softPeak) > 0.05f);
    assert (hardPeak <= 0.9801f);
}

void testStateRoundTrip()
{
    PluginHarness source;
    const auto sourceParameters = source.plugin.getParameters();
    source.plugin.setCurrentPreset (2);
    sourceParameters[0]->setValue (0.37f);
    sourceParameters[1]->setValue (0.82f);
    sourceParameters[2]->setValue (0.63f);

    yup::MemoryBlock data;
    assert (source.plugin.saveStateIntoMemory (data).wasOk());

    PluginHarness target;
    assert (target.plugin.loadStateFromMemory (data).wasOk());
    const auto targetParameters = target.plugin.getParameters();
    assert (target.plugin.getCurrentPreset() == 2);
    assert (targetParameters.size() == 7);
    assert (std::fabs (targetParameters[0]->getValue() - 0.37f) <= 1.0e-6f);
    assert (std::fabs (targetParameters[1]->getValue() - 0.82f) <= 1.0e-6f);
    assert (std::fabs (targetParameters[2]->getValue() - 0.63f) <= 1.0e-6f);
}

} // namespace

int main()
{
    testHostedSilencePreserved();
    testHostedDoesNotAcceptMidi();
    testParameterResponse();
    testStateRoundTrip();

    std::cout << "VoidFurnacePluginBridgeTests passed\n";
    return 0;
}
