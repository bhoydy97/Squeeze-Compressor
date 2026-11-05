# LBAudio plugins

Five mixing and mastering plugins by LBAudio, built with [JUCE 8](https://juce.com).
Each builds as **VST3** on Windows, macOS and Linux, and also as **Audio Unit** on macOS (for Logic / GarageBand).

| | Plugin | What it is |
|---|---|---|
| ![](docs/squeeze.png) | **Squeeze** | Clean, zero-latency compressor with sidechain, parallel mix and auto makeup |
| ![](docs/carve.png) | **Carve** | 7-band EQ with a live spectrum analyser you can drag bands around on |
| ![](docs/ember.png) | **Ember** | Saturator: tape, tube and clip characters, 16x oversampled |
| ![](docs/haze.png) | **Haze** | Algorithmic reverb with an exact decay time and a display of how it dies away |
| ![](docs/ceiling.png) | **Ceiling** | True-peak limiter with LUFS loudness metering |

All of them: double-click a knob to reset it; click a value to type one in (`1.2 s`, `250 ms`, `4k`, `off` all work).

## The plugins

### Squeeze (compressor)

| Control | Range | What it does |
|---|---|---|
| Threshold | −60 … 0 dB | Level above which compression starts |
| Ratio | 1:1 … 20:1 | How hard signal above the threshold is reduced |
| Knee | 0 … 24 dB | Width of the soft transition around the threshold (0 = hard knee) |
| SC HPF | Off … 500 Hz | High-pass on the detector only, so bass doesn't cause pumping |
| Attack / Release | 0.1 … 200 ms / 5 ms … 2 s | How fast gain reduction starts and recovers |
| Makeup | −12 … +24 dB | Output gain (or Auto Gain sets it from threshold and ratio) |
| Mix | 0 … 100 % | Parallel compression. 0 % is bit-identical to the input |
| Ext Sidechain | on/off | Detect from the sidechain input, e.g. kick ducking a bass |

A stereo-linked peak detector feeds a soft-knee gain computer with attack and release smoothed in the dB domain (Giannoulis, Massberg & Reiss, JAES 2012). Zero latency.

### Carve (EQ)

Seven bands: high-pass, low shelf, three bells, high shelf, low-pass, plus Output (±18 dB).

| Band | Frequency | Gain | Also |
|---|---|---|---|
| High-pass / Low-pass | 10 Hz – 1 kHz / 1 – 22 kHz | | On/off; slope 12, 24 or 48 dB/oct |
| Low shelf / High shelf | 20 Hz – 1 kHz / 1 – 20 kHz | ±18 dB | |
| Bell 1–3 | 20 Hz – 20 kHz | ±18 dB | Q 0.1 – 18 |

Drag a node to set frequency and gain, scroll to change Q (or the slope of the pass filters), double-click to reset gain or switch a pass filter on and off. The curve drawn is computed from the same filters that process the audio, behind a live spectrum of the signal. Parameters glide in the log domain, so automation is smooth. Zero latency.

### Ember (saturator)

| Control | Range | What it does |
|---|---|---|
| Character | Tape / Tube / Clip | Tape: smooth, odd harmonics. Tube: asymmetric, adds even harmonics. Clip: firm knee |
| Drive | 0 … 36 dB | How hard the signal hits the curve |
| Tone | ±6 dB | Tilt EQ around 1 kHz, after the saturation |
| Mix | 0 … 100 % | Parallel saturation (the dry path is delayed to stay in phase) |
| Output | −24 … +12 dB | Output level |
| Auto Gain | on/off | Keeps the level about the same as you change Drive |

The curve runs at 16x the sample rate, so aliasing stays below −90 dB even for a hard-driven 9 kHz tone. Latency: 67 samples, reported to the DAW.

### Haze (reverb)

| Control | Range | What it does |
|---|---|---|
| Pre-delay | 0 … 200 ms | Gap before the reverb starts |
| Size | 0 … 100 % | How big the space is |
| Decay | 0.2 … 20 s | Time for the tail to fall 60 dB (RT60) |
| Modulation | 0 … 100 % | Slow movement in the tail, which smooths out metallic ringing |
| Damping | 1 … 20 kHz | Above this frequency the tail dies away four times faster |
| Low Cut | 20 Hz … 1 kHz | Keeps low end out of the reverb |
| Width | 0 … 200 % | Stereo spread of the reverb (0 % = mono) |
| Mix | 0 … 100 % | Dry/wet balance (equal-power). 0 % is bit-identical to the input |

An 8-line feedback delay network with a 4-step diffuser. Each line's gain is set from its length, so the measured decay time matches the knob (within 3 %) at every size. Damping is a shelf in each loop, so it sounds the same at every size and decay. Zero latency; mono in, stereo out works.

### Ceiling (limiter)

| Control | Range | What it does |
|---|---|---|
| Gain | 0 … 24 dB | Pushes the level into the limiter |
| Ceiling | −12 … 0 dB | The output never goes above this |
| Release | 1 ms … 1 s | How quickly the level recovers after a peak |
| True Peak | on/off | Also catches peaks between samples, which clip in converters and MP3/AAC encoders |

Readouts: momentary, short-term and integrated loudness (LUFS, ITU-R BS.1770-4 / EBU R128 with gating) and the true-peak maximum (dBTP); **Reset** restarts them. The graph shows the level going in and coming out over the last few seconds, with the gain reduction in amber.

2 ms lookahead: the gain is held and smoothed so it has fully arrived before each peak, with no clipping involved. True-peak detection takes the larger of an 8x interpolator and the BS.1770 meter filter, so standard true-peak meters read at or under the ceiling. Latency: 2 ms plus 16 samples (111 samples at 48 kHz), reported to the DAW.

## Getting the plugins

### Option A: download a release
Open the repository's **Releases** page. Each release has a zip per plugin per system, plus `LBAudio-Bundle-…` zips with all five. Installation steps are listed under each release and in the `INSTALL.txt` inside every zip.

### Option B: build them yourself
You need **CMake 3.22+**, **Git** and a C++17 compiler:
- **Windows:** Visual Studio 2022 (Community is free) with the *Desktop development with C++* workload
- **macOS:** Xcode, or its command line tools (`xcode-select --install`)
- **Linux:** GCC or Clang plus
  `sudo apt install libasound2-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev libfreetype-dev libfontconfig1-dev libgl1-mesa-dev`

Then, from this folder:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

JUCE is downloaded automatically the first time you configure, which takes a minute or two.
Each plugin ends up in `build/plugins/<Name>/<Name>_artefacts/Release/VST3/<Name>.vst3` (and `…/AU/<Name>.component` on a Mac).

Options for the first command:

| Option | Effect |
|---|---|
| `"-DLBAUDIO_PLUGINS=Haze;Ceiling"` | Build only some of the plugins |
| `-DLBAUDIO_COPY_AFTER_BUILD=ON` | Copy the plugins into your plugin folders after each build (Windows: needs an Administrator terminal) |
| `"-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"` | Universal Mac binary (Apple Silicon and Intel) |
| `-DLBAUDIO_LTO=OFF` | Skip link-time optimisation: quicker builds while developing |
| `-DLBAUDIO_VERSION=1.2.0` | Version shown in DAWs (releases set this from the tag) |

## Publishing a new release

Create a release on GitHub (**Releases → Draft a new release**), type a new tag such as `v1.1.0`, and publish it.
About 15 minutes later, 18 zips are attached to it automatically: each of the five plugins for Windows, macOS and Linux, plus the bundle for each system.
The tag's number becomes the version every plugin shows in your DAW, so use the `vMAJOR.MINOR.PATCH` format.

Every push to `main` builds the plugins on Linux and runs all the tests and pluginval (see the **Actions** tab). The Windows and Mac builds only run for release tags, or when you start the workflow by hand (**Actions → Build plugins → Run workflow**), which keeps GitHub Actions minutes low. A hand-started run puts the zips in that run's *Artifacts* section.

## Installing

Copy the whole `.vst3` folder (it's a bundle, not one file) to:

| OS | VST3 folder |
|---|---|
| Windows | `C:\Program Files\Common Files\VST3\` |
| macOS | `~/Library/Audio/Plug-Ins/VST3/` (AU: `~/Library/Audio/Plug-Ins/Components/`) |
| Linux | `~/.vst3/` |

Then rescan plugins in your DAW. They show up under **LBAudio**.

**macOS note:** until the Mac signing secrets are set up, the plugins aren't code-signed. If your DAW refuses to load them, run
`xattr -cr ~/Library/Audio/Plug-Ins/VST3 ~/Library/Audio/Plug-Ins/Components`.

## Testing

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DLBAUDIO_BUILD_TESTS=ON
cmake --build build --config Release --parallel
./build/plugins/Haze/HazeTests_artefacts/Release/HazeTests build/plugins/Haze/Haze_artefacts/Release/VST3/Haze.vst3 haze.png
```

Each plugin has a test tool that loads the built VST3 the way a DAW does, then runs the same checks again on the plugin's own code, measuring the output against the maths. It finishes by rendering the UI to the PNG you name.

- **Squeeze:** knee curves, ratio, attack/release time constants, mix null, makeup, sidechain filter, external sidechain, state recall
- **Carve:** transparency at 0 dB, the full curve against the filter maths at 14 frequencies, pass-filter slopes, no zipper noise on fast moves, state recall
- **Ember:** latency alignment, bit-exact dry mix, auto gain per character, harmonics per character, aliasing (−90 dB), tone tilt
- **Haze:** RT60 against the Decay knob at three sizes, damping per band, pre-delay timing, low cut, stereo width, stability at 20 s decay, click-free Size changes
- **Ceiling:** sample and true-peak ceilings on hard-driven signals (checked with an independent 16x meter), bit-exact below the ceiling, release time constants, no distortion on steady tones, loudness against EBU Tech 3341

All five also pass [pluginval](https://github.com/Tracktion/pluginval) at strictness level 10.

## Code layout

```
plugins/<Name>/Source/   each plugin: its DSP (plain C++, no JUCE), processor (parameters, state) and editor (UI)
plugins/<Name>/CMakeLists.txt   the plugin's name, type, category and 4-letter code
shared/LBAudio/          shared by all plugins: look and feel, knobs and meters (UI.h), parameter helpers (Params.h),
                         filters (Biquad.h), spectrum buffer (Scope.h), loudness meter (Loudness.h)
cmake/LBAudioPlugin.cmake   lbaudio_add_plugin(): the settings every LBAudio plugin shares
tests/                   Harness.h (host + measurement tools) and a <Name>Tests.cpp per plugin
packaging/               INSTALL.txt for each system, added to every zip
website/                 the sales site
```

### Adding a plugin

1. Copy one plugin's folder in `plugins/` and rename it and its classes.
2. In its `CMakeLists.txt`, set the name, type, VST3 category and a **new** 4-character plugin code.
3. Add the name to `LBAUDIO_PLUGINS` in the root `CMakeLists.txt`, and write `tests/<Name>Tests.cpp`.

The build, the tests on every push, packaging and Mac signing all pick it up automatically.

## Website

The sales site lives in `website/`. It's plain HTML with no build step, hosted on Cloudflare Pages straight from this repo.
Prices, Payhip product codes and the support email are set in `website/assets/config.js`.
Changes to `website/` don't trigger plugin builds.

## Mac signing

Once the Apple secrets are added to the repo (`MACOS_CERTIFICATE`, `MACOS_CERTIFICATE_PASSWORD`, `APPLE_ID`, `APPLE_APP_PASSWORD`, `APPLE_TEAM_ID`), every plugin in the macOS build is signed with your Developer ID and notarized by Apple (`.github/scripts/macos-sign.sh`). Without them the step is skipped.

## Plugin identity

All plugins share company **LBAudio**, manufacturer code `LBau`, and bundle IDs `com.lbaudio.<name>`.

| Plugin | Code | VST3 category |
|---|---|---|
| Squeeze | `Sqz1` | Fx / Dynamics |
| Carve | `Crv1` | Fx / EQ |
| Ember | `Emb1` | Fx / Distortion |
| Haze | `Hze1` | Fx / Reverb |
| Ceiling | `Cei1` | Fx / Dynamics / Mastering |

DAWs recognise a plugin by its manufacturer and plugin codes. Don't change them once people own a copy, or their saved sessions won't find it.

## Licensing

Copyright (c) 2026 LBAudio. All rights reserved.

- **JUCE** is dual-licensed: AGPLv3, or the commercial JUCE licence. To sell these plugins as closed source you need a JUCE licence; the free Starter tier has a revenue limit. See [juce.com/get-juce](https://juce.com/get-juce).
- **Third-party notices:** every build writes `THIRD_PARTY_NOTICES.txt` into the build folder, with the licences of the code JUCE compiles into the plugins (the VST 3 SDK under MIT, HarfBuzz, libpng, zlib and others). Every release zip includes it.
- **VST logo:** the VST 3 SDK is MIT-licensed, but the VST name and logo are Steinberg trademarks. If you show the logo on a product page, follow the `VST3_Usage_Guidelines.pdf` that ships in the SDK, inside JUCE.
