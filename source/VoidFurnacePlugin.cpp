#include "VoidFurnacePlugin.h"

#include "ProductState.h"

#if ! VOIDFURNACE_HEADLESS_TEST
#include "ParameterGridEditor.h"
#endif

#include <algorithm>
#include <array>
#include <cmath>

namespace voidfurnace::plugin
{
namespace
{
constexpr std::array<char, 4> stateMagic {{ 'V', 'D', 'F', '1' }};
constexpr int stateVersion = 1;
constexpr std::size_t presetParameterCount = 7;

constexpr std::array<std::array<float, presetParameterCount>, 4> presetValues {{
    {{ 0.18f, 0.52f, 0.58f, 0.46f, 0.42f, 0.62f, 0.44f }},
    {{ 0.05f, 0.34f, 0.38f, 0.71f, 0.28f, 0.48f, 0.38f }},
    {{ 0.26f, 0.77f, 0.82f, 0.34f, 0.68f, 0.76f, 0.56f }},
    {{ 0.41f, 0.93f, 0.74f, 0.55f, 0.88f, 0.94f, 0.63f }}
}};

yup::AudioParameter::Ptr makeParameter (const char* id,
                                        const char* name,
                                        int hostID,
                                        float minValue,
                                        float maxValue,
                                        float defaultValue,
                                        yup::AudioParameter::ParameterUnit unit,
                                        float smoothingMs)
{
    return yup::AudioParameterBuilder()
        .withID (id)
        .withName (name)
        .withHostID (static_cast<yup::uint32> (hostID))
        .withRange (minValue, maxValue)
        .withDefault (defaultValue)
        .withSmoothing (smoothingMs)
        .withModulatable (true)
        .withUnit (unit)
        .build();
}
} // namespace

VoidFurnacePlugin::VoidFurnacePlugin()
    : yup::AudioProcessor ("VoidFurnace",
                           yup::AudioBusLayout ({
                                                    yup::AudioBus ("main", yup::AudioBus::Audio, yup::AudioBus::Input, 2),
                                                },
                                                {
                                                    yup::AudioBus ("main", yup::AudioBus::Audio, yup::AudioBus::Output, 2),
                                                }))
{
    parameters[preDelay] = makeParameter ("predelay", "PreDelay", preDelay, 0.0f, 1.0f, presetValues[0][preDelay], yup::AudioParameter::ParameterUnit::Percent, 24.0f);
    parameters[size] = makeParameter ("size", "Size", size, 0.0f, 1.0f, presetValues[0][size], yup::AudioParameter::ParameterUnit::Percent, 28.0f);
    parameters[decay] = makeParameter ("decay", "Decay", decay, 0.0f, 1.0f, presetValues[0][decay], yup::AudioParameter::ParameterUnit::Percent, 24.0f);
    parameters[damping] = makeParameter ("damping", "Damping", damping, 0.0f, 1.0f, presetValues[0][damping], yup::AudioParameter::ParameterUnit::Percent, 28.0f);
    parameters[density] = makeParameter ("density", "Density", density, 0.0f, 1.0f, presetValues[0][density], yup::AudioParameter::ParameterUnit::Percent, 18.0f);
    parameters[diffusion] = makeParameter ("diffusion", "Diffusion", diffusion, 0.0f, 1.0f, presetValues[0][diffusion], yup::AudioParameter::ParameterUnit::Percent, 22.0f);
    parameters[mix] = makeParameter ("mix", "Mix", mix, 0.0f, 1.0f, presetValues[0][mix], yup::AudioParameter::ParameterUnit::Percent, 20.0f);

    for (const auto& parameter : parameters)
        addParameter (parameter);

    syncParameterValuesFromParameters();
    updateEngineParameters();
}

void VoidFurnacePlugin::prepareToPlay (const yup::AudioSpec& spec)
{
    engine.prepare (spec.sampleRate);
    engine.reset();

    for (std::size_t i = 0; i < parameterHandles.size(); ++i)
        parameterHandles[i] = yup::AudioParameterHandle (*parameters[i], spec.sampleRate);

    syncParameterValuesFromParameters();
    updateEngineParameters();
    controlUpdateCountdown = 0;
    inputPeakMilli.store (0, std::memory_order_relaxed);
    outputPeakMilli.store (0, std::memory_order_relaxed);

#if defined(YUP_AUDIO_PLUGIN_ENABLE_STANDALONE)
    auditionSampleRate = std::isfinite (spec.sampleRate) && spec.sampleRate > 1.0 ? spec.sampleRate : 44100.0;
    auditionPhase = 0.0f;
    auditionNoise = 0x6d2b79f5u;
#endif
}

void VoidFurnacePlugin::releaseResources()
{
}

void VoidFurnacePlugin::processBlock (yup::AudioProcessContext<float>& context)
{
    auto& audio = context.audio;
    const auto numSamples = audio.getNumSamples();
    const auto numChannels = audio.getNumChannels();

    for (std::size_t i = 0; i < parameterHandles.size(); ++i)
        parameterHandles[i].prepareBlock (context.params, parameters[i]->getIndexInContainer());

    auto* left = numChannels > 0 ? audio.getWritePointer (0) : nullptr;
    auto* right = numChannels > 1 ? audio.getWritePointer (1) : nullptr;
    float blockInputPeak = 0.0f;
    float blockOutputPeak = 0.0f;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        advanceParameterHandles (sample);
        if (controlUpdateCountdown <= 0)
        {
            updateEngineParameters();
            controlUpdateCountdown = parameterUpdateCadenceSamples;
        }
        --controlUpdateCountdown;

        auto inputLeft = left != nullptr ? left[sample] : 0.0f;
        auto inputRight = right != nullptr ? right[sample] : inputLeft;

#if defined(YUP_AUDIO_PLUGIN_ENABLE_STANDALONE)
        const auto audition = renderAuditionFrame();
        inputLeft += audition.left;
        inputRight += audition.right;
#endif

        blockInputPeak = std::max (blockInputPeak, std::max (std::fabs (inputLeft), std::fabs (inputRight)));

        const auto frame = engine.processSample (inputLeft, inputRight);
        if (left != nullptr)
            left[sample] = frame.left;
        if (right != nullptr)
            right[sample] = frame.right;
        blockOutputPeak = std::max (blockOutputPeak, std::max (std::fabs (frame.left), std::fabs (frame.right)));

        for (int channel = 2; channel < numChannels; ++channel)
            audio.getWritePointer (channel)[sample] = 0.0f;
    }

    inputPeakMilli.store (static_cast<int> (std::clamp (blockInputPeak, 0.0f, 1.0f) * 1000.0f + 0.5f),
                          std::memory_order_relaxed);
    outputPeakMilli.store (static_cast<int> (std::clamp (blockOutputPeak, 0.0f, 1.0f) * 1000.0f + 0.5f),
                           std::memory_order_relaxed);
    context.midi.clear();
}

void VoidFurnacePlugin::flush()
{
    engine.reset();
    controlUpdateCountdown = 0;
    inputPeakMilli.store (0, std::memory_order_relaxed);
    outputPeakMilli.store (0, std::memory_order_relaxed);
#if defined(YUP_AUDIO_PLUGIN_ENABLE_STANDALONE)
    auditionPhase = 0.0f;
    auditionNoise = 0x6d2b79f5u;
#endif
}

bool VoidFurnacePlugin::acceptsMidi() const noexcept
{
    return false;
}

bool VoidFurnacePlugin::producesMidi() const noexcept
{
    return false;
}

int VoidFurnacePlugin::getCurrentPreset() const noexcept
{
    return currentPreset.load (std::memory_order_relaxed);
}

void VoidFurnacePlugin::setCurrentPreset (int index) noexcept
{
    if (! yup::isPositiveAndBelow (index, static_cast<int> (presetValues.size())))
        return;

    currentPreset.store (index, std::memory_order_relaxed);
    for (std::size_t i = 0; i < parameters.size(); ++i)
        parameters[i]->setValue (presetValues[static_cast<std::size_t> (index)][i]);
}

int VoidFurnacePlugin::getNumPresets() const
{
    return static_cast<int> (presetNames.size());
}

yup::String VoidFurnacePlugin::getPresetName (int index) const
{
    if (yup::isPositiveAndBelow (index, static_cast<int> (presetNames.size())))
        return presetNames[static_cast<std::size_t> (index)];
    return "Invalid Preset";
}

void VoidFurnacePlugin::setPresetName (int index, yup::StringRef newName)
{
    if (yup::isPositiveAndBelow (index, static_cast<int> (presetNames.size())))
        presetNames[static_cast<std::size_t> (index)] = newName;
}

yup::Result VoidFurnacePlugin::loadStateFromMemory (const yup::MemoryBlock& data)
{
    int loadedPreset = 0;
    const auto result = loadProductState (*this, data, stateMagic, stateVersion, getNumPresets(), loadedPreset);
    if (result.failed())
        return result;

    currentPreset.store (loadedPreset, std::memory_order_relaxed);
    return yup::Result::ok();
}

yup::Result VoidFurnacePlugin::saveStateIntoMemory (yup::MemoryBlock& data)
{
    return saveProductState (*this, data, stateMagic, stateVersion, currentPreset.load (std::memory_order_relaxed));
}

bool VoidFurnacePlugin::hasEditor() const
{
#if VOIDFURNACE_HEADLESS_TEST
    return false;
#else
    return true;
#endif
}

yup::AudioProcessorEditor* VoidFurnacePlugin::createEditor()
{
#if VOIDFURNACE_HEADLESS_TEST
    return nullptr;
#else
    return new ParameterGridEditor (*this,
                                    "VoidFurnace",
                                    "Hadamard FDN reverb with standalone-only audition.",
                                    0xffd8d8d8u);
#endif
}

float VoidFurnacePlugin::getInputPeakLevel() const noexcept
{
    return static_cast<float> (inputPeakMilli.load (std::memory_order_relaxed)) * 0.001f;
}

float VoidFurnacePlugin::getOutputPeakLevel() const noexcept
{
    return static_cast<float> (outputPeakMilli.load (std::memory_order_relaxed)) * 0.001f;
}

#if defined(YUP_AUDIO_PLUGIN_ENABLE_STANDALONE)
void VoidFurnacePlugin::setAuditionEnabled (bool shouldBeEnabled) noexcept
{
    auditionEnabled.store (shouldBeEnabled ? 1 : 0, std::memory_order_relaxed);
}

bool VoidFurnacePlugin::isAuditionEnabled() const noexcept
{
    return auditionEnabled.load (std::memory_order_relaxed) != 0;
}

void VoidFurnacePlugin::setAuditionType (int type) noexcept
{
    auditionType.store (std::clamp (type, 0, 1), std::memory_order_relaxed);
}

int VoidFurnacePlugin::getAuditionType() const noexcept
{
    return auditionType.load (std::memory_order_relaxed);
}
#endif

void VoidFurnacePlugin::advanceParameterHandles (int samplePosition) noexcept
{
    for (std::size_t i = 0; i < parameterHandles.size(); ++i)
    {
        parameterHandles[i].advanceToSample (samplePosition);
        currentParameterValues[i] = parameterHandles[i].getNextValue();
    }
}

void VoidFurnacePlugin::syncParameterValuesFromParameters() noexcept
{
    for (std::size_t i = 0; i < parameters.size(); ++i)
        currentParameterValues[i] = parameters[i]->getValue();
}

void VoidFurnacePlugin::updateEngineParameters() noexcept
{
    voidfurnace::VoidFurnaceParameters engineParameters;
    engineParameters.preDelay = currentParameterValues[preDelay];
    engineParameters.size = currentParameterValues[size];
    engineParameters.decay = currentParameterValues[decay];
    engineParameters.damping = currentParameterValues[damping];
    engineParameters.density = currentParameterValues[density];
    engineParameters.diffusion = currentParameterValues[diffusion];
    engineParameters.mix = currentParameterValues[mix];
    engine.setParameters (engineParameters);
}

#if defined(YUP_AUDIO_PLUGIN_ENABLE_STANDALONE)
StereoFrame VoidFurnacePlugin::renderAuditionFrame() noexcept
{
    if (auditionEnabled.load (std::memory_order_relaxed) == 0)
        return {};

    auditionPhase += 96.0f / static_cast<float> (auditionSampleRate);
    if (auditionPhase >= 1.0f)
        auditionPhase -= 1.0f;

    auditionNoise ^= auditionNoise << 13u;
    auditionNoise ^= auditionNoise >> 17u;
    auditionNoise ^= auditionNoise << 5u;
    if (auditionNoise == 0u)
        auditionNoise = 0x6d2b79f5u;

    const auto type = auditionType.load (std::memory_order_relaxed);
    const auto noise = static_cast<float> (static_cast<double> (auditionNoise) / 2147483648.0 - 1.0);
    const auto pulse = auditionPhase < 0.18f ? 1.0f : -0.55f;
    const auto saw = auditionPhase * 2.0f - 1.0f;
    const auto source = type == 0 ? saw * 0.22f + noise * 0.035f : pulse * 0.18f + noise * 0.055f;
    return { source, source * 0.93f };
}
#endif

} // namespace voidfurnace::plugin

extern "C" yup::AudioProcessor* createPluginProcessor()
{
    return new voidfurnace::plugin::VoidFurnacePlugin();
}
