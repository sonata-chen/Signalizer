target_include_directories(Signalizer PRIVATE
    ./
    ./Config
    ../External/
    ../External/cpl)

target_sources(Signalizer PRIVATE
    Common/SignalizerDesign.cpp
    # Common/MixGraphListener.cpp
    Common/SignalizerDesign.cpp
    # Common/HostGraph.cpp
    # Editor/GraphEditor.cpp
    Editor/MainEditor.cpp
    Oscilloscope/Oscilloscope.cpp
    # Oscilloscope/OscilloscopeController.cpp
    Oscilloscope/OscilloscopeRendering.cpp
    Processor/PluginProcessor.cpp
    Spectrum/Spectrum.cpp
    # Spectrum/SpectrumController.cpp
    Spectrum/SpectrumDSP.cpp
    Spectrum/SpectrumRendering.cpp
    # Unity/SignalizerSource.cpp
    Vectorscope/Vectorscope.cpp
    # Vectorscope/VectorscopeController.cpp
    Vectorscope/VectorscopeRendering.cpp

    ../External/cpl/CPLSource.cpp)

    #../External/cpl/AudioStream.cpp
    #../External/cpl/CExclusiveFile.cpp
    #../External/cpl/CModule.cpp
    #../External/cpl/CMutex.cpp
    ##../External/cpl/CPLSource.cpp
    ##../External/cpl/CPLTests.cpp
    #../External/cpl/CPresetManager.cpp
    #../External/cpl/CTimer.cpp
    #../External/cpl/Exceptions.cpp
    #../External/cpl/Misc.cpp
    #../External/cpl/Protected.cpp
    #../External/cpl/Resources.cpp
    #../External/cpl/SigMathImp.cpp
    #../External/cpl/ffts/dustfft.cpp
    #../External/cpl/gui/CBaseControl.cpp
    #../External/cpl/gui/CCtrlEditSpace.cpp
    #../External/cpl/gui/CEditSpaceSpawner.cpp
    #../External/cpl/gui/CToolTip.cpp
    #../External/cpl/gui/DesignBase.cpp
    #../External/cpl/gui/GUI.cpp
    #../External/cpl/gui/GraphicComponents.cpp
    #../External/cpl/gui/controls/CButton.cpp
    #../External/cpl/gui/controls/CColourControl.cpp
    #../External/cpl/gui/controls/CComboBox.cpp
    #../External/cpl/gui/controls/CInputControl.cpp
    #../External/cpl/gui/controls/CKnobSlider.cpp
    #../External/cpl/gui/controls/CKnobSliderEditor.cpp
    #../External/cpl/gui/controls/CValueComboBox.cpp
    #../External/cpl/gui/controls/CValueControl.cpp
    #../External/cpl/gui/controls/CValueInputControl.cpp
    #../External/cpl/gui/controls/CValueKnobSlider.cpp
    #../External/cpl/gui/controls/Controls.cpp
    #../External/cpl/gui/widgets/CDSPWindowWidget.cpp
    #../External/cpl/gui/widgets/CPowerSlopeWidget.cpp
    #../External/cpl/gui/widgets/CPresetWidget.cpp
    #../External/cpl/gui/widgets/CTransformWidget.cpp
    #../External/cpl/gui/widgets/Widgets.cpp
    #../External/cpl/lib/md5/md5.cpp
    #../External/cpl/octave/octave_all.cpp
    #../External/cpl/octave/signal/octave_signal.cpp
    #../External/cpl/octave/signal/octave_signal_ultrawin.cpp
    #../External/cpl/process/Process.cpp
    #../External/cpl/process/ProcessUnix.cpp
    ## ../External/cpl/process/ProcessWindows.cpp
    #../External/cpl/rendering/CDisplaySetup.cpp
    #../External/cpl/rendering/CSubpixelSoftwareGraphics.cpp
    #../External/cpl/rendering/DisplayOrientationAndroid.cpp
    #../External/cpl/rendering/DisplayOrientationIOS.cpp
    #../External/cpl/rendering/DisplayOrientationLinux.cpp
    #../External/cpl/rendering/DisplayOrientationMac.cpp
    #../External/cpl/rendering/DisplayOrientationWindows.cpp
    #../External/cpl/simd/simd_consts.cpp
    #../External/cpl/state/CSerializer.cpp
    #../External/cpl/state/SafeSerializableObject.cpp
    #../External/cpl/state/State.cpp
    #../External/cpl/stdext.cpp
    #../External/cpl/system/InstructionSet.cpp
    #../External/cpl/system/System.cpp
