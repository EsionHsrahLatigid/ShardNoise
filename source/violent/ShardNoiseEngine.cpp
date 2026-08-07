#include "violent/ShardNoiseEngine.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace violent
{

ShardNoiseEngine::ShardNoiseEngine()
{
    prepare (44100.0);
}

void ShardNoiseEngine::prepare (double newSampleRate) noexcept
{
    sampleRate = std::isfinite (newSampleRate) && newSampleRate > 1.0 ? newSampleRate : 44100.0;
    updateFilters();
    reset();
}

void ShardNoiseEngine::reset() noexcept
{
    sampleCounter = 0u;
    envelope = 0.0f;
    velocity = 0.0f;
    gateHeld = false;
    resetChannel (leftChannel);
    resetChannel (rightChannel);
}

void ShardNoiseEngine::setParameters (const ShardNoiseParameters& parameters) noexcept
{
    params.cut = clampUnit (parameters.cut, params.cut);
    params.edge = clampUnit (parameters.edge, params.edge);
    params.burst = clampUnit (parameters.burst, params.burst);
    params.scatter = clampUnit (parameters.scatter, params.scatter);
    params.aliasBudget = clampUnit (parameters.aliasBudget, params.aliasBudget);
    params.stereoSplit = clampUnit (parameters.stereoSplit, params.stereoSplit);
    params.outputGain = clampFinite (parameters.outputGain, 0.0f, 2.0f, params.outputGain);
    updateFilters();
}

void ShardNoiseEngine::noteOn (int newNoteNumber, float newVelocity) noexcept
{
    noteNumber = std::clamp (newNoteNumber, 0, 127);
    velocity = clampUnit (newVelocity, 1.0f);
    gateHeld = velocity > 0.0f;
    envelope = gateHeld ? velocity : 0.0f;
    sampleCounter = 0u;

    const auto note = static_cast<std::uint32_t> (noteNumber);
    const auto burstBucket = static_cast<std::uint32_t> (params.burst * 7.0f);
    noteSeed = mixSeed (0x9e3779b9u ^ (note * 0x45d9f3bu) ^ (burstBucket * 0x27d4eb2du));
    subdivision = 1 + static_cast<int> ((note + burstBucket) % 8u);

    leftChannel.binary.reset (mixSeed (noteSeed ^ 0x13579bdfu));
    leftChannel.velvet.reset (mixSeed (noteSeed ^ 0x2468ace1u));
    rightChannel.binary.reset (mixSeed (noteSeed ^ 0xa5a5f00du ^ static_cast<std::uint32_t> (params.stereoSplit * 65535.0f)));
    rightChannel.velvet.reset (mixSeed (noteSeed ^ 0x5a5a0ff0u ^ static_cast<std::uint32_t> (params.stereoSplit * 104729.0f)));

    for (auto* channel : { &leftChannel, &rightChannel })
    {
        channel->previousFoldInput = 0.0f;
        channel->highPass.reset();
        channel->preSmooth.reset();
        channel->postSmooth.reset();
        for (auto& band : channel->bands)
            band.reset();
    }
}

void ShardNoiseEngine::noteOff() noexcept
{
    gateHeld = false;
}

StereoFrame ShardNoiseEngine::processSample() noexcept
{
    const auto env = nextEnvelope();
    if (env <= 0.0f)
        return {};

    const auto left = nextVoiceSample (leftChannel, 0x10203040u) * env * params.outputGain;
    const auto right = nextVoiceSample (rightChannel, 0x40302010u) * env * params.outputGain;
    ++sampleCounter;

    return { sanitizeOutput (left), sanitizeOutput (right) };
}

void ShardNoiseEngine::process (float* left, float* right, int numSamples) noexcept
{
    if (left == nullptr || right == nullptr || numSamples <= 0)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto frame = processSample();
        left[i] = frame.left;
        right[i] = frame.right;
    }
}

bool ShardNoiseEngine::isActive() const noexcept
{
    return envelope > 0.0f || gateHeld;
}

std::uint32_t ShardNoiseEngine::mixSeed (std::uint32_t value) noexcept
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value != 0u ? value : 0x6d2b79f5u;
}

float ShardNoiseEngine::fold (float input, float edge) noexcept
{
    const auto drive = 1.0f + 15.0f * clampUnit (edge, 0.0f);
    auto x = clampFinite (input, -4.0f, 4.0f, 0.0f) * drive;
    x = std::fabs (std::fmod (x + 3.0f, 4.0f) - 2.0f) - 1.0f;
    return boundedDrive (x, 0.9f + 2.1f * edge);
}

float ShardNoiseEngine::clampUnit (float value, float fallback) noexcept
{
    return clampFinite (value, 0.0f, 1.0f, fallback);
}

float ShardNoiseEngine::sanitizeOutput (float value) noexcept
{
    return clampFinite (value, -0.98f, 0.98f, 0.0f);
}

void ShardNoiseEngine::updateFilters() noexcept
{
    const auto nyquist = static_cast<float> (sampleRate * 0.5);
    const auto cutHz = std::min (nyquist * 0.82f, 3600.0f + params.cut * 8200.0f);
    leftChannel.highPass.setHighPass (sampleRate, cutHz, 0.70710678f);
    rightChannel.highPass.setHighPass (sampleRate, cutHz * (1.0f + 0.025f * params.stereoSplit), 0.70710678f);

    const auto smoothHz = std::min (nyquist * 0.92f, 7000.0f + params.cut * 9000.0f);
    leftChannel.preSmooth.setLowPass (sampleRate, smoothHz, 0.70710678f);
    rightChannel.preSmooth.setLowPass (sampleRate, smoothHz * (1.0f - 0.02f * params.stereoSplit), 0.70710678f);
    leftChannel.postSmooth.setLowPass (sampleRate, smoothHz, 0.70710678f);
    rightChannel.postSmooth.setLowPass (sampleRate, smoothHz * (1.0f - 0.02f * params.stereoSplit), 0.70710678f);

    for (int i = 0; i < numShardBands; ++i)
    {
        const auto index = static_cast<float> (i);
        const auto spread = 900.0f + 2200.0f * params.scatter;
        const auto base = 4800.0f + params.cut * 5000.0f + index * spread;
        const auto clampedLeft = std::min (nyquist * 0.94f, base);
        const auto clampedRight = std::min (nyquist * 0.94f, base * (1.0f + (index - 1.5f) * 0.018f * params.stereoSplit));
        const auto quality = 3.0f + params.edge * 9.0f + index * 0.75f;
        leftChannel.bands[static_cast<std::size_t> (i)].setBandPass (sampleRate, clampedLeft, quality);
        rightChannel.bands[static_cast<std::size_t> (i)].setBandPass (sampleRate, clampedRight, quality);
    }
}

void ShardNoiseEngine::resetChannel (ChannelState& channel) noexcept
{
    channel.binary.reset (0x6d2b79f5u);
    channel.velvet.reset (0x13579bdfu);
    channel.previousFoldInput = 0.0f;
    channel.highPass.reset();
    channel.preSmooth.reset();
    channel.postSmooth.reset();
    for (auto& band : channel.bands)
        band.reset();
}

float ShardNoiseEngine::nextVoiceSample (ChannelState& channel, std::uint32_t channelSalt) noexcept
{
    const auto binary = channel.binary.nextBinary();
    const auto sparseMask = static_cast<int> ((sampleCounter + (channelSalt & 7u)) % static_cast<std::uint32_t> (subdivision)) == 0;
    const auto scatterThreshold = static_cast<std::uint32_t> ((0.015f + 0.20f * params.scatter) * 4294967295.0f);
    const auto velvetHit = sparseMask && channel.velvet.nextWord() <= scatterThreshold;
    const auto velvet = velvetHit ? channel.velvet.nextBinary() * (1.0f + 2.0f * params.burst) : 0.0f;
    const auto source = binary * (0.35f + 0.35f * params.edge) + velvet;

    const auto high = channel.highPass.process (source);
    auto shards = high * 0.3f;
    for (int i = 0; i < numShardBands; ++i)
    {
        const auto band = channel.bands[static_cast<std::size_t> (i)].process (high);
        shards += band * (0.65f / (1.0f + static_cast<float> (i)));
    }

    const auto rawFold = fold (shards, params.edge);
    const auto smoothInput = channel.preSmooth.process (shards);
    const auto smoothedFold = channel.postSmooth.process (foldOversampled (channel, smoothInput));
    const auto folded = smoothedFold + (rawFold - smoothedFold) * params.aliasBudget;
    return sanitizeOutput (folded);
}

float ShardNoiseEngine::foldOversampled (ChannelState& channel, float input) noexcept
{
    const auto midpoint = 0.5f * (channel.previousFoldInput + input);
    const auto midpointOutput = fold (midpoint, params.edge);
    const auto currentOutput = fold (input, params.edge);
    channel.previousFoldInput = input;
    return 0.5f * (midpointOutput + currentOutput);
}

float ShardNoiseEngine::nextEnvelope() noexcept
{
    if (envelope <= 0.0f)
        return 0.0f;

    const auto seconds = gateHeld ? currentDecaySeconds() : currentDecaySeconds() * 0.35f;
    const auto coefficient = std::exp (-6.90775527898f / static_cast<float> (std::max (1.0, sampleRate * seconds)));
    envelope *= coefficient;
    if (envelope < 1.0e-6f)
    {
        envelope = 0.0f;
        gateHeld = false;
    }

    return envelope;
}

float ShardNoiseEngine::currentDecaySeconds() const noexcept
{
    return 0.018f + params.burst * 0.34f;
}

} // namespace violent
