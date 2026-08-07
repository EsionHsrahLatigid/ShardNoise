#pragma once

#include "violent/ViolentDspPrimitives.h"

#include <array>
#include <cstdint>

namespace violent
{

/** User-facing controls for the ShardNoise monophonic MIDI instrument.

    All values are realtime-safe to update and are clamped internally:
    cut [0, 1] maps the high-pass and shard band centers into the high spectrum;
    edge [0, 1] controls nonlinear fold amount;
    burst [0, 1] controls envelope length and note-derived subdivision density;
    scatter [0, 1] controls velvet impulse probability and band spread;
    aliasBudget [0, 1] crossfades smoothed 2x-midpoint folded output to raw fold;
    stereoSplit [0, 1] offsets right-channel seeds and band centers;
    outputGain [0, 2] is final bounded gain.
*/
struct ShardNoiseParameters
{
    float cut = 0.65f;
    float edge = 0.35f;
    float burst = 0.35f;
    float scatter = 0.45f;
    float aliasBudget = 0.25f;
    float stereoSplit = 0.35f;
    float outputGain = 0.45f;
};

/** Monophonic high-frequency binary/velvet noise shard instrument.

    noteOn() is the only trigger source. The MIDI note number changes the
    deterministic bit pattern and burst subdivision, but it is deliberately not
    interpreted as oscillator pitch. processSample() and process() allocate no
    memory and return silence until a note has triggered the engine.
*/
class ShardNoiseEngine
{
public:
    ShardNoiseEngine();

    /** Sets sample rate, rebuilds filters, and resets render state to silence. */
    void prepare (double sampleRate) noexcept;

    /** Clears envelopes, filters, and deterministic generator state. */
    void reset() noexcept;

    /** Sets clamped realtime parameters. Filter coefficients update immediately. */
    void setParameters (const ShardNoiseParameters& parameters) noexcept;

    /** Starts or restarts the monophonic shard for a MIDI note and velocity. */
    void noteOn (int noteNumber, float velocity) noexcept;

    /** Releases a currently ringing shard into its decay tail. */
    void noteOff() noexcept;

    /** Renders one bounded finite stereo sample. */
    [[nodiscard]] StereoFrame processSample() noexcept;

    /** Renders stereo samples into existing buffers. Null buffers or nonpositive sizes are ignored. */
    void process (float* left, float* right, int numSamples) noexcept;

    /** Returns true while the envelope can still produce non-silent output. */
    [[nodiscard]] bool isActive() const noexcept;

private:
    static constexpr int numShardBands = 4;

    struct ClampedParameters
    {
        float cut = 0.65f;
        float edge = 0.35f;
        float burst = 0.35f;
        float scatter = 0.45f;
        float aliasBudget = 0.25f;
        float stereoSplit = 0.35f;
        float outputGain = 0.45f;
    };

    struct ChannelState
    {
        DeterministicNoise binary;
        DeterministicNoise velvet;
        Biquad highPass;
        Biquad preSmooth;
        Biquad postSmooth;
        std::array<Biquad, numShardBands> bands {};
        float previousFoldInput = 0.0f;
    };

    static std::uint32_t mixSeed (std::uint32_t value) noexcept;
    static float fold (float input, float edge) noexcept;
    static float clampUnit (float value, float fallback) noexcept;
    static float sanitizeOutput (float value) noexcept;

    void updateFilters() noexcept;
    void resetChannel (ChannelState& channel) noexcept;
    float nextVoiceSample (ChannelState& channel, std::uint32_t channelSalt) noexcept;
    float foldOversampled (ChannelState& channel, float input) noexcept;
    float nextEnvelope() noexcept;
    [[nodiscard]] float currentDecaySeconds() const noexcept;

    ClampedParameters params;
    ChannelState leftChannel;
    ChannelState rightChannel;
    double sampleRate = 44100.0;
    std::uint32_t noteSeed = 0x6d2b79f5u;
    std::uint32_t sampleCounter = 0u;
    int noteNumber = 60;
    int subdivision = 3;
    float envelope = 0.0f;
    float velocity = 0.0f;
    bool gateHeld = false;
};

} // namespace violent
