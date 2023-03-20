/*
  ==============================================================================

	This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

const auto knob_color = juce::Colour(54u, 88u, 114u);
const auto knob_border_color = juce::Colour(53u, 161u, 154u);
const auto zero_db_color = juce::Colour(50u, 172u, 1u);

struct LookAndFeel : juce::LookAndFeel_V4
{
	void drawRotarySlider(
		juce::Graphics&,
		int x, int y, int width, int height,
		float sliderPosProportional,
		float rotaryStartAngle,
		float rotaryEndAngle,
		juce::Slider&) override;
};

struct RotarySliderWithLabels : juce::Slider
{
	RotarySliderWithLabels(
		juce::RangedAudioParameter& rap,
		const juce::String& unitSuffix,
		const juce::Range<float> offRange
	) :
		juce::Slider(
			juce::Slider::SliderStyle::RotaryHorizontalVerticalDrag,
			juce::Slider::TextEntryBoxPosition::NoTextBox),
		param(&rap),
		suffix(unitSuffix),
		offRange(offRange)
	{
		setLookAndFeel(&lnf);
	}

	RotarySliderWithLabels(
		juce::RangedAudioParameter& rap,
		const juce::String& unitSuffix
	) :
		juce::Slider(
			juce::Slider::SliderStyle::RotaryHorizontalVerticalDrag,
			juce::Slider::TextEntryBoxPosition::NoTextBox),
		param(&rap),
		suffix(unitSuffix)
	{
		setLookAndFeel(&lnf);
	}

	~RotarySliderWithLabels()
	{
		setLookAndFeel(nullptr);
	}

	struct LabelPos
	{
		float pos;
		juce::String label;
	};

	juce::Array<LabelPos> labels;

	void paint(juce::Graphics& g) override;

	juce::Rectangle<int> getSliderBounds() const;
	int getTextHeight() const { return 14; }
	juce::String getDisplayString();
private:
	LookAndFeel lnf;
	juce::RangedAudioParameter* param;
	juce::String suffix;
	juce::Range<float> offRange = juce::Range<float>(0, 0.01);
};

struct ResponseCurveComponent : juce::Component,
	juce::AudioProcessorParameter::Listener,
	juce::Timer
{
	ResponseCurveComponent(SimpleEQAudioProcessor&);
	~ResponseCurveComponent() override;

	/** Receives a callback when a parameter has been changed.

		IMPORTANT NOTE: This will be called synchronously when a parameter changes, and
		many audio processors will change their parameter during their audio callback.
		This means that not only has your handler code got to be completely thread-safe,
		but it's also got to be VERY fast, and avoid blocking. If you need to handle
		this event on your message thread, use this callback to trigger an AsyncUpdater
		or ChangeBroadcaster which you can respond to on the message thread.
	*/
	void parameterValueChanged(int parameterIndex, float newValue) override;
	void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override {}


	void timerCallback() override;

	void paint(juce::Graphics& g) override;

	void resized() override;
private:
	SimpleEQAudioProcessor& audioProcessor;
	juce::Atomic<bool> parametersChanged{ false };

	void updateChain();

	MonoChain monoChain;

	juce::Image background;

	juce::Rectangle<int> getRenderedArea();

	juce::Rectangle<int> getAnalysisArea();
};

//==============================================================================
/**
*/
class SimpleEQAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
	SimpleEQAudioProcessorEditor(SimpleEQAudioProcessor&);
	~SimpleEQAudioProcessorEditor() override;

	//==============================================================================
	void paint(juce::Graphics&) override;
	void resized() override;

private:
	// This reference is provided as a quick way for your editor to
	// access the processor object that created it.
	SimpleEQAudioProcessor& audioProcessor;

	RotarySliderWithLabels peakFreqSlider,
		peakGainSlider,
		peakQualitySlider,
		lowCutFreqSlider,
		highCutFreqSlider,
		lowCutSlopeSlider,
		highCutSlopeSlider;

	ResponseCurveComponent responseCurveComponent;

	using APVTS = juce::AudioProcessorValueTreeState;
	using Attachment = APVTS::SliderAttachment;

	Attachment peakFreqSliderAttachment,
		peakGainSliderAttachment,
		peakQualitySliderAttachment,
		lowCutFreqSliderAttachment,
		highCutFreqSliderAttachment,
		lowCutSlopeSliderAttachment,
		highCutSlopeSliderAttachment;

	std::vector<juce::Component*> getComps();

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SimpleEQAudioProcessorEditor)
};
