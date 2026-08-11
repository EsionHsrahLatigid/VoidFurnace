#include "voidfurnace/VoidFurnaceEngine.h"

#include <algorithm>
#include <cmath>

namespace voidfurnace
{
namespace
{
constexpr float ceiling = 0.98f;
constexpr std::array<int, VoidFurnaceEngine::lineCount> baseLengths {{ 1499, 2137, 2791, 3571 }};
}

VoidFurnaceEngine::VoidFurnaceEngine()
{
    prepare (44100.0);
    reset();
}

void VoidFurnaceEngine::prepare (double newSampleRate) noexcept
{
    sampleRate = std::isfinite (newSampleRate) && newSampleRate > 1.0 ? newSampleRate : 44100.0;
    reset();
}

void VoidFurnaceEngine::reset() noexcept
{
    for (auto& line : lines)
        line.fill (0.0f);
    damped.fill (0.0f);
    writeIndex = 0;
    sampleCounter = 0;
    velvetNoise.reset (0xa7736b91u);
}

void VoidFurnaceEngine::setParameters (const VoidFurnaceParameters& parameters) noexcept
{
    params.preDelay = clampFinite (parameters.preDelay, 0.0f, 1.0f, VoidFurnaceParameters {}.preDelay);
    params.size = clampFinite (parameters.size, 0.0f, 1.0f, VoidFurnaceParameters {}.size);
    params.decay = clampFinite (parameters.decay, 0.0f, 1.0f, VoidFurnaceParameters {}.decay);
    params.damping = clampFinite (parameters.damping, 0.0f, 1.0f, VoidFurnaceParameters {}.damping);
    params.density = clampFinite (parameters.density, 0.0f, 1.0f, VoidFurnaceParameters {}.density);
    params.diffusion = clampFinite (parameters.diffusion, 0.0f, 1.0f, VoidFurnaceParameters {}.diffusion);
    params.mix = clampFinite (parameters.mix, 0.0f, 1.0f, VoidFurnaceParameters {}.mix);
}

StereoFrame VoidFurnaceEngine::processSample (float inputLeft, float inputRight) noexcept
{
    const auto dryLeft = sanitizeAudio (inputLeft);
    const auto dryRight = sanitizeAudio (inputRight);
    const auto mono = (dryLeft + dryRight) * 0.5f;
    const auto activity = std::max (std::fabs (dryLeft), std::fabs (dryRight));

    std::array<float, lineCount> tap {};
    const auto sizeScale = 0.45f + params.size * 1.85f;
    const auto preDelaySamples = static_cast<int> ((0.001f + params.preDelay * 0.095f) * static_cast<float> (sampleRate));
    for (int i = 0; i < lineCount; ++i)
    {
        const auto delay = std::clamp (preDelaySamples + static_cast<int> (static_cast<float> (baseLengths[static_cast<std::size_t> (i)]) * sizeScale),
                                       1,
                                       maxDelaySamples - 2);
        tap[static_cast<std::size_t> (i)] = readLine (i, delay);
        const auto dampAmount = 0.04f + params.damping * 0.88f;
        damped[static_cast<std::size_t> (i)] += dampAmount * (tap[static_cast<std::size_t> (i)] - damped[static_cast<std::size_t> (i)]);
        tap[static_cast<std::size_t> (i)] = damped[static_cast<std::size_t> (i)];
    }

    const auto a = tap[0] + tap[1] + tap[2] + tap[3];
    const auto b = tap[0] - tap[1] + tap[2] - tap[3];
    const auto c = tap[0] + tap[1] - tap[2] - tap[3];
    const auto d = tap[0] - tap[1] - tap[2] + tap[3];
    const std::array<float, lineCount> hadamard {{ a, b, c, d }};
    const auto feedback = params.decay * (0.18f + params.diffusion * 0.70f);
    const auto velvet = activity > 1.0e-5f ? velvetImpulse() * activity * params.density : 0.0f;

    for (int i = 0; i < lineCount; ++i)
    {
        const auto sign = (i & 1) == 0 ? 1.0f : -1.0f;
        const auto injected = mono * (0.20f + params.density * 0.42f) + velvet * sign;
        lines[static_cast<std::size_t> (i)][static_cast<std::size_t> (writeIndex)] =
            sanitizeAudio (injected + hadamard[static_cast<std::size_t> (i)] * feedback * 0.5f);
    }
    writeIndex = (writeIndex + 1) % maxDelaySamples;
    ++sampleCounter;

    const auto width = 0.32f + params.diffusion * 0.68f;
    const auto wetLeft = (tap[0] + tap[2]) * 0.62f + (tap[1] - tap[3]) * width * 0.45f;
    const auto wetRight = (tap[1] + tap[3]) * 0.62f - (tap[0] - tap[2]) * width * 0.45f;
    const auto dry = 1.0f - params.mix;
    return sanitizeFrame (dryLeft * dry + wetLeft * params.mix,
                          dryRight * dry + wetRight * params.mix);
}

void VoidFurnaceEngine::process (float* left, float* right, int numSamples) noexcept
{
    if (left == nullptr || right == nullptr || numSamples <= 0)
        return;
    for (int i = 0; i < numSamples; ++i)
    {
        const auto frame = processSample (left[i], right[i]);
        left[i] = frame.left;
        right[i] = frame.right;
    }
}

float VoidFurnaceEngine::readLine (int line, int delaySamples) const noexcept
{
    auto index = writeIndex - delaySamples;
    while (index < 0)
        index += maxDelaySamples;
    return lines[static_cast<std::size_t> (line)][static_cast<std::size_t> (index)];
}

float VoidFurnaceEngine::velvetImpulse() noexcept
{
    const auto threshold = static_cast<std::uint32_t> (1u + params.density * 63.0f);
    return (velvetNoise.nextWord() & 1023u) < threshold ? velvetNoise.nextBinary() * 0.72f : 0.0f;
}

float VoidFurnaceEngine::sanitizeAudio (float value) const noexcept
{
    return clampFinite (value, -8.0f, 8.0f, 0.0f);
}

StereoFrame VoidFurnaceEngine::sanitizeFrame (float left, float right) const noexcept
{
    auto safeLeft = boundedDrive (left, 1.04f + params.diffusion * 0.32f);
    auto safeRight = boundedDrive (right, 1.04f + params.diffusion * 0.32f);
    if (std::fabs (safeLeft) < 1.0e-20f)
        safeLeft = 0.0f;
    if (std::fabs (safeRight) < 1.0e-20f)
        safeRight = 0.0f;
    return { std::clamp (safeLeft, -ceiling, ceiling),
             std::clamp (safeRight, -ceiling, ceiling) };
}

} // namespace voidfurnace
