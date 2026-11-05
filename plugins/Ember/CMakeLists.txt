lbaudio_add_plugin(Ember
    PLUGIN_CODE     Emb1
    TYPE            "Saturator"
    VST3_CATEGORIES Fx Distortion
    SOURCES         Source/PluginProcessor.cpp
                    Source/PluginEditor.cpp
    MODULES         juce_dsp)
