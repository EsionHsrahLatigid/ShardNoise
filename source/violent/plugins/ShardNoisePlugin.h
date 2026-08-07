#pragma once

#include "violent/ShardNoiseEngine.h"

#include <yup_audio_processors/yup_audio_processors.h>

#include <array>
#include <atomic>

namespace violent::plugin
{

class ShardNoisePlugin final : public yup::AudioProcessor
{
public:
    ShardNoisePlugin();

    void prepareToPlay (const yup::AudioSpec& spec) override;
    void releaseResources() override;
    void processBlock (yup::AudioProcessContext<float>& context) override;
    void flush() override;

    bool acceptsMidi() const noexcept override;
    bool producesMidi() const noexcept override;
    int getNumVoices() const override;

    int getCurrentPreset() const noexcept override;
    void setCurrentPreset (int index) noexcept override;
    int getNumPresets() const override;
    yup::String getPresetName (int index) const override;
    void setPresetName (int index, yup::StringRef newName) override;

    yup::Result loadStateFromMemory (const yup::MemoryBlock& data) override;
    yup::Result saveStateIntoMemory (yup::MemoryBlock& data) override;

    bool hasEditor() const override;
    yup::AudioProcessorEditor* createEditor() override;

private:
    enum ParameterIndex
    {
        cut,
        edge,
        burst,
        scatter,
        aliasBudget,
        stereoSplit,
        output,
        parameterCount
    };

    void updateHandlesForSample (int samplePosition);
    void pushEngineParameters();
    void handleMidiMessage (const yup::MidiMessage& message) noexcept;

    std::array<yup::AudioParameter::Ptr, parameterCount> parameters;
    std::array<yup::AudioParameterHandle, parameterCount> parameterHandles;
    std::array<float, parameterCount> smoothedValues {};
    ShardNoiseEngine engine;

    int lastNote = -1;
    std::atomic<int> currentPreset { 0 };
    std::array<yup::String, 4> presetNames {
        "Glass Teeth",
        "Static Razor",
        "Dust Trigger",
        "Split Shards"
    };
};

} // namespace violent::plugin
