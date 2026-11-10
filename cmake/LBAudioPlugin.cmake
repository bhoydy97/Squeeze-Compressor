# lbaudio_add_plugin(<Name>
#     [VERSION 1.0.0]                  # defaults to LBAUDIO_VERSION (set from the release tag)
#     PLUGIN_CODE Sqz1                 # 4 characters, unique per plugin, never change after release
#     TYPE "Compressor"                # shown in the plugin header and README
#     VST3_CATEGORIES Fx Dynamics
#     SOURCES Source/PluginProcessor.cpp Source/PluginEditor.cpp
#     [MODULES juce_dsp ...]
#     [INSTRUMENT])                    # a MIDI-played instrument (synth/sampler) rather than an effect
#
# Adds the plugin (VST3 + AU), and when LBAUDIO_BUILD_TESTS is on, a <Name>Tests tool
# built from tests/<Name>Tests.cpp.
function(lbaudio_add_plugin name)
    cmake_parse_arguments(ARG "INSTRUMENT" "VERSION;PLUGIN_CODE;TYPE" "VST3_CATEGORIES;SOURCES;MODULES" ${ARGN})
    string(TOLOWER "${name}" lower)
    if(NOT ARG_VERSION)
        set(ARG_VERSION "${LBAUDIO_VERSION}")
    endif()
    if(ARG_INSTRUMENT)
        set(is_synth TRUE)
        set(au_type kAudioUnitType_MusicDevice)
    else()
        set(is_synth FALSE)
        set(au_type kAudioUnitType_Effect)
    endif()

    juce_add_plugin(${name}
        COMPANY_NAME                "LBAudio"
        COMPANY_COPYRIGHT           "Copyright (c) 2026 LBAudio. All rights reserved."
        PRODUCT_NAME                "${name}"
        VERSION                     ${ARG_VERSION}
        BUNDLE_ID                   com.lbaudio.${lower}
        PLUGIN_MANUFACTURER_CODE    LBau
        PLUGIN_CODE                 ${ARG_PLUGIN_CODE}
        FORMATS                     ${LBAUDIO_FORMATS}
        IS_SYNTH                    ${is_synth}
        NEEDS_MIDI_INPUT            ${is_synth}
        NEEDS_MIDI_OUTPUT           FALSE
        IS_MIDI_EFFECT              FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        VST3_CATEGORIES             ${ARG_VST3_CATEGORIES}
        AU_MAIN_TYPE                ${au_type}
        COPY_PLUGIN_AFTER_BUILD     ${LBAUDIO_COPY_AFTER_BUILD})

    list(TRANSFORM ARG_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
    target_sources(${name} PRIVATE ${ARG_SOURCES})
    target_compile_definitions(${name} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        LBAUDIO_PLUGIN_TYPE="${ARG_TYPE}")

    set(modules juce::juce_audio_processors juce::juce_gui_basics)
    foreach(m ${ARG_MODULES})
        list(APPEND modules juce::${m})
    endforeach()

    target_link_libraries(${name}
        PRIVATE
            lbaudio_shared
            ${modules}
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
    if(LBAUDIO_LTO)
        target_link_libraries(${name} PUBLIC juce::juce_recommended_lto_flags)
    endif()

    if(LBAUDIO_BUILD_TESTS AND EXISTS "${PROJECT_SOURCE_DIR}/tests/${name}Tests.cpp")
        juce_add_console_app(${name}Tests PRODUCT_NAME "${name}Tests")
        target_sources(${name}Tests PRIVATE "${PROJECT_SOURCE_DIR}/tests/${name}Tests.cpp" ${ARG_SOURCES})
        target_include_directories(${name}Tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source" "${PROJECT_SOURCE_DIR}/tests")
        target_compile_definitions(${name}Tests PRIVATE
            JucePlugin_Name="${name}"
            LBAUDIO_PLUGIN_TYPE="${ARG_TYPE}"
            JUCE_PLUGINHOST_VST3=1
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0)
        target_link_libraries(${name}Tests
            PRIVATE
                lbaudio_shared
                ${modules}
                juce::juce_audio_formats
            PUBLIC
                juce::juce_recommended_config_flags
                juce::juce_recommended_warning_flags)
        add_dependencies(${name}Tests ${name}_VST3)
    endif()
endfunction()
