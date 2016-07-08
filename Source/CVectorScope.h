/*************************************************************************************
 
	Signalizer - cross-platform audio visualization plugin - v. 0.x.y
 
	Copyright (C) 2016 Janus Lynggaard Thorborg (www.jthorborg.com)
 
	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.
 
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.
 
	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
 
	See \licenses\ for additional details on licenses associated with this program.
 
**************************************************************************************
 
	file:CVectorScope.h

		Interface for the vectorscope view
 
*************************************************************************************/

#ifndef SIGNALIZER_CVECTORSCOPE_H
	#define SIGNALIZER_CVECTORSCOPE_H

	#include "CommonSignalizer.h"
	#include <cpl/Utility.h>
	#include <cpl/gui/controls/Controls.h>
	#include <cpl/gui/Widgets/Widgets.h>
	#include <memory>
	#include <cpl/simd.h>
	#include "VectorScopeParameters.h"

	namespace cpl
	{
		namespace OpenGLRendering
		{
			class COpenGLStack;
			
		};
	};

	namespace Signalizer
	{
		
		template<typename T, std::size_t size>
			class LookupTable
			{
			public:
				typedef T Ty;
				static const std::size_t tableSize = size;
				
				inline Ty linearLookup(Ty dx) const noexcept
				{
					Ty scaled = dx * tableSize;
					std::size_t x1 = std::size_t(scaled);
					std::size_t x2 = x1 + 1;
					Ty fraction = scaled - x1;
					
					return table[x1] * (Ty(1) - fraction) + table[x2] * fraction;
				}
				
				Ty table[tableSize + 1];
			};
		
		template<typename T, std::size_t size>
			class QuarterCircleLut : public LookupTable<T, size>
			{
			public:
				QuarterCircleLut()
				{
					double increase = 1.0 / (size - 1);
					for (int i = 0; i < size; ++i)
					{
						
						// describe first left upper part of circle
						// maybe use the parabola like any sane person
						this->table[i] = (T)std::sin(std::acos(1.0 - increase * i));
					}
					this->table[this->tableSize] = (T)1;
				}
			};

		enum class EnvelopeModes : int;
		
		class CVectorScope 
		: 
			public cpl::COpenGLView, 
			protected AudioStream::Listener
		{

		public:

			static const double higherAutoGainBounds;
			static const double lowerAutoGainBounds;

			CVectorScope(AudioStream & data, ParameterSet * params);
			virtual ~CVectorScope();

			// Component overrides
			void onGraphicsRendering(Graphics & g) override;
			void mouseWheelMove(const MouseEvent& event, const MouseWheelDetails& wheel) override;
			void mouseDoubleClick(const MouseEvent& event) override;
			void mouseDrag(const MouseEvent& event) override;
			void mouseUp(const MouseEvent& event) override;
			void mouseDown(const MouseEvent& event) override;
			// OpenGLRender overrides
			void onOpenGLRendering() override;
			void initOpenGL() override;
			void closeOpenGL() override;
			// View overrides
			juce::Component * getWindow() override;
			void suspend() override;
			void resume() override;
			void freeze() override;
			void unfreeze() override;

			std::unique_ptr<juce::Component> createEditor() override { return content->createEditor(); }

			// cbasecontrol overrides
			bool isEditorOpen() const;
			double getGain();

		protected:

			bool onAsyncAudio(const AudioStream & source, AudioStream::DataType ** buffer, std::size_t numChannels, std::size_t numSamples) override;
			void onAsyncChangedProperties(const AudioStream & source, const AudioStream::AudioStreamInfo & before) override;
			virtual void paint2DGraphics(juce::Graphics & g);
			/// <summary>
			/// Handles all set flags in mtFlags.
			/// Make sure the audioStream is locked while doing this.
			/// </summary>
			virtual void handleFlagUpdates();

		private:

			void deserialize(cpl::CSerializer::Builder & builder, cpl::Version version) override {};
			void serialize(cpl::CSerializer::Archiver & archive, cpl::Version version) override {};

			template<typename V>
				void vectorGLRendering();

			// vector-accelerated drawing, rendering and processing
			template<typename V>
				void drawPolarPlot(cpl::OpenGLRendering::COpenGLStack &, const AudioStream::AudioBufferAccess &);

			template<typename V>
				void drawRectPlot(cpl::OpenGLRendering::COpenGLStack &, const AudioStream::AudioBufferAccess &);

			template<typename V>
				void drawWireFrame(cpl::OpenGLRendering::COpenGLStack &);

			template<typename V>
				void drawGraphText(cpl::OpenGLRendering::COpenGLStack &, const AudioStream::AudioBufferAccess &);

			template<typename V>
				void drawStereoMeters(cpl::OpenGLRendering::COpenGLStack &, const AudioStream::AudioBufferAccess &);

			template<typename V>
				void runPeakFilter(const AudioStream::AudioBufferAccess &);

			template<typename V>
				void audioProcessing(typename cpl::simd::scalar_of<V>::type ** buffer, std::size_t numChannels, std::size_t numSamples);

			void initPanelAndControls();

			// guis and whatnot
			cpl::CBoxFilter<double, 60> avgFps;

			juce::MouseCursor displayCursor;

			// vars
			long long lastFrameTick, renderCycles;

			struct FilterStates
			{
				AudioStream::DataType envelope[2];
				AudioStream::DataType balance[2][2];
				AudioStream::DataType phase[2];
				
			} filters;

			struct Flags
			{
				cpl::ABoolFlag
					firstRun,
					/// <summary>
					/// Set this if the audio buffer window size was changed from somewhere else.
					/// </summary>
					audioWindowWasResized;
			} mtFlags;

			// contains non-atomic structures
			struct StateOptions
			{
				bool isPolar, normalizeGain, isFrozen, fillPath, fadeHistory, antialias, diagnostics;
				float primitiveSize, rotation;
				float stereoCoeff;
				float envelopeCoeff;
				/// <summary>
				/// A constant factor slower than the stereoCoeff
				/// </summary>
				float secondStereoFilterSpeed;
				juce::Colour colourBackground, colourWire, colourGraph, colourDraw, colourMeter;
				cpl::ValueT envelopeGain;
				EnvelopeModes envelopeMode;
			} state;
			
			VectorScopeContent * content;
			AudioStream & audioStream;
			//cpl::AudioBuffer audioStreamCopy;
			cpl::Utility::LazyPointer<QuarterCircleLut<GLfloat, 128>> circleData;
			juce::Component * editor;
			double oldWindowSize;
			// unused.
			std::unique_ptr<char> textbuf;
			unsigned long long processorSpeed; // clocks / sec
			juce::Point<float> lastMousePos;
			std::vector<std::unique_ptr<juce::OpenGLTexture>> textures;

		};
	
	};

#endif