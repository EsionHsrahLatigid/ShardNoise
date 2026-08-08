#include "violent/plugins/ShardNoisePlugin.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace
{

float renderBlockEnergy (violent::plugin::ShardNoisePlugin& processor,
                         yup::AudioBuffer<float>& audio,
                         yup::MidiBuffer& midi,
                         yup::ParameterChangeBuffer& parameterChanges)
{
    audio.clear();
    yup::AudioProcessContext<float> context { audio, midi, parameterChanges, nullptr };
    processor.processBlock (context);

    auto total = 0.0f;
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
    {
        const auto* samples = audio.getReadPointer (channel);
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
        {
            assert (std::isfinite (samples[sample]));
            total += samples[sample] * samples[sample];
        }
    }
    return total;
}

struct PluginHarness
{
    violent::plugin::ShardNoisePlugin processor;
    yup::AudioBuffer<float> audio { 2, 256 };
    yup::MidiBuffer midi;
    yup::ParameterChangeBuffer parameterChanges;

    PluginHarness()
    {
        processor.prepareToPlay ({ 48000.0f, audio.getNumSamples(), audio.getNumChannels() });
        parameterChanges.reserve (64);
    }

    float render()
    {
        return renderBlockEnergy (processor, audio, midi, parameterChanges);
    }
};

void testSyntheticStandaloneTriggerBridge()
{
    PluginHarness harness;

    assert (harness.render() == 0.0f);
    assert (harness.processor.getOutputPeak() == 0.0f);

    harness.processor.setStandaloneTriggerGate (true);
    const auto triggeredEnergy = harness.render();
    assert (triggeredEnergy > 0.01f);
    assert (harness.processor.getOutputPeak() > 0.0f);

    harness.processor.setStandaloneTriggerGate (false);
    (void) harness.render();
}

void testExternalMidiStillTriggers()
{
    PluginHarness harness;

    harness.midi.addEvent (yup::MidiMessage::noteOn (1, 64, 0.9f), 0);
    const auto midiEnergy = harness.render();
    assert (midiEnergy > 0.01f);
    assert (harness.midi.isEmpty());
}

void testMidiNoteOffHandsBackToHeldStandaloneGate()
{
    PluginHarness harness;

    harness.processor.setStandaloneTriggerGate (true);
    const auto standaloneEnergy = harness.render();
    assert (standaloneEnergy > 0.01f);
    assert (harness.processor.getStandaloneTriggerGate());

    harness.midi.addEvent (yup::MidiMessage::noteOn (1, 72, 1.0f), 0);
    harness.midi.addEvent (yup::MidiMessage::noteOff (1, 72), 128);
    const auto overlapEnergy = harness.render();
    assert (overlapEnergy > 0.01f);

    const auto handoffEnergy = harness.render();
    assert (handoffEnergy > 0.01f);
    assert (harness.processor.getStandaloneTriggerGate());
    assert (harness.processor.getOutputPeak() > 0.0f);
}

void testMidiNoteOffHandoffOutlivesMidiReleaseTail()
{
    violent::plugin::ShardNoisePlugin processor;
    processor.getParameters()[2]->setValue (0.0f);
    processor.prepareToPlay ({ 48000.0f, 1, 2 });

    yup::AudioBuffer<float> audio { 2, 1 };
    yup::MidiBuffer midi;
    yup::ParameterChangeBuffer parameterChanges;
    parameterChanges.reserve (8);

    auto renderOne = [&]() { return renderBlockEnergy (processor, audio, midi, parameterChanges); };

    processor.setStandaloneTriggerGate (true);
    assert (renderOne() > 0.0f);

    midi.addEvent (yup::MidiMessage::noteOn (1, 72, 1.0f), 0);
    assert (renderOne() > 0.0f);

    midi.addEvent (yup::MidiMessage::noteOff (1, 72), 0);
    assert (renderOne() > 0.0f);

    auto lateHandoffEnergy = 0.0f;
    for (int sample = 0; sample < 760; ++sample)
    {
        const auto energy = renderOne();
        if (sample >= 640)
            lateHandoffEnergy += energy;
    }

    assert (lateHandoffEnergy > 1.0e-8f);
    assert (processor.getStandaloneTriggerGate());
}

void testUiEdgesDoNotStealHeldMidiNote()
{
    violent::plugin::ShardNoisePlugin reference;
    violent::plugin::ShardNoisePlugin withUiEdges;
    reference.prepareToPlay ({ 48000.0f, 1, 2 });
    withUiEdges.prepareToPlay ({ 48000.0f, 1, 2 });

    yup::AudioBuffer<float> referenceAudio { 2, 1 };
    yup::AudioBuffer<float> uiAudio { 2, 1 };
    yup::MidiBuffer referenceMidi;
    yup::MidiBuffer uiMidi;
    yup::ParameterChangeBuffer referenceParams;
    yup::ParameterChangeBuffer uiParams;
    referenceParams.reserve (8);
    uiParams.reserve (8);

    auto renderReference = [&]() { return renderBlockEnergy (reference, referenceAudio, referenceMidi, referenceParams); };
    auto renderUi = [&]() { return renderBlockEnergy (withUiEdges, uiAudio, uiMidi, uiParams); };

    referenceMidi.addEvent (yup::MidiMessage::noteOn (1, 72, 1.0f), 0);
    uiMidi.addEvent (yup::MidiMessage::noteOn (1, 72, 1.0f), 0);
    assert (renderReference() == renderUi());

    withUiEdges.setStandaloneTriggerGate (true);
    assert (renderReference() == renderUi());

    withUiEdges.setStandaloneTriggerGate (false);
    assert (renderReference() == renderUi());
    assert (! withUiEdges.getStandaloneTriggerGate());
}

void testMidiReleaseDoesNotHandoffAfterUiReleased()
{
    PluginHarness harness;

    harness.midi.addEvent (yup::MidiMessage::noteOn (1, 72, 1.0f), 0);
    assert (harness.render() > 0.01f);

    harness.processor.setStandaloneTriggerGate (true);
    assert (harness.render() > 0.01f);
    harness.processor.setStandaloneTriggerGate (false);
    assert (harness.render() > 0.01f);
    assert (! harness.processor.getStandaloneTriggerGate());

    harness.midi.addEvent (yup::MidiMessage::noteOff (1, 72), 0);
    assert (harness.render() > 0.0f);

    for (int i = 0; i < 240; ++i)
        (void) harness.render();

    assert (harness.render() == 0.0f);
}

void testRapidStandalonePressReleaseBeforeAudioCallbackStillProducesPulse()
{
    PluginHarness harness;

    harness.processor.setStandaloneTriggerGate (true);
    harness.processor.setStandaloneTriggerGate (false);
    assert (! harness.processor.getStandaloneTriggerGate());

    const auto pulseEnergy = harness.render();
    assert (pulseEnergy > 0.01f);
    assert (harness.processor.getOutputPeak() > 0.0f);
}

void testStandaloneReleaseDecaysToSilence()
{
    PluginHarness harness;

    harness.processor.setStandaloneTriggerGate (true);
    assert (harness.render() > 0.01f);

    harness.processor.setStandaloneTriggerGate (false);
    for (int i = 0; i < 240; ++i)
        (void) harness.render();

    assert (harness.render() == 0.0f);
    assert (harness.processor.getOutputPeak() == 0.0f);
}

void testFlushClearsStandaloneLifecycleState()
{
    PluginHarness harness;

    harness.processor.setStandaloneTriggerGate (true);
    assert (harness.render() > 0.01f);
    harness.processor.flush();

    assert (! harness.processor.getStandaloneTriggerGate());
    assert (harness.processor.getOutputPeak() == 0.0f);
    assert (harness.render() == 0.0f);
}

} // namespace

int main()
{
    testSyntheticStandaloneTriggerBridge();
    testExternalMidiStillTriggers();
    testMidiNoteOffHandsBackToHeldStandaloneGate();
    testMidiNoteOffHandoffOutlivesMidiReleaseTail();
    testUiEdgesDoNotStealHeldMidiNote();
    testMidiReleaseDoesNotHandoffAfterUiReleased();
    testRapidStandalonePressReleaseBeforeAudioCallbackStillProducesPulse();
    testStandaloneReleaseDecaysToSilence();
    testFlushClearsStandaloneLifecycleState();

    std::cout << "ShardNoisePluginTests passed\n";
    return 0;
}
