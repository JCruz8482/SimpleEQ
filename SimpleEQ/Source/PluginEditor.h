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
const auto atually_right_response_curve_color = juce::Colours::mediumpurple;
const auto right_response_curve_color = juce::Colours::indianred;
const auto analyzer_border_color = juce::Colours::orange;

enum FFTOrder
{
	order2048 = 11,
	order4096 = 12,
	order8192 = 13
};

template<typename BlockType>
struct FFTDataGenerator
{
	/**
	 produces the FFT data from an audio buffer.
	 */
	void produceFFTDataForRendering(const juce::AudioBuffer<float>& audioData, const float negativeInfinity)
	{
		const auto fftSize = getFFTSize();

		fftData.assign(fftData.size(), 0);
		auto* readIndex = audioData.getReadPointer(0);
		std::copy(readIndex, readIndex + fftSize, fftData.begin());

		// first apply a windowing function to our data
		window->multiplyWithWindowingTable(fftData.data(), fftSize);       // [1]

		// then render our FFT data..
		forwardFFT->performFrequencyOnlyForwardTransform(fftData.data());  // [2]

		int numBins = (int)fftSize / 2;

		//normalize the fft values.
		for (int i = 0; i < numBins; ++i)
		{
			auto v = fftData[i];
			if (!std::isinf(v) && !std::isnan(v))
			{
				v /= float(numBins);
			}
			else
			{
				v = 0.f;
			}
			fftData[i] = v;
		}

		//convert them to decibels
		for (int i = 0; i < numBins; ++i)
		{
			fftData[i] = juce::Decibels::gainToDecibels(fftData[i], negativeInfinity);
		}

		fftDataFifo.push(fftData);
	}

	void changeOrder(FFTOrder newOrder)
	{
		//when you change order, recreate the window, forwardFFT, fifo, fftData
		//also reset the fifoIndex
		//things that need recreating should be created on the heap via std::make_unique<>

		order = newOrder;
		auto fftSize = getFFTSize();

		forwardFFT = std::make_unique<juce::dsp::FFT>(order);
		window = std::make_unique<juce::dsp::WindowingFunction<float>>(fftSize, juce::dsp::WindowingFunction<float>::blackmanHarris);

		fftData.clear();
		fftData.resize(fftSize * 2, 0);

		fftDataFifo.prepare(fftData.size());
	}
	//==============================================================================
	int getFFTSize() const { return 1 << order; }
	int getNumAvailableFFTDataBlocks() const { return fftDataFifo.getNumAvailableForReading(); }
	//==============================================================================
	bool getFFTData(BlockType& fftData) { return fftDataFifo.pull(fftData); }
private:
	FFTOrder order;
	BlockType fftData;
	std::unique_ptr<juce::dsp::FFT> forwardFFT;
	std::unique_ptr<juce::dsp::WindowingFunction<float>> window;

	Fifo<BlockType> fftDataFifo;
};

template<typename PathType>
struct AnalyzerPathGenerator
{
	/*
	 converts 'renderData[]' into a juce::Path
	 */
	void generatePath(const std::vector<float>& renderData,
		juce::Rectangle<float> fftBounds,
		int fftSize,
		float binWidth,
		float negativeInfinity)
	{
		auto top = fftBounds.getY();
		auto bottom = fftBounds.getHeight();
		auto width = fftBounds.getWidth();

		int numBins = (int)fftSize / 2;

		PathType p;
		p.preallocateSpace(3 * (int)fftBounds.getWidth());

		auto map = [bottom, top, negativeInfinity](float v)
		{
			return juce::jmap(v,
				negativeInfinity, 0.f,
				float(bottom + 10), top);
		};

		auto y = map(renderData[0]);

		if (std::isnan(y) || std::isinf(y))
			y = bottom;

		p.startNewSubPath(0, y);

		const int pathResolution = 2; //you can draw line-to's every 'pathResolution' pixels.

		for (int binNum = 1; binNum < numBins; binNum += pathResolution)
		{
			y = map(renderData[binNum]);

			if (!std::isnan(y) && !std::isinf(y))
			{
				auto binFreq = binNum * binWidth;
				auto normalizedBinX = juce::mapFromLog10(binFreq, 10.f, 20000.f);
				int binX = std::floor(normalizedBinX * width);
				p.lineTo(binX, y);
			}
		}

		pathFifo.push(p);
	}

	int getNumPathsAvailable() const
	{
		return pathFifo.getNumAvailableForReading();
	}

	bool getPath(PathType& path)
	{
		return pathFifo.pull(path);
	}
private:
	Fifo<PathType> pathFifo;
};

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

struct PathProducer
{
	PathProducer(SingleChannelSampleFifo<SimpleEQAudioProcessor::BlockType>& scsf) :
		fifo(&scsf)
	{
		fftDataGenerator.changeOrder(FFTOrder::order2048);
		monoBuffer.setSize(1, fftDataGenerator.getFFTSize());
	}

	void process(juce::Rectangle<float> fftBounds, double sampleRate);
	juce::Path getPath() { return fftPath; }
private:
	SingleChannelSampleFifo<SimpleEQAudioProcessor::BlockType>* fifo;
	juce::AudioBuffer<float> monoBuffer;
	FFTDataGenerator<std::vector<float>> fftDataGenerator;
	AnalyzerPathGenerator<juce::Path> pathProducer;
	juce::Path fftPath;
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

	PathProducer leftPathProducer, rightPathProducer;
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

	RotarySliderWithLabels peak1FreqSlider,
		peak1GainSlider,
		peak1QualitySlider,
		peak2FreqSlider,
		peak2GainSlider,
		peak2QualitySlider,
		peak3FreqSlider,
		peak3GainSlider,
		peak3QualitySlider,
		peak4FreqSlider,
		peak4GainSlider,
		peak4QualitySlider,
		peak5FreqSlider,
		peak5GainSlider,
		peak5QualitySlider,
		lowCutFreqSlider,
		highCutFreqSlider,
		lowCutSlopeSlider,
		highCutSlopeSlider;

	ResponseCurveComponent responseCurveComponent;

	using APVTS = juce::AudioProcessorValueTreeState;
	using Attachment = APVTS::SliderAttachment;

	Attachment peak1FreqSliderAttachment,
		peak1GainSliderAttachment,
		peak1QualitySliderAttachment,
		peak2FreqSliderAttachment,
		peak2GainSliderAttachment,
		peak2QualitySliderAttachment,
		peak3FreqSliderAttachment,
		peak3GainSliderAttachment,
		peak3QualitySliderAttachment,
		peak4FreqSliderAttachment,
		peak4GainSliderAttachment,
		peak4QualitySliderAttachment,
		peak5FreqSliderAttachment,
		peak5GainSliderAttachment,
		peak5QualitySliderAttachment,
		lowCutFreqSliderAttachment,
		highCutFreqSliderAttachment,
		lowCutSlopeSliderAttachment,
		highCutSlopeSliderAttachment;

	juce::ToggleButton lowcutBypassButton, highcutBypassButton, peakBypassButton, analyzerBypassButton;

	std::vector<juce::Component*> getComps();

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SimpleEQAudioProcessorEditor)
};
