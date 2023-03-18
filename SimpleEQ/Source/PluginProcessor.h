/*
  ==============================================================================

	This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

// NUM_FILTER_SLOPES == num entries in Slope == num filters in CutFilter
const int NUM_FILTER_SLOPES = 8;
enum Slope
{
	Slope_12,
	Slope_24,
	Slope_36,
	Slope_48,
	Slope_60,
	Slope_72,
	Slope_84,
	Slope_96
};

struct ChainSettings
{
	float peakFreq{ 0 }, peakGainInDecibels{ 0 }, peakQuality{ 1.f };
	float lowCutFreq{ 0 }, highCutFreq{ 0 };
	Slope lowCutSlope{ Slope_12 }, highCutSlope{ Slope_12 };
};

ChainSettings getChainSettings(juce::AudioProcessorValueTreeState& apvts);

enum ChainPositions
{
	LowCut,
	Peak,
	HighCut
};

using Filter = juce::dsp::IIR::Filter<float>;

using CutFilter = juce::dsp::ProcessorChain<Filter, Filter, Filter, Filter, Filter, Filter, Filter, Filter>;

using MonoChain = juce::dsp::ProcessorChain<CutFilter, Filter, CutFilter>;

using Coefficients = Filter::CoefficientsPtr;


Coefficients makePeakFilter(const ChainSettings& chainSettings, double sampleRate);

//==============================================================================
/**
*/
class SimpleEQAudioProcessor : public juce::AudioProcessor
#if JucePlugin_Enable_ARA
	, public juce::AudioProcessorARAExtension
#endif
{
public:
	//==============================================================================
	SimpleEQAudioProcessor();
	~SimpleEQAudioProcessor() override;

	//==============================================================================
	void prepareToPlay(double sampleRate, int samplesPerBlock) override;
	void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
	bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif

	void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

	//==============================================================================
	juce::AudioProcessorEditor* createEditor() override;
	bool hasEditor() const override;

	//==============================================================================
	const juce::String getName() const override;

	bool acceptsMidi() const override;
	bool producesMidi() const override;
	bool isMidiEffect() const override;
	double getTailLengthSeconds() const override;

	//==============================================================================
	int getNumPrograms() override;
	int getCurrentProgram() override;
	void setCurrentProgram(int index) override;
	const juce::String getProgramName(int index) override;
	void changeProgramName(int index, const juce::String& newName) override;

	//==============================================================================
	void getStateInformation(juce::MemoryBlock& destData) override;
	void setStateInformation(const void* data, int sizeInBytes) override;

	static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
	juce::AudioProcessorValueTreeState apvts{ *this, nullptr, "Parameters", createParameterLayout() };

private:

	MonoChain leftChain, rightChain;

	void updatePeakFilter(const ChainSettings& chainSettings);

	template<int Index, typename ChainType, typename CoefficientType>
	void update(ChainType& chain, const CoefficientType& coefficients)
	{
		updateCoefficients(chain.template get<Index>().coefficients, coefficients[Index]);
		chain.template setBypassed<Index>(false);
	}

	template<typename ChainType, typename CoefficientType>
	void updateCutFilter(
		ChainType& cutFilter,
		const CoefficientType& cutCoefficients,
		const Slope slope)
	{
		cutFilter.setBypassed<0>(true);
		cutFilter.setBypassed<1>(true);
		cutFilter.setBypassed<2>(true);
		cutFilter.setBypassed<3>(true);
		cutFilter.setBypassed<4>(true);
		cutFilter.setBypassed<5>(true);
		cutFilter.setBypassed<6>(true);
		cutFilter.setBypassed<7>(true);

		switch (slope)
		{
			case Slope_96:
			{
				update<7>(cutFilter, cutCoefficients);
			}
			case Slope_84:
			{
				update<6>(cutFilter, cutCoefficients);
			}
			case Slope_72:
			{
				update<5>(cutFilter, cutCoefficients);
			}
			case Slope_60:
			{
				update<4>(cutFilter, cutCoefficients);
			}
			case Slope_48:
			{
				update<3>(cutFilter, cutCoefficients);
			}
			case Slope_36:
			{
				update<2>(cutFilter, cutCoefficients);
			}
			case Slope_24:
			{
				update<1>(cutFilter, cutCoefficients);
			}
			case Slope_12:
			{
				update<0>(cutFilter, cutCoefficients);
			}
		}
	}

	template<int Index>
	void updateCutFilter(
		const float cutFreq,
		const Slope slope,
		juce::ReferenceCountedArray<juce::dsp::IIR::Coefficients<float>>(*func)(float, double, int));
	void updateFilters();

	void updateCoefficients(Coefficients& old, const Coefficients& replacements);

	//==============================================================================
	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SimpleEQAudioProcessor)
};
