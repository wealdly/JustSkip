# JustSkip

**Game speed control for Crimson Desert**

Skip through slow animations, long cutscenes, tedious crafting, and endless travel with customizable hotkeys. Toggle or hold to speed up — release to return to normal.

## Features

- **Toggle speeds** — press once to lock a speed, press again to return to 1x
- **Hold speed** — game runs fast only while you hold the key, releases back to normal
- **Mutually exclusive toggles** — activating one toggle deactivates the others
- **Hold overrides toggle** — hold key takes priority, returns to your active toggle on release
- **Hot-reload** — edit the INI while playing and press `F6` to apply changes instantly
- **Fully configurable** — change any hotkey, speed value, or add up to 6 slots via the INI
- **Patch-proof** — hooks a Windows API (QueryPerformanceCounter), not game code. Survives game updates.
- **No Cheat Engine required** — standalone ASI plugin, lightweight (~24 KB)

## Default Hotkeys

| Key | Mode | Speed |
|-----|------|-------|
| F6 | — | Reload INI config |
| F7 | Toggle | 1.2x (subtle everyday speedup) |
| F8 | Toggle | 2.0x (travel, looting) |
| F9 | Toggle | 4.0x (cutscenes, crafting) |
| F10 | Hold | 12.0x (blast through anything) |

## Requirements

**[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases)** — required for loading ASI plugins.

If you don't already have `winmm.dll` in your `bin64/` folder:

1. Go to the [latest release](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/latest) (v9.7.0 or newer)
2. Download **`Ultimate-ASI-Loader_x64.zip`** (the x64 version — Crimson Desert is 64-bit)
3. Extract it — you'll get a single DLL (e.g. `dinput8.dll`)
4. **Rename** it to `winmm.dll`
5. Place `winmm.dll` into your Crimson Desert `bin64/` folder

> **Tip:** If another mod already placed a `winmm.dll` there, you're good — skip this step.

## Installation (recommended)

1. Download `JustSkip-v1.0-Manual.zip`
2. Extract both files into your Crimson Desert `bin64/` folder:
   - `JustSkip.asi`
   - `JustSkip.ini`

That's it. Launch the game.

### Alternative: JSON Mod Manager

> **Note:** JSON Mod Manager can be buggy. Manual install above is recommended.

Requires [JSON Mod Manager](https://www.nexusmods.com/crimsondesert/mods/113) by PhorgeForge.

1. Download `JustSkip-v1.0-CDMM.zip`
2. Extract the `_asi` folder into your JSON Mod Manager `mods/` directory
3. Open JSON Mod Manager — JustSkip will appear in the ASI mods list
4. Enable it and click Apply

## Configuration

Edit `JustSkip.ini` to customize. All values can be changed without restarting the game — press your reload key (default `F6`) to apply.

### Adding a custom speed

Uncomment one of the unused `[Speed5]` / `[Speed6]` sections and set your values:

```ini
[Speed5]
Hotkey=74
Hold=0
Speed=1.5
```

### Key reference

```
F1=70  F2=71  F3=72  F4=73  F5=74  F6=75
F7=76  F8=77  F9=78  F10=79 F11=7A F12=7B
```

See the INI file for a full key code reference.

## Troubleshooting

Set `DebugLog=1` in the INI to generate `JustSkip.log` next to the ASI. Check that:
- The ASI loader is present (`winmm.dll` or `version.dll` in `bin64/`)
- The log shows "QPC hook installed"
- The game is not blocking ASI loading

## Building from Source

Requires CMake 3.20+ and Visual Studio 2022 (or Build Tools).

```
cmake -B build -A x64
cmake --build build --config Release
```

Output: `build/bin/Release/JustSkip.asi` + `JustSkip.ini`

## License

MIT License — see [LICENSE](LICENSE)

## Credits

- [MinHook](https://github.com/TsudaKageyu/minhook) by TsudaKageyu (BSD-2-Clause)
