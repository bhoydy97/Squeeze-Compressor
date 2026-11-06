lbaudio_add_plugin(Hush
    PLUGIN_CODE     Hsh1
    TYPE            "De-esser"
    VST3_CATEGORIES Fx Dynamics
    SOURCES         Source/PluginProcessor.cpp
                    Source/PluginEditor.cpp
    MODULES         juce_dsp)
