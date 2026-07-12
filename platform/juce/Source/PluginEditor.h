#pragma once

#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class BubbleCloudAudioProcessorEditor : public juce::AudioProcessorEditor,
                                        private juce::Timer
{
public:
    explicit BubbleCloudAudioProcessorEditor(BubbleCloudAudioProcessor&);
    ~BubbleCloudAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    struct ControlBinding
    {
        juce::String parameterId;
        juce::String title;
        juce::String role;
        juce::Slider slider;
        juce::Label titleLabel;
        juce::Label roleLabel;
        juce::Label valueLabel;
    };

    class BubblesLookAndFeel;
    class CloudVisualizer;

    ControlBinding& addControl(std::vector<std::unique_ptr<ControlBinding>>& target,
                               const juce::String& parameterId,
                               const juce::String& title,
                               const juce::String& role);

    void layoutControls(std::vector<std::unique_ptr<ControlBinding>>& controls,
                        juce::Rectangle<int> bounds,
                        int columns);

    void updateControlValue(ControlBinding& control);
    void timerCallback() override;

    BubbleCloudAudioProcessor& audioProcessor;

    std::unique_ptr<BubblesLookAndFeel> bubblesLookAndFeel;
    std::unique_ptr<CloudVisualizer> cloudVisualizer;

    juce::ComboBox presetBox;
    juce::TextButton compareAButton { "A" };
    juce::TextButton compareBButton { "B" };
    juce::TextButton undoButton { "Undo" };
    juce::TextButton redoButton { "Redo" };
    juce::TextButton advancedButton { "Advanced" };

    std::vector<std::unique_ptr<ControlBinding>> macroControls;
    std::vector<std::unique_ptr<ControlBinding>> secondaryControls;
    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BubbleCloudAudioProcessorEditor)
};
