#pragma once

#include "voidfurnace/VoidFurnaceDspPrimitives.h"

#include <array>

namespace voidfurnace
{

struct VoidFurnaceParameters
{
    float preDelay = 0.18f;
    float size = 0.52f;
    float decay = 0.58f;
    float damping = 0.46f;
    float density = 0.42f;
    float diffusion = 0.62f;
    float mix = 0.44f;
};

class VoidFurnaceEngine
{
public:
    VoidFurnaceEngine();

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void setParameters (const VoidFurnaceParameters& parameters) noexcept;
    [[nodiscard]] StereoFrame processSample (float inputLeft, float inputRight) noexcept;
    void process (float* left, float* right, int numSamples) noexcept;

private:
    static constexpr int maxDelaySamples = 96000;
public:
    static constexpr int lineCount = 4;
private:

    struct ClampedParameters
    {
        float preDelay = 0.18f;
        float size = 0.52f;
        float decay = 0.58f;
        float damping = 0.46f;
        float density = 0.42f;
        float diffusion = 0.62f;
        float mix = 0.44f;
    };

    [[nodiscard]] float readLine (int line, int delaySamples) const noexcept;
    [[nodiscard]] float velvetImpulse() noexcept;
    [[nodiscard]] float sanitizeAudio (float value) const noexcept;
    [[nodiscard]] StereoFrame sanitizeFrame (float left, float right) const noexcept;

    ClampedParameters params;
    double sampleRate = 44100.0;
    std::array<std::array<float, maxDelaySamples>, lineCount> lines {};
    std::array<float, lineCount> damped {};
    int writeIndex = 0;
    int sampleCounter = 0;
    DeterministicNoise velvetNoise;
};

} // namespace voidfurnace
