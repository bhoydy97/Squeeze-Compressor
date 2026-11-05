lbaudio_add_plugin(Carve
    PLUGIN_CODE     Crv1
    TYPE            "Equaliser"
    VST3_CATEGORIES Fx EQ
    SOURCES         Source/PluginProcessor.cpp
                    Source/PluginEditor.cpp
    MODULES         juce_dsp)
