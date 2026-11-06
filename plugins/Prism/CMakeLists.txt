lbaudio_add_plugin(Prism
    PLUGIN_CODE     Prm1
    TYPE            "Multiband"
    VST3_CATEGORIES Fx Dynamics
    SOURCES         Source/PluginProcessor.cpp
                    Source/PluginEditor.cpp
    MODULES         juce_dsp)
