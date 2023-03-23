/*
  ==============================================================================

	This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"

namespace IDs
{
	static juce::String paramOutput{ "output" };
	static juce::String paramType{ "type" };
	static juce::String paramFreq{ "freq" };
	static juce::String paramGain{ "gain" };
	static juce::String paramQuality{ "quality" };
}

auto createPostUpdateLambda(foleys::MagicProcessorState& magicState, const juce::String& plotID)
{
	return [plot = magicState.getObjectWithType<foleys::MagicFilterPlot>(plotID)](const SimpleEQAudioProcessor::FilterAttachment& a)
	{
		if (plot != nullptr)
		{
			plot->setIIRCoefficients(a.coefficients, maxLevel);
		}
	};
}

//==============================================================================
SimpleEQAudioProcessor::SimpleEQAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
	: foleys::MagicProcessor(BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
		.withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
		.withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
	)
#endif
{
	FOLEYS_SET_SOURCE_PATH(__FILE__);
	magicState.setGuiValueTree(BinaryData::SimpleEQPeaksSeparate_xml, BinaryData::SimpleEQPeaksSeparate_xmlSize);
	analyzer = magicState.createAndAddObject<foleys::MagicAnalyser>("input");

	// GUI MAGIC: add plots to be displayed in the GUI
	for (size_t i = 0; i < attachments.size(); ++i)
	{
		auto name = "plot" + juce::String(i + 1);
		magicState.createAndAddObject<foleys::MagicFilterPlot>(name);
		attachments.at(i)->postFilterUpdate = createPostUpdateLambda(magicState, name);
	}

	plotSum = magicState.createAndAddObject<foleys::MagicFilterPlot>("plotSum");
}

SimpleEQAudioProcessor::~SimpleEQAudioProcessor()
{
}

//==============================================================================
const juce::String SimpleEQAudioProcessor::getName() const
{
	return JucePlugin_Name;
}

bool SimpleEQAudioProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
	return true;
#else
	return false;
#endif
}

bool SimpleEQAudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
	return true;
#else
	return false;
#endif
}

bool SimpleEQAudioProcessor::isMidiEffect() const
{
#if JucePlugin_IsMidiEffect
	return true;
#else
	return false;
#endif
}

double SimpleEQAudioProcessor::getTailLengthSeconds() const
{
	return 0.0;
}

int SimpleEQAudioProcessor::getNumPrograms()
{
	return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
				// so this should be at least 1, even if you're not really implementing programs.
}

int SimpleEQAudioProcessor::getCurrentProgram()
{
	return 0;
}

void SimpleEQAudioProcessor::setCurrentProgram(int index)
{
}

const juce::String SimpleEQAudioProcessor::getProgramName(int index)
{
	return {};
}

void SimpleEQAudioProcessor::changeProgramName(int index, const juce::String& newName)
{
}

//==============================================================================
void SimpleEQAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
	magicState.prepareToPlay(sampleRate, samplesPerBlock);
	// Use this method as the place to do any pre-playback
	// initialisation that you need..
	juce::dsp::ProcessSpec spec;
	spec.maximumBlockSize = samplesPerBlock;
	spec.numChannels = 1;
	spec.sampleRate = sampleRate;

	leftChain.prepare(spec);
	rightChain.prepare(spec);

	auto chainSettings = getChainSettings(apvts);

	updateFilters();

	leftChannelFifo.prepare(samplesPerBlock);
	rightChannelFifo.prepare(samplesPerBlock);
	analyzer->prepareToPlay(sampleRate, samplesPerBlock);
	plotSum->prepareToPlay(sampleRate, samplesPerBlock);
}

void SimpleEQAudioProcessor::releaseResources()
{
	// When playback stops, you can use this as an opportunity to free up any
	// spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool SimpleEQAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
	juce::ignoreUnused(layouts);
	return true;
#else
  // This is the place where you check if the layout is supported.
  // In this template code we only support mono or stereo.
  // Some plugin hosts, such as certain GarageBand versions, will only
  // load plugins that support stereo bus layouts
	if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
		&& layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
		return false;

	// This checks if the input layout matches the output layout
#if ! JucePlugin_IsSynth
	if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
		return false;
#endif

	return true;
#endif
}
#endif

void SimpleEQAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
	juce::ScopedNoDenormals noDenormals;
	auto totalNumInputChannels = getTotalNumInputChannels();
	auto totalNumOutputChannels = getTotalNumOutputChannels();

	// In case we have more outputs than inputs, this code clears any output
	// channels that didn't contain input data, (because these aren't
	// guaranteed to be empty - they may contain garbage).
	// This is here to avoid people getting screaming feedback
	// when they first compile a plugin, but obviously you don't need to keep
	// this code if your algorithm always overwrites all the output channels.
	for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
		buffer.clear(i, 0, buffer.getNumSamples());

	updateFilters();

	juce::dsp::AudioBlock<float> block(buffer);

	auto leftBlock = block.getSingleChannelBlock(0);
	auto rightBlock = block.getSingleChannelBlock(1);

	juce::dsp::ProcessContextReplacing<float> leftContext(leftBlock);
	juce::dsp::ProcessContextReplacing<float> rightContext(rightBlock);

	leftChain.process(leftContext);
	rightChain.process(rightContext);

	leftChannelFifo.update(buffer);
	rightChannelFifo.update(buffer);

	analyzer->pushSamples(buffer);
}

////==============================================================================
//void SimpleEQAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
//{
//	// You should use this method to store your parameters in the memory block.
//	// You could do that either as raw data, or use the XML or ValueTree classes
//	// as intermediaries to make it easy to save and load complex data.
//
//	juce::MemoryOutputStream mos(destData, true);
//	apvts.state.writeToStream(mos);
//}
//
//void SimpleEQAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
//{
//	// You should use this method to restore your parameters from this memory block,
//	// whose contents will have been created by the getStateInformation() call.
//
//	auto tree = juce::ValueTree::readFromData(data, sizeInBytes);
//	if (tree.isValid())
//	{
//		apvts.replaceState(tree);
//		updateFilters();
//	}
//}

void updateCoefficients(Coefficients& old, const Coefficients& replacements)
{
	*old = *replacements;
}

Coefficients makePeakFilter(float freq, float q, float gainInDB, double sampleRate)
{
	return juce::dsp::IIR::Coefficients<float>::makePeakFilter(
		sampleRate,
		freq,
		q,
		juce::Decibels::decibelsToGain(gainInDB));
}

void SimpleEQAudioProcessor::updatePeakFilter(const ChainSettings& chainSettings)
{
	auto peak1Coefficients = makePeakFilter(
		chainSettings.peak1Freq,
		chainSettings.peak1Quality,
		chainSettings.peak1GainInDecibels,
		getSampleRate());
	auto peak2Coefficients = makePeakFilter(
		chainSettings.peak2Freq,
		chainSettings.peak2Quality,
		chainSettings.peak2GainInDecibels,
		getSampleRate());
	auto peak3Coefficients = makePeakFilter(
		chainSettings.peak3Freq,
		chainSettings.peak3Quality,
		chainSettings.peak3GainInDecibels,
		getSampleRate());
	auto peak4Coefficients = makePeakFilter(
		chainSettings.peak4Freq,
		chainSettings.peak4Quality,
		chainSettings.peak4GainInDecibels,
		getSampleRate());
	auto peak5Coefficients = makePeakFilter(
		chainSettings.peak5Freq,
		chainSettings.peak5Quality,
		chainSettings.peak5GainInDecibels,
		getSampleRate());

	attachment1.coefficients = peak1Coefficients;
	attachment2.coefficients = peak2Coefficients;
	attachment3.coefficients = peak3Coefficients;
	attachment4.coefficients = peak4Coefficients;
	attachment5.coefficients = peak5Coefficients;

	updateCoefficients(leftChain.get<ChainPositions::Peak1>().coefficients, peak1Coefficients);
	updateCoefficients(rightChain.get<ChainPositions::Peak1>().coefficients, peak1Coefficients);
	updateCoefficients(leftChain.get<ChainPositions::Peak2>().coefficients, peak2Coefficients);
	updateCoefficients(rightChain.get<ChainPositions::Peak2>().coefficients, peak2Coefficients);
	updateCoefficients(leftChain.get<ChainPositions::Peak3>().coefficients, peak3Coefficients);
	updateCoefficients(rightChain.get<ChainPositions::Peak3>().coefficients, peak3Coefficients);
	updateCoefficients(leftChain.get<ChainPositions::Peak4>().coefficients, peak4Coefficients);
	updateCoefficients(rightChain.get<ChainPositions::Peak4>().coefficients, peak4Coefficients);
	updateCoefficients(leftChain.get<ChainPositions::Peak5>().coefficients, peak5Coefficients);
	updateCoefficients(rightChain.get<ChainPositions::Peak5>().coefficients, peak5Coefficients);
}

void SimpleEQAudioProcessor::handleAsyncUpdate()
{
	std::vector<juce::dsp::IIR::Coefficients<float>::Ptr> coefficients;
	for (auto* a : attachments)
		coefficients.push_back(a->coefficients);

	plotSum->setIIRCoefficients(gain, coefficients, maxLevel);
}

SimpleEQAudioProcessor::FilterAttachment::FilterAttachment(
	Filter& filterToControl,
	const juce::String& prefixToUse,
	const juce::CriticalSection& lock)
	: filter(filterToControl),
	prefix(prefixToUse),
	callbackLock(lock)
{
}

ChainSettings getChainSettings(juce::AudioProcessorValueTreeState& apvts)
{
	ChainSettings settings;

	settings.lowCutFreq = apvts.getRawParameterValue("LowCut Freq")->load();
	settings.highCutFreq = apvts.getRawParameterValue("HighCut Freq")->load();
	settings.peak1Freq = apvts.getRawParameterValue("Peak1 Freq")->load();
	settings.peak1GainInDecibels = apvts.getRawParameterValue("Peak1 Gain")->load();
	settings.peak1Quality = apvts.getRawParameterValue("Peak1 Quality")->load();
	settings.peak2Freq = apvts.getRawParameterValue("Peak2 Freq")->load();
	settings.peak2GainInDecibels = apvts.getRawParameterValue("Peak2 Gain")->load();
	settings.peak2Quality = apvts.getRawParameterValue("Peak2 Quality")->load();
	settings.peak3Freq = apvts.getRawParameterValue("Peak3 Freq")->load();
	settings.peak3GainInDecibels = apvts.getRawParameterValue("Peak3 Gain")->load();
	settings.peak3Quality = apvts.getRawParameterValue("Peak3 Quality")->load();
	settings.peak4Freq = apvts.getRawParameterValue("Peak4 Freq")->load();
	settings.peak4GainInDecibels = apvts.getRawParameterValue("Peak4 Gain")->load();
	settings.peak4Quality = apvts.getRawParameterValue("Peak4 Quality")->load();
	settings.peak5Freq = apvts.getRawParameterValue("Peak5 Freq")->load();
	settings.peak5GainInDecibels = apvts.getRawParameterValue("Peak5 Gain")->load();
	settings.peak5Quality = apvts.getRawParameterValue("Peak5 Quality")->load();
	settings.lowCutSlope = static_cast<Slope>(apvts.getRawParameterValue("LowCut Slope")->load());
	settings.highCutSlope = static_cast<Slope>(apvts.getRawParameterValue("HighCut Slope")->load());
	//settings.lowCutBypassed = apvts.getRawParameterValue("LowCut Bypassed")->load();
	//settings.highCutBypassed = apvts.getRawParameterValue("HighCut Bypassed")->load();
	//settings.peak1Bypassed = apvts.getRawParameterValue("Peak Bypassed")->load();

	return settings;
}

void SimpleEQAudioProcessor::updateFilters()
{
	auto chainSettings = getChainSettings(apvts);

	auto lowCutFreq = chainSettings.lowCutFreq;
	bool isOff = low_cut_off_range.contains(lowCutFreq);

	/*leftChain.setBypassed<ChainPositions::LowCut>(chainSettings.lowCutBypassed);
	rightChain.setBypassed<ChainPositions::LowCut>(chainSettings.lowCutBypassed);
	leftChain.setBypassed<ChainPositions::HighCut>(chainSettings.highCutBypassed);
	rightChain.setBypassed<ChainPositions::HighCut>(chainSettings.highCutBypassed);
	leftChain.setBypassed<ChainPositions::Peak1>(chainSettings.peak1Bypassed);
	rightChain.setBypassed<ChainPositions::Peak1>(chainSettings.peak1Bypassed);*/

	updateCutFilter<ChainPositions::LowCut>(
		chainSettings.lowCutFreq,
		chainSettings.lowCutSlope,
		lowCutButterworthMethod,
		isOff);

	updatePeakFilter(chainSettings);

	auto highCutFreq = chainSettings.highCutFreq;
	isOff = high_cut_off_range.contains(highCutFreq);

	updateCutFilter<ChainPositions::HighCut>(
		highCutFreq,
		chainSettings.highCutSlope,
		highCutButterworthMethod,
		isOff);
}

template<int Index> void SimpleEQAudioProcessor::updateCutFilter(
	const float cutFreq,
	const Slope slope,
	CoefficientRefArray(*filterDesignMethod)(float, double, int),
	const bool isOff)
{
	auto cutCoefficients = makeCutFilter(cutFreq, getSampleRate(), slope, filterDesignMethod);

	auto& leftCut = leftChain.get<Index>();
	auto& rightCut = rightChain.get<Index>();

	applyCoefficientsToCutFilter(leftCut, cutCoefficients, slope, isOff);
	applyCoefficientsToCutFilter(rightCut, cutCoefficients, slope, isOff);
}

juce::AudioProcessorValueTreeState::ParameterLayout SimpleEQAudioProcessor::createParameterLayout()
{
	const auto freq_range = juce::NormalisableRange<float>(5.f, 22000.f, 1.f, 0.5f);
	const auto gain_range = juce::NormalisableRange<float>(-24.f, 24.f, 0.5f, 1.f);
	const auto quality_range = juce::NormalisableRange<float>(0.1f, 10.f, 0.05f, 1.f);
	juce::StringArray filterSlopeValues;
	for (int i = 0; i < NUM_FILTER_SLOPES; ++i)
	{
		juce::String str;
		str << (12 + i * 12);
		str << " db/Oct";
		filterSlopeValues.add(str);
	}

	juce::AudioProcessorValueTreeState::ParameterLayout layout;

	layout.add(std::make_unique<juce::AudioParameterFloat>(
		"LowCut Freq",
		"LowCut Freq",
		freq_range,
		5.f));

	layout.add(std::make_unique<juce::AudioParameterFloat>(
		"HighCut Freq",
		"HighCut Freq",
		freq_range,
		22000.f));

	layout.add(std::make_unique<juce::AudioParameterFloat>(
		"Peak1 Freq",
		"Peak1 Freq",
		freq_range,
		120.f));

	layout.add(std::make_unique<juce::AudioParameterFloat>(
		"Peak1 Gain",
		"Peak1 Gain",
		gain_range,
		0.f));

	layout.add(std::make_unique<juce::AudioParameterFloat>(
		"Peak1 Quality",
		"Peak1 Quality",
		quality_range,
		1.f));

	layout.add(std::make_unique<juce::AudioParameterFloat>(
		"Peak2 Freq",
		"Peak2 Freq",
		freq_range,
		250.f));

	layout.add(std::make_unique<juce::AudioParameterFloat>(
		"Peak2 Gain",
		"Peak2 Gain",
		gain_range,
		0.f));

	layout.add(std::make_unique<juce::AudioParameterFloat>(
		"Peak2 Quality",
		"Peak2 Quality",
		quality_range,
		1.f));

	layout.add(std::make_unique<juce::AudioParameterFloat>(
		"Peak3 Freq",
		"Peak3 Freq",
		freq_range,
		500.f));

	layout.add(std::make_unique<juce::AudioParameterFloat>(
		"Peak3 Gain",
		"Peak3 Gain",
		gain_range,
		0.f));

	layout.add(std::make_unique<juce::AudioParameterFloat>(
		"Peak3 Quality",
		"Peak3 Quality",
		quality_range,
		1.f));

	layout.add(std::make_unique<juce::AudioParameterFloat>(
		"Peak4 Freq",
		"Peak4 Freq",
		freq_range,
		1000.f));

	layout.add(std::make_unique<juce::AudioParameterFloat>(
		"Peak4 Gain",
		"Peak4 Gain",
		gain_range,
		0.f));

	layout.add(std::make_unique<juce::AudioParameterFloat>(
		"Peak4 Quality",
		"Peak4 Quality",
		quality_range,
		1.f));

	layout.add(std::make_unique<juce::AudioParameterFloat>(
		"Peak5 Freq",
		"Peak5 Freq",
		freq_range,
		3200.f));

	layout.add(std::make_unique<juce::AudioParameterFloat>(
		"Peak5 Gain",
		"Peak5 Gain",
		gain_range,
		0.f));

	layout.add(std::make_unique<juce::AudioParameterFloat>(
		"Peak5 Quality",
		"Peak5 Quality",
		quality_range,
		1.f));

	layout.add(std::make_unique<juce::AudioParameterChoice>
		("LowCut Slope", "LowCut Slope", filterSlopeValues, 0));
	layout.add(std::make_unique<juce::AudioParameterChoice>(
		"HighCut Slope", "HighCut Slope", filterSlopeValues, 0));

	/*layout.add(std::make_unique<juce::AudioParameterBool>("LowCut Bypassed", "LowCut Bypassed", false));
	layout.add(std::make_unique<juce::AudioParameterBool>("Peak Bypassed", "Peak Bypassed", false));
	layout.add(std::make_unique<juce::AudioParameterBool>("HighCut Bypassed", "High Cut Bypassed", false));
	layout.add(std::make_unique<juce::AudioParameterBool>("Analyzer Bypassed", "Analyzer Bypassed", true));*/

	return layout;
}
//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
	return new SimpleEQAudioProcessor();
}
