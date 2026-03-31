================================================================================
  JustSkip — Game Speed Control for Crimson Desert
================================================================================

  Toggle or hold a hotkey to speed up the game. Fully configurable,
  supports keyboard and XInput gamepad. No Cheat Engine required.

FEATURES
--------

  - Toggle speeds: press to lock a speed, press again for 1x (mutually exclusive)
  - Hold speed: fast only while held, overrides any active toggle
  - Hot-reload: edit the INI mid-game, press F6 to apply
  - Gamepad: XInput with modifier + D-pad combos (bypasses Steam Input)
  - Combat detection: optional multi-signal fusion resets speed during fights
  - Patch-proof: hooks QueryPerformanceCounter, not game code — survives updates

DEFAULT HOTKEYS
---------------

  Keyboard        Gamepad (Back +)        Mode        Speed
  --------        ----------------        ----        -----
  F7              D-pad Up                Toggle      1.2x
  F8              D-pad Left              Toggle      2x
  F9              D-pad Down              Toggle      4x
  F10             D-pad Right             Hold        8x
  F6              Start                   —           Reload config

  Why Back/View as modifier? Least disruptive during cutscenes — the primary
  use case for speed control. XInput is read directly from System32, bypassing
  Steam Input entirely.

INSTALLATION
------------

  Manual Install

    Step 1 — ASI Loader

      Already have an ASI loader (e.g. from another mod)? Skip to Step 2.

      1. Download Ultimate-ASI-Loader_x64.zip (must be x64):
         https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/latest
      2. Rename the extracted DLL to winmm.dll
      3. Place it in your Crimson Desert bin64/ folder

    Step 2 — JustSkip

      1. Extract JustSkip.asi and JustSkip.ini into bin64/
      2. Launch the game — press F8 to verify

  Mod Manager (alternative)

    You still need an ASI loader from Step 1 above.

    CDUMM (recommended mod manager):
      1. Download JustSkip-vX.X-CDUMM.zip
      2. Drag and drop the zip onto the CDUMM window
      3. JustSkip appears in the ASI Plugins tab — enable and apply

    JSON Mod Manager (CDMM):
      1. Download JustSkip-vX.X-CDMM.zip
      2. Extract the _asi folder into your CDMM mods/ directory
      3. Open CDMM, enable JustSkip, click Apply

CONFIGURATION
-------------

  Edit JustSkip.ini to customize hotkeys, speeds, toggle/hold modes, and
  gamepad bindings. Up to 6 slots. All changes apply on hot-reload (F6).

  Speed slot example:

    [Speed1]
    Hotkey=76           ; F7
    GamepadButton=0001  ; D-pad Up
    Hold=0              ; 0 = toggle, 1 = hold
    Speed=1.2

  Gamepad:

    [Settings]
    GamepadEnabled=1           ; 0 = keyboard only (skips XInput entirely)
    GamepadModifier=0020       ; Back/View (hold first)
    GamepadReloadButton=0010   ; Start (with modifier)
    GamepadIndex=0             ; Controller 0-3

  Set GamepadEnabled=0 to disable all gamepad functionality (no XInput loading or polling).
  Set a slot's GamepadButton=0000 to disable its gamepad binding.

COMBAT DETECTION (optional)
----------------------------

  Detects combat and overrides speed using three independent signals
  (any one triggers):

    Strong vibration     — melee hits, getting hit, parries
    LT + attack button   — lock-on combat engagement
    Rapid attack mashing — 3+ presses within 2 seconds

    [Combat]
    Enabled=0
    CombatSpeed=1.0       ; 1.0 = normal, 0.95 = subtle slow-mo
    Timeout=3000          ; ms after last signal to exit combat
    VibrationThreshold=5000
    AttackButtons=C200    ; X + Y + RB
    MashCount=3
    MashWindow=2000

  If the community discovers the game's in-memory combat flag, paste an
  AOB pattern for perfect detection:

    SignaturePattern=48 8B 05 ?? ?? ?? ?? 80 78 1A 01
    SignatureOffset=3

TROUBLESHOOTING
---------------

  Set DebugLog=1 in the INI to generate JustSkip.log. Check that:
    - An ASI loader is present in bin64/ (e.g. winmm.dll)
    - The log shows "QPC hook installed"

BUILDING FROM SOURCE
--------------------

  cmake -B build -A x64
  cmake --build build --config Release

  Output: build/bin/Release/JustSkip.asi

KEY REFERENCE
-------------

  Keyboard (hex VK codes):

    F1=70  F2=71  F3=72  F4=73  F5=74  F6=75
    F7=76  F8=77  F9=78  F10=79 F11=7A F12=7B

    A-Z: 41-5A    0-9: 30-39    Num0-9: 60-69
    Shift=10  Ctrl=11  Alt=12  Tab=09  Space=20  Esc=1B  Enter=0D

  Gamepad (hex XInput flags):

    D-pad:   Up=0001  Down=0002  Left=0004  Right=0008
    Buttons: Start=0010  Back=0020  LS=0040  RS=0080
    Bumpers: LB=0100  RB=0200
    Face:    A=1000  B=2000  X=4000  Y=8000

  Crimson Desert keys to avoid:

    Keyboard: W/A/S/D, Shift, Space, Q, LMB/RMB, 1-8, R, F, V, E,
              I/Tab, M, J, P, H, Esc, F1-F5, F12
    Safe:     F6-F10 (defaults), numpad, Home/End/Insert/PgUp/PgDn

    Gamepad:  All face/bumper/trigger/stick buttons used in combat.
              D-pad and Start are safe with a modifier.
              Back/View is the best modifier.

LICENSE
-------

  GPL-3.0-or-later — see LICENSE file.
  Uses MinHook by TsudaKageyu (BSD-2-Clause).
