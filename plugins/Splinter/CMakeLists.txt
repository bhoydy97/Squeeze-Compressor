lbaudio_add_plugin(Splinter
    PLUGIN_CODE     Spl1
    TYPE            "Sampler"
    VST3_CATEGORIES Instrument Sampler
    SOURCES         Source/PluginProcessor.cpp
                    Source/PluginEditor.cpp
    MODULES         juce_dsp juce_audio_formats
    INSTRUMENT)
