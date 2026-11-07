**LBAudio plugins**: Squeeze (compressor), Carve (EQ), Ember (saturator), Haze (reverb), Ceiling (limiter), Drift (delay), Hush (de-esser), Spread (stereo width), Snap (transient shaper), Orbit (chorus) and Halation (multiband compressor and exciter).

Download the zip for your system from **Assets** below. Each plugin has its own zip. The bundle zips: `LBAudio-Core5-Bundle-…` (Squeeze, Carve, Ember, Haze, Ceiling), `LBAudio-FX5-Bundle-…` (Drift, Hush, Spread, Snap, Orbit) and `LBAudio-Complete-Bundle-…` (the core 5 and the FX 5). Halation has its own zip only.

| System | Files | Copy to |
|---|---|---|
| Windows | `…-Windows-VST3.zip` | `C:\Program Files\Common Files\VST3\` |
| macOS (Apple Silicon and Intel) | `…-macOS-VST3-AU.zip` | `.vst3` → `~/Library/Audio/Plug-Ins/VST3/`<br>`.component` → `~/Library/Audio/Plug-Ins/Components/` (Logic/GarageBand) |
| Linux | `…-Linux-VST3.zip` | `~/.vst3/` |

Copy the whole `.vst3` folder, then rescan plugins in your DAW. The plugins appear under **LBAudio**. Every zip includes an `INSTALL.txt` with these steps.

**macOS:** if the plugins aren't code-signed yet and your DAW won't load them, open Terminal and run:

```
xattr -cr ~/Library/Audio/Plug-Ins/VST3 ~/Library/Audio/Plug-Ins/Components
```
