cmake_minimum_required(VERSION 3.22)

project(Squeeze VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
endif()

# ------------------------------------------------------------------------------
# JUCE is downloaded automatically. To use a local copy instead, configure with
#   -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.10
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# ------------------------------------------------------------------------------
set(SQUEEZE_FORMATS VST3 AU CACHE STRING "Plugin formats to build (AU is only built on macOS)")
option(SQUEEZE_COPY_AFTER_BUILD "Copy the plugin into your system plugin folder after building" OFF)
option(SQUEEZE_BUILD_TESTS "Build the test/validation tool" OFF)

juce_add_plugin(Squeeze
    COMPANY_NAME                "Squeeze Audio"
    PRODUCT_NAME                "Squeeze"
    VERSION                     ${PROJECT_VERSION}
    BUNDLE_ID                   com.squeezeaudio.squeeze
    PLUGIN_MANUFACTURER_CODE    Sqza
    PLUGIN_CODE                 Sqz1
    FORMATS                     ${SQUEEZE_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Fx Dynamics
    AU_MAIN_TYPE                kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD     ${SQUEEZE_COPY_AFTER_BUILD})

target_sources(Squeeze PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(Squeeze PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Squeeze
    PRIVATE
        juce::juce_audio_processors
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------------------
# Test tool: hosts the built VST3 and checks the DSP against theory,
# then renders the UI to a PNG.
if(SQUEEZE_BUILD_TESTS)
    juce_add_console_app(SqueezeTests PRODUCT_NAME "SqueezeTests")

    target_sources(SqueezeTests PRIVATE
        tests/PluginTests.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)

    target_compile_definitions(SqueezeTests PRIVATE
        JucePlugin_Name="Squeeze"
        JUCE_PLUGINHOST_VST3=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)

    target_link_libraries(SqueezeTests
        PRIVATE
            juce::juce_audio_processors
            juce::juce_gui_basics
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    add_dependencies(SqueezeTests Squeeze_VST3)
endif()
