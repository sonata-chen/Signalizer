cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
# set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_OSX_DEPLOYMENT_TARGET 10.11 CACHE STRING "Minimum macOS version")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif ()

project(Signalizer VERSION 0.3.2)

message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")

# Global compile options
add_compile_options(
    # Build with Multiple Processes on Visual Studio
    # $<$<CXX_COMPILER_ID:MSVC>:/MP>
    # Set source and executable charsets to UTF-8. Required for building on CJK Windows.
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>

    -mavx

    )

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_subdirectory(JUCE)
# juce_set_vst2_sdk_path(${PROJECT_SOURCE_DIR}/VST_SDK2)

juce_add_plugin(Signalizer
    # VERSION ...                               # Set this if the plugin version is different to the project version
    # ICON_BIG ...                              # ICON_* arguments specify a path to an image file to use as an icon for the Standalone
    # ICON_SMALL ...
    COMPANY_NAME Lightbridge                    # Specify the name of the plugin's author
    COMPANY_WEBSITE  www.jthorborg.com
    COMPANY_EMAIL janustl@outlook.com
    IS_SYNTH FALSE                              # Is this a synth or an effect?
    NEEDS_MIDI_INPUT FALSE                      # Does the plugin need midi input?
    NEEDS_MIDI_OUTPUT FALSE                     # Does the plugin need midi output?
    IS_MIDI_EFFECT FALSE                        # Is this plugin a MIDI effect?
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE            # Does the editor need keyboard focus?
    # COPY_PLUGIN_AFTER_BUILD TRUE/FALSE        # Should the plugin be installed to a default location after building?
    PLUGIN_MANUFACTURER_CODE LbJt               # A four-character manufacturer id with at least one upper-case character
    PLUGIN_CODE Sign                            # A unique four-character plugin id with exactly one upper-case character
                                                # GarageBand 10.3 requires the first letter to be upper-case, and the remaining letters to be lower-case
    LV2URI https://tt.com
    LV2_URI https://tt.com
    FORMATS AU VST3 Standalone                  # The formats to build. Other valid formats are: AAX Unity VST AU AUv3
    PRODUCT_NAME "Signalizer")                  # The name of the final executable, which can differ from the target name

juce_generate_juce_header(Signalizer)


if (APPLE)
    message (STATUS "APPLE")
    set(VST3_PATH "${CMAKE_BINARY_DIR}/Signalizer_artefacts/${CMAKE_BUILD_TYPE}/VST3/Signalizer.vst3/Contents/Resources/")
    set(VST_PATH "${CMAKE_BINARY_DIR}/Signalizer_artefacts/${CMAKE_BUILD_TYPE}/VST/Signalizer.vst/Contents/Resources/")
    set(AU_PATH "${CMAKE_BINARY_DIR}/Signalizer_artefacts/${CMAKE_BUILD_TYPE}/VST/Signalizer.component/Contents/Resources/")
elseif(WIN32)
    message (STATUS "WIN32")
    set(VST3_PATH "${CMAKE_BINARY_DIR}/Signalizer_artefacts/${CMAKE_BUILD_TYPE}/VST3/Signalizer.vst3/Contents/${CMAKE_SYSTEM_PROCESSOR}-win/")
    set(VST_PATH "${CMAKE_BINARY_DIR}/Signalizer_artefacts/${CMAKE_BUILD_TYPE}/VST/")
elseif(UNIX)
    message (STATUS "UNIX")
    set(VST3_PATH "${CMAKE_BINARY_DIR}/Signalizer_artefacts/${CMAKE_BUILD_TYPE}/VST3/Signalizer.vst3/Contents/${CMAKE_SYSTEM_PROCESSOR}-linux/")
    set(VST_PATH "${CMAKE_BINARY_DIR}/Signalizer_artefacts/${CMAKE_BUILD_TYPE}/VST/")
endif()

if(TARGET Signalizer_VST3)
    add_custom_command(TARGET Signalizer_VST3 POST_BUILD VERBATIM
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/Make/Skeleton/ ${VST3_PATH}
    )
endif()

if(TARGET Signalizer_VST)
    add_custom_command(TARGET Signalizer_VST POST_BUILD VERBATIM
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/Make/Skeleton/ ${VST_PATH}
    )
endif()

if(TARGET Signalizer_AU)
    add_custom_command(TARGET Signalizer_AU POST_BUILD VERBATIM
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/Make/Skeleton/ ${AU_PATH}
    )
endif()

# if(CMAKE_BUILD_TYPE STREQUAL "Debug")
# else()
# endif()

add_subdirectory(Source)

target_compile_definitions(Signalizer PRIVATE
        # JUCE_WEB_BROWSER and JUCE_USE_CURL would be on by default, but you might not need them.
        JUCE_WEB_BROWSER=0  # If you remove this, add `NEEDS_WEB_BROWSER TRUE` to the `juce_add_plugin` call
        JUCE_USE_CURL=0     # If you remove this, add `NEEDS_CURL TRUE` to the `juce_add_plugin` call
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_MODAL_LOOPS_PERMITTED=1
        DONT_SET_USING_JUCE_NAMESPACE=1
        $<$<BOOL:${UNIX}>:JUCE_JACK_CLIENT_NAME=JucePlugin_Name>)

target_link_libraries(Signalizer
    PRIVATE
        # If we'd created a binary data target, we'd link to it here
        juce::juce_audio_utils
        juce::juce_opengl
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags 
)
