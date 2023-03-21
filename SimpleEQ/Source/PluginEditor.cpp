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

		auto text = rswl->getDisplayString();
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

	//g.setColour(Colours::red);
	//g.drawRect(getLocalBounds());
	//g.setColour(Colours::yellow);
	//g.drawRect(sliderBounds);

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

	auto center = sliderBounds.toFloat().getCentre();
	auto radius = sliderBounds.getWidth() * 0.5f;

	g.setColour(Colour(0u, 172u, 1u));
	g.setFont(getTextHeight());

	auto numChoices = labels.size();

	// This loops through labels (initilized in constructor) and applies them to gui
	// at 7 and 5 o'clock locations
	for (int i = 0; i < numChoices; ++i)
	{
		auto pos = labels[i].pos;
		jassert(0.f, <= pos);
		jassert(pos <= 1.f);

		auto angle = jmap(pos, 0.f, 1.f, startAngle, endAngle);

		auto c = center.getPointOnCircumference(radius + getTextHeight() * 0.5f + 1, angle);

		Rectangle<float> r;

		auto str = labels[i].label;
		r.setSize(g.getCurrentFont().getStringWidth(str), getTextHeight());
		r.setCentre(c);
		r.setY(r.getY() + getTextHeight());

		g.drawFittedText(str, r.toNearestInt(), juce::Justification::centred, 1);
	}
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

juce::String RotarySliderWithLabels::getDisplayString()
{
	auto value = getValue();
	if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*>(param))
		return choiceParam->getCurrentChoiceName();
	if (offRange.contains(value))
		return "off";

	juce::String label;

	bool addK = false;

	if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*>(param))
	{
		if (value > 999.f)
		{
			value /= 1000.f;
			addK = true;
		}
		label = juce::String(value, (addK ? 2 : 0));
	}
	else
	{
		jassertfalse; // this should never happen
	}

	label << " ";
	if (addK)
		label << "k";
	label << suffix;

	return label;
}

ResponseCurveComponent::ResponseCurveComponent(SimpleEQAudioProcessor& p) :
	audioProcessor(p),
	leftPathProducer(audioProcessor.leftChannelFifo),
	rightPathProducer(audioProcessor.rightChannelFifo)
{
	const auto& params = audioProcessor.getParameters();
	for (auto param : params)
	{
		param->addListener(this);
	}

	updateChain();
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

void ResponseCurveComponent::updateChain()
{
	auto sampleRate = audioProcessor.getSampleRate();
	auto chainSettings = getChainSettings(audioProcessor.apvts);
	auto peakCoefficients = makePeakFilter(chainSettings, sampleRate);
	auto lowCutFreq = chainSettings.lowCutFreq;
	auto highCutFreq = chainSettings.highCutFreq;

	auto lowCutCoefficients = makeCutFilter(
		lowCutFreq,
		sampleRate,
		chainSettings.lowCutSlope,
		lowCutButterworthMethod);

	auto highCutCoefficients = makeCutFilter(
		highCutFreq,
		sampleRate,
		chainSettings.highCutSlope,
		highCutButterworthMethod);

	updateCoefficients(monoChain.get<ChainPositions::Peak>().coefficients, peakCoefficients);
	applyCoefficientsToCutFilter(
		monoChain.get<ChainPositions::LowCut>(),
		lowCutCoefficients,
		chainSettings.lowCutSlope,
		low_cut_off_range.contains(lowCutFreq));
	applyCoefficientsToCutFilter(
		monoChain.get<ChainPositions::HighCut>(),
		highCutCoefficients,
		chainSettings.highCutSlope,
		high_cut_off_range.contains(highCutFreq));

}

void PathProducer::process(juce::Rectangle<float> fftBounds, double sampleRate)
{
	juce::AudioBuffer<float> tempIncomingBuffer;

	while (fifo->getNumCompleteBuffersAvailable() > 0)
	{
		if (fifo->getAudioBuffer(tempIncomingBuffer))
		{
			auto size = tempIncomingBuffer.getNumSamples();
			juce::FloatVectorOperations::copy(
				monoBuffer.getWritePointer(0, 0),
				monoBuffer.getReadPointer(0, size),
				monoBuffer.getNumSamples() - size);

			juce::FloatVectorOperations::copy(
				monoBuffer.getWritePointer(0, monoBuffer.getNumSamples() - size),
				tempIncomingBuffer.getReadPointer(0, 0),
				size);

			fftDataGenerator.produceFFTDataForRendering(monoBuffer, -48.f);
		}
	}

	const auto fftSize = fftDataGenerator.getFFTSize();

	const auto binWidth = sampleRate / (double)fftSize;
	while (fftDataGenerator.getNumAvailableFFTDataBlocks() > 0)
	{
		std::vector<float> fftData;
		if (fftDataGenerator.getFFTData(fftData))
		{
			pathProducer.generatePath(fftData, fftBounds, fftSize, binWidth, -48.f);
		}
	}

	while (pathProducer.getNumPathsAvailable())
	{
		pathProducer.getPath(fftPath);
	}
}

void ResponseCurveComponent::timerCallback()
{
	auto fftBounds = getAnalysisArea().toFloat();
	auto sampleRate = audioProcessor.getSampleRate();

	leftPathProducer.process(fftBounds, sampleRate);
	rightPathProducer.process(fftBounds, sampleRate);

	if (parametersChanged.compareAndSetBool(false, true))
	{
		updateChain();
		repaint();
	}

	repaint();
}

void ResponseCurveComponent::paint(juce::Graphics& g)
{
	using namespace juce;

	// (Our component is opaque, so we must completely fill the background with a solid colour)
	g.fillAll(Colours::black);
	g.drawImage(background, getLocalBounds().toFloat());

	auto responseArea = getAnalysisArea();
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
		auto freq = mapToLog10(double(i) / double(w), 10.0, 20000.0);

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

	auto leftChannelFFTPath = leftPathProducer.getPath();
	leftChannelFFTPath.applyTransform(AffineTransform().translation(responseArea.getX(), responseArea.getY() - 10));
	g.setColour(atually_right_response_curve_color);
	g.strokePath(leftChannelFFTPath, PathStrokeType(0.8f));

	auto rightChannelFFTPath = rightPathProducer.getPath();
	rightChannelFFTPath.applyTransform(AffineTransform().translation(responseArea.getX(), responseArea.getY() - 10));
	g.setColour(right_response_curve_color);
	g.strokePath(rightChannelFFTPath, PathStrokeType(0.8f));

	g.setColour(analyzer_border_color);
	g.drawRoundedRectangle(getAnalysisArea().toFloat(), 4.f, 1.f);

	g.setColour(knob_border_color);
	g.strokePath(responseCurve, PathStrokeType(3.2f));
}

void ResponseCurveComponent::resized()
{
	using namespace juce;
	background = Image(Image::PixelFormat::RGB, getWidth(), getHeight(), true);

	Graphics g(background);

	Array<float> freqs
	{
		10, 20, 50, 100, 200, 300, 500, 1000, 2000, 5000, 10000, 20000
	};

	auto renderArea = getAnalysisArea();
	auto left = renderArea.getX();
	auto right = renderArea.getRight();
	auto top = renderArea.getY();
	auto bottom = renderArea.getBottom();
	auto width = renderArea.getWidth();

	Array<float> xs;
	for (auto f : freqs)
	{
		auto normX = mapFromLog10(f, 10.f, 20000.f);
		xs.add(left + width * normX);
	}

	g.setColour(Colours::dimgrey);
	for (auto x : xs)
	{
		// fill vertical frequency lines
		g.fillRect(x, float(top), 0.8f, float(bottom) - float(top));
	}

	const int fontHeight = 10;
	g.setFont(fontHeight);

	for (float i = -24; i <= 24; i += 6)
	{
		// fill horizontal gain lines
		auto y = jmap(i, -24.f, 24.f, float(bottom), float(top));
		g.setColour(i == 0.f ? zero_db_color : Colours::darkgrey);
		g.fillRect(float(left), y, float(right) - float(left), 0.8f);


		// fill gain label
		String str;
		if (i > 0)
			str << "+";
		str << i;

		auto textWidth = g.getCurrentFont().getStringWidth(str);

		Rectangle<int> r;
		r.setSize(textWidth, fontHeight);

		r.setX(getWidth() - textWidth);
		r.setCentre(r.getCentreX(), y);

		g.setColour(i == 0 ? zero_db_color : Colours::lightgrey);
		g.drawFittedText(str, r, juce::Justification::centred, 1);

		str.clear();
		str << (i - 24.f);

		r.setX(1);
		textWidth = g.getCurrentFont().getStringWidth(str);
		r.setSize(textWidth, fontHeight);
		g.drawFittedText(str, r, juce::Justification::right, 1);

	}

	g.setColour(Colours::lightgrey);

	for (int i = 0; i < freqs.size(); ++i)
	{
		auto f = freqs[i];
		auto x = xs[i];

		bool addK = false;
		String str;
		if (f > 999.f)
		{
			addK = true;
			f /= 1000.f;
		}

		str << f;
		if (addK)
			str << "k";
		str << "Hz";

		auto textWidth = g.getCurrentFont().getStringWidth(str);

		Rectangle<int> r;
		r.setSize(textWidth, fontHeight);
		r.setCentre(x, 0);
		r.setY(1);

		// write frequencies at top
		g.drawFittedText(str, r, juce::Justification::centred, 1);

		// write frequencies at bottom
		r.setCentre(x, getRenderedArea().getBottom());
		g.drawFittedText(str, r, juce::Justification::centred, 1);
	}
}

juce::Rectangle<int> ResponseCurveComponent::getRenderedArea()
{
	auto bounds = getLocalBounds();
	bounds.removeFromTop(12);

	bounds.removeFromBottom(10);
	bounds.removeFromLeft(20);
	bounds.removeFromRight(20);

	return bounds;
}

juce::Rectangle<int> ResponseCurveComponent::getAnalysisArea()
{
	auto bounds = getRenderedArea();
	bounds.removeFromTop(5);
	bounds.removeFromBottom(10);

	return bounds;
}

//==============================================================================
SimpleEQAudioProcessorEditor::SimpleEQAudioProcessorEditor(SimpleEQAudioProcessor& p)
	: AudioProcessorEditor(&p), audioProcessor(p),
	peakFreqSlider(*audioProcessor.apvts.getParameter("Peak Freq"), "Hz"),
	peakGainSlider(*audioProcessor.apvts.getParameter("Peak Gain"), "db"),
	peakQualitySlider(*audioProcessor.apvts.getParameter("Peak Quality"), ""),
	lowCutFreqSlider(*audioProcessor.apvts.getParameter("LowCut Freq"), "Hz", low_cut_off_range),
	lowCutSlopeSlider(*audioProcessor.apvts.getParameter("LowCut Slope"), "db/Oct"),
	highCutFreqSlider(*audioProcessor.apvts.getParameter("HighCut Freq"), "Hz", high_cut_off_range),
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

	// Uncomment to display min and max values on each end of slider
	// peakFreqSlider.labels.add({ 0.f, "20 Hz" });
	// peakFreqSlider.labels.add({ 1.f, "20 kHz" });

	for (auto* comp : getComps())
	{
		addAndMakeVisible(comp);
	}

	setSize(600, 500);
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

	//float heightRatio = JUCE_LIVE_CONSTANT(33) / 100.f;
	auto responseArea = bounds.removeFromTop(bounds.getHeight() * 0.4);

	responseCurveComponent.setBounds(responseArea);

	bounds.removeFromTop(10);
	auto lowCutArea = bounds.removeFromLeft(bounds.getWidth() * 0.33);
	auto highCutArea = bounds.removeFromRight(bounds.getWidth() * 0.5);

	lowcutBypassButton.setBounds(lowCutArea.removeFromTop(25));
	lowCutFreqSlider.setBounds(lowCutArea.removeFromTop(lowCutArea.getHeight() * 0.5));
	lowCutSlopeSlider.setBounds(lowCutArea);

	highcutBypassButton.setBounds(highCutArea.removeFromTop(25));
	highCutFreqSlider.setBounds(highCutArea.removeFromTop(highCutArea.getHeight() * 0.5));
	highCutSlopeSlider.setBounds(highCutArea);

	peakBypassButton.setBounds(bounds.removeFromTop(25));
	peakFreqSlider.setBounds(bounds.removeFromTop(bounds.getHeight() * 0.33));
	peakGainSlider.setBounds(bounds.removeFromTop(bounds.getHeight() * 0.5));
	peakQualitySlider.setBounds(bounds);

	//analyzerBypassButton.setBounds(bounds);
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
		&responseCurveComponent,
		&highcutBypassButton,
		&lowcutBypassButton,
		&peakBypassButton,
		&analyzerBypassButton
	};
}
