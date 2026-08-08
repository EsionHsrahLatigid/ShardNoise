#pragma once

#include <yup_audio_processors/yup_audio_processors.h>
#include <yup_gui/yup_gui.h>

#include <memory>
#include <vector>

namespace violent::plugin
{

class ShardNoisePlugin;

/** Reusable parameter-grid shell; product DSP and parameter semantics stay processor-owned. */
class ParameterGridEditor final
    : public yup::AudioProcessorEditor
    , private yup::Timer
{
public:
    ParameterGridEditor (yup::AudioProcessor& processor,
                         yup::StringRef title,
                         yup::StringRef warning,
                         std::uint32_t accentColor);
    ~ParameterGridEditor() override;

    bool isResizable() const override;
    bool shouldPreserveAspectRatio() const override;
    yup::Size<int> getPreferredSize() const override;
    void paint (yup::Graphics& graphics) override;
    void resized() override;
    void keyDown (const yup::KeyPress& keys, const yup::Point<float>& position) override;
    void keyUp (const yup::KeyPress& keys, const yup::Point<float>& position) override;
    void focusLost() override;

private:
    void timerCallback() override;
    void updateStandaloneGate() noexcept;

    yup::String title;
    yup::String warning;
    std::uint32_t accentColor = 0xffff3300u;
    ShardNoisePlugin* shardNoiseProcessor = nullptr;
    bool mouseGateHeld = false;
    bool spaceGateHeld = false;
    std::unique_ptr<yup::Label> titleLabel;
    std::unique_ptr<yup::Label> warningLabel;
    std::unique_ptr<yup::TextButton> triggerButton;
    std::unique_ptr<yup::Label> meterLabel;
    std::unique_ptr<yup::Component> outputMeter;
    std::vector<yup::AudioParameter::Ptr> parameters;
    std::vector<std::unique_ptr<yup::Label>> labels;
    std::vector<std::unique_ptr<yup::Slider>> sliders;
    std::vector<std::unique_ptr<yup::Label>> valueLabels;
};

} // namespace violent::plugin
