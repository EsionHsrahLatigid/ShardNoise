#include "violent/ParameterGridEditor.h"

#include "violent/plugins/ShardNoisePlugin.h"

#include <algorithm>
#include <functional>

namespace violent::plugin
{
namespace
{
class MomentaryTriggerButton final : public yup::TextButton
{
public:
    MomentaryTriggerButton()
        : yup::TextButton ("trigger")
    {
        setButtonText ("Trigger");
    }

    std::function<void (bool)> onGateChanged;

    void setGateActive (bool active)
    {
        gateActive = active;
        repaint();
    }

    void mouseDown (const yup::MouseEvent& event) override
    {
        yup::TextButton::mouseDown (event);
        if (auto* parent = getParentComponent())
            parent->takeKeyboardFocus();
        setGateActive (true);
        if (onGateChanged)
            onGateChanged (true);
    }

    void mouseUp (const yup::MouseEvent& event) override
    {
        yup::TextButton::mouseUp (event);
        if (auto* parent = getParentComponent())
            parent->takeKeyboardFocus();
        setGateActive (false);
        if (onGateChanged)
            onGateChanged (false);
    }

    void paintButton (yup::Graphics& graphics) override
    {
        yup::TextButton::paintButton (graphics);
        if (gateActive)
        {
            graphics.setFillColor (0x55ff4a1cu);
            graphics.fillRect (getLocalBounds().to<float>().reduced (3.0f));
        }
    }

private:
    bool gateActive = false;
};

class OutputMeter final : public yup::Component
{
public:
    void setLevel (float newLevel)
    {
        level = std::clamp (newLevel, 0.0f, 1.0f);
        repaint();
    }

    void paint (yup::Graphics& graphics) override
    {
        const auto bounds = getLocalBounds().to<float>();
        graphics.setFillColor (0xff171a1fu);
        graphics.fillRect (bounds);
        graphics.setFillColor (0xffff4a1cu);
        graphics.fillRect (bounds.withWidth (bounds.getWidth() * level));
    }

private:
    float level = 0.0f;
};

class EditorSlider final : public yup::Slider
{
public:
    EditorSlider()
        : yup::Slider (yup::Slider::RotaryVerticalDrag)
    {
        setClickingGrabFocus (false);
    }

    void mouseDown (const yup::MouseEvent& event) override
    {
        yup::Slider::mouseDown (event);
        restoreParentFocus();
    }

    void mouseUp (const yup::MouseEvent& event) override
    {
        yup::Slider::mouseUp (event);
        restoreParentFocus();
    }

private:
    void restoreParentFocus()
    {
        if (auto* parent = getParentComponent())
            parent->takeKeyboardFocus();
    }
};
} // namespace

ParameterGridEditor::ParameterGridEditor (yup::AudioProcessor& processor,
                                          yup::StringRef newTitle,
                                          yup::StringRef newWarning,
                                          std::uint32_t newAccentColor)
    : title (newTitle)
    , warning (newWarning)
    , accentColor (newAccentColor)
    , shardNoiseProcessor (dynamic_cast<ShardNoisePlugin*> (&processor))
{
    setWantsKeyboardFocus (true);

    const auto processorParameters = processor.getParameters();
    parameters.assign (processorParameters.begin(), processorParameters.end());

    titleLabel = std::make_unique<yup::Label>();
    titleLabel->setText (title, yup::dontSendNotification);
    titleLabel->setJustification (yup::Justification::centerLeft);
    addAndMakeVisible (*titleLabel);

    warningLabel = std::make_unique<yup::Label>();
    warningLabel->setText (warning, yup::dontSendNotification);
    warningLabel->setJustification (yup::Justification::centerLeft);
    addAndMakeVisible (*warningLabel);

    if (shardNoiseProcessor != nullptr)
    {
        auto trigger = std::make_unique<MomentaryTriggerButton>();
        trigger->setClickingGrabFocus (false);
        trigger->onGateChanged = [this] (bool active)
        {
            mouseGateHeld = active;
            updateStandaloneGate();
        };
        addAndMakeVisible (*trigger);
        triggerButton = std::move (trigger);

        meterLabel = std::make_unique<yup::Label>();
        meterLabel->setText ("Output", yup::dontSendNotification);
        meterLabel->setJustification (yup::Justification::centerLeft);
        addAndMakeVisible (*meterLabel);

        outputMeter = std::make_unique<OutputMeter>();
        addAndMakeVisible (*outputMeter);
    }

    labels.reserve (parameters.size());
    sliders.reserve (parameters.size());
    valueLabels.reserve (parameters.size());

    for (const auto& parameter : parameters)
    {
        auto label = std::make_unique<yup::Label>();
        label->setText (parameter->getName(), yup::dontSendNotification);
        label->setJustification (yup::Justification::center);
        addAndMakeVisible (*label);
        labels.push_back (std::move (label));

        auto slider = std::make_unique<EditorSlider>();
        slider->setRange (parameter->getMinimumValue(),
                          parameter->getMaximumValue(),
                          parameter->isStepped() ? 1.0 : 0.0);
        slider->setDefaultValue (parameter->getDefaultValue());
        slider->setValue (parameter->getValue(), yup::dontSendNotification);
        slider->setTextBoxStyle (yup::Slider::NoTextBox);
        slider->setPopupDisplayEnabled (false);
        slider->setMouseCursor (yup::MouseCursor::Hand);
        slider->onDragStart = [parameter] (const yup::MouseEvent&) { parameter->beginChangeGesture(); };
        slider->onValueChanged = [parameter] (double value)
        {
            parameter->setValueNotifyingHost (static_cast<float> (value));
        };
        slider->onDragEnd = [parameter] (const yup::MouseEvent&) { parameter->endChangeGesture(); };
        addAndMakeVisible (*slider);
        sliders.push_back (std::move (slider));

        auto valueLabel = std::make_unique<yup::Label>();
        valueLabel->setText (parameter->toString(), yup::dontSendNotification);
        valueLabel->setJustification (yup::Justification::center);
        addAndMakeVisible (*valueLabel);
        valueLabels.push_back (std::move (valueLabel));
    }

    setSize (getPreferredSize().to<float>());
    startTimerHz (30);
}

ParameterGridEditor::~ParameterGridEditor()
{
    mouseGateHeld = false;
    spaceGateHeld = false;
    updateStandaloneGate();
}

bool ParameterGridEditor::isResizable() const
{
    return true;
}

bool ParameterGridEditor::shouldPreserveAspectRatio() const
{
    return true;
}

yup::Size<int> ParameterGridEditor::getPreferredSize() const
{
    return { 940, 520 };
}

void ParameterGridEditor::paint (yup::Graphics& graphics)
{
    graphics.setFillColor (0xff0a0b0du);
    graphics.fillAll();

    graphics.setFillColor (accentColor);
    graphics.fillRect (0.0f, 0.0f, getWidth(), 5.0f);

}

void ParameterGridEditor::resized()
{
    constexpr int columns = 5;
    constexpr float margin = 20.0f;
    constexpr float top = 78.0f;
    constexpr float gap = 12.0f;
    constexpr float labelHeight = 24.0f;
    constexpr float valueHeight = 24.0f;
    constexpr float controlGap = 4.0f;

    const auto bounds = getLocalBounds();
    const auto cellWidth = (bounds.getWidth() - 2.0f * margin - gap * (columns - 1)) / columns;
    const auto rows = std::max (1, static_cast<int> ((sliders.size() + columns - 1) / columns));
    const auto availableHeight = bounds.getHeight() - top - margin;
    const auto cellHeight = (availableHeight - gap * (rows - 1)) / rows;

    titleLabel->setBounds (24.0f, 12.0f, bounds.getWidth() - 48.0f, 30.0f);
    warningLabel->setBounds (24.0f, 43.0f, bounds.getWidth() - 280.0f, 24.0f);

    if (triggerButton != nullptr && meterLabel != nullptr && outputMeter != nullptr)
    {
        triggerButton->setBounds (bounds.getWidth() - 236.0f, 18.0f, 86.0f, 34.0f);
        meterLabel->setBounds (bounds.getWidth() - 136.0f, 16.0f, 64.0f, 18.0f);
        outputMeter->setBounds (bounds.getWidth() - 136.0f, 38.0f, 112.0f, 12.0f);
    }

    for (std::size_t i = 0; i < sliders.size(); ++i)
    {
        const auto column = static_cast<int> (i) % columns;
        const auto row = static_cast<int> (i) / columns;
        const auto x = margin + column * (cellWidth + gap);
        const auto y = top + row * (cellHeight + gap);
        const auto controlHeight = cellHeight - labelHeight - valueHeight - 2.0f * controlGap;
        const auto controlSize = std::max (20.0f, std::min (cellWidth - 8.0f, controlHeight));
        const auto controlX = x + 0.5f * (cellWidth - controlSize);
        const auto controlY = y + labelHeight + controlGap;

        labels[i]->setBounds (x, y, cellWidth, labelHeight);
        sliders[i]->setBounds (controlX, controlY, controlSize, controlSize);
        valueLabels[i]->setBounds (x, y + cellHeight - valueHeight, cellWidth, valueHeight);
    }
}

void ParameterGridEditor::keyDown (const yup::KeyPress& keys, const yup::Point<float>& position)
{
    if (shardNoiseProcessor != nullptr && keys.getKey() == yup::KeyPress::spaceKey)
    {
        if (! spaceGateHeld)
        {
            spaceGateHeld = true;
            updateStandaloneGate();
        }
        return;
    }

    yup::AudioProcessorEditor::keyDown (keys, position);
}

void ParameterGridEditor::keyUp (const yup::KeyPress& keys, const yup::Point<float>& position)
{
    if (shardNoiseProcessor != nullptr && keys.getKey() == yup::KeyPress::spaceKey)
    {
        spaceGateHeld = false;
        updateStandaloneGate();
        return;
    }

    yup::AudioProcessorEditor::keyUp (keys, position);
}

void ParameterGridEditor::focusLost()
{
    mouseGateHeld = false;
    spaceGateHeld = false;
    updateStandaloneGate();
}

void ParameterGridEditor::timerCallback()
{
    for (std::size_t i = 0; i < sliders.size(); ++i)
    {
        if (! sliders[i]->isCurrentlyBeingDragged())
            sliders[i]->setValue (parameters[i]->getValue(), yup::dontSendNotification);
        valueLabels[i]->setText (parameters[i]->toString(), yup::dontSendNotification);
    }

    if (shardNoiseProcessor != nullptr)
    {
        const auto gate = shardNoiseProcessor->getStandaloneTriggerGate();
        if (auto* button = dynamic_cast<MomentaryTriggerButton*> (triggerButton.get()))
            button->setGateActive (gate);

        if (auto* meter = dynamic_cast<OutputMeter*> (outputMeter.get()))
            meter->setLevel (std::max (shardNoiseProcessor->getOutputPeak(), gate ? 0.08f : 0.0f));
    }
}

void ParameterGridEditor::updateStandaloneGate() noexcept
{
    if (shardNoiseProcessor != nullptr)
        shardNoiseProcessor->setStandaloneTriggerGate (mouseGateHeld || spaceGateHeld);
}

} // namespace violent::plugin
