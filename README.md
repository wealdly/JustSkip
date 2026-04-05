# JustSkip — Game Speed Control for Crimson Desert

Toggle or hold a hotkey to speed up the game. Fully configurable, supports keyboard and XInput gamepad. No Cheat Engine required.

## Features

- **Toggle speeds** — press to lock a speed, press again for 1x (mutually exclusive)
- **Hold speed** — fast only while held, overrides any active toggle
- **Hot-reload** — edit the INI mid-game, press F6 to apply
- **Gamepad** — XInput with Back/View + face button combos (bypasses Steam Input)
- **Combat detection** — optional multi-signal fusion resets speed during fights
- **Patch-proof** — hooks QueryPerformanceCounter, not game code — survives updates
- **Button suppression** — gamepad modifier and speed buttons are hidden from the game, preventing accidental zoom/menu activation
- **Frame generation compatible** — QPC bypass for OptiScaler, FSR-FG, and DLSS-G keeps frame gen running during speed changes

## Default Hotkeys

| Keyboard | Gamepad | Mode | Speed |
|----------|---------|------|-------|
| F6 | — | — | Reload config |
| F7 | Back + X | Toggle | 1.2x |
| F8 | — | Toggle | 2x |
| F9 | Back + A | Toggle | 4x |
| F10 | — | Hold | 8x |

XInput is read directly from System32, bypassing Steam Input entirely.

## Installation

### Step 1 — ASI Loader

Already have an ASI loader (e.g. from another mod)? Skip to Step 2.

1. Download **Ultimate-ASI-Loader_x64.zip** (must be x64) from the [latest release](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/latest)
2. Rename the extracted DLL to `winmm.dll`
3. Place it in your Crimson Desert `bin64/` folder

### Step 2 — JustSkip

1. Extract `JustSkip.asi` and `JustSkip.ini` into `bin64/`
2. Launch the game — press F8 to verify

### Mod Manager (alternative)

You still need an ASI loader from Step 1 above.

**CDUMM** (recommended mod manager):
1. Download `JustSkip-vX.X-CDUMM.zip`
2. Drag and drop the zip onto the [CDUMM](https://www.nexusmods.com/crimsondesert/mods/207) window
3. JustSkip appears in the ASI Plugins tab — enable and apply

**CDMM** (JSON Mod Manager):
1. Download `JustSkip-vX.X-CDMM.zip`
2. Extract the `_asi` folder into your CDMM `mods/` directory
3. Open CDMM, enable JustSkip, click Apply

## Configuration

Edit `JustSkip.ini` to customize hotkeys, speeds, toggle/hold modes, and gamepad bindings. Up to 6 slots. All changes apply on hot-reload (F6).

**Speed slot example:**
```ini
[Speed1]
Hotkey=76           ; F7
GamepadButton=4000  ; X
Hold=0              ; 0 = toggle, 1 = hold
Speed=1.2
```

**Gamepad:**
```ini
[Settings]
GamepadEnabled=1           ; 0 = keyboard only (skips XInput entirely)
GamepadModifier=0020       ; Back/View (hold first). 0000 = no modifier (direct presses)
GamepadIndex=0             ; Controller 0-3
SuppressButtons=1          ; Hide modifier+speed buttons from game (prevents zoom toggle)
```

Set `GamepadEnabled=0` to disable all gamepad functionality (no XInput loading or polling).
Set a slot's `GamepadButton=0000` to disable its gamepad binding.
Set `GamepadModifier=0000` for direct button presses with no modifier required.

## Combat Detection (optional)

Detects combat and overrides speed using three independent signals (any one triggers):

- **Strong vibration** — melee hits, getting hit, parries
- **LT + attack button** — lock-on combat engagement
- **Rapid attack mashing** — 3+ presses within 2 seconds

```ini
[Combat]
Enabled=0
CombatSpeed=1.0       ; 1.0 = normal, 0.95 = subtle slow-mo
Timeout=3000          ; ms after last signal to exit combat
VibrationThreshold=5000
AttackButtons=C200    ; X + Y + RB
MashCount=3
MashWindow=2000
```

If the community discovers the game's in-memory combat flag, paste an AOB pattern for perfect detection:

```ini
SignaturePattern=48 8B 05 ?? ?? ?? ?? 80 78 1A 01
SignatureOffset=3
```

## Compatibility

- **OptiScaler / FSR Frame Generation** — fully compatible. JustSkip detects frame-generation DLLs at startup and returns unscaled QPC values to them, so frame gen stays active during speed changes.
- **DLSS-G / Streamline** — same bypass applies to `sl.dlss_g.dll` and `sl.interposer.dll`.
- **Other ASI mods** — no conflicts; uses standard MinHook hooking.

## Troubleshooting

Set `DebugLog=1` in the INI to generate `JustSkip.log`.

**Basic checks:**
- An ASI loader is present in `bin64/` (e.g. `winmm.dll`)
- Log shows `QPC hook installed`

**Gamepad not working:**
- Log shows `XInput: using xinput1_4.dll` (or `1_3` / `9_1_0`) — confirms DLL found
- Log shows `Gamepad connected` — confirms controller is seen via the hook
- Log shows `Modifier pressed` when you hold Back/View — confirms button state reaches the mod (only logged when a modifier is configured)
- Log shows `Startup grace: suppressing speed` — buttons work but grace period hasn't expired yet (default: 10s after launch)
- If `Gamepad enabled but XInput hooks failed` appears, the game uses an unsupported XInput path; file a bug report with the full log

## Building from Source

```
cmake -B build -A x64
cmake --build build --config Release
```

Output: `build/bin/Release/JustSkip.asi`

<details>
<summary>Key Reference</summary>

### Keyboard (hex VK codes)

| | | | | | |
|---|---|---|---|---|---|
| F1=70 | F2=71 | F3=72 | F4=73 | F5=74 | F6=75 |
| F7=76 | F8=77 | F9=78 | F10=79 | F11=7A | F12=7B |

A-Z: `41`-`5A` · 0-9: `30`-`39` · Num0-9: `60`-`69`
Shift=`10` · Ctrl=`11` · Alt=`12` · Tab=`09` · Space=`20` · Esc=`1B` · Enter=`0D`

### Gamepad (hex XInput flags)

| | | | |
|---|---|---|---|
| D-pad Up=`0001` | Down=`0002` | Left=`0004` | Right=`0008` |
| Start=`0010` | Back=`0020` | LS=`0040` | RS=`0080` |
| LB=`0100` | RB=`0200` | A=`1000` | B=`2000` |
| X=`4000` | Y=`8000` | | |

### Crimson Desert keys to avoid

**Keyboard:** W/A/S/D, Shift, Space, Q, LMB/RMB, 1-8, R, F, V, E, I/Tab, M, J, P, H, Esc, F1-F5, F12
**Safe:** F6-F10 (defaults), numpad, Home/End/Insert/PgUp/PgDn

**Gamepad:** All face/bumper/trigger/stick buttons used in combat. Back/View is the default modifier — hold it first, then press the speed button. Button suppression prevents the game from seeing these presses.

</details>

## License

GPL-3.0-or-later — see [LICENSE](LICENSE).
Uses [MinHook](https://github.com/TsudaKageyu/minhook) by TsudaKageyu (BSD-2-Clause).
