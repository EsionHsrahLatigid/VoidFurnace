#include "voidfurnace/VoidFurnaceEngine.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

using voidfurnace::VoidFurnaceEngine;
using voidfurnace::VoidFurnaceParameters;

namespace
{
std::vector<float> renderImpulse (VoidFurnaceParameters params, int samples)
{
    VoidFurnaceEngine engine;
    engine.prepare (48000.0);
    engine.setParameters (params);
    engine.reset();

    std::vector<float> output;
    output.reserve (static_cast<std::size_t> (samples));
    for (int i = 0; i < samples; ++i)
    {
        const auto input = i == 0 ? 0.85f : 0.0f;
        output.push_back (engine.processSample (input, -input * 0.25f).left);
    }
    return output;
}

float energyAfter (const std::vector<float>& samples, int start)
{
    float energy = 0.0f;
    for (std::size_t i = static_cast<std::size_t> (start); i < samples.size(); ++i)
        energy += samples[i] * samples[i];
    return energy;
}

float energyBetween (const std::vector<float>& samples, int start, int end)
{
    float energy = 0.0f;
    const auto safeStart = std::max (0, start);
    const auto safeEnd = std::min (end, static_cast<int> (samples.size()));
    for (int i = safeStart; i < safeEnd; ++i)
        energy += samples[static_cast<std::size_t> (i)] * samples[static_cast<std::size_t> (i)];
    return energy;
}

int activeSamples (const std::vector<float>& samples)
{
    int count = 0;
    for (const auto sample : samples)
        count += std::fabs (sample) > 1.0e-5f ? 1 : 0;
    return count;
}

void testSilenceStaysSilent()
{
    VoidFurnaceEngine engine;
    engine.prepare (48000.0);
    engine.reset();
    for (int i = 0; i < 8192; ++i)
    {
        const auto frame = engine.processSample (0.0f, 0.0f);
        assert (std::fabs (frame.left) <= 1.0e-7f);
        assert (std::fabs (frame.right) <= 1.0e-7f);
    }
}

void testSizeAndPreDelayMoveEarlyEnergy()
{
    VoidFurnaceParameters small;
    small.preDelay = 0.0f;
    small.size = 0.05f;
    small.decay = 0.2f;
    small.mix = 1.0f;

    VoidFurnaceParameters large = small;
    large.preDelay = 0.7f;
    large.size = 0.9f;

    const auto smallOutput = renderImpulse (small, 24000);
    const auto largeOutput = renderImpulse (large, 24000);
    // Small settings place the first four taps between roughly 0.8k and
    // 2.0k samples. Large + predelay settings move them beyond 6.4k.
    assert (energyBetween (smallOutput, 700, 2400) > energyBetween (largeOutput, 700, 2400) * 3.0f + 1.0e-5f);
    assert (energyBetween (largeOutput, 6200, 11800) > energyBetween (smallOutput, 6200, 11800) * 2.0f + 1.0e-5f);
}

void testDecayCreatesLateEnergy()
{
    VoidFurnaceParameters shortDecay;
    shortDecay.size = 0.32f;
    shortDecay.decay = 0.05f;
    shortDecay.mix = 1.0f;

    VoidFurnaceParameters longDecay = shortDecay;
    longDecay.decay = 0.92f;
    longDecay.diffusion = 0.9f;

    const auto shortOutput = renderImpulse (shortDecay, 24000);
    const auto longOutput = renderImpulse (longDecay, 24000);
    assert (energyAfter (longOutput, 12000) > energyAfter (shortOutput, 12000) + 1.0e-5f);
}

void testDensityChangesSparseResponse()
{
    VoidFurnaceParameters sparse;
    sparse.density = 0.0f;
    sparse.mix = 1.0f;

    VoidFurnaceParameters dense = sparse;
    dense.density = 1.0f;

    const auto sparseOutput = renderImpulse (sparse, 16000);
    const auto denseOutput = renderImpulse (dense, 16000);
    assert (std::abs (activeSamples (denseOutput) - activeSamples (sparseOutput)) > 16);
}

void testDeterministic()
{
    VoidFurnaceParameters params;
    params.preDelay = 0.23f;
    params.size = 0.71f;
    params.decay = 0.84f;
    params.density = 0.73f;

    const auto a = renderImpulse (params, 16000);
    const auto b = renderImpulse (params, 16000);
    for (std::size_t i = 0; i < a.size(); ++i)
        assert (std::fabs (a[i] - b[i]) <= 1.0e-6f);
}

void testFiniteBoundedExtremeParameters()
{
    VoidFurnaceParameters params;
    params.preDelay = 1000.0f;
    params.size = 1000.0f;
    params.decay = 1000.0f;
    params.damping = std::numeric_limits<float>::infinity();
    params.density = 1000.0f;
    params.diffusion = 1000.0f;
    params.mix = 1000.0f;

    VoidFurnaceEngine engine;
    engine.prepare (0.0);
    engine.setParameters (params);
    engine.reset();
    for (int i = 0; i < 8192; ++i)
    {
        const auto frame = engine.processSample (1000.0f, -1000.0f);
        assert (std::isfinite (frame.left));
        assert (std::isfinite (frame.right));
        assert (frame.left >= -0.9801f && frame.left <= 0.9801f);
        assert (frame.right >= -0.9801f && frame.right <= 0.9801f);
    }
}

void testDenormalInputDoesNotLeak()
{
    VoidFurnaceEngine engine;
    engine.prepare (48000.0);
    engine.reset();
    for (int i = 0; i < 1024; ++i)
    {
        const auto frame = engine.processSample (1.0e-30f, -1.0e-30f);
        assert (std::fabs (frame.left) <= 1.0e-7f);
        assert (std::fabs (frame.right) <= 1.0e-7f);
    }
}
} // namespace

int main()
{
    testSilenceStaysSilent();
    testSizeAndPreDelayMoveEarlyEnergy();
    testDecayCreatesLateEnergy();
    testDensityChangesSparseResponse();
    testDeterministic();
    testFiniteBoundedExtremeParameters();
    testDenormalInputDoesNotLeak();

    std::cout << "VoidFurnaceEngineTests passed\n";
    return 0;
}
