/*
  ==============================================================================

	This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <array>

const auto low_cut_off_range = juce::Range<float>(0, 6);
const auto high_cut_off_range = juce::Range<float>(21500, 22001);

template<typename T>
struct Fifo
{
	void prepare(int numChannels, int numSamples)
	{
		static_assert(std::is_same_v<T, juce::AudioBuffer<float>>,
			"prepare(numChannels, numSamples) should only be used on Fifo<AudioBuffer<float>> ");
		for (auto& buffer : buffers)
		{
			buffer.setSize(
				numChannels,
				false,
				true,
				true
			);
			buffer.clear();
		}
	}

	void prepare(size_t numElements)
	{
		static_assert(std::is_same_v<T, std::vector<float>>,
			"prepare(numElements) should only be used on Fifo<std::vector<float>>");
		for (auto& buffer : buffers)
		{
			buffer.clear();
			buffer.resize(numElements, 0);
		}
	}

	bool push(const T& t)
	{
		auto write = fifo.write(1);
		if (write.blockSize1 > 0)
		{
			buffers[write.startIndex1] = t;
			return true;
		}
		return false;
	}

	bool pull(T& t)
	{
		auto read = fifo.read(1);
		if (read.blockSize1 > 0)
		{
			t = buffers[read.startIndex1];
			return true;
		}
		return false;
	}

	int getNumAvailableForReading() const
	{
		return fifo.getNumReady();
	}
private:
	static constexpr int Capacity = 30;
	std::array<T, Capacity> buffers;
	juce::AbstractFifo fifo{ Capacity };
};

enum Channel
{
	Right,
	Left
};

template<typename BlockType>
struct SingleChannelSampleFifo
{
	SingleChannelSampleFifo(Channel ch) : channelToUse(ch)
	{
		prepared.set(false);
	}

	void update(const BlockType& buffer)
	{
		jassert(prepared.get());
		jassert(buffer.getNumChannels() > channelToUse);
		auto* channelPtr = buffer.getReadPointer(channelToUse);

		for (int i = 0; i < buffer.getNumSamples(); ++i)
		{
			pushNextSampleIntoFifo(channelPtr[i]);
		}
	}

	void prepare(int bufferSize)
	{
		prepared.set(false);
		size.set(bufferSize);

		bufferToFill.setSize(1, bufferSize, false, true, true);

		audioBufferFifo.prepare(1, bufferSize);
		fifoIndex = 0;
		prepared.set(true);
	}

	int getNumCompleteBuffersAvailable() const { return audioBufferFifo.getNumAvailableForReading(); }
	bool isPrepared() const { return prepared.get() }
	int getSize() const { return size.get() }

	bool getAudioBuffer(BlockType& buffer) { return audioBufferFifo.pull(buffer); }
private:
	Channel channelToUse;
	int fifoIndex = 0;
	Fifo<BlockType> audioBufferFifo;
	BlockType bufferToFill;
	juce::Atomic<bool> prepared = false;
	juce::Atomic<int> size = 0;

	void pushNextSampleIntoFifo(float sample)
	{
		if (fifoIndex == bufferToFill.getNumSamples())
		{
			auto ok = audioBufferFifo.push(bufferToFill);

			juce::ignoreUnused(ok);
			fifoIndex = 0;
		}

		bufferToFill.setSample(0, fifoIndex, sample);
		++fifoIndex;
	}
};

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
	bool lowCutBypassed{ false }, peakBypassed{ false }, highCutBypassed{ false };
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

using CoefficientRefArray = juce::ReferenceCountedArray<juce::dsp::IIR::Coefficients<float>>;

inline CoefficientRefArray(*lowCutButterworthMethod)(float, double, int) =
&juce::dsp::FilterDesign<float>::designIIRHighpassHighOrderButterworthMethod;

inline CoefficientRefArray(*highCutButterworthMethod)(float, double, int) =
&juce::dsp::FilterDesign<float>::designIIRLowpassHighOrderButterworthMethod;

using Coefficients = Filter::CoefficientsPtr;
void updateCoefficients(Coefficients& old, const Coefficients& replacements);

Coefficients makePeakFilter(const ChainSettings& chainSettings, double sampleRate);

template<int Index, typename ChainType, typename CoefficientType>
void update(ChainType& chain, const CoefficientType& coefficients)
{
	updateCoefficients(chain.template get<Index>().coefficients, coefficients[Index]);
	chain.template setBypassed<Index>(false);
}

template<typename ChainType, typename CoefficientType>
void applyCoefficientsToCutFilter(
	ChainType& cutFilter,
	const CoefficientType& cutCoefficients,
	const Slope slope,
	const bool isOff)
{
	cutFilter.template setBypassed<0>(true);
	cutFilter.template setBypassed<1>(true);
	cutFilter.template setBypassed<2>(true);
	cutFilter.template setBypassed<3>(true);
	cutFilter.template setBypassed<4>(true);
	cutFilter.template setBypassed<5>(true);
	cutFilter.template setBypassed<6>(true);
	cutFilter.template setBypassed<7>(true);

	if (isOff)
		return;

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

inline auto makeCutFilter(
	const float cutFreq,
	double sampleRate,
	Slope slope,
	CoefficientRefArray(*filterDesignMethod)(float, double, int))
{
	return filterDesignMethod(cutFreq, sampleRate, (2 * (slope + 1)));
}

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

	using BlockType = juce::AudioBuffer<float>;
	SingleChannelSampleFifo<BlockType> leftChannelFifo{ Channel::Left };
	SingleChannelSampleFifo<BlockType> rightChannelFifo{ Channel::Right };

private:

	MonoChain leftChain, rightChain;

	void updatePeakFilter(const ChainSettings& chainSettings);
	void updateFilters();
	template<int Index>
	void updateCutFilter(
		const float cutFreq,
		const Slope slope,
		CoefficientRefArray(*filterDesignMethod)(float, double, int),
		const bool isOff);
	//==============================================================================
	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SimpleEQAudioProcessor)
};
