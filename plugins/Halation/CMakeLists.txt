lbaudio_add_plugin(Halation
    PLUGIN_CODE     Hal1
    TYPE            "Multiband"
    VST3_CATEGORIES Fx Dynamics
    SOURCES         Source/PluginProcessor.cpp
                    Source/PluginEditor.cpp
    MODULES         juce_dsp)
