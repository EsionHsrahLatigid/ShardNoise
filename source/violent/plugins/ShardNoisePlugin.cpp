#include "violent/plugins/ShardNoisePlugin.h"

#include "violent/ParameterGridEditor.h"
#include "violent/ProductState.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace violent::plugin
{
namespace
{
constexpr std::array<char, 4> stateMagic { 'S', 'H', 'N', '1' };
constexpr int stateVersion = 1;
constexpr int engineUpdateCadence = 16;
constexpr int standaloneTriggerNote = 60;
constexpr std::size_t presetParameterCount = 7;

constexpr std::array<std::array<float, presetParameterCount>, 4> presetValues {{
    {{ 0.70f, 0.42f, 0.34f, 0.46f, 0.22f, 0.38f, -10.0f }},
    {{ 0.86f, 0.78f, 0.22f, 0.72f, 0.84f, 0.64f, -14.0f }},
    {{ 0.54f, 0.28f, 0.82f, 0.92f, 0.36f, 0.28f, -12.0f }},
    {{ 0.76f, 0.60f, 0.48f, 0.58f, 0.52f, 0.95f, -13.0f }}
}};

float decibelsToLinear (float decibels) noexcept
{
    return std::pow (10.0f, decibels / 20.0f);
}
} // namespace

ShardNoisePlugin::ShardNoisePlugin()
    : yup::AudioProcessor ("ShardNoise",
                           yup::AudioBusLayout (
                               { yup::AudioBus ("midi", yup::AudioBus::Midi, yup::AudioBus::Input, 0) },
                               { yup::AudioBus ("main", yup::AudioBus::Audio, yup::AudioBus::Output, 2) }))
{
    parameters[cut] = yup::AudioParameterBuilder()
                          .withID ("cut")
                          .withName ("Cut")
                          .withHostID (cut)
                          .withRange (0.0f, 1.0f)
                          .withDefault (0.70f)
                          .withSmoothing (18.0f)
                          .withModulatable (true)
                          .build();
    parameters[edge] = yup::AudioParameterBuilder()
                           .withID ("edge")
                           .withName ("Edge")
                           .withHostID (edge)
                           .withRange (0.0f, 1.0f)
                           .withDefault (0.42f)
                           .withSmoothing (20.0f)
                           .withModulatable (true)
                           .build();
    parameters[burst] = yup::AudioParameterBuilder()
                            .withID ("burst")
                            .withName ("Burst")
                            .withHostID (burst)
                            .withRange (0.0f, 1.0f)
                            .withDefault (0.34f)
                            .withSmoothing (16.0f)
                            .withModulatable (true)
                            .build();
    parameters[scatter] = yup::AudioParameterBuilder()
                              .withID ("scatter")
                              .withName ("Scatter")
                              .withHostID (scatter)
                              .withRange (0.0f, 1.0f)
                              .withDefault (0.46f)
                              .withSmoothing (22.0f)
                              .withModulatable (true)
                              .build();
    parameters[aliasBudget] = yup::AudioParameterBuilder()
                                  .withID ("alias_budget")
                                  .withName ("Alias Budget")
                                  .withHostID (aliasBudget)
                                  .withRange (0.0f, 1.0f)
                                  .withDefault (0.22f)
                                  .withSmoothing (12.0f)
                                  .withModulatable (true)
                                  .build();
    parameters[stereoSplit] = yup::AudioParameterBuilder()
                                  .withID ("stereo_split")
                                  .withName ("Stereo Split")
                                  .withHostID (stereoSplit)
                                  .withRange (0.0f, 1.0f)
                                  .withDefault (0.38f)
                                  .withSmoothing (18.0f)
                                  .withModulatable (true)
                                  .build();
    parameters[output] = yup::AudioParameterBuilder()
                             .withID ("output")
                             .withName ("Output")
                             .withHostID (output)
                             .withRange (-48.0f, 6.0f)
                             .withDefault (-10.0f)
                             .withSmoothing (30.0f)
                             .withModulatable (true)
                             .withUnit (yup::AudioParameter::ParameterUnit::Decibels)
                             .build();

    for (const auto& parameter : parameters)
        addParameter (parameter);
}

void ShardNoisePlugin::prepareToPlay (const yup::AudioSpec& spec)
{
    engine.prepare (spec.sampleRate);

    for (std::size_t i = 0; i < parameterHandles.size(); ++i)
    {
        parameterHandles[i] = yup::AudioParameterHandle (*parameters[i], spec.sampleRate);
        smoothedValues[i] = parameterHandles[i].getCurrentValue();
    }

    pushEngineParameters();
}

void ShardNoisePlugin::releaseResources()
{
}

void ShardNoisePlugin::processBlock (yup::AudioProcessContext<float>& context)
{
    auto& audio = context.audio;
    const auto numSamples = audio.getNumSamples();
    const auto numChannels = audio.getNumChannels();
    auto blockPeak = 0.0f;

    for (std::size_t i = 0; i < parameterHandles.size(); ++i)
        parameterHandles[i].prepareBlock (context.params, parameters[i]->getIndexInContainer());

    auto midi = context.midi.begin();
    const auto midiEnd = context.midi.end();
    auto* left = numChannels > 0 ? audio.getWritePointer (0) : nullptr;
    auto* right = numChannels > 1 ? audio.getWritePointer (1) : nullptr;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        updateHandlesForSample (sample);
        if ((sample % engineUpdateCadence) == 0)
            pushEngineParameters();

        consumeStandaloneTriggerCommands();
        consumePendingStandaloneRestart();

        while (midi != midiEnd && (*midi).samplePosition <= sample)
        {
            handleMidiMessage ((*midi).getMessage());
            ++midi;
        }

        const auto frame = engine.processSample();

        if (left != nullptr)
            left[sample] = frame.left;
        if (right != nullptr)
            right[sample] = frame.right;
        blockPeak = std::max (blockPeak, std::max (std::fabs (frame.left), std::fabs (frame.right)));

        for (int channel = 2; channel < numChannels; ++channel)
            audio.getWritePointer (channel)[sample] = 0.0f;
    }

    outputPeakMilli.store (static_cast<int> (std::clamp (blockPeak, 0.0f, 1.0f) * 1000.0f + 0.5f),
                           std::memory_order_release);
    context.midi.clear();
}

void ShardNoisePlugin::flush()
{
    lastNote = -1;
    gateOwner = GateOwner::none;
    restartStandaloneTriggerOnNextSample = false;
    standaloneTriggerGate.store (false, std::memory_order_release);
    consumedStandaloneTriggerOnCount = standaloneTriggerOnCount.load (std::memory_order_acquire);
    consumedStandaloneTriggerOffCount = standaloneTriggerOffCount.load (std::memory_order_acquire);
    outputPeakMilli.store (0, std::memory_order_release);
    engine.reset();
}

bool ShardNoisePlugin::acceptsMidi() const noexcept
{
    return true;
}

bool ShardNoisePlugin::producesMidi() const noexcept
{
    return false;
}

int ShardNoisePlugin::getNumVoices() const
{
    return 1;
}

int ShardNoisePlugin::getCurrentPreset() const noexcept
{
    return currentPreset.load (std::memory_order_relaxed);
}

void ShardNoisePlugin::setCurrentPreset (int index) noexcept
{
    if (! yup::isPositiveAndBelow (index, static_cast<int> (presetValues.size())))
        return;

    currentPreset.store (index, std::memory_order_relaxed);
    for (std::size_t i = 0; i < parameters.size(); ++i)
        parameters[i]->setValue (presetValues[static_cast<std::size_t> (index)][i]);
}

int ShardNoisePlugin::getNumPresets() const
{
    return static_cast<int> (presetNames.size());
}

yup::String ShardNoisePlugin::getPresetName (int index) const
{
    if (yup::isPositiveAndBelow (index, static_cast<int> (presetNames.size())))
        return presetNames[static_cast<std::size_t> (index)];
    return "Invalid Preset";
}

void ShardNoisePlugin::setPresetName (int index, yup::StringRef newName)
{
    if (yup::isPositiveAndBelow (index, static_cast<int> (presetNames.size())))
        presetNames[static_cast<std::size_t> (index)] = newName;
}

yup::Result ShardNoisePlugin::loadStateFromMemory (const yup::MemoryBlock& data)
{
    auto loadedPreset = currentPreset.load (std::memory_order_relaxed);
    const auto result = loadProductState (*this, data, stateMagic, stateVersion, getNumPresets(), loadedPreset);
    if (result.wasOk())
        currentPreset.store (loadedPreset, std::memory_order_relaxed);
    return result;
}

yup::Result ShardNoisePlugin::saveStateIntoMemory (yup::MemoryBlock& data)
{
    return saveProductState (*this, data, stateMagic, stateVersion, currentPreset.load (std::memory_order_relaxed));
}

bool ShardNoisePlugin::hasEditor() const
{
    return true;
}

yup::AudioProcessorEditor* ShardNoisePlugin::createEditor()
{
    return new ParameterGridEditor (*this,
                                    "ShardNoise",
                                    "Standalone trigger: button or Space. External MIDI remains active.",
                                    0xffff4a1cu);
}

void ShardNoisePlugin::setStandaloneTriggerGate (bool shouldBeHeld) noexcept
{
    const auto previous = standaloneTriggerGate.exchange (shouldBeHeld, std::memory_order_acq_rel);
    if (previous == shouldBeHeld)
        return;

    auto& counter = shouldBeHeld ? standaloneTriggerOnCount : standaloneTriggerOffCount;
    counter.fetch_add (1u, std::memory_order_release);
}

bool ShardNoisePlugin::getStandaloneTriggerGate() const noexcept
{
    return standaloneTriggerGate.load (std::memory_order_acquire);
}

float ShardNoisePlugin::getOutputPeak() const noexcept
{
    return static_cast<float> (outputPeakMilli.load (std::memory_order_acquire)) * 0.001f;
}

void ShardNoisePlugin::updateHandlesForSample (int samplePosition)
{
    for (std::size_t i = 0; i < parameterHandles.size(); ++i)
    {
        parameterHandles[i].advanceToSample (samplePosition);
        smoothedValues[i] = parameterHandles[i].getNextValue();
    }
}

void ShardNoisePlugin::pushEngineParameters()
{
    ShardNoiseParameters engineParameters;
    engineParameters.cut = smoothedValues[cut];
    engineParameters.edge = smoothedValues[edge];
    engineParameters.burst = smoothedValues[burst];
    engineParameters.scatter = smoothedValues[scatter];
    engineParameters.aliasBudget = smoothedValues[aliasBudget];
    engineParameters.stereoSplit = smoothedValues[stereoSplit];
    engineParameters.outputGain = decibelsToLinear (smoothedValues[output]);

    engine.setParameters (engineParameters);
}

void ShardNoisePlugin::consumeStandaloneTriggerCommands() noexcept
{
    const auto onCount = standaloneTriggerOnCount.load (std::memory_order_acquire);
    const auto offCount = standaloneTriggerOffCount.load (std::memory_order_acquire);
    const auto sawOn = onCount != consumedStandaloneTriggerOnCount;
    const auto sawOff = offCount != consumedStandaloneTriggerOffCount;

    if (! sawOn && ! sawOff)
        return;

    consumedStandaloneTriggerOnCount = onCount;
    consumedStandaloneTriggerOffCount = offCount;

    if (gateOwner == GateOwner::midi)
        return;

    if (sawOn && sawOff)
    {
        if (standaloneTriggerGate.load (std::memory_order_acquire))
        {
            stopStandaloneTrigger();
            startStandaloneTrigger();
        }
        else
        {
            startStandaloneTrigger();
            stopStandaloneTrigger();
        }
        return;
    }

    if (sawOn)
        startStandaloneTrigger();
    else
        stopStandaloneTrigger();
}

void ShardNoisePlugin::consumePendingStandaloneRestart() noexcept
{
    if (! restartStandaloneTriggerOnNextSample)
        return;

    restartStandaloneTriggerOnNextSample = false;
    if (standaloneTriggerGate.load (std::memory_order_acquire))
        startStandaloneTrigger();
}

void ShardNoisePlugin::startStandaloneTrigger() noexcept
{
    restartStandaloneTriggerOnNextSample = false;
    gateOwner = GateOwner::standalone;
    engine.noteOn (standaloneTriggerNote, 1.0f);
}

void ShardNoisePlugin::stopStandaloneTrigger() noexcept
{
    restartStandaloneTriggerOnNextSample = false;
    if (gateOwner == GateOwner::standalone)
    {
        engine.noteOff();
        gateOwner = GateOwner::none;
    }
}

void ShardNoisePlugin::handleMidiMessage (const yup::MidiMessage& message) noexcept
{
    if (message.isNoteOn())
    {
        lastNote = std::clamp (message.getNoteNumber(), 0, 127);
        restartStandaloneTriggerOnNextSample = false;
        gateOwner = GateOwner::midi;
        engine.noteOn (lastNote, message.getFloatVelocity());
    }
    else if (message.isNoteOff())
    {
        const auto note = std::clamp (message.getNoteNumber(), 0, 127);
        if (note == lastNote)
        {
            if (gateOwner == GateOwner::midi)
            {
                engine.noteOff();
                gateOwner = GateOwner::none;
                restartStandaloneTriggerOnNextSample = standaloneTriggerGate.load (std::memory_order_acquire);
            }
            lastNote = -1;
        }
    }
}

} // namespace violent::plugin

extern "C" yup::AudioProcessor* createPluginProcessor()
{
    return new violent::plugin::ShardNoisePlugin();
}
