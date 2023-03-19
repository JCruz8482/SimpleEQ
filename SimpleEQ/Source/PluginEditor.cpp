/*
  ==============================================================================

	This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

void LookAndFeel::drawRotarySlider(
	juce::Graphics& g,
	int x,
	int y,
	int width,
	int height,
	float sliderPosProportional,
	float rotaryStartAngle,
	float rotaryEndAngle,
	juce::Slider& slider)
{
	using namespace juce;

	auto bounds = Rectangle<float>(x, y, width, height);

	g.setColour(knob_color);
	g.fillEllipse(bounds);

	g.setColour(knob_border_color);
	g.drawEllipse(bounds, 1.f);

	if (auto* rswl = dynamic_cast<RotarySliderWithLabels*>(&slider))
	{
		auto center = bounds.getCentre();

		Path p;

		Rectangle<float> r;
		r.setLeft(center.getX() - 2);
		r.setRight(center.getX() + 2);
		r.setTop(bounds.getY());
		r.setBottom(center.getY() - rswl->getTextHeight() * 1.5);
		p.addRoundedRectangle(r, 2.f);

		jassert(rotaryStartAngle < rotaryEndAngle);

		auto sliderAngleRadians = jmap(sliderPosProportional, 0.f, 1.f, rotaryStartAngle, rotaryEndAngle);

		p.applyTransform(AffineTransform().rotated(sliderAngleRadians, center.getX(), center.getY()));

		g.fillPath(p);

		g.setFont(rswl->getTextHeight());

		auto text = rswl->getDisplayLabel();
		auto strWidth = g.getCurrentFont().getStringWidth(text);

		r.setSize(strWidth + 4, rswl->getTextHeight() + 2);
		r.setCentre(center);

		g.setColour(Colours::black);
		g.fillRect(r);

		g.setColour(Colours::white);
		g.drawFittedText(text, r.toNearestInt(), juce::Justification::centred, 1);
	}
}

void RotarySliderWithLabels::paint(juce::Graphics& g)
{
	using namespace juce;

	auto startAngle = degreesToRadians(180.f + 45.f);
	auto endAngle = degreesToRadians(180.f - 45.f) + MathConstants<float>::twoPi;

	auto range = getRange();
	auto sliderBounds = getSliderBounds();

	g.setColour(Colours::red);
	g.drawRect(getLocalBounds());
	g.setColour(Colours::yellow);
	g.drawRect(sliderBounds);


	getLookAndFeel().drawRotarySlider(
		g,
		sliderBounds.getX(),
		sliderBounds.getY(),
		sliderBounds.getWidth(),
		sliderBounds.getHeight(),
		jmap(getValue(), range.getStart(), range.getEnd(), 0.0, 1.0),
		startAngle,
		endAngle,
		*this);
}

juce::Rectangle<int> RotarySliderWithLabels::getSliderBounds() const
{
	auto bounds = getLocalBounds();

	auto size = juce::jmin(bounds.getWidth(), bounds.getHeight());

	size -= getTextHeight() * 2;
	juce::Rectangle<int> r;
	r.setSize(size, size);
	r.setCentre(bounds.getCentreX(), 0);
	r.setY(2);

	return r;
}

juce::String RotarySliderWithLabels::getDisplayLabel()
{
	auto value = getValue();
	juce::String label;

	return offRange.contains(value) ? "off" : juce::String(value);
}

ResponseCurveComponent::ResponseCurveComponent(SimpleEQAudioProcessor& p) : audioProcessor(p)
{
	const auto& params = audioProcessor.getParameters();
	for (auto param : params)
	{
		param->addListener(this);
	}

	startTimerHz(60);
}

ResponseCurveComponent::~ResponseCurveComponent()
{
	const auto& params = audioProcessor.getParameters();
	for (auto param : params)
	{
		param->removeListener(this);
	}
}

void ResponseCurveComponent::parameterValueChanged(int parameterIndex, float newValue)
{
	parametersChanged.set(true);
}

void ResponseCurveComponent::timerCallback()
{
	if (parametersChanged.compareAndSetBool(false, true))
	{
		auto sampleRate = audioProcessor.getSampleRate();
		auto chainSettings = getChainSettings(audioProcessor.apvts);
		auto peakCoefficients = makePeakFilter(chainSettings, sampleRate);

		auto lowCutCoefficients = makeCutFilter(
			chainSettings.lowCutFreq,
			sampleRate,
			chainSettings.lowCutSlope,
			lowCutButterworthMethod);

		auto highCutCoefficients = makeCutFilter(
			chainSettings.highCutFreq,
			sampleRate,
			chainSettings.highCutSlope,
			highCutButterworthMethod);

		updateCoefficients(monoChain.get<ChainPositions::Peak>().coefficients, peakCoefficients);
		applyCoefficientsToCutFilter(
			monoChain.get<ChainPositions::LowCut>(),
			lowCutCoefficients,
			chainSettings.lowCutSlope);
		applyCoefficientsToCutFilter(
			monoChain.get<ChainPositions::HighCut>(),
			highCutCoefficients,
			chainSettings.highCutSlope);

		repaint();
	}
}

void ResponseCurveComponent::paint(juce::Graphics& g)
{
	using namespace juce;

	// (Our component is opaque, so we must completely fill the background with a solid colour)
	g.fillAll(Colours::black);

	auto responseArea = getLocalBounds();
	auto w = responseArea.getWidth();

	auto& lowCut = monoChain.get<ChainPositions::LowCut>();
	auto& peak = monoChain.get<ChainPositions::Peak>();
	auto& highCut = monoChain.get<ChainPositions::HighCut>();

	auto sampleRate = audioProcessor.getSampleRate();

	std::vector<double> mags;

	mags.resize(w);

	for (int i = 0; i < w; ++i)
	{
		double mag = 1.f;
		auto freq = mapToLog10(double(i) / double(w), 20.0, 20000.0);

		if (!monoChain.isBypassed<ChainPositions::Peak>())
			mag *= peak.coefficients->getMagnitudeForFrequency(freq, sampleRate);

		if (!lowCut.isBypassed<0>())
			mag *= lowCut.get<0>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
		if (!lowCut.isBypassed<1>())
			mag *= lowCut.get<1>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
		if (!lowCut.isBypassed<2>())
			mag *= lowCut.get<2>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
		if (!lowCut.isBypassed<3>())
			mag *= lowCut.get<3>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
		if (!lowCut.isBypassed<4>())
			mag *= lowCut.get<4>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
		if (!lowCut.isBypassed<5>())
			mag *= lowCut.get<5>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
		if (!lowCut.isBypassed<6>())
			mag *= lowCut.get<6>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
		if (!lowCut.isBypassed<7>())
			mag *= lowCut.get<7>().coefficients->getMagnitudeForFrequency(freq, sampleRate);

		if (!highCut.isBypassed<0>())
			mag *= highCut.get<0>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
		if (!highCut.isBypassed<1>())
			mag *= highCut.get<1>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
		if (!highCut.isBypassed<2>())
			mag *= highCut.get<2>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
		if (!highCut.isBypassed<3>())
			mag *= highCut.get<3>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
		if (!highCut.isBypassed<4>())
			mag *= highCut.get<4>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
		if (!highCut.isBypassed<5>())
			mag *= highCut.get<5>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
		if (!highCut.isBypassed<6>())
			mag *= highCut.get<6>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
		if (!highCut.isBypassed<7>())
			mag *= highCut.get<7>().coefficients->getMagnitudeForFrequency(freq, sampleRate);

		mags[i] = Decibels::gainToDecibels(mag);
	}

	Path responseCurve;

	const double outputMin = responseArea.getBottom();
	double outputMax = responseArea.getY();
	auto map = [outputMin, outputMax](double input)
	{
		return jmap(input, -24.0, 24.0, outputMin, outputMax);
	};

	responseCurve.startNewSubPath(responseArea.getX(), map(mags.front()));

	for (size_t i = 1; i < mags.size(); ++i)
		responseCurve.lineTo(responseArea.getX() + i, map(mags[i]));

	g.setColour(Colours::orange);
	g.drawRoundedRectangle(responseArea.toFloat(), 4.f, 1.f);

	g.setColour(Colours::white);
	g.strokePath(responseCurve, PathStrokeType(2.f));
}

//==============================================================================
SimpleEQAudioProcessorEditor::SimpleEQAudioProcessorEditor(SimpleEQAudioProcessor& p)
	: AudioProcessorEditor(&p), audioProcessor(p),
	peakFreqSlider(*audioProcessor.apvts.getParameter("Peak Freq"), "Hz"),
	peakGainSlider(*audioProcessor.apvts.getParameter("Peak Gain"), "db"),
	peakQualitySlider(*audioProcessor.apvts.getParameter("Peak Quality"), ""),
	lowCutFreqSlider(*audioProcessor.apvts.getParameter("LowCut Freq"), "Hz", juce::Range<double>(0, 6)),
	lowCutSlopeSlider(*audioProcessor.apvts.getParameter("LowCut Slope"), "db/Oct"),
	highCutFreqSlider(*audioProcessor.apvts.getParameter("HighCut Freq"), "Hz", juce::Range<double>(21500, 22001)),
	highCutSlopeSlider(*audioProcessor.apvts.getParameter("HighCut Slope"), "db/Oct"),

	responseCurveComponent(audioProcessor),
	peakFreqSliderAttachment(audioProcessor.apvts, "Peak Freq", peakFreqSlider),
	peakGainSliderAttachment(audioProcessor.apvts, "Peak Gain", peakGainSlider),
	peakQualitySliderAttachment(audioProcessor.apvts, "Peak Quality", peakQualitySlider),
	lowCutFreqSliderAttachment(audioProcessor.apvts, "LowCut Freq", lowCutFreqSlider),
	highCutFreqSliderAttachment(audioProcessor.apvts, "HighCut Freq", highCutFreqSlider),
	lowCutSlopeSliderAttachment(audioProcessor.apvts, "LowCut Slope", lowCutSlopeSlider),
	highCutSlopeSliderAttachment(audioProcessor.apvts, "HighCut Slope", highCutSlopeSlider)
{
	// Make sure that before the constructor has finished, you've set the
	// editor's size to whatever you need it to be.

	for (auto* comp : getComps())
	{
		addAndMakeVisible(comp);
	}

	setSize(600, 600);
}

SimpleEQAudioProcessorEditor::~SimpleEQAudioProcessorEditor()
{
}

//==============================================================================
void SimpleEQAudioProcessorEditor::paint(juce::Graphics& g)
{
	using namespace juce;

	// (Our component is opaque, so we must completely fill the background with a solid colour)
	g.fillAll(Colours::black);
}

void SimpleEQAudioProcessorEditor::resized()
{
	// This is generally where you'll want to lay out the positions of any
	// subcomponents in your editor..

	auto bounds = getLocalBounds();
	auto responseArea = bounds.removeFromTop(bounds.getHeight() * 0.33);

	responseCurveComponent.setBounds(responseArea);

	auto lowCutArea = bounds.removeFromLeft(bounds.getWidth() * 0.33);
	auto highCutArea = bounds.removeFromRight(bounds.getWidth() * 0.5);

	lowCutFreqSlider.setBounds(lowCutArea.removeFromTop(lowCutArea.getHeight() * 0.5));
	lowCutSlopeSlider.setBounds(lowCutArea);

	highCutFreqSlider.setBounds(highCutArea.removeFromTop(highCutArea.getHeight() * 0.5));
	highCutSlopeSlider.setBounds(highCutArea);

	peakFreqSlider.setBounds(bounds.removeFromTop(bounds.getHeight() * 0.33));
	peakGainSlider.setBounds(bounds.removeFromTop(bounds.getHeight() * 0.5));
	peakQualitySlider.setBounds(bounds);
}

std::vector<juce::Component*> SimpleEQAudioProcessorEditor::getComps()
{
	return
	{
		&peakFreqSlider,
		&peakGainSlider,
		&peakQualitySlider,
		&lowCutFreqSlider,
		&highCutFreqSlider,
		&lowCutSlopeSlider,
		&highCutSlopeSlider,
		&responseCurveComponent
	};
}
