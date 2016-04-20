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
 
	file:MainEditor.cpp
 
		Implementation of MainEditor.h

*************************************************************************************/

#include "MainEditor.h"
#include "CVectorScope.h"
//#include "COscilloscope.h"
#include "CSpectrum.h"
#include "SignalizerDesign.h"
#include <cpl/CPresetManager.h>
#include <cpl/LexicalConversion.h>
#include "version.h"

namespace cpl
{
	const ProgramInfo programInfo
	{
		"Signalizer",
		cpl::Version::fromParts(SIGNALIZER_MAJOR, SIGNALIZER_MINOR, SIGNALIZER_BUILD),
		"Janus Thorborg",
		"sgn",
		false,
		nullptr,
		SIGNALIZER_STRING_BUILD_INFO
	};

};

namespace Signalizer
{
	const static int kdefaultMaxSkippedFrames = 10;

	const static int kdefaultLength = 700, kdefaultHeight = 480;
	const static std::vector<std::string> RenderingEnginesList = { "Software", "OpenGL" };

	const static juce::String MainEditorName = "Main Editor Settings";

	const char * ViewIndexToMap[] = 
	{
		"Vectorscope",
		"Oscilloscope",
		"Spectrum",
		"Statistics"
	};

	enum class ViewTypes
	{
		Vectorscope,
		Oscilloscope,
		Spectrum,
		end
	};

	enum class Editors
	{
		GlobalSettings,
		Vectorscope,
		Oscilloscope,
		Spectrum,
		end
	};
	enum class RenderTypes
	{
		Software,
		openGL,
		end
	};

	enum class Utility
	{
		Freeze,
		Sync,
		IdleInBack,
		end

	};

	/// <summary>
	/// TODO: Query this at runtime
	/// </summary>
	const static std::array<int, 5> AntialisingLevels =
	{
		1,
		2,
		4,
		8,
		16
	};
	
	/// <summary>
	/// TODO: Query this at runtime
	/// </summary>
	const static std::vector<std::string> AntialisingStringLevels =
	{
		"1",
		"2",
		"4",
		"8",
		"16"
	};

	MainEditor::MainEditor(SignalizerAudioProcessor * e)
	:
		engine(e),
		AudioProcessorEditor(e),
		CTopView(this, "Signalizer main editor"),
		rcc(this, this),
		krenderEngine("Rendering Engine", RenderingEnginesList),
		krefreshRate("Refresh Rate"),
		refreshRate(0),
		oldRefreshRate(0),
		unFocused(true), 
		idleInBack(false),
		isEditorVisible(false),
		selTab(0),
		currentView(nullptr),
		kstableFps("Stable FPS"),
		kvsync("Vertical Sync"),
		kioskCoords(-1, -1),
		firstKioskMode(false),
		hasAnyTabBeenSelected(false),
		viewTopCoord(0),
		krefreshState("Reset state"),
		kpresets(this, "main", kpresets.WithDefault),
		kmaxHistorySize("History size")

	{
		setOpaque(true);
		setMinimumSize(50, 50);
		setBounds(0, 0, kdefaultLength, kdefaultHeight);
		initUI();

		kpresets.loadDefaultPreset();
		oglc.setComponentPaintingEnabled(false);
	}


	std::unique_ptr<juce::Component> MainEditor::createEditor()
	{
		auto content = new Signalizer::CContentPage();
		content->setName(MainEditorName);
		if (auto page = content->addPage("Settings", "icons/svg/wrench.svg"))
		{
			if (auto section = new Signalizer::CContentPage::MatrixSection())
			{				
				section->addControl(&krefreshRate, 0);
				section->addControl(&kstableFps, 1);
				section->addControl(&kswapInterval, 0);
				section->addControl(&kvsync, 1);
				page->addSection(section, "Update");
			}
			if (auto section = new Signalizer::CContentPage::MatrixSection())
			{
				section->addControl(&krenderEngine, 0);

				section->addControl(&kantialias, 1);

				page->addSection(section, "Quality");
			}
			if (auto section = new Signalizer::CContentPage::MatrixSection())
			{
				section->addControl(&krefreshState, 0);
				section->addControl(&kidle, 1);
				section->addControl(&kmaxHistorySize, 2);
				page->addSection(section, "Utility");
			}
		}
		if (auto page = content->addPage("Colours", "icons/svg/brush.svg"))
		{
			if (auto section = new Signalizer::CContentPage::MatrixSection())
			{
				int numKnobsPerLine = static_cast<int>(colourControls.size() / 2);
				int rem = colourControls.size() % 2;

				for (int y = 0; y < 2; ++y)
				{
					for (int x = 0; x < numKnobsPerLine; ++x)
					{
						section->addControl(&colourControls[y * numKnobsPerLine + x], y);
					}
				}
				if (rem)
					section->addControl(&colourControls[colourControls.size() - 1], 0);
				page->addSection(section, "Colour Scheme");
			}
		}
		if (auto page = content->addPage("Presets", "icons/svg/save.svg"))
		{

			if (auto section = new Signalizer::CContentPage::MatrixSection())
			{
				section->addControl(&kpresets, 0);
				page->addSection(section, "Presets");
			}
		}

		return std::unique_ptr<juce::Component>(content);
	}

	void MainEditor::focusGained(FocusChangeType cause)
	{
		if (idleInBack)
		{
			if (unFocused)
			{
				krefreshRate.bSetValue(oldRefreshRate);
			}
		}

		unFocused = false;
	}
	void MainEditor::focusLost(FocusChangeType cause)
	{
		oldRefreshRate = krefreshRate.bGetValue();
		if (idleInBack)
		{
			if (!unFocused)
			{
				krefreshRate.bSetValue(0.5f);
			}
			unFocused = true;
		}
	}

	void MainEditor::onObjectDestruction(const cpl::Utility::DestructionServer<cpl::CBaseControl>::ObjectProxy & destroyedObject)
	{
		// no-op.
	}

	void MainEditor::pushEditor(juce::Component * editor)
	{
		pushEditor(std::unique_ptr<juce::Component>(editor));
		
	}
	void MainEditor::pushEditor(std::unique_ptr<juce::Component> newEditor)
	{
		if (!newEditor.get())
			return;

		if (auto editor = getTopEditor())
		{
			editor->setVisible(false);
		}

		editorStack.push_back(std::move(newEditor));

		// beware of move construction! newEditor is now invalid!
		if (auto editor = getTopEditor())
		{
			addAndMakeVisible(editor);
			resized();
			repaint();
		}
	}
	juce::Component * MainEditor::getTopEditor() const
	{
		return editorStack.empty() ? nullptr : editorStack.back().get();
	}
	
	void MainEditor::deleteEditor(MainEditor::EditorIterator i)
	{
		editorStack.erase(i);
		
		if(editorStack.empty() && tabs.isOpen())
			tabs.closePanel();
		else
		{
			if (auto editor = getTopEditor())
			{
				editor->setVisible(true);
			}
			resized();
			repaint();
		}
	}

	void MainEditor::popEditor()
	{
		if (!editorStack.empty())
		{
			return deleteEditor(editorStack.begin() + editorStack.size() - 1);
		}
	}
	void MainEditor::clearEditors()
	{
		while (!editorStack.empty())
			popEditor();
	}
	cpl::CView * MainEditor::viewFromIndex(std::size_t index)
	{
		auto it = views.end();
		if ((ViewTypes)index < ViewTypes::end)
			it = views.find(ViewIndexToMap[index]);

		return (it != views.end()) ? it->second.view.get() : nullptr;
	}

	void MainEditor::setRefreshRate(int rate)
	{
		refreshRate = cpl::Math::confineTo(rate, 10, 1000);
		if (kstableFps.bGetValue() > 0.5)
		{
			juce::HighResolutionTimer::startTimer(refreshRate);
		}
		else
		{
			juce::Timer::startTimer(refreshRate);
		}
		if (currentView)
			currentView->setApproximateRefreshRate(refreshRate);

	}
	void MainEditor::resume()
	{
		setRefreshRate(refreshRate);
	}

	void MainEditor::suspend()
	{
		juce::HighResolutionTimer::stopTimer();
		juce::Timer::stopTimer();
	}

	// these handle cases where our component is being thrown off the kiosk mode by (possibly)
	// another JUCE plugin.
	void MainEditor::componentMovedOrResized(Component& component, bool wasMoved, bool wasResized)
	{
		if (&component == currentView->getWindow())
		{
			if (currentView->getIsFullScreen() && component.isOnDesktop())
			{
				if (juce::ComponentPeer * peer = component.getPeer())
				{
					if (!peer->isKioskMode())
					{
						exitFullscreen();
						if (kkiosk.bGetValue() > 0.5)
						{
							kkiosk.bSetInternal(0.0);
						}
					}
				}
			}
		}
	}
	void MainEditor::componentParentHierarchyChanged(Component& component)
	{
		if (&component == currentView->getWindow())
		{
			if (currentView->getIsFullScreen() && component.isOnDesktop())
			{
				if (juce::ComponentPeer * peer = component.getPeer())
				{
					if (!peer->isKioskMode())
					{
						exitFullscreen();
						if (kkiosk.bGetValue() > 0.5)
						{
							kkiosk.bSetInternal(0.0);
						}
					}
				}
			}
		}
	}

	void MainEditor::valueChanged(const cpl::CBaseControl * c)
	{
		// TODO: refactor all behaviour here out to semantic functions
		// bail out early if we aren't showing anything.
		if (!currentView)
			return;

		auto value = c->bGetValue();

		// freezing of displays
		if (c == &kfreeze)
		{
			if (value > 0.5)
			{
				engine->stream.setSuspendedState(true);
			}
			else
			{
				engine->stream.setSuspendedState(false);
			}
		}
		// lower display rate if we are unfocused
		else if (c == &kidle)
		{
			idleInBack = value > 0.5 ? true : false;
		}

		else if (c == &ksettings)
		{
			if (value > 0.5)
			{
				// spawn the global setting editor
				pushEditor(this->createEditor());
				tabs.openPanel();
			}
			else
			{
				// -- remove it
				if (auto editor = getTopEditor())
				{
					if (editor->getName() == MainEditorName)
					{
						popEditor();
					}
				}
			}
		}
		else if (c == &kkiosk)
		{
			if (currentView)
			{
				if (value > 0.5 && currentView)
				{
					// set a window to fullscreen.
					if (firstKioskMode)
					{
						// dont fetch the current coords if we set the view the first time..
						firstKioskMode = false;
					}
					else
					{
						if (juce::Desktop::getInstance().getKioskModeComponent() == currentView->getWindow())
							return;
						kioskCoords = currentView->getWindow()->getScreenPosition();
					}

					preFullScreenSize = getBounds().withZeroOrigin();

					removeChildComponent(currentView->getWindow());
					currentView->getWindow()->addToDesktop(juce::ComponentPeer::StyleFlags::windowAppearsOnTaskbar);

					currentView->getWindow()->setTopLeftPosition(kioskCoords.x, kioskCoords.y);
					bool useMenusAndBars = false;
					#ifdef CPL_MAC
						useMenusAndBars = true;
					#endif
					juce::Desktop::getInstance().setKioskModeComponent(currentView->getWindow(), useMenusAndBars);
					currentView->setFullScreenMode(true);
					currentView->getWindow()->setWantsKeyboardFocus(true);
					currentView->getWindow()->grabKeyboardFocus();
					// add listeners.

					currentView->getWindow()->addKeyListener(this);
					currentView->getWindow()->addComponentListener(this);
					
					// sets a minimal view when entering full screen
					setBounds(getBounds().withBottom(getViewTopCoordinate()));
				}
				else
				{
					exitFullscreen();
				}
			}
		}
		else if (c == &kstableFps)
		{
			if (kstableFps.bGetValue() > 0.5)
			{
				juce::Timer::stopTimer();
				juce::HighResolutionTimer::startTimer(refreshRate);
			}
			else
			{
				juce::HighResolutionTimer::stopTimer();
				juce::Timer::startTimer(refreshRate);
			}
		}
		else if (c == &kswapInterval)
		{
			newc.swapInterval.store(
				cpl::Math::round<int>(kswapInterval.bGetValue() * kdefaultMaxSkippedFrames),
				std::memory_order_release
			);
			
			mtFlags.swapIntervalChanged = true;
		}
		else if (c == &kvsync)
		{
			if (kvsync.bGetValue() > 0.5)
			{
				if (currentView)
				{
					// this is kind of stupid; the sync setting must be set after the context is created..
					struct RetrySync
					{
						RetrySync(MainEditor * h) : handle(h) {};
						MainEditor * handle;

						void operator()()
						{
							if (handle->oglc.isAttached())
								handle->oglc.setContinuousRepainting(true);
							else
								cpl::GUIUtils::FutureMainEvent(200, RetrySync(handle), handle);
						}

					};

					RetrySync(this)();
				}
			}
			else
			{
				if (currentView)
				{
					oglc.setContinuousRepainting(false);
				}
			}
		}
		// change of refresh rate
		else if (c == &krefreshRate)
		{
			refreshRate = cpl::Math::round<int>(cpl::Math::UnityScale::exp(value, 10.0, 1000.0));
			setRefreshRate(refreshRate);
		}
		// change of the rendering engine
		else if (c == &krenderEngine)
		{
			auto index = cpl::distribute<RenderTypes>(value);

			switch (index)
			{
			case RenderTypes::Software:

				if (currentView && currentView->isOpenGL())
				{
					currentView->detachFromOpenGL(oglc);
					if (oglc.isAttached())
						oglc.detach();
				}


				break;
			case RenderTypes::openGL:
				if (oglc.isAttached())
				{
					if (currentView && !currentView->isOpenGL())
					{
						// ?? freaky
						if (cpl::CView * unknownView = dynamic_cast<cpl::CView *>(oglc.getTargetComponent()))
							unknownView->detachFromOpenGL(oglc);
						oglc.detach();
					}
				}
				if (currentView)
					currentView->attachToOpenGL(oglc);
				break;
			}
		}
		else if (c == &kantialias)
		{
			setAntialiasing();
		}
		else if (c == &krefreshState)
		{
			if (currentView)
				currentView->resetState();
		}
		else if(c == &khelp)
		{
			showAboutBox();
		}
		else if (c == &kmaxHistorySize)
		{

			struct RetryResizeCapacity
			{
				RetryResizeCapacity(MainEditor * h) : handle(h) {};
				MainEditor * handle;

				void operator()()
				{
					auto currentSampleRate = handle->engine->stream.getInfo().sampleRate.load(std::memory_order_acquire);
					if (currentSampleRate > 0)
					{
						std::int64_t value;
						std::string contents = handle->kmaxHistorySize.getInputValue();
						if (cpl::lexicalConversion(contents, value) && value >= 0)
						{
							auto msCapacity = cpl::Math::round<std::size_t>(currentSampleRate * 0.001 * value);

							handle->engine->stream.setAudioHistoryCapacity(msCapacity);

							if (contents.find_first_of("ms") == std::string::npos)
							{
								contents.append(" ms");
								handle->kmaxHistorySize.setInputValueInternal(contents);
							}

							handle->kmaxHistorySize.indicateSuccess();
						}
						else
						{
							std::string result;
							auto msCapacity = cpl::Math::round<std::size_t>(1000 * handle->engine->stream.getAudioHistoryCapacity() / handle->engine->stream.getInfo().sampleRate);
							if (cpl::lexicalConversion(msCapacity, result))
								handle->kmaxHistorySize.setInputValueInternal(result + " ms");
							else
								handle->kmaxHistorySize.setInputValueInternal("error");
							handle->kmaxHistorySize.indicateError();
						}
					}
					else
					{
						cpl::GUIUtils::FutureMainEvent(200, RetryResizeCapacity(handle), handle);
					}

				}
			};

			RetryResizeCapacity(this)();

		}
		else
		{
			// check if it was one of the colours
			for (unsigned i = 0; i < colourControls.size(); ++i)
			{
				if (c == &colourControls[i])
				{
					// change colour and broadcast event.
					cpl::CLookAndFeel_CPL::defaultLook().getSchemeColour(i).colour = colourControls[i].getControlColourAsColour();
					cpl::CLookAndFeel_CPL::defaultLook().updateColours();
					repaint();
				}

			}
		}
	}

	void MainEditor::setPreferredKioskCoords(juce::Point<int> preferredCoords) noexcept
	{
		firstKioskMode = true;
		kioskCoords = preferredCoords;
	}

	void MainEditor::panelOpened(cpl::CTextTabBar<> * obj)
	{
		isEditorVisible = true;
		// -- settings editor spawned the panel view
		if(!ksettings.bGetBoolState() && !getTopEditor())
		{
			if (cpl::CView * view = viewFromIndex(selTab))
			{
				pushEditor(view->createEditor());
			}
		}


		auto editorBottom = getViewTopCoordinate();

		if (getBottom() < editorBottom)
			setBounds(getBounds().withBottom(editorBottom));
		else
			resized();
		repaint();
	}

	void MainEditor::panelClosed(cpl::CTextTabBar<> * obj)
	{
		clearEditors();
		ksettings.setToggleState(false, NotificationType::dontSendNotification);
		resized();
		isEditorVisible = false;
	}
	
	void MainEditor::setAntialiasing(int multisamplingLevel)
	{

		int sanitizedLevel = 1;
		
		if(multisamplingLevel == -1)
		{
			auto val = cpl::Math::confineTo(kantialias.getZeroBasedSelIndex(), 0, AntialisingLevels.size() - 1);
			sanitizedLevel = AntialisingLevels[val];
		}
		else
		{
			for(unsigned i = 0; i < AntialisingLevels.size(); ++i)
			{
				if(AntialisingLevels[i] == multisamplingLevel)
				{
					sanitizedLevel = AntialisingLevels[i];
					break;
				}
			}
		}
		// TODO: Query OpenGL for glGet GL_MAX_INTEGER_SAMPLES, to see what the maximum supported
		// multisampling level is

		if(sanitizedLevel > 0)
		{
			OpenGLPixelFormat fmt;
			fmt.multisamplingLevel = sanitizedLevel;
			// true if a view exists and it is attached
			bool reattach = false;
			if(currentView)
			{
				if(currentView->isOpenGL())
				{
					currentView->detachFromOpenGL(oglc);
					reattach = true;
				}
			}
			oglc.setMultisamplingEnabled(true);
			oglc.setPixelFormat(fmt);
			
			if(reattach)
			{
				currentView->attachToOpenGL(oglc);
			}
			
		}
		else
		{
			oglc.setMultisamplingEnabled(false);
		}
		
	}

	int MainEditor::getRenderEngine()
	{
		return (int)cpl::distribute<RenderTypes>(krenderEngine.bGetValue());
	}

	void MainEditor::suspendView(cpl::CView * view)
	{
		if (view)
		{
			view->suspend();
			if (oglc.isAttached())
			{
				view->detachFromOpenGL(oglc);
				oglc.detach();
			}
			else
			{
				// check if view is still attached to a dead context:
				if(view->isOpenGL())
				{
					// something else must have killed the context, check if it's the same
					
					if(view->getAttachedContext() == &oglc)
					{
						// okay, so we detach it anyway and eat the exceptions:
						view->detachFromOpenGL(oglc);
					}
				}
				
			}
			
			// serializing the main editor to a preset where the kkiosk is false, while the main editor has
			// a full screen window creates a.. interesting situation (replaced && with || to be sure).
			if (juce::Desktop::getInstance().getKioskModeComponent() == view->getWindow() || kkiosk.bGetValue() > 0.5)
			{
				exitFullscreen();
			}
			removeChildComponent(currentView->getWindow());
			view->getWindow()->removeMouseListener(this);
		}

	}

	void MainEditor::initiateView(cpl::CView * view, bool spawnNewEditor)
	{
		if (view)
		{
			if (view != currentView)
			{
				// trying to add the same window twice without suspending it?
				jassertfalse;
			}

			currentView = view;
			addAndMakeVisible(view->getWindow());

			if ((RenderTypes)getRenderEngine() == RenderTypes::openGL)
			{
				// init all openGL stuff.
				if (auto oglView = dynamic_cast<cpl::COpenGLView*>(view))
				{
					oglView->addOpenGLEventListener(this);

					setAntialiasing();
					oglView->attachToOpenGL(oglc);
				}

			}
			
			if (spawnNewEditor)
			{
				auto newEditor = currentView->createEditor();
				
				if (newEditor.get())
				{
					pushEditor(std::move(newEditor));
					tabs.openPanel();
				}
				else
				{
					tabs.closePanel();
				}
			}
			
			if (kkiosk.bGetValue() > 0.5)
			{
				if (firstKioskMode)
					enterFullscreenIfNeeded(kioskCoords);
				else
					enterFullscreenIfNeeded();
			}
			resized();
			view->getWindow()->addMouseListener(this, true);
			view->resume();
		}
		else
		{
			// adding non-existant window?
			jassertfalse;
		}
	}

	void MainEditor::mouseUp(const MouseEvent& event)
	{
		if (currentView && event.eventComponent == currentView->getWindow())
		{
			if (event.mods.testFlags(ModifierKeys::rightButtonModifier))
			{
				kfreeze.setToggleState(!kfreeze.getToggleState(), juce::NotificationType::sendNotification);
			}
		}
		else
		{
			AudioProcessorEditor::mouseUp(event);
		}
	}
	void  MainEditor::mouseDown(const MouseEvent& event)
	{
		if (currentView && event.eventComponent == currentView->getWindow())
		{
			if (event.mods.testFlags(ModifierKeys::rightButtonModifier))
			{
				kfreeze.setToggleState(!kfreeze.getToggleState(), juce::NotificationType::sendNotification);
			}
		}
		else
		{
			AudioProcessorEditor::mouseUp(event);
		}
	}
	void MainEditor::tabSelected(cpl::CTextTabBar<> * obj, int index)
	{

		hasAnyTabBeenSelected = true;
		// these lines disable the global editor if you switch view.
		//if (ksettings.getToggleState())
		//	ksettings.setToggleState(false, NotificationType::sendNotification);

		// see if the new view exists.
		auto const & mappedView = Signalizer::ViewIndexToMap[index];
		auto it = views.find(mappedView);

		cpl::CSubView * view = nullptr;

		if (it == views.end())
		{

			// insert the new view into the map
			switch ((ViewTypes)index)
			{
			case ViewTypes::Vectorscope:
				view = new CVectorScope(engine->stream);
				break;
			case ViewTypes::Oscilloscope:
			//	view = new COscilloscope(engine->audioBuffer);
				break;
			case ViewTypes::Spectrum:
				view = new CSpectrum(engine->stream);
				break;
			default:
				break;
			}
			if (view)
			{
				auto && entry = views.emplace(mappedView, ViewWithSerializedFlag {std::unique_ptr<cpl::CSubView>(view), false });
				auto & key = viewSettings.getContent("Serialized Views").getContent(mappedView);
				if (!key.isEmpty())
				{
					key >> view;
					entry.first->second.hasBeenRestored = true;
				}

			}
			else
			{
				view = &defaultView;
			}
		}
		else
		{
			view = it->second.view.get();
			// in general, constructing a view != when it should be serialized
			if (!it->second.hasBeenRestored)
			{
				// TODO: Merge with same lines above.
				auto & key = viewSettings.getContent("Serialized Views").getContent(mappedView);
				if (!key.isEmpty())
				{
					key >> view;
					it->second.hasBeenRestored = true;
				}
			}
		}
		// if any editor is open currently, we have to close it and open the new.
		bool openNewEditor = false;
		// deattach old view
		if (currentView)
		{
			if (currentView->getIsFullScreen() && view)
				setPreferredKioskCoords(currentView->getWindow()->getPosition());


			if (getTopEditor())
				openNewEditor = true;

			clearEditors();

			suspendView(currentView);
			currentView = nullptr;
		}

		currentView = view;
		if (currentView)
		{
			initiateView(currentView, openNewEditor);
		}
		
		if (openNewEditor && ksettings.bGetBoolState())
			ksettings.bSetInternal(0.0);



		selTab = index;
	}

	template<typename Pred>
		bool MainEditor::removeAnyEditor(Pred pred)
	{
		bool alteredStack = false;
		for(std::size_t i = 0; i < editorStack.size(); ++i)
		{
			while(i < editorStack.size() && pred(editorStack[i].get()))
			{
				alteredStack = true;
				deleteEditor(editorStack.begin() + i);
			}
		}
		return alteredStack;
	}

	void MainEditor::activeTabClicked(cpl::CTextTabBar<>* obj, int index)
	{
		// TODO: spawn the clicked editor, if it doesn't exist.
		if(ksettings.bGetBoolState())
		{
			removeAnyEditor([](juce::Component * e) { return e->getName() == MainEditorName; });
			ksettings.bSetInternal(0);
			// make sure an editor is active
			if(editorStack.empty() && currentView)
			{
				pushEditor(currentView->createEditor());
			}
			
		}
	}


	void MainEditor::addTab(const std::string & name)
	{
		tabs.addTab(name);
	}

	void MainEditor::serialize(cpl::CSerializer & data, cpl::Version version)
	{

		data << krefreshRate;
		data << krenderEngine;
		data << khelp;
		data << kfreeze;
		data << kidle;
		data << getBounds().withZeroOrigin();
		data << isEditorVisible;
		data << selTab;
		data << kioskCoords;
		data << hasAnyTabBeenSelected;
		data << kkiosk;

		// stuff that gets set the last.
		data << kantialias;
		data << kvsync;
		data << kswapInterval;


		for (auto & colour : colourControls)
		{
			data.getContent("Colours").getContent(colour.bGetTitle()) << colour;
		}

		// save any view data

		// copy old session data

		// walk the list of possible plugins
		for (auto & viewName : ViewIndexToMap)
		{		
			
			auto viewInstanceIt = views.find(viewName);
			// see if they're instantiated, in which case
			if (viewInstanceIt != views.end())
			{
				// serialize fresh data - // watch out, or it'll save the std::unique_ptr!
				data.getContent("Serialized Views").getContent(viewInstanceIt->first) << viewInstanceIt->second.view.get();
			}
			else
			{
				// otherwise, see if we have some old session data:
				auto serializedView = viewSettings.getContent("Serialized Views").getContent(viewName);
				if (!serializedView.isEmpty())
				{
					data.getContent("Serialized Views").getContent(viewName) = serializedView;
				}
			}
		}

	}

	void MainEditor::enterFullscreenIfNeeded(juce::Point<int> where)
	{
		// full screen set?
		if (kkiosk.bGetValue() > 0.5)
		{

			// avoid storing the current window position into kioskCoords
			// the first time we spawn the view.
			firstKioskMode = true;
			currentView->getWindow()->setTopLeftPosition(where.x, where.y);
			kkiosk.bForceEvent();
		}
	}

	void MainEditor::enterFullscreenIfNeeded()
	{
		// full screen set?
		if (currentView && !currentView->getIsFullScreen() && kkiosk.bGetValue() > 0.5)
		{
			// avoid storing the current window position into kioskCoords
			// the first time we spawn the view.
			firstKioskMode = false;
			kkiosk.bForceEvent();
		}
	}

	void MainEditor::exitFullscreen()
	{
		juce::Desktop & instance = juce::Desktop::getInstance();
		if (currentView && currentView->getIsFullScreen() && !this->isParentOf(currentView->getWindow()))
		{
			currentView->getWindow()->removeKeyListener(this);
			currentView->getWindow()->removeComponentListener(this);
			if (currentView->getWindow() == instance.getKioskModeComponent())
			{
				juce::Desktop::getInstance().setKioskModeComponent(nullptr);
			}

			currentView->getWindow()->setTopLeftPosition(0, 0);
			addChildComponent(currentView->getWindow());
			currentView->setFullScreenMode(false);

			if (preFullScreenSize.getWidth() > 0 && preFullScreenSize.getHeight() > 0)
			{
				// restores from minimal window
				setBounds(preFullScreenSize);
			}
			else
			{
				resized();
			}
		}

	}

	void MainEditor::deserialize(cpl::CSerializer & data, cpl::Version version)
	{
		for (auto & currentViewEntry : views)
			currentViewEntry.second.hasBeenRestored = false;
		
		viewSettings = data;
		//cpl::iCtrlPrec_t dataVal(0);
		juce::Rectangle<int> bounds;
		
		data >> krefreshRate;
		data >> krenderEngine;
		data >> khelp;
		data >> kfreeze;
		data >> kidle;
		data >> bounds;
		data >> isEditorVisible;
		data >> selTab;
		data >> kioskCoords;
		data >> hasAnyTabBeenSelected;
		// kind of a hack, but we don't really want to enter kiosk mode immediately.
		kkiosk.bRemovePassiveChangeListener(this);
		data >> kkiosk;
		kkiosk.bAddPassiveChangeListener(this);
		for (auto & colour : colourControls)
		{
			auto & content = viewSettings.getContent("Colours").getContent(colour.bGetTitle());
			if (!content.isEmpty())
				content >> colour;
		}

		// sanitize bounds...
		setBounds(bounds.constrainedWithin(
			juce::Desktop::getInstance().getDisplays().getDisplayContaining(bounds.getPosition()).userArea
		).withZeroOrigin());

		// reinitiaty any current views (will not be done through tab selection further down)
		for (auto & viewPair : views)
		{
			auto & key = viewSettings.getContent("Serialized Views").getContent(viewPair.first);
			if (!key.isEmpty())
				key << viewPair.second;
		}

		// will take care of opening the correct view
		if (hasAnyTabBeenSelected)
		{
			// settings this will cause setSelectedTab to 
			// enter fullscreen at kioskCoords
			if (kkiosk.bGetValue() > 0.5)
				firstKioskMode = true;
			
			if(tabs.getSelectedTab() == selTab)
			{
				// TODO: rewrite the following
				// tabSelected will NOT get called in this case, do it manually.
				tabSelected(&tabs, selTab);
			}
			
			tabs.setSelectedTab(selTab);
		}
		else
		{
			// if not, make sure we entered full screen if needed.
			enterFullscreenIfNeeded(kioskCoords);
		}


		// set this kind of stuff after view is initiated. note, these cause events to be fired!
		data >> kantialias;
		data >> kvsync;
		data >> kswapInterval;

	}

	bool MainEditor::stringToValue(const cpl::CBaseControl * ctrl, const std::string & valString, cpl::iCtrlPrec_t & val)
	{
		double newVal = 0;

		if (ctrl == &krefreshRate)
		{
			if (cpl::lexicalConversion(valString, newVal))
			{
				newVal = cpl::Math::confineTo
				(
					cpl::Math::UnityScale::Inv::exp
					(
						cpl::Math::confineTo(newVal, 10.0, 1000.0), 
						10.0, 
						1000.0
					),
					0.0,
					1.0
				);
				val = newVal;
				return true;
			}
		}
		else if (ctrl == &kswapInterval)
		{
			if (cpl::lexicalConversion(valString, newVal))
			{
				newVal = cpl::Math::confineTo(newVal, 0, kdefaultMaxSkippedFrames);
				val = cpl::Math::UnityScale::Inv::linear(newVal, 0.0, (double)kdefaultMaxSkippedFrames);
				return true;
			}
		}
		return false;
	}

	bool MainEditor::valueToString(const cpl::CBaseControl * ctrl, std::string & valString, cpl::iCtrlPrec_t val)
	{
		if (ctrl == &krefreshRate)
		{
			auto refreshRateVal = cpl::Math::round<int>(cpl::Math::UnityScale::exp(val, 10.0, 1000.0));
			valString = std::to_string(refreshRateVal) + " ms";
			return true;
		}
		else if (ctrl == &kswapInterval)
		{
			char buf[100];
			sprintf_s(buf, "%d frames", cpl::Math::round<int>(val * kdefaultMaxSkippedFrames));
			valString = buf;
			return true;
		}
		return false;
	}

	bool MainEditor::keyPressed(const KeyPress &key, Component *originatingComponent)
	{
		if (currentView && (currentView->getWindow() == originatingComponent))
		{
			if (key.isKeyCode(key.escapeKey) && kkiosk.bGetValue() > 0.5)
			{
				exitFullscreen();

				kkiosk.bSetInternal(0.0);
				return true;
			}
		}
		return false;
	}
	MainEditor::~MainEditor()
	{
		notifyDestruction();
		suspendView(currentView);
		exitFullscreen();
		juce::Timer::stopTimer();
		juce::HighResolutionTimer::stopTimer();

	}
	void MainEditor::resizeEnd()
	{
		resized();
		resume();
	}

	void MainEditor::resizeStart()
	{
		suspend();
	}

	int MainEditor::getViewTopCoordinate() const noexcept
	{
		auto editor = getTopEditor();

		if (editor)
		{
			auto maxHeight = elementSize * 5;
			auto possibleBounds = std::make_pair(getWidth() - elementBorder * 2, maxHeight);
			// content pages knows their own (dynamic) size.
			if (auto signalizerEditor = dynamic_cast<Signalizer::CContentPage *>(editor))
			{
				maxHeight = std::max(0, std::min(maxHeight, signalizerEditor->getSuggestedSize(possibleBounds).second));
			}
			return tabs.getHeight() + maxHeight + elementBorder;
		}
		else
		{
			return tabs.getBottom() + elementBorder;
		}
	}

	void MainEditor::resized()
	{
		auto const buttonSize = elementSize - elementBorder * 2;
		auto const buttonSizeW = elementSize - elementBorder * 2;
		rcc.setBounds(getWidth() - 15, getHeight() - 15, 15, 15);
		// dont resize while user is dragging
		if (rcc.isMouseButtonDown())
			return;
		// resize panel to width

		auto const width = getWidth();
		auto leftBorder = width - elementSize + elementBorder;
		ksettings.setBounds(1, 1, buttonSizeW, buttonSize);

		kfreeze.setBounds(leftBorder, 1, buttonSizeW, buttonSize);
		leftBorder -= elementSize - elementBorder;

		khelp.setBounds(leftBorder, 1, buttonSizeW, buttonSize);
		leftBorder -= elementSize - elementBorder;
		kkiosk.setBounds(leftBorder, 1, buttonSizeW, buttonSize);

		tabs.setBounds
		(
			ksettings.getBounds().getRight() + elementBorder,
			elementBorder,
			getWidth() - (ksettings.getWidth() + getWidth() - leftBorder + elementBorder * 3),
			elementSize - elementBorder * 2
		 );


		auto editor = getTopEditor();
		if (editor)
		{
			// TODO: refactor code to merge with getViewTopCoordinate -> getEditorRect()
			auto maxHeight = elementSize * 5;
			auto possibleBounds = std::make_pair(getWidth() - elementBorder * 2, maxHeight);
			// content pages knows their own (dynamic) size.
			if (auto signalizerEditor = dynamic_cast<Signalizer::CContentPage *>(editor))
			{
				maxHeight = std::max(0, std::min(maxHeight, signalizerEditor->getSuggestedSize(possibleBounds).second));
			}
			editor->setBounds(elementBorder, tabs.getBottom(), possibleBounds.first, maxHeight);
			viewTopCoord = tabs.getHeight() + maxHeight + elementBorder;
		}
		else
		{
			viewTopCoord = tabs.getBottom() + elementBorder;
		}
		// full screen components resize themselves.
		if (currentView && !currentView->getIsFullScreen())
		{
			currentView->getWindow()->setBounds(0, viewTopCoord, getWidth(), getHeight() - viewTopCoord);
		}

		//rightButtonOutlines.addRectangle(juce::Rectangle<float>(0.5f, 0.5f, getWidth() - 1.5f, editor ? editor->getBottom() : elementSize - 1.5f));
	}


	void MainEditor::timerCallback()
	{
		
		if (currentView)
		{
			//const MessageManagerLock mml;

			if (idleInBack)
			{
				if (!hasKeyboardFocus(true))
					focusLost(FocusChangeType::focusChangedDirectly);
				else if (unFocused)
					focusGained(FocusChangeType::focusChangedDirectly);
			}

			if(!kvsync.bGetBoolState())
				currentView->repaintMainContent();
		}
	}
	void MainEditor::hiResTimerCallback()
	{
		if (currentView)
		{
			if (idleInBack)
			{
				const MessageManagerLock mml;
				if (!hasKeyboardFocus(true))
					focusLost(FocusChangeType::focusChangedDirectly);
				else if (unFocused)
					focusGained(FocusChangeType::focusChangedDirectly);
			}

			if (!kvsync.bGetBoolState())
				currentView->repaintMainContent();
		}
	}


	//==============================================================================
	void MainEditor::paint(Graphics& g)
	{
		// make sure to paint everything completely opaque.
		g.setColour(cpl::GetColour(cpl::ColourEntry::Separator).withAlpha(1.0f));
		g.fillRect(getBounds().withZeroOrigin().withBottom(viewTopCoord));
		if (kkiosk.bGetValue() > 0.5)
		{
			g.setColour(cpl::GetColour(cpl::ColourEntry::Deactivated).withAlpha(1.0f));
			g.fillRect(getBounds().withZeroOrigin().withTop(viewTopCoord));
			g.setColour(cpl::GetColour(cpl::ColourEntry::AuxillaryText));
			g.drawText("View is fullscreen", getBounds().withZeroOrigin().withTop(viewTopCoord), juce::Justification::centred, true);
		}
	}


	void MainEditor::onOGLRendering(cpl::COpenGLView * view) noexcept
	{
		if (mtFlags.swapIntervalChanged.cas())
		{
			oglc.setSwapInterval(newc.swapInterval.load(std::memory_order_acquire));
			view->setSwapInterval(newc.swapInterval.load(std::memory_order_acquire));
		}
	}

	void MainEditor::onOGLContextCreation(cpl::COpenGLView * view) noexcept
	{
		mtFlags.swapIntervalChanged = true;
	}

	void MainEditor::onOGLContextDestruction(cpl::COpenGLView * view) noexcept
	{

	}

	void MainEditor::showAboutBox()
	{
		khelp.bSetInternal(1);
		using namespace cpl;
		std::string contents =
			programInfo.name + " " + programInfo.version.toString() + newl + 
			"Build info: " + programInfo.customBuildInfo + newl + newl +
			"Written by Janus Lynggaard Thorborg, (C) 2016" + newl +
			programInfo.name + " is free and open source (GPL v3), see more at the home page: " + newl + "www.jthorborg.com/index.html?ipage=signalizer" + newl + newl +
			"Open the readme file (contains information you must read upon first use)?";
		
		auto ret = Misc::MsgBox(contents, "About " + programInfo.name, Misc::MsgStyle::sYesNo | Misc::MsgIcon::iInfo);
		switch (ret) {
			case Misc::MsgButton::bYes:
				#ifdef CPL_WINDOWS
					std::system(("Notepad.exe \"" + Misc::DirectoryPath() + "/READ ME.txt\"").c_str());
				#elif defined(CPL_MAC)
					std::string cmdLine = "open \"" + Misc::DirectoryPath() + "/READ ME.txt\"";
					std::system((cmdLine).c_str());
				#else 
					#error "Implement a text-opener for your platform.
				#endif
				break;
		}
		khelp.bSetInternal(0);
	}
	
	void MainEditor::initUI()
	{
		auto & lnf = cpl::CLookAndFeel_CPL::defaultLook();
		// add listeners
		krefreshRate.bAddFormatter(this);
		kfreeze.bAddPassiveChangeListener(this);
		kkiosk.bAddPassiveChangeListener(this);
		kidle.bAddPassiveChangeListener(this);
		krenderEngine.bAddPassiveChangeListener(this);
		krefreshRate.bAddPassiveChangeListener(this);
		ksettings.bAddPassiveChangeListener(this);
		kstableFps.bAddPassiveChangeListener(this);
		kvsync.bAddPassiveChangeListener(this);
		tabs.addListener(this);
		kmaxHistorySize.bAddPassiveChangeListener(this);
		kswapInterval.bAddPassiveChangeListener(this);
		kantialias.bAddPassiveChangeListener(this);
		khelp.bAddPassiveChangeListener(this);
		krefreshState.bAddPassiveChangeListener(this);
		kswapInterval.bAddFormatter(this);
		// design
		kfreeze.setImage("icons/svg/freeze.svg");
		ksettings.setImage("icons/svg/gears.svg");
		khelp.setImage("icons/svg/help.svg");
		kkiosk.setImage("icons/svg/fullscreen.svg");

		kstableFps.setSize(cpl::ControlSize::Rectangle.width, cpl::ControlSize::Rectangle.height / 2);
		kvsync.setSize(cpl::ControlSize::Rectangle.width, cpl::ControlSize::Rectangle.height / 2);
		krefreshState.setSize(cpl::ControlSize::Rectangle.width, cpl::ControlSize::Rectangle.height / 2);
		kstableFps.setToggleable(true);
		kvsync.setToggleable(true);
		kantialias.bSetTitle("Antialiasing");
		kidle.bSetTitle("Idle in back");
		kswapInterval.bSetTitle("Swap interval");
		// setup
		krenderEngine.setValues(RenderingEnginesList);
		kantialias.setValues(AntialisingStringLevels);

		// initiate colours
		for (unsigned i = 0; i < colourControls.size(); ++i)
		{
			auto & schemeColour = lnf.getSchemeColour(i);
			colourControls[i].setControlColour(schemeColour.colour.getPixelARGB());
			colourControls[i].bSetTitle(schemeColour.name);
			colourControls[i].bSetDescription(schemeColour.description);
			colourControls[i].bAddPassiveChangeListener(this);

		}

		// add stuff
		addAndMakeVisible(ksettings);
		addAndMakeVisible(kfreeze);
		addAndMakeVisible(khelp);
		addAndMakeVisible(kkiosk);
		addAndMakeVisible(tabs);

		tabs.setOrientation(tabs.Horizontal);
		tabs.addTab("VectorScope").addTab("Oscilloscope").addTab("Spectrum").addTab("Statistics");

		// additions
		addAndMakeVisible(rcc);
		rcc.setAlwaysOnTop(true);
		currentView = &defaultView; // note: enables callbacks on value set in this function
		addAndMakeVisible(defaultView);
		krefreshRate.bSetValue(0.12);


		// descriptions
		kstableFps.bSetDescription("Stabilize frame rate using a high precision timer.");
		kvsync.bSetDescription("Synchronizes graphic view rendering to your monitors refresh rate.");
		kantialias.bSetDescription("Set the level of hardware antialising applied.");
		krefreshRate.bSetDescription("How often the view is redrawn.");
		khelp.bSetDescription("About this program");
		kkiosk.bSetDescription("Puts the view into fullscreen mode. Press Escape to untoggle, or tab out of the view.");
		kidle.bSetDescription("If set, lowers the frame rate of the view if this plugin is not in the front.");
		ksettings.bSetDescription("Open the global settings for the plugin (presets, themes and graphics).");
		kfreeze.bSetDescription("Stops the view from updating, allowing you to examine the current point in time.");
		krefreshState.bSetDescription("Resets any processing state in the active view to the default.");
		kmaxHistorySize.bSetDescription("The maximum audio history capacity, set in the respective views. No limit, so be careful!");
		kswapInterval.bSetDescription("Determines the swap interval for the graphics context; a value of zero means the graphics will"
			"update as fast as possible, a value of 1 means it updates synced to the vertical sync, a value of N means it updates every Nth vertical frame sync.");


		// initial values that should be through handlers
		kmaxHistorySize.setInputValue("1000");

		
		resized();
	}
};