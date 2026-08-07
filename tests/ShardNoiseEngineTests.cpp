#include "violent/ShardNoiseEngine.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

using violent::ShardNoiseEngine;
using violent::ShardNoiseParameters;

namespace
{

std::vector<float> renderLeft (int note, float velocity, ShardNoiseParameters params, int samples)
{
    ShardNoiseEngine engine;
    engine.prepare (48000.0);
    engine.setParameters (params);
    engine.noteOn (note, velocity);

    std::vector<float> output;
    output.reserve (static_cast<std::size_t> (samples));
    for (int i = 0; i < samples; ++i)
        output.push_back (engine.processSample().left);
    return output;
}

float energy (const std::vector<float>& values, int begin, int end)
{
    auto total = 0.0f;
    for (int i = begin; i < end; ++i)
        total += values[static_cast<std::size_t> (i)] * values[static_cast<std::size_t> (i)];
    return total;
}

float adjacentDifferenceEnergy (const std::vector<float>& values)
{
    auto total = 0.0f;
    for (std::size_t i = 1; i < values.size(); ++i)
    {
        const auto diff = values[i] - values[i - 1u];
        total += diff * diff;
    }
    return total;
}

float lowPassedBlockEnergy (const std::vector<float>& values)
{
    constexpr int block = 96;
    auto total = 0.0f;
    for (std::size_t i = 0; i + block <= values.size(); i += block)
    {
        auto sum = 0.0f;
        for (int j = 0; j < block; ++j)
            sum += values[i + static_cast<std::size_t> (j)];
        const auto average = sum / static_cast<float> (block);
        total += average * average * static_cast<float> (block);
    }
    return total;
}

void testDeterministicTriggerBehavior()
{
    ShardNoiseParameters params;
    params.cut = 0.72f;
    params.edge = 0.6f;
    params.burst = 0.42f;
    params.scatter = 0.55f;

    assert (renderLeft (64, 0.8f, params, 2048) == renderLeft (64, 0.8f, params, 2048));

    const auto a = renderLeft (60, 0.8f, params, 2048);
    const auto b = renderLeft (72, 0.8f, params, 2048);
    int different = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        different += a[i] != b[i] ? 1 : 0;

    assert (different > 1600);
}

void testSilenceBeforeTriggerAndAfterDecay()
{
    ShardNoiseParameters params;
    params.burst = 0.0f;
    params.outputGain = 0.7f;

    ShardNoiseEngine engine;
    engine.prepare (48000.0);
    engine.setParameters (params);

    for (int i = 0; i < 256; ++i)
    {
        const auto frame = engine.processSample();
        assert (frame.left == 0.0f);
        assert (frame.right == 0.0f);
    }

    engine.noteOn (61, 1.0f);
    auto triggeredEnergy = 0.0f;
    for (int i = 0; i < 512; ++i)
    {
        const auto frame = engine.processSample();
        triggeredEnergy += frame.left * frame.left + frame.right * frame.right;
    }
    assert (triggeredEnergy > 0.01f);

    engine.noteOff();
    for (int i = 0; i < 48000; ++i)
        (void) engine.processSample();

    assert (! engine.isActive());
    for (int i = 0; i < 128; ++i)
    {
        const auto frame = engine.processSample();
        assert (frame.left == 0.0f);
        assert (frame.right == 0.0f);
    }
}

void testBoundedExtremeAndNonfiniteParameters()
{
    ShardNoiseParameters params;
    params.cut = std::numeric_limits<float>::infinity();
    params.edge = std::numeric_limits<float>::quiet_NaN();
    params.burst = 200.0f;
    params.scatter = -200.0f;
    params.aliasBudget = std::numeric_limits<float>::infinity();
    params.stereoSplit = std::numeric_limits<float>::quiet_NaN();
    params.outputGain = 100.0f;

    ShardNoiseEngine engine;
    engine.prepare (std::numeric_limits<double>::infinity());
    engine.setParameters (params);
    engine.noteOn (999, std::numeric_limits<float>::infinity());

    for (int i = 0; i < 4096; ++i)
    {
        const auto frame = engine.processSample();
        assert (std::isfinite (frame.left));
        assert (std::isfinite (frame.right));
        assert (frame.left >= -0.9801f && frame.left <= 0.9801f);
        assert (frame.right >= -0.9801f && frame.right <= 0.9801f);
    }
}

void testHighFrequencySignature()
{
    ShardNoiseParameters params;
    params.cut = 0.95f;
    params.edge = 0.7f;
    params.burst = 0.2f;
    params.scatter = 0.75f;
    params.aliasBudget = 0.2f;
    params.outputGain = 0.8f;

    const auto output = renderLeft (49, 1.0f, params, 8192);
    const auto totalEnergy = energy (output, 0, static_cast<int> (output.size()));
    const auto highMetric = adjacentDifferenceEnergy (output);
    const auto lowMetric = lowPassedBlockEnergy (output);

    assert (totalEnergy > 0.01f);
    assert (highMetric > totalEnergy * 0.7f);
    assert (highMetric > lowMetric * 45.0f);
}

} // namespace

int main()
{
    testDeterministicTriggerBehavior();
    testSilenceBeforeTriggerAndAfterDecay();
    testBoundedExtremeAndNonfiniteParameters();
    testHighFrequencySignature();

    std::cout << "ShardNoiseEngineTests passed\n";
    return 0;
}
