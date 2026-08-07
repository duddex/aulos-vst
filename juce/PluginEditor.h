////////////////////////////////////////////////////////////
//
//   PluginEditor.h
//
//   Custom editor: animated pipe display (ported from PipeDisplay) plus a
//   parameter panel bound to the APVTS.
//
////////////////////////////////////////////////////////////

#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "AulosEngine.h"

// ── PipeComponent: the two-tube visualizer, ported from PipeDisplay ──────────
class PipeComponent : public juce::Component {
public:
    AulosEngine::Display data;   // set by the editor timer each frame
    void paint(juce::Graphics&) override;

private:
    void drawTube(juce::Graphics& g,
                  float activeFraction, float bore, float breath, float overblow,
                  float registerValue, float rms, float air, float chiff,
                  float edge, float saturation,
                  float bx, float by, float bw, float bh, float timeSec);
};

// ── LookAndFeel: colored-arc rotary knobs ────────────────────────────────────
class AulosLookAndFeel : public juce::LookAndFeel_V4 {
public:
    AulosLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h,
                          float sliderPos, float startAngle, float endAngle,
                          juce::Slider&) override;
};

// ── Editor ───────────────────────────────────────────────────────────────────
class AulosAudioProcessorEditor : public juce::AudioProcessorEditor,
                                  private juce::Timer {
public:
    explicit AulosAudioProcessorEditor(AulosAudioProcessor&);
    ~AulosAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    AulosAudioProcessor& proc;
    AulosLookAndFeel lnf;
    PipeComponent pipe;

    juce::ComboBox presetBox;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct SliderCtl {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<SliderAttachment> attach;
        int section = 0;
    };
    struct ToggleCtl {
        juce::ToggleButton button;
        std::unique_ptr<ButtonAttachment> attach;
    };

    juce::OwnedArray<SliderCtl> sliders;
    juce::OwnedArray<ToggleCtl> toggles;
    juce::OwnedArray<juce::Label> sectionLabels;
    juce::Array<juce::Rectangle<int>> sectionRects;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AulosAudioProcessorEditor)
};
