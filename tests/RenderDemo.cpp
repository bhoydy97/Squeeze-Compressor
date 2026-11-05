// LBAudioRender: runs an audio file through a built LBAudio VST3, exactly as a DAW would.
//
//   LBAudioRender <Plugin.vst3> <in.wav> <out.wav> "Threshold=-20" "Ratio=4" ...
//
// Parameters are set by name using the same text a user would type into the plugin; switches take
// on/off. The plugin's reported latency is compensated, as a DAW does, so the output lines up with
// the input sample for sample. Output is 32-bit float WAV.

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc < 4)
    {
        std::cerr << "usage: LBAudioRender <Plugin.vst3> <in.wav> <out.wav> [Name=Value ...]" << std::endl;
        return 2;
    }

    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const auto pluginFile = cwd.getChildFile (argv[1]);
    const auto inFile = cwd.getChildFile (argv[2]);
    const auto outFile = cwd.getChildFile (argv[3]);

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (inFile));
    if (reader == nullptr) { std::cerr << "cannot read " << inFile.getFullPathName() << std::endl; return 1; }

    const double sampleRate = reader->sampleRate;
    const int numChannels = juce::jlimit (1, 2, (int) reader->numChannels);
    const int numSamples = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> audio (numChannels, numSamples);
    reader->read (&audio, 0, numSamples, 0, true, numChannels > 1);

    juce::VST3PluginFormat vst3;
    juce::OwnedArray<juce::PluginDescription> types;
    vst3.findAllTypesForFile (types, pluginFile.getFullPathName());
    if (types.isEmpty()) { std::cerr << "no plugin in " << pluginFile.getFullPathName() << std::endl; return 1; }

    constexpr int blockSize = 512;
    juce::String error;
    auto plugin = vst3.createInstanceFromDescription (*types[0], sampleRate, blockSize, error);
    if (plugin == nullptr) { std::cerr << "load failed: " << error << std::endl; return 1; }

    // Main bus in the file's channel layout; sidechain bus left at its default (fed silence).
    auto layout = plugin->getBusesLayout();
    const auto set = numChannels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    layout.getMainInputChannelSet() = set;
    layout.getMainOutputChannelSet() = set;
    plugin->setBusesLayout (layout);

    auto findParam = [&] (const juce::String& name) -> juce::AudioProcessorParameter*
    {
        for (auto* p : plugin->getParameters())
            if (p->getName (64).equalsIgnoreCase (name)) return p;
        return nullptr;
    };

    for (int i = 4; i < argc; ++i)
    {
        const juce::String arg (argv[i]);
        const auto name = arg.upToFirstOccurrenceOf ("=", false, false).trim();
        const auto value = arg.fromFirstOccurrenceOf ("=", false, false).trim();
        auto* p = findParam (name);
        if (p == nullptr) { std::cerr << "unknown parameter: " << name << std::endl; return 1; }
        const bool isSwitch = value.equalsIgnoreCase ("on") || value.equalsIgnoreCase ("off");
        p->setValueNotifyingHost (isSwitch ? (value.equalsIgnoreCase ("on") ? 1.0f : 0.0f) : p->getValueForText (value));
    }

    plugin->setRateAndBufferSizeDetails (sampleRate, blockSize);
    plugin->prepareToPlay (sampleRate, blockSize);

    const int bufferChannels = juce::jmax (plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels());
    juce::AudioBuffer<float> block (bufferChannels, blockSize);
    juce::MidiBuffer midi;

    // One silent block first so the parameter changes reach the plugin, then a clean start.
    block.clear();
    plugin->processBlock (block, midi);
    plugin->reset();

    // Run 'latency' extra samples of silence through, then drop the first 'latency' samples of output
    const int latency = juce::jmax (0, plugin->getLatencySamples());
    const int total = numSamples + latency;
    juce::AudioBuffer<float> output (numChannels, total);
    for (int start = 0; start < total; start += blockSize)
    {
        const int n = juce::jmin (blockSize, total - start);
        block.setSize (bufferChannels, n, false, false, true);
        block.clear();
        const int fromInput = juce::jlimit (0, n, numSamples - start);
        for (int ch = 0; ch < numChannels; ++ch)
            if (fromInput > 0) block.copyFrom (ch, 0, audio, ch, start, fromInput);
        plugin->processBlock (block, midi);
        for (int ch = 0; ch < numChannels; ++ch)
            output.copyFrom (ch, start, block, ch, 0, n);
    }
    std::cout << "latency: " << latency << " samples (compensated)" << std::endl;
    plugin->releaseResources();

    std::cout << "settings:";
    for (auto* p : plugin->getParameters())
        std::cout << " " << p->getName (32) << "=" << p->getText (p->getValue(), 32) << ";";
    std::cout << std::endl;

    outFile.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (outFile);
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                   .withSampleRate (sampleRate)
                                                   .withNumChannels (numChannels)
                                                   .withBitsPerSample (32)
                                                   .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    if (writer == nullptr) { std::cerr << "cannot write " << outFile.getFullPathName() << std::endl; return 1; }
    juce::AudioBuffer<float> aligned (output.getArrayOfWritePointers(), numChannels, latency, numSamples);
    writer->writeFromAudioSampleBuffer (aligned, 0, numSamples);
    std::cout << "wrote " << outFile.getFullPathName() << std::endl;
    return 0;
}
