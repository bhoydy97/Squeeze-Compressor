# Squeeze by LBAudio — compressor plugin (VST3 / AU)

![Squeeze UI](docs/screenshot.png)

Squeeze is a clean, zero-latency feed-forward compressor by LBAudio, built with [JUCE 8](https://juce.com).
It builds as **VST3** on Windows, macOS and Linux, and also as **Audio Unit** on macOS (for Logic / GarageBand).

## Controls

| Control | Range | What it does |
|---|---|---|
| Threshold | −60 … 0 dB | Level above which compression starts |
| Ratio | 1:1 … 20:1 | How hard signal above the threshold is reduced (20:1 is close to a limiter) |
| Knee | 0 … 24 dB | Width of the soft transition around the threshold (0 = hard knee) |
| SC HPF | Off … 500 Hz | High-pass filter on the detector only, so bass doesn't trigger pumping. The audio itself is not filtered |
| Attack | 0.1 … 200 ms | How fast gain reduction kicks in |
| Release | 5 ms … 2 s | How fast it recovers |
| Makeup | −12 … +24 dB | Output gain applied to the compressed signal |
| Mix | 0 … 100 % | Dry/wet blend for parallel ("New York") compression. 0 % is bit-identical to the input |
| Auto Gain | on/off | Sets makeup automatically from threshold and ratio (replaces the Makeup knob) |
| Ext Sidechain | on/off | Detect from the plugin's sidechain input, e.g. kick ducking a bass |

The display shows the transfer curve (input vs output level) with a dot for the current input level, and IN / GR (gain reduction) / OUT meters.
Double-click any knob to reset it; click a value to type one in (`1.2 s`, `250 ms`, `off`, and so on all work).

**How it works:** a stereo-linked peak detector feeds a soft-knee gain computer. Attack and release smoothing run in the dB domain, which gives exact time constants (attack and release are the time to cover 63 % of the change). Makeup and mix are smoothed so automation doesn't click. The design follows Giannoulis, Massberg & Reiss, *Digital Dynamic Range Compressor Design* (JAES, 2012).

## Getting the plugin

### Option A: download a release
Open the repository's **Releases** page and download the zip for your system from the latest release. Installation steps are listed under each release.

Every push to `main` is also built automatically. Those builds are under the **Actions** tab, in each run's *Artifacts* section, and GitHub deletes them after 90 days.

### Option B: build it yourself
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
The plugin ends up in `build/Squeeze_artefacts/Release/VST3/Squeeze.vst3` (and `.../AU/Squeeze.component` on a Mac).

For a universal Mac binary (Apple Silicon and Intel), add `"-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"` to the first command.
Add `-DSQUEEZE_COPY_AFTER_BUILD=ON` to copy the plugin into your plugin folder automatically after each build (on Windows this needs an Administrator terminal).

## Publishing a new release

Create a release on GitHub (**Releases → Draft a new release**), type a new tag such as `v1.0.1`, and publish it.
About 10 minutes later the Windows, macOS and Linux zips are attached to it automatically.
You can also push a tag from the command line instead: `git tag v1.0.1 && git push origin v1.0.1`.
The tag's number becomes the version your DAW shows, so use the `vMAJOR.MINOR.PATCH` format.

## Installing

Copy the whole `Squeeze.vst3` folder (it's a bundle, not one file) to:

| OS | VST3 folder |
|---|---|
| Windows | `C:\Program Files\Common Files\VST3\` |
| macOS | `~/Library/Audio/Plug-Ins/VST3/` (AU: `~/Library/Audio/Plug-Ins/Components/`) |
| Linux | `~/.vst3/` |

Then rescan plugins in your DAW. Squeeze shows up under **LBAudio → Squeeze** in the Dynamics category.

**macOS note:** plugins you built yourself or downloaded from GitHub Actions aren't code-signed. If your DAW refuses to load it, run
`xattr -cr ~/Library/Audio/Plug-Ins/VST3/Squeeze.vst3` (and the same for the `.component`).

**Using the sidechain:** route a track into Squeeze's sidechain input in your DAW, then switch on **EXT SIDECHAIN**.
If the switch is on but nothing is routed, Squeeze falls back to detecting from its main input.

## Testing

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSQUEEZE_BUILD_TESTS=ON
cmake --build build --config Release --parallel
./build/SqueezeTests_artefacts/Release/SqueezeTests build/Squeeze_artefacts/Release/VST3/Squeeze.vst3 ui.png
```

The test tool loads the built VST3 the way a DAW does and checks it against the maths: the hard- and soft-knee curves, the 20:1 ratio, attack and release time constants, the mix null test, manual and auto makeup, the sidechain filter, the external sidechain, and state recall. It then renders the UI to `ui.png`.
The plugin also passes [pluginval](https://github.com/Tracktion/pluginval) at strictness level 10.

## Code layout

```
Source/CompressorDSP.h      the compressor itself: plain C++ with no JUCE dependency, so it's easy to reuse
Source/PluginProcessor.*    parameters, buses (main + sidechain), state save/load
Source/PluginEditor.*       UI layout
Source/UI.h                 look and feel, transfer-curve display, meters
tests/PluginTests.cpp       host-based test tool
```

## Website

The sales site lives in `website/`. It's plain HTML with no build step, hosted on Cloudflare Pages straight from this repo.
Prices, the Payhip product code and the support email are set in `website/assets/config.js`.
Product images for the store are in `website/assets/` (`cover-1600x900.png`, `square-1080.png`, `social-1200x630.png`).
Changes to `website/` don't trigger plugin builds.

## Mac signing

Once the Apple secrets are added to the repo (`MACOS_CERTIFICATE`, `MACOS_CERTIFICATE_PASSWORD`, `APPLE_ID`, `APPLE_APP_PASSWORD`, `APPLE_TEAM_ID`), every macOS build is signed with your Developer ID and notarized by Apple by `.github/scripts/macos-sign.sh`. Without them the step is skipped.

## Plugin identity

The name, company and IDs are set in `CMakeLists.txt`:

| Setting | Value |
|---|---|
| Product | Squeeze |
| Company | LBAudio |
| Bundle ID | `com.lbaudio.squeeze` |
| Manufacturer code | `LBau` |
| Plugin code | `Sqz1` |

DAWs recognise the plugin by its manufacturer and plugin codes. Don't change them once people own a copy, or their saved sessions won't find it.
Give any future LBAudio plugin the same manufacturer code (`LBau`) and its own new plugin code.

## Licensing

Copyright (c) 2026 LBAudio. All rights reserved.

- **JUCE** is dual-licensed: AGPLv3, or the commercial JUCE licence. To sell Squeeze as closed source you need a JUCE licence; the free Starter tier has a revenue limit. See [juce.com/get-juce](https://juce.com/get-juce).
- **Third-party notices:** every build writes `THIRD_PARTY_NOTICES.txt` into the build folder, with the licences of the code JUCE compiles into the plugin (the VST 3 SDK under MIT, HarfBuzz, libpng, zlib and others). Include it with every copy you distribute; the GitHub release zips already do.
- **VST logo:** the VST 3 SDK is MIT-licensed, but the VST name and logo are Steinberg trademarks. If you show the logo on your product page, follow the `VST3_Usage_Guidelines.pdf` that ships in the SDK, inside JUCE.
