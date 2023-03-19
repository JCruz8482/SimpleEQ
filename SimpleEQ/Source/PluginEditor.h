/*
  ==============================================================================

	This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

struct CustomRotarySlider : juce::Slider
{
	CustomRotarySlider() : juce::Slider(
		juce::Slider::SliderStyle::RotaryHorizontalVerticalDrag,
		juce::Slider::TextEntryBoxPosition::NoTextBox)
	{

	}
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
private:
	SimpleEQAudioProcessor& audioProcessor;
	juce::Atomic<bool> parametersChanged{ false };

	MonoChain monoChain;
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

	CustomRotarySlider peakFreqSlider,
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
