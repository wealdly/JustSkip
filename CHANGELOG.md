# Changelog

## v2.72 — 2026-04-05

### Fixed
- **Gamepad broken when `GamepadModifier=0000`** — GetState hooks (which populate the shadow buffer) were only installed when the modifier was non-zero; with no modifier configured, `g_xinputLoaded` stayed false, the shadow buffer was never written, and all gamepad input was dead regardless of `GamepadEnabled=1`; hooks now install unconditionally when `GamepadEnabled=1`
- **`modHeld` blocked by modifier==0 check** — hotkey thread also gated pad-state reading and `modHeld` on `g_gamepadModifier != 0`; `modHeld` now evaluates to `true` whenever the pad is connected and no modifier is configured (direct button presses)
- **`ScanSignature` DWORD underflow** — `size - sigLen` used unsigned subtraction without a bounds check; if a section was smaller than the AOB pattern, the loop counter wrapped to ~4 billion and scanned garbage memory; an early `continue` now skips undersized sections
- **`xinput9_1_0.dll` missing from `LoadLibraryA` fallback** — the Vista-era XInput variant was probed by `GetModuleHandleA` but omitted from the load-if-not-found fallback; delay-loading games on that variant silently produced "No XInput DLL loaded" instead of working

### Changed
- XInput DLL search unified into a candidate table — probes `xinput1_4`, `xinput1_3`, `xinput9_1_0` in order via `GetModuleHandleA` then `LoadLibraryA`; startup log now reports which DLL won and whether it was already in the process or loaded by the fallback
- **Debug log (`DebugLog=1`) now covers all silent failure modes:**
  - `Gamepad enabled but XInput hooks failed — keyboard only` — when `GamepadEnabled=1` but no hooks installed
  - `Gamepad connected` / `Gamepad disconnected` — edge-detected from shadow buffer validity
  - `Modifier pressed (buttons=0x…)` / `Modifier released` — edge-detected, only when a modifier is configured
  - `Startup grace: suppressing speed X.XXx (Nms remaining)` — once per unique speed attempt during grace, so silent "buttons do nothing" is explained
  - `Startup grace expired — speed changes active` — marks the exact tick when grace ends
- **Strip log dedup fixed** — the `stripped==0` branch in the suppression logger was resetting `g_suppressLastLog` on every poll from the virtual XInput slot (which always returns `wButtons=0`), causing the next real poll to always log as if the mask had changed; the spurious reset is removed so dedup works across the full press duration

## v2.71 — 2026-04-02

### Fixed
- **Gamepad combos broken when `SuppressButtons=0`** — XInput hooks (which populate the shadow buffer for combo detection) were only installed when button suppression was enabled; users with suppression disabled got no shadow data and combos silently stopped working, a regression from v2.6

## v2.7 — 2026-04-01

### Fixed
- **Button suppression now stable with Steam's virtual controller slot** — `modHeld`/`speedHeld` in the suppression logic now read from the OR of all shadow slots instead of the per-call `pState`; previously, polls against Steam's virtual slot (always `wButtons=0`) would start the release debounce countdown while Back was still physically held, causing Back to bleed through as a zoom or menu open
- **Suppression log noise** — debug log no longer emits `stripped 0x0000` entries when a zero-button poll passes through the suppression path

## v2.6 — 2026-04-01

### Fixed
- **Gamepad combos now work with Steam** — replaced independent XInput polling with a shadow state buffer; the hook captures raw button state from the game's own XInput calls before stripping, so the hotkey thread reads real physical buttons regardless of which module Steam routes data through
- **Controller index detection** — hotkey thread now ORs all 4 XInput shadow slots, eliminating false detection of Steam's virtual remapping slot at index 0 when the physical controller is at index 1 or higher
- **Sub-frame button flicker** — shadow buffer debounced at 32ms; consecutive zero-button polls from the game's multi-path XInput calls no longer clear the last-seen button state between hotkey thread reads

### Changed
- Removed `LoadXInput()` and independent XInput polling entirely — button state is now sourced exclusively from the game's own XInput calls via the hook
- `g_xinputLoaded` now reflects whether GetState hooks are installed rather than whether a separate DLL was loaded

### Performance
- `SuppressButtons` and `XInputGetStateImpl` defer `GetTickCount()` calls to only when timers are active (idle path costs zero syscalls)
- Strip logging atomic (`g_suppressLastLog`) guarded by `g_debugLog` — zero overhead with debug off

## v2.51 — 2026-04-01

### Fixed
- **OSD speed display precision** — speed now shows two decimal places (e.g. 0.35x instead of 0.3x)

## v2.5 — 2026-03-31

### Added
- **QPC caller bypass for frame generation** — `_ReturnAddress()` check skips time scaling for OptiScaler, FSR Frame Generation, and Streamline/DLSS-G DLLs, preventing frame generation from disabling when speed changes

### Fixed
- **Quick-tap reliability** — increased tap threshold from 150ms to 250ms and replay window from 60ms to 150ms for more consistent Back/View passthrough on quick presses

## v2.4 — 2026-03-31

### Added
- **Button suppression** — hooks the game's `XInputGetState` via MinHook; when the modifier (Back/View) is held, the modifier bit and all configured speed-button bits are stripped from `wButtons` before the game sees them, preventing accidental menu opens and in-game interactions during speed combos
- `SuppressButtons=1` INI setting (default enabled) — set to 0 to disable if needed

## v2.3 — 2026-03-31

### Changed
- Gamepad bindings are now all toggles: Back + X (1.2x), Back + A (4x) — no more holds on gamepad
- Changed gamepad modifier back to Back/View (LB conflicts with targeting)
- Removed gamepad reload button binding (keyboard F6 only)
- Keyboard F10 remains 8x hold (unchanged)

### Fixed
- Startup logging silenced inside `LoadConfig()` — config details (gamepad status, combat settings) now print to log
- Version string corrected (was still showing v1.0)

## v2.0 — 2026-03-31

### Added
- **XInput gamepad support** — configurable modifier+button combos, auto-detection of active controller
- **GamepadEnabled toggle** — `GamepadEnabled=0` disables all gamepad functionality (no XInput loading or polling)
- **Combat detection** — optional multi-signal fusion (vibration hooks, button mashing, AOB signature scanning) to auto-throttle speed during combat (disabled by default)
- **On-screen display (OSD)** — GDI overlay shows current speed on toggle/hold
- **Hold-key safety** — max hold duration (default 120s) and focus-check (releases hold when game loses focus)
- **Startup grace period** — ignores speed changes for a configurable number of seconds after launch (`StartupGraceSeconds`)
- **Hot-reload** — press F6 (or gamepad Back+Start) to reload INI without restarting
- **CDUMM and CDMM mod manager support** — release archives for both managers plus manual install
- **Release script** (`release.ps1`) — automated CMake build + packaging into 3 zip archives

### Changed
- Default fastest speed changed from 12x to 8x
- Replaced `std::mutex` with SRWLock (shared-read path for QPC hot loop)
- INI trimmed from ~220 lines to ~85 with inline comments
- Nexus description rewritten with BBCode formatting

### Fixed
- Startup logging silenced by `LoadConfig()` overwriting debug flag — startup logs now always print regardless of INI setting
- Shadowed `DWORD now` variable in combat detection block
- Thread handle leak from hotkey polling thread
- `g_detectedPadIndex` not seeded from INI `GamepadIndex` on startup

## v1.0 — 2025-12-01

- Initial release
- Keyboard-only speed control via QueryPerformanceCounter hook
- 4 configurable speed slots (toggle or hold)
- INI-based configuration
- Debug logging
