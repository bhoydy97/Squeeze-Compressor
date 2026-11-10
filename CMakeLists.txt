cmake_minimum_required(VERSION 3.22)

project(LBAudio VERSION 1.2.1 LANGUAGES C CXX)

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
set(LBAUDIO_VERSION "${PROJECT_VERSION}" CACHE STRING "Version the DAW shows for every plugin (CI sets it from the release tag)")
set(LBAUDIO_FORMATS VST3 AU CACHE STRING "Plugin formats to build (AU is only built on macOS)")
set(LBAUDIO_PLUGINS Squeeze Carve Ember Haze Ceiling Drift Hush Spread Snap Orbit Halation Splinter CACHE STRING "Which plugins to build")
option(LBAUDIO_COPY_AFTER_BUILD "Copy plugins into your system plugin folders after building" OFF)
option(LBAUDIO_BUILD_TESTS "Build the test tools" OFF)
option(LBAUDIO_LTO "Link-time optimisation for release builds (slower to build)" ON)

# Shared look and helpers (header-only)
add_library(lbaudio_shared INTERFACE)
target_include_directories(lbaudio_shared INTERFACE "${PROJECT_SOURCE_DIR}/shared")

include(cmake/LBAudioPlugin.cmake)

foreach(plugin ${LBAUDIO_PLUGINS})
    add_subdirectory(plugins/${plugin})
endforeach()
# The plugins in this build, one a line: CI tests and packages these (a plugin still in the works can have a
# folder under plugins/ without being in the list)
list(JOIN LBAUDIO_PLUGINS "\n" lbaudio_plugin_lines)
file(WRITE "${CMAKE_BINARY_DIR}/plugins.txt" "${lbaudio_plugin_lines}\n")

# ------------------------------------------------------------------------------
# Licence notices for the third-party code compiled into the plugins.
# Generated from the JUCE version in use; ship it with every copy you distribute
# (the GitHub release zips include it automatically).
set(LBAUDIO_NOTICES "${CMAKE_BINARY_DIR}/THIRD_PARTY_NOTICES.txt")
file(WRITE "${LBAUDIO_NOTICES}"
    "LBAudio plugins\n"
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
    file(APPEND "${LBAUDIO_NOTICES}"
        "\n==============================================================================\n"
        "${notice_name}\n"
        "==============================================================================\n\n"
        "${notice_text}\n")
endforeach()

# ------------------------------------------------------------------------------
# Renders audio files through any built VST3 (used for the website's demos)
if(LBAUDIO_BUILD_TESTS)
    juce_add_console_app(LBAudioRender PRODUCT_NAME "LBAudioRender")
    target_sources(LBAudioRender PRIVATE tests/RenderDemo.cpp)
    target_compile_definitions(LBAudioRender PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(LBAudioRender
        PRIVATE
            juce::juce_audio_formats
            juce::juce_audio_processors
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()
