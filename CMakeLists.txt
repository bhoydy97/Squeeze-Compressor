cmake_minimum_required(VERSION 3.22)

# The version your DAW shows. Release builds set this from the git tag (v1.2.3 -> 1.2.3).
set(SQUEEZE_VERSION "1.0.0" CACHE STRING "Plugin version (major.minor.patch)")

project(Squeeze VERSION ${SQUEEZE_VERSION} LANGUAGES C CXX)

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
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# ------------------------------------------------------------------------------
set(SQUEEZE_FORMATS VST3 AU CACHE STRING "Plugin formats to build (AU is only built on macOS)")
option(SQUEEZE_COPY_AFTER_BUILD "Copy the plugin into your system plugin folder after building" OFF)
option(SQUEEZE_BUILD_TESTS "Build the test/validation tool" OFF)

juce_add_plugin(Squeeze
    COMPANY_NAME                "LBAudio"
    PRODUCT_NAME                "Squeeze"
    VERSION                     ${PROJECT_VERSION}
    BUNDLE_ID                   com.lbaudio.squeeze
    PLUGIN_MANUFACTURER_CODE    LBau
    PLUGIN_CODE                 Sqz1
    FORMATS                     ${SQUEEZE_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Fx Dynamics
    AU_MAIN_TYPE                kAudioUnitType_Effect
    COMPANY_COPYRIGHT           "Copyright (c) 2026 LBAudio. All rights reserved."
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
# Licence notices for the third-party code compiled into the plugin.
# Generated from the JUCE version in use; ship it with every copy you distribute
# (the GitHub release zips include it automatically).
set(SQUEEZE_NOTICES "${CMAKE_BINARY_DIR}/THIRD_PARTY_NOTICES.txt")
file(WRITE "${SQUEEZE_NOTICES}"
    "Squeeze by LBAudio\n"
    "Copyright (c) 2026 LBAudio. All rights reserved.\n\n"
    "This product includes third-party software, used under the licences below.\n\n"
    "JUCE (https://juce.com) is used under the JUCE licence.\n\n"
    "This software is based in part on the work of the Independent JPEG Group.\n")
foreach(entry
        "VST 3 SDK (Steinberg Media Technologies GmbH)|juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt"
        "Audio Unit SDK (Apple Inc.)|juce_audio_plugin_client/AU/AudioUnitSDK/LICENSE.txt"
        "HarfBuzz|juce_graphics/fonts/harfbuzz/COPYING"
        "SheenBidi|juce_graphics/unicode/sheenbidi/LICENSE"
        "libpng|juce_graphics/image_formats/pnglib/LICENSE"
        "zlib|juce_core/zip/zlib/README")
    string(REPLACE "|" ";" parts "${entry}")
    list(GET parts 0 notice_name)
    list(GET parts 1 notice_file)
    file(READ "${juce_SOURCE_DIR}/modules/${notice_file}" notice_text)
    file(APPEND "${SQUEEZE_NOTICES}"
        "\n==============================================================================\n"
        "${notice_name}\n"
        "==============================================================================\n\n"
        "${notice_text}\n")
endforeach()

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

    # Renders audio files through the built VST3 (used for the website's demos)
    juce_add_console_app(SqueezeRender PRODUCT_NAME "SqueezeRender")
    target_sources(SqueezeRender PRIVATE tests/RenderDemo.cpp)
    target_compile_definitions(SqueezeRender PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(SqueezeRender
        PRIVATE
            juce::juce_audio_formats
            juce::juce_audio_processors
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
    add_dependencies(SqueezeRender Squeeze_VST3)
endif()
