Download the zip for your system from **Assets** below, unzip it, and copy the plugin into your plugin folder:

| System | File | Copy to |
|---|---|---|
| Windows | `Squeeze-…-Windows-VST3.zip` | `C:\Program Files\Common Files\VST3\` |
| macOS (Apple Silicon and Intel) | `Squeeze-…-macOS-VST3-AU.zip` | `Squeeze.vst3` → `~/Library/Audio/Plug-Ins/VST3/`<br>`Squeeze.component` → `~/Library/Audio/Plug-Ins/Components/` (Logic/GarageBand) |
| Linux | `Squeeze-…-Linux-VST3.zip` | `~/.vst3/` |

Copy the whole `Squeeze.vst3` folder, then rescan plugins in your DAW. Squeeze appears under **Squeeze Audio** in the Dynamics category.

**macOS:** the plugin isn't code-signed. If your DAW won't load it, open Terminal and run:

```
xattr -cr ~/Library/Audio/Plug-Ins/VST3/Squeeze.vst3 ~/Library/Audio/Plug-Ins/Components/Squeeze.component
```
