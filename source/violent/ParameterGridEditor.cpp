#include "violent/ParameterGridEditor.h"

#include <ehl/yup_plugin_ui/EhlPluginTheme.h>
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
        const auto bounds = getLocalBounds().to<float>();
        const auto active = gateActive || isButtonDown();
        const auto over = isButtonOver();

        graphics.setFillColor (active ? ehl::ui::paper : (over ? ehl::ui::mid : ehl::ui::low));
        graphics.fillRect (bounds);
        graphics.setStrokeColor (hasKeyboardFocus() ? ehl::ui::paper : ehl::ui::mid);
        graphics.setStrokeWidth (hasKeyboardFocus() ? 2.0f : 1.0f);
        graphics.strokeRect (bounds.reduced (1.0f));

        graphics.setFillColor (active || over ? ehl::ui::ink : ehl::ui::paper);
        graphics.fillFittedText (getStyledText(), getTextBounds());
    }

private:
    bool gateActive = false;
};
} // namespace

ParameterGridEditor::ParameterGridEditor (yup::AudioProcessor& processor,
                                          yup::StringRef newTitle,
                                          yup::StringRef newWarning,
                                          std::uint32_t newAccentColor)
    : title (newTitle)
    , warning (newWarning)
    , shardNoiseProcessor (dynamic_cast<ShardNoisePlugin*> (&processor))
{
    (void) newAccentColor;

    setWantsKeyboardFocus (true);

    const auto processorParameters = processor.getParameters();
    parameters.assign (processorParameters.begin(), processorParameters.end());

    titleLabel = std::make_unique<yup::Label>();
    titleLabel->setText (title, yup::dontSendNotification);
    titleLabel->setJustification (yup::Justification::centerLeft);
    ehl::ui::styleLabel (*titleLabel, ehl::ui::TextRole::primary);
    addAndMakeVisible (*titleLabel);

    warningLabel = std::make_unique<yup::Label>();
    warningLabel->setText (warning, yup::dontSendNotification);
    warningLabel->setJustification (yup::Justification::centerLeft);
    ehl::ui::styleLabel (*warningLabel, ehl::ui::TextRole::secondary);
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
        ehl::ui::styleLabel (*meterLabel, ehl::ui::TextRole::secondary);
        addAndMakeVisible (*meterLabel);

        outputMeter = std::make_unique<ehl::ui::StripMeter> (ehl::ui::paper);
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
        ehl::ui::styleLabel (*label, ehl::ui::TextRole::secondary);
        addAndMakeVisible (*label);
        labels.push_back (std::move (label));

        auto slider = std::make_unique<ehl::ui::PixelSlider> (yup::Slider::RotaryVerticalDrag);
        slider->setRange (parameter->getMinimumValue(),
                          parameter->getMaximumValue(),
                          parameter->isStepped() ? 1.0 : 0.0);
        slider->setDefaultValue (parameter->getDefaultValue());
        slider->setValue (parameter->getValue(), yup::dontSendNotification);
        slider->setTextBoxStyle (yup::Slider::NoTextBox);
        slider->setPopupDisplayEnabled (false);
        slider->setMouseCursor (yup::MouseCursor::Hand);
        slider->setClickingGrabFocus (false);
        slider->onDragStart = [this, parameter] (const yup::MouseEvent&)
        {
            takeKeyboardFocus();
            parameter->beginChangeGesture();
        };
        slider->onValueChanged = [parameter] (double value)
        {
            parameter->setValueNotifyingHost (static_cast<float> (value));
        };
        slider->onDragEnd = [this, parameter] (const yup::MouseEvent&)
        {
            takeKeyboardFocus();
            parameter->endChangeGesture();
        };
        addAndMakeVisible (*slider);
        sliders.push_back (std::move (slider));

        auto valueLabel = std::make_unique<yup::Label>();
        valueLabel->setText (parameter->toString(), yup::dontSendNotification);
        valueLabel->setJustification (yup::Justification::center);
        ehl::ui::styleLabel (*valueLabel, ehl::ui::TextRole::primary);
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
    return ehl::ui::preferredSize;
}

void ParameterGridEditor::paint (yup::Graphics& graphics)
{
    ehl::ui::paintEditorBackground (graphics, getWidth(), getHeight());
}

void ParameterGridEditor::resized()
{
    constexpr int columns = 7;
    constexpr float margin = 16.0f;
    constexpr float top = 128.0f;
    constexpr float gap = 8.0f;
    constexpr float labelHeight = 24.0f;
    constexpr float valueHeight = 24.0f;
    constexpr float controlSize = 72.0f;

    const auto bounds = getLocalBounds();
    const auto cellWidth = (bounds.getWidth() - 2.0f * margin - gap * (columns - 1)) / columns;
    const auto rows = std::max (1, static_cast<int> ((sliders.size() + columns - 1) / columns));
    const auto availableHeight = bounds.getHeight() - top - margin;
    const auto cellHeight = (availableHeight - gap * (rows - 1)) / rows;

    titleLabel->setBounds (20.0f, 8.0f, bounds.getWidth() - 40.0f, 28.0f);
    warningLabel->setBounds (20.0f, 36.0f, bounds.getWidth() - 280.0f, 20.0f);

    if (triggerButton != nullptr && meterLabel != nullptr && outputMeter != nullptr)
    {
        triggerButton->setBounds (margin, 72.0f, 96.0f, 28.0f);
        meterLabel->setBounds (margin + 112.0f, 68.0f, 56.0f, 16.0f);
        outputMeter->setBounds (margin + 168.0f, 76.0f, std::max (120.0f, bounds.getWidth() - margin - 192.0f), 12.0f);
    }

    for (std::size_t i = 0; i < sliders.size(); ++i)
    {
        const auto column = static_cast<int> (i) % columns;
        const auto row = static_cast<int> (i) / columns;
        const auto x = margin + column * (cellWidth + gap);
        const auto y = top + row * (cellHeight + gap);
        const auto inset = rows > 1 ? 4.0f : 12.0f;
        const auto labelY = y + inset;
        const auto valueY = y + cellHeight - valueHeight - inset;
        const auto controlTop = labelY + labelHeight;
        const auto controlBottom = valueY;
        const auto fittedControlSize = std::min ({ controlSize,
                                                   cellWidth - 8.0f,
                                                   std::max (20.0f, controlBottom - controlTop) });
        const auto controlX = x + 0.5f * (cellWidth - fittedControlSize);
        const auto controlY = controlTop + 0.5f * (controlBottom - controlTop - fittedControlSize);

        labels[i]->setBounds (x + 2.0f, labelY, cellWidth - 4.0f, labelHeight);
        sliders[i]->setBounds (controlX, controlY, fittedControlSize, fittedControlSize);
        valueLabels[i]->setBounds (x + 2.0f, valueY, cellWidth - 4.0f, valueHeight);
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

        if (outputMeter != nullptr)
            outputMeter->setLevel (std::max (shardNoiseProcessor->getOutputPeak(), gate ? 0.08f : 0.0f));
    }
}

void ParameterGridEditor::updateStandaloneGate() noexcept
{
    if (shardNoiseProcessor != nullptr)
        shardNoiseProcessor->setStandaloneTriggerGate (mouseGateHeld || spaceGateHeld);
}

} // namespace violent::plugin
