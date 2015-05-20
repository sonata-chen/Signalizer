
#include "CVectorScope.h"
#include <cstdint>
#include <cpl/CMutex.h>
#include <cpl/Mathext.h>
#include "SignalizerDesign.h"
#include <cpl/rendering/OpenGLRasterizers.h>
#include <cpl/simd.h>
#include <cpl/dsp/filterdesign.h>

namespace Signalizer
{
	// swapping the right channel might give an more intuitive view
	
	static const char * ChannelDescriptions[] = { "+L", "+R", "-L", "-R", "L", "R", "C"};
	static std::vector<std::string> OperationalModeNames = {"Lissajous", "Polar"};
	
	static const float quarterPISinCos = 0.707106781186547f;
	static const float circleScaleFactor = 1.1f;
	
	enum class OperationalModes
	{
		Lissajous,
		Polar
		
	};
	
	enum class EnvelopeModes : int
	{
		None,
		RMS,
		PeakDecay
	};
	
	enum Textures
	{
		LPlus,
		RPlus,
		LMinus,
		RMinus,
		Left,
		Right,
		Center
	};


	void CVectorScope::onGraphicsRendering(juce::Graphics & g)
	{

		auto cStart = cpl::Misc::ClockCounter();

		if (state.normalizeGain && state.isEditorOpen)
		{
			setGainAsFraction(envelopeGain);
		}

		// do software rendering
		if(!isOpenGL())
		{
			g.fillAll(state.colourBackground.withAlpha(1.0f));
			g.setColour(state.colourBackground.withAlpha(1.0f).contrasting());
			g.drawText("Enable OpenGL in settings to use the vectorscope", getLocalBounds(), juce::Justification::centred);
			
			// post fps anyway
			auto tickNow = juce::Time::getHighResolutionTicks();
			avgFps.setNext(tickNow - lastFrameTick);
			lastFrameTick = tickNow;
			
		}
		
		if (kdiagnostics.bGetValue() > 0.5)
		{
			auto fps = 1.0 / (avgFps.getAverage() / juce::Time::getHighResolutionTicksPerSecond());

			auto totalCycles = renderCycles + cpl::Misc::ClockCounter() - cStart;
			double cpuTime = (double(totalCycles) / (processorSpeed * 1000 * 1000) * 100) * fps;
			g.setColour(juce::Colours::blue);
			sprintf(textbuf.get(), "%dx%d: %.1f fps - %.1f%% cpu, deltaG = %f, deltaO = %f",
				getWidth(), getHeight(), fps, cpuTime, graphicsDeltaTime(), openGLDeltaTime());
			g.drawSingleLineText(textbuf.get(), 10, 20);
			
		}
	}

	void CVectorScope::initOpenGL()
	{
		const int imageSize = 128;
		const float fontToPixelScale = 90 / 64.0f;
		
		textures.clear();

		for (int i = 0; i < ArraySize(ChannelDescriptions); ++i)
		{

			juce::Image letter(juce::Image::ARGB, imageSize, imageSize, true);
			{
				juce::Graphics g(letter);
				g.fillAll(juce::Colours::transparentBlack);
				g.setColour(Colours::white);
				g.setFont(letter.getHeight() * fontToPixelScale * 0.5);
				g.drawText(ChannelDescriptions[i], letter.getBounds().toFloat(), juce::Justification::centred, false);
			}
			textures.push_back(std::unique_ptr<juce::OpenGLTexture>((new juce::OpenGLTexture())));
			textures[i]->loadImage(letter);
		}
	}
	void CVectorScope::closeOpenGL()
	{
		textures.clear();
	}


	void CVectorScope::onOpenGLRendering()
	{
		if (audioStream.empty())
			return;

		auto cStart = cpl::Misc::ClockCounter();
		juce::OpenGLHelpers::clear(state.colourBackground);

		cpl::AudioBuffer * buffer;

		if (state.isFrozen)
		{
			buffer = &audioStreamCopy;
		}
		else if (isSynced)
		{
			std::vector<cpl::CMutex> locks;

			for (auto & buffer : audioStream)
				locks.emplace_back(buffer);

			for (unsigned i = 0; i < audioStream.size(); ++i)
			{
				audioStream[i].clone(audioStreamCopy[i]);

			}
			buffer = &audioStreamCopy;
		}
		else
		{
			buffer = &audioStream;

		}
		const std::size_t numSamples = (*buffer)[0].size - 1;

		cpl::OpenGLEngine::COpenGLStack openGLStack;
		// set up openGL
		openGLStack.setBlender(GL_ONE, GL_ONE_MINUS_SRC_COLOR);
		openGLStack.loadIdentityMatrix();



		openGLStack.applyTransform3D(ktransform.getTransform3D());
		state.antialias ? openGLStack.enable(GL_MULTISAMPLE) : openGLStack.disable(GL_MULTISAMPLE);

		cpl::CAudioBuffer::CChannelBuffer * leftChannel, * rightChannel;
		leftChannel = &((*buffer)[0]);
		rightChannel = &((*buffer)[1]);

		// the peak filter has to run on the whole buffer each time.
		if (state.envelopeMode == EnvelopeModes::PeakDecay)
		{
			runPeakFilter<cpl::simd::v8sf>(*buffer, numSamples);
		}
		   
		openGLStack.setLineSize(state.primitiveSize * 10);
		openGLStack.setPointSize(state.primitiveSize * 10);

		// draw actual stereoscopic plot
		if (state.isPolar)
		{
			drawPolarPlot<cpl::simd::v4sf>(openGLStack, *buffer);
		}
		else // is Lissajous
		{
			drawRectPlot<cpl::simd::v4sf>(openGLStack, *buffer);
		}

		openGLStack.setLineSize(2.0f);
		
		// draw graph and wireframe
		drawWireFrame<cpl::simd::v4sf>(openGLStack);

		// draw channel text(ures)
		drawGraphText<cpl::simd::v4sf>(openGLStack, *buffer);

		// draw 2d stuff (like stereo meters)

		drawStereoMeters<cpl::simd::v4sf>(openGLStack, *buffer);


		renderCycles = cpl::Misc::ClockCounter() - cStart;
		auto tickNow = juce::Time::getHighResolutionTicks();
		avgFps.setNext(tickNow - lastFrameTick);
		lastFrameTick = tickNow;
	}

	
	template<typename V>
		void CVectorScope::drawGraphText(cpl::OpenGLEngine::COpenGLStack & openGLStack, const cpl::AudioBuffer &)
		{
			using consts = cpl::simd::consts<float>;
			// draw channel rotations letters.
			if(!state.isPolar)
			{
				auto rotation = -state.rotation * 2 * consts::pi;
				const float heightToWidthFactor = float(getHeight()) / getWidth();
				using namespace cpl::simd;
				
				cpl::OpenGLEngine::MatrixModification m;
				// this undoes the text squashing due to variable aspect ratios.
				m.scale(heightToWidthFactor, 1.0f, 1.0f);
				
				// calculate coordinates using sin/cos simd pairs.
				suitable_container<v4sf> phases, xcoords, ycoords;
				
				// set phases (we rotate L/R etc. text around in a circle)
				phases[0] = rotation;
				phases[1] = consts::pi * 0.5f + rotation;
				phases[2] = consts::pi + rotation;
				phases[3] = consts::pi * 1.5f + rotation;
				
				// some registers
				v4sf
				vsines,
				vcosines,
				vscale = set1<v4sf>(circleScaleFactor),
				vadd = set1<v4sf>(1.0f - circleScaleFactor),
				vheightToWidthFactor = set1<v4sf>(1.0f / heightToWidthFactor);
				
				// do 8 trig functions in one go!
				cpl::simd::sincos(phases.toType(), &vsines, &vcosines);
				
				// place the circle just outside the graph, and offset it.
				xcoords = vsines * vscale * vheightToWidthFactor + vadd;
				ycoords = vcosines * vscale + vadd;
				
				auto jcolour = state.colourGraph;
				// render texture text at coordinates.
				for (int i = 0; i < 4; ++i)
				{
					cpl::OpenGLEngine::ImageDrawer text(openGLStack, *textures[i]);
					text.setColour(jcolour);
					text.drawAt({ xcoords[i], ycoords[i], 0.1f, 0.1f });
				}
			}
			else
			{
				const float heightToWidthFactor = float(getHeight()) / getWidth();
				using namespace cpl::simd;
				
				cpl::OpenGLEngine::MatrixModification m;
				// this undoes the text squashing due to variable aspect ratios.
				m.scale(heightToWidthFactor, 1.0f, 1.0f);
				
				auto jcolour = state.colourGraph;
				float nadd = 1.0f - circleScaleFactor;
				float xcoord = consts::sqrt_half_two * circleScaleFactor / heightToWidthFactor + nadd;
				float ycoord = consts::sqrt_half_two * circleScaleFactor + nadd;
				
				// render texture text at coordinates.
				{
					cpl::OpenGLEngine::ImageDrawer text(openGLStack, *textures[Textures::Left]);
					text.setColour(jcolour);
					text.drawAt({ -xcoord + nadd, ycoord, 0.1f, 0.1f });
				}
				{
					cpl::OpenGLEngine::ImageDrawer text(openGLStack, *textures[Textures::Center]);
					text.setColour(jcolour);
					text.drawAt({ 0 + nadd * 0.5f, 1, 0.1f, 0.1f });
				}
				{
					cpl::OpenGLEngine::ImageDrawer text(openGLStack, *textures[Textures::Right]);
					text.setColour(jcolour);
					text.drawAt({ xcoord, ycoord, 0.1f, 0.1f });
				}
				
			}
			
		}
	
	
	template<typename V>
		void CVectorScope::drawWireFrame(cpl::OpenGLEngine::COpenGLStack & openGLStack)
		{
			using consts = cpl::simd::consts<float>;
			// draw skeleton graph
			if (!state.isPolar)
			{
				cpl::OpenGLEngine::PrimitiveDrawer<128> drawer(openGLStack, GL_LINES);
				drawer.addColour(state.colourWire);
				int nlines = 14;
				auto rel = 1.0f / nlines;
				
				// front vertival
				for (int i = 0; i <= nlines; ++i)
				{
					drawer.addVertex(i * rel * 2 - 1, -1.f, 0.0f);
					drawer.addVertex(i * rel * 2 - 1, 1.f, 0.0f);
				}
				// front horizontal
				for (int i = 0; i <= nlines; ++i)
				{
					drawer.addVertex(-1.0f, i * rel * 2 - 1, 0.0f);
					drawer.addVertex(1.0f, i * rel * 2 - 1, 0.0f);
				}
				// back vertical
				for (int i = 0; i <= nlines; ++i)
				{
					drawer.addVertex(i * rel * 2 - 1, -1.f, -1.0f);
					drawer.addVertex(i * rel * 2 - 1, 1.f, -1.0f);
				}
				// back horizontal
				for (int i = 0; i <= nlines; ++i)
				{
					drawer.addVertex(-1.0f, i * rel * 2 - 1, -1.0f);
					drawer.addVertex(1.0f, i * rel * 2 - 1, -1.0f);
				}
			}
			else
			{
				// draw two half circles
				auto lut = circleData.get();
				
				int numInt = lut->tableSize;
				float advance = 1.0f / (numInt - 1);
				{
					cpl::OpenGLEngine::PrimitiveDrawer<512> drawer(openGLStack, GL_LINES);
					drawer.addColour(state.colourWire);
					
					float oldY = 0.0f;
					for (int i = 1; i < numInt; ++i)
					{
						auto fraction = advance * i;
						auto yCoordinate = lut->linearLookup(fraction);
						auto leftX = -1.0f + fraction;
						auto rightX = 1.0f - fraction;
						// left part
						drawer.addVertex(leftX - advance, oldY, 0);
						drawer.addVertex(leftX, yCoordinate, 0);
						drawer.addVertex(leftX - advance, oldY, -1);
						drawer.addVertex(leftX, yCoordinate, -1);
						
						// right part
						drawer.addVertex(rightX + advance, oldY, 0);
						drawer.addVertex(rightX, yCoordinate, 0);
						drawer.addVertex(rightX + advance, oldY, -1);
						drawer.addVertex(rightX, yCoordinate, -1);
						//drawer.addVertex(1 - fraction, yCoordinate, 0);
						
						oldY = yCoordinate;
					}
					
					// add front and back horizontal lines.
					drawer.addVertex(-1.0f, 0.0f, 0.0f);
					drawer.addVertex(1.0f, 0.0f, 0.0f);
					drawer.addVertex(-1.0f, 0.0f, -1.0f);
					drawer.addVertex(1.0f, 0.0f, -1.0f);
					
					// add critical diagonal phase lines.
					drawer.addVertex(0.0f, 0.0f, 0.0f);
					drawer.addVertex(consts::sqrt_half_two, consts::sqrt_half_two, 0.0f);
					drawer.addVertex(0.0f, 0.0f, 0.0f);
					drawer.addVertex(-consts::sqrt_half_two, consts::sqrt_half_two, 0.0f);
					
					drawer.addVertex(0.0f, 0.0f, -1.0f);
					drawer.addVertex(consts::sqrt_half_two, consts::sqrt_half_two, -1.0f);
					drawer.addVertex(0.0f, 0.0f, -1.0f);
					drawer.addVertex(-consts::sqrt_half_two, consts::sqrt_half_two, -1.0f);;
				}
			}
			
			
			// Draw basic graph
			
			{
				cpl::OpenGLEngine::PrimitiveDrawer<12> drawer(openGLStack, GL_LINES);
				drawer.addColour(state.colourGraph);
				// front x, y axii
				drawer.addVertex(-1.0f, 0.0f, 0.0f);
				drawer.addVertex(1.0f, 0.0f, 0.0f);
				drawer.addVertex(0.0f, 1.0f, 0.0f);
				drawer.addVertex(0.0f, state.isPolar ? 0.0f : -1.0f, 0.0f);
			}
		}
	
	
	template<typename V>
		void CVectorScope::drawRectPlot(cpl::OpenGLEngine::COpenGLStack & openGLStack, const cpl::AudioBuffer & buf)
		{
			cpl::OpenGLEngine::MatrixModification matrixMod;
			// apply the custom rotation to the waveform
			matrixMod.rotate(state.rotation * 360, 0, 0, 1);
			// and apply the gain:
			GLfloat gain = getGain();
			matrixMod.scale(gain, gain, 1);
			std::size_t numSamples = buf[0].size - 1;
			float sampleFade = 1.0 / numSamples, sleft, sright;
			
			if (!state.fadeHistory)
			{
				cpl::OpenGLEngine::PrimitiveDrawer<1024> drawer(openGLStack, state.fillPath ? GL_LINE_STRIP : GL_POINTS);
				
				drawer.addColour(state.colourDraw);
				for (std::size_t i = 0; i < numSamples; ++i)
				{
					// use glDrawArrays instead here
					sleft = buf[0].singleConstAccess(i);
					sright = buf[1].singleConstAccess(i);
					
					drawer.addVertex(sright, sleft, i * sampleFade - 1);
				}
			}
			else
			{
				cpl::OpenGLEngine::PrimitiveDrawer<1024> drawer(openGLStack, state.fillPath ? GL_LINE_STRIP : GL_POINTS);
				
				auto jcolour = state.colourDraw;
				float red = jcolour.getFloatRed(), blue = jcolour.getFloatBlue(),
				green = jcolour.getFloatGreen(), alpha = jcolour.getFloatGreen();
				
				float fade = 0;
				
				for (std::size_t i = 0; i < numSamples; ++i)
				{
					sleft = buf[0].singleConstAccess(i);
					sright = buf[1].singleConstAccess(i);
					
					fade = i * sampleFade;
					
					drawer.addColour(fade * red, fade * green, fade * blue, alpha);
					drawer.addVertex(sright, sleft, i * sampleFade - 1);
					
				}
			}
			
			
		}
	

	template<typename V>
		void CVectorScope::drawPolarPlot(cpl::OpenGLEngine::COpenGLStack & openGLStack, const cpl::AudioBuffer & buf)
		{
			using namespace cpl::simd;
			typedef typename scalar_of<V>::type Ty;

			cpl::OpenGLEngine::MatrixModification matrixMod;
			auto const gain = (GLfloat)getGain();
			auto const numSamples = buf[0].size;
			static const long long vectorLength = elements_of<V>::value;
			matrixMod.scale(gain, gain, 1);
			suitable_container<V> outX, outY, outFade;

			// simd consts
			const Ty cosineRotation = consts<Ty>::sqrt_half_two_minus;
			const Ty sineRotation = consts<Ty>::sqrt_half_two;
			const V vCosine = consts<V>::sqrt_half_two_minus; // equals e^i*0.75*pi
			const V vSine = consts<V>::sqrt_half_two;
			const V vZero = zero<V>();
			const V vOne = consts<V>::one;
			const V vSign = consts<V>::sign_bit;
			auto const fadePerSample = (Ty)1.0 / numSamples;
			auto const vIncrementalFade = set1<V>(fadePerSample * vectorLength);

			const float
				red = state.colourDraw.getFloatRed(),
				green = state.colourDraw.getFloatGreen(),
				blue = state.colourDraw.getFloatBlue();

			for (int i = 0; i < vectorLength; ++i)
			{
				outFade[i] = fadePerSample * i;
			}

			V vSampleFade = outFade;


			auto const lit = buf[0].getIterator<vectorLength>();
			auto const rit = buf[1].getIterator<vectorLength>();
			if(!state.fadeHistory)
			{
				cpl::OpenGLEngine::PrimitiveDrawer<1024> drawer(openGLStack, state.fillPath ? GL_LINE_STRIP : GL_POINTS);
				drawer.addColour(red, green, blue);
				// iterate front of buffer, then back.
				for (int section = 0; section < 2; ++section)
				{

					// using long longs to safely jump out of loops with elements_of<V> > sectionSamples
					long long i = 0;

					const Ty * left = lit.getIndex(section);
					const Ty * right = rit.getIndex(section);
					const long long sectionSamples = lit.sizes[section];

					for (; i < (sectionSamples - vectorLength); i += vectorLength)
					{
						V vLeft = loadu<V>(left + i);
						V vRight = loadu<V>(right + i);

						// the length of the hypotenuse of the triangle, we 
						// convert the unit square to.
						auto const vLength = max(abs(vLeft), abs(vRight));

						// rotate our view manually (to center on Y-axis)
						V vY = vLeft * vCosine - vRight * vSine;
						V vX = vLeft * vSine + vRight * vCosine;

						// check for any zero elements.
						vLeft = (vLeft == vZero);
						vRight = (vRight == vZero);
						auto vMask = vnot(vand(vLeft, vRight));

						// get the phase angle. use atan2 if you want to draw the full circle.
						// x and y are swapped at this point, btw.
						auto vAngle = atan(vX / vY);
						// replace nan elements of angle with zero
						vAngle = vand(vMask, vAngle);
						// calcuate x,y coordinates for the right triangle
						sincos(vAngle, &vX, &vY);

						// construct triangle.
						outX = vX * vLength;
						outY = vY * vLength;

						outFade = vSampleFade - vOne;

						// draw vertices.
						for (cpl::Types::fint_t n = 0; n < vectorLength; ++n)
						{
							drawer.addVertex(outX[n], outY[n], outFade[n]);
						}

						vSampleFade += vIncrementalFade;

					}
					//continue;
					// deal with remainder, scalar route
					long long remaindingSamples = 0;
					auto currentSampleFade = outFade[vectorLength - 1];

					for (; i < sectionSamples; i++, remaindingSamples++)
					{
						Ty vLeft = left[i];
						Ty vRight = right[i];

						// the length of the hypotenuse of the triangle, we 
						// convert the unit square to.
						auto const length = std::max(std::abs(vLeft), std::abs(vRight));

						// rotate our view manually (to center on Y-axis)
						Ty vY = vLeft * cosineRotation - vRight * sineRotation;
						Ty vX = vLeft * sineRotation + vRight * cosineRotation;

						// check for any zero elements.

						// get the phase angle. use atan2 if you want to draw the full circle.
						// x and y are swapped at this point, btw.
						auto angle = std::atan(vX / vY);
						// replace nan elements of angle with zero
						angle = (vLeft == Ty(0) && vRight == Ty(0)) ? Ty(0) : angle;
						// calcuate x,y coordinates for the right triangle
						sincos(angle, &vX, &vY);

						drawer.addVertex(vX * length, vY * length, (currentSampleFade - remaindingSamples * fadePerSample));



					}
					// fractionally increase sample fade levels
					vSampleFade += set1<V>(fadePerSample * remaindingSamples);
				}
			}
			else // apply fading
			{

				const V 
					vRed = set1<V>(red), 
					vGreen = set1<V>(green),
					vBlue = set1<V>(blue);

				suitable_container<V> outRed, outGreen, outBlue;
				// iterate front of buffer, then back.

				cpl::OpenGLEngine::PrimitiveDrawer<1024> drawer(openGLStack, state.fillPath ? GL_LINE_STRIP : GL_POINTS);

				for (int section = 0; section < 2; ++section)
				{

					// using long longs to safely jump out of loops with elements_of<V> > sectionSamples
					long long i = 0;

					const Ty * left = lit.getIndex(section);
					const Ty * right = rit.getIndex(section);
					const long long sectionSamples = lit.sizes[section];

					for (; i < (sectionSamples - vectorLength); i += vectorLength)
					{
						V vLeft = loadu<V>(left + i);
						V vRight = loadu<V>(right + i);

						// the length of the hypotenuse of the triangle, we 
						// convert the unit square to.
						auto const vLength = max(abs(vLeft), abs(vRight));

						// rotate our view manually (to center on Y-axis)
						V vY = vLeft * vCosine - vRight * vSine;
						V vX = vLeft * vSine + vRight * vCosine;

						// check for any zero elements.
						vLeft = (vLeft == vZero);
						vRight = (vRight == vZero);
						auto vMask = vnot(vand(vLeft, vRight));

						// get the phase angle. use atan2 if you want to draw the full circle.
						// x and y are swapped at this point, btw.
						auto vAngle = atan(vX / vY);
						// replace nan elements of angle with zero
						vAngle = vand(vMask, vAngle);
						// calcuate x,y coordinates for the right triangle
						sincos(vAngle, &vX, &vY);

						// construct triangle.
						outX = vX * vLength;
						outY = vY * vLength;

						outFade = vSampleFade - vOne;

						// set colours

						outRed = vRed * vSampleFade;
						outBlue = vBlue * vSampleFade;
						outGreen = vGreen * vSampleFade;

						// draw vertices.
						for (cpl::Types::fint_t n = 0; n < vectorLength; ++n)
						{
							drawer.addColour(outRed[n], outGreen[n], outBlue[n]);
							drawer.addVertex(outX[n], outY[n], outFade[n]);
						}

						vSampleFade += vIncrementalFade;

					}
					//continue;
					// deal with remainder, scalar route
					long long remaindingSamples = 0;
					auto currentSampleFade = outFade[vectorLength - 1];

					for (; i < sectionSamples; i++, remaindingSamples++)
					{
						Ty vLeft = left[i];
						Ty vRight = right[i];

						// the length of the hypotenuse of the triangle, we 
						// convert the unit square to.
						auto const length = std::max(std::abs(vLeft), std::abs(vRight));

						// rotate our view manually (to center on Y-axis)
						Ty vY = vLeft * cosineRotation - vRight * sineRotation;
						Ty vX = vLeft * sineRotation + vRight * cosineRotation;

						// check for any zero elements.

						// get the phase angle. use atan2 if you want to draw the full circle.
						// x and y are swapped at this point, btw.
						auto angle = std::atan(vX / vY);
						// replace nan elements of angle with zero
						angle = (vLeft == Ty(0) && vRight == Ty(0)) ? Ty(0) : angle;
						// calcuate x,y coordinates for the right triangle
						sincos(angle, &vX, &vY);

						auto currentFade = (currentSampleFade - remaindingSamples * fadePerSample);
						drawer.addColour(red * (currentFade + 1), green * (currentFade + 1), blue * (currentFade + 1));
						drawer.addVertex(vX * length, vY * length, currentFade);

					}
					// fractionally increase sample fade levels
					vSampleFade += set1<V>(fadePerSample * remaindingSamples);

				}
			}
		}

	template<typename V>
		void CVectorScope::drawStereoMeters(cpl::OpenGLEngine::COpenGLStack & openGLStack, const cpl::AudioBuffer & buf)
		{
			using namespace cpl;
			OpenGLEngine::MatrixModification m;
			m.loadIdentityMatrix();
			const float heightToWidthFactor = float(getHeight()) / getWidth();

			const float balanceX = -0.925f;
			const float balanceLength = 1.8f;
			const float stereoY = -0.8f;
			const float stereoLength = 1.7f;
			const float sideSize = 0.05f;
			const float indicatorSize = 0.05f;

			// remember, y / x
			float balanceQuick = std::atan(filters.balance[0][1] / filters.balance[0][0]) / (simd::consts<float>::pi * 0.5f);
			if (!std::isnormal(balanceQuick))
				balanceQuick = 0.5f;
			float balanceSlow = std::atan(filters.balance[1][1] / filters.balance[1][0]) / (simd::consts<float>::pi * 0.5f);
			if (!std::isnormal(balanceSlow))
				balanceSlow = 0.5f;

			const float stereoQuick = filters.phase[0] * 0.5f + 0.5f;
			const float stereoSlow = filters.phase[1] * 0.5f + 0.5f;
			
			// this undoes the squashing due to variable aspect ratios.
			//m.scale(1.0f, 1.0f / heightToWidthFactor, 1.0f);
			OpenGLEngine::RectangleDrawer2D<> rect(openGLStack);

			// draw slow balance
			rect.setColour(state.colourMeter.withMultipliedBrightness(0.75f));
			rect.setBounds(balanceX + (balanceLength - indicatorSize) * balanceSlow, balanceX, indicatorSize, sideSize);
			rect.fill();

			// draw quick balance
			rect.setColour(state.colourMeter);
			rect.setBounds(balanceX + (balanceLength - indicatorSize * 0.25f) * balanceQuick, balanceX, indicatorSize * 0.25f, sideSize);
			rect.fill();


			// draw slow stereo
			rect.setColour(state.colourMeter.withMultipliedBrightness(0.75f));
			rect.setBounds(-balanceX, stereoY + (stereoLength - indicatorSize) * stereoSlow, sideSize, indicatorSize);
			rect.fill();

			// draw quick stereo
			rect.setColour(state.colourMeter);
			rect.setBounds(-balanceX, stereoY + (stereoLength - indicatorSize * 0.25f) * stereoQuick, sideSize, indicatorSize * 0.25f);
			rect.fill();


			// draw bounding rectangle of balance meter
			rect.setBounds(balanceX, balanceX, balanceLength, sideSize);
			rect.setColour(state.colourMeter.withMultipliedBrightness(0.5f));
			rect.renderOutline();

			// draw bounding rectangle of correlation meter
			rect.setBounds(-balanceX, stereoY, sideSize, stereoLength);
			rect.setColour(state.colourMeter.withMultipliedBrightness(0.5f));
			rect.renderOutline();

			auto pieceColour = state.colourBackground.contrasting().withMultipliedBrightness(state.colourMeter.getPerceivedBrightness() * 0.5f);
			rect.setColour(pieceColour);
			// draw contrasting center piece for balance
			rect.setBounds(balanceX + balanceLength * 0.5f - indicatorSize * 0.125f, balanceX, indicatorSize * 0.125f, sideSize);
			rect.fill();

			// draw contrasting center piece for stereo
			rect.setBounds(-balanceX, stereoY + stereoLength * 0.5f - indicatorSize * 0.125f, sideSize, indicatorSize * 0.125f);
			rect.fill();
		}


	template<typename V>
		void CVectorScope::runPeakFilter(cpl::AudioBuffer & buf, std::size_t numSamples)
		{
			if (state.normalizeGain)
			{
				double currentEnvelope = 0;
				// since this runs in every frame, we need to scale the coefficient by how often this function runs
				// (and the amount of samples)
				double power = numSamples * (avgFps.getAverage() / juce::Time::getHighResolutionTicksPerSecond());

				double coeff = std::pow(state.envelopeCoeff, power);

				// there is a number of optimisations we can do here, mostly that we actually don't care about
				// timing, we are only interested in the current largest value in the set.
				using namespace cpl;
				using namespace cpl::simd;
				using cpl::simd::load;

				V
					vLMax = zero<V>(),
					vRMax = zero<V>(),
					vSign = consts<V>::sign_mask;

				auto const loopIncrement = elements_of<V>::value;

				auto * leftBuffer = buf[0].buffer;
				auto * rightBuffer = buf[1].buffer;

				auto stop = numSamples - (numSamples & (loopIncrement - 1));
				if (stop <= 0)
					stop = 0;

				for (std::size_t i = 0; i < stop; i += loopIncrement)
				{
					auto const vLInput = loadu<V>(leftBuffer + i);
					vLMax = max(vand(vLInput, vSign), vLMax);
					auto const vRInput = loadu<V>(rightBuffer + i);
					vRMax = max(vand(vRInput, vSign), vRMax);
				}

				suitable_container<V> lmax = vLMax, rmax = vRMax;

				double highestLeft = *std::max_element(lmax.begin(), lmax.end());
				double highestRight = *std::max_element(rmax.begin(), rmax.end());

				filters.envelope[0] = std::max(filters.envelope[0] * coeff, highestLeft  * highestLeft);
				filters.envelope[1] = std::max(filters.envelope[1] * coeff, highestRight * highestRight);

				currentEnvelope = 1.0 / std::max(std::sqrt(filters.envelope[0]), std::sqrt(filters.envelope[1]));

				if (std::isnormal(currentEnvelope))
				{
					envelopeGain = cpl::Math::confineTo(currentEnvelope, lowerAutoGainBounds, higherAutoGainBounds);
				}
			}
		}
};
