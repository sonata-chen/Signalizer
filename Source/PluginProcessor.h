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
 
	file:PluginProcessor.h

		Defines the processing interface of the plugin.
 
*************************************************************************************/
#ifndef PLUGINPROCESSOR_H_INCLUDED
#define PLUGINPROCESSOR_H_INCLUDED

#include <cpl/Common.h>
#include <cpl/CAudioStream.h>
#include <cpl/CSerializer.h>
#include <cpl/gui/CViews.h>
#include "CommonSignalizer.h"
#include <cpl/gui/widgets/CPresetWidget.h>

namespace Signalizer
{
	class MainEditor;

	class AudioProcessor
		: public juce::AudioProcessor
		, cpl::CView::EventListener
		, ParameterSet::AutomatedProcessor
	{
		friend class MainEditor;

	public:

		struct ParameterMap
		{
			void insert(std::pair<std::string, std::unique_ptr<ProcessorState>> entry)
			{
				map.emplace_back(std::move(entry));
			}

			ParameterSet::ParameterView * findParameter(cpl::Parameters::Handle index)
			{
				for (std::size_t i = 0; i < map.size(); ++i)
					if (auto param = parameterSets[i]->findParameter(index))
						return param;

				CPL_RUNTIME_EXCEPTION("Parameter index from host is out of bounds");
			}

			ParameterSet * getSet(const std::string & s) noexcept
			{
				for (std::size_t i = 0; i < map.size(); ++i)
				{
					if (map[i].first == s)
						return parameterSets[i];
				}

				return nullptr;
			}

			std::size_t size() const noexcept { 
				std::size_t r(0);
				for (auto & i : parameterSets)
					r += i->size();
				return r;
			};

			std::vector<ParameterSet *> parameterSets;
			std::vector<std::pair<std::string, std::unique_ptr<ProcessorState>>> map;
		};

		//==============================================================================
		AudioProcessor();
		~AudioProcessor() noexcept;

		//==============================================================================
		void prepareToPlay(double sampleRate, int samplesPerBlock);
		void releaseResources();

		void processBlock(AudioSampleBuffer& buffer, MidiBuffer& midiMessages);

		//==============================================================================
		AudioProcessorEditor* createEditor();
		bool hasEditor() const;

		//==============================================================================
		const String getName() const;

		int getNumParameters();

		float getParameter(int index);
		void setParameter(int index, float newValue);

		const String getParameterName(int index);
		const String getParameterText(int index);

		const String getInputChannelName(int channelIndex) const;
		const String getOutputChannelName(int channelIndex) const;
		bool isInputChannelStereoPair(int index) const;
		bool isOutputChannelStereoPair(int index) const;

		bool acceptsMidi() const;
		bool producesMidi() const;
		bool silenceInProducesSilenceOut() const;
		double getTailLengthSeconds() const;

		//==============================================================================
		int getNumPrograms();
		int getCurrentProgram();
		void setCurrentProgram(int index);
		const String getProgramName(int index);
		void changeProgramName(int index, const String& newName);

		//==============================================================================
		void getStateInformation(MemoryBlock& destData);
		void setStateInformation(const void* data, int sizeInBytes);

		void onServerDestruction(cpl::DestructionNotifier * v) override;

	private:

		virtual void automatedTransmitChangeMessage(int parameter, ParameterSet::FrameworkType value) override;
		virtual void automatedBeginChangeGesture(int parameter) override;
		virtual void automatedEndChangeGesture(int parameter) override;

		//==============================================================================
		JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioProcessor)
		cpl::CPresetWidget::SerializerType serializedData;
		Signalizer::MainEditor * editor;
		Signalizer::AudioStream stream;
		bool hasDefaultPresetBeenLoaded;
		int nChannels;



		ParameterMap parameterMap;
	};
};
#endif 
