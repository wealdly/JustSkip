# Changelog

## v2.51 — 2026-04-01

### Fixed
- **OSD speed display precision** — speed now shows two decimal places (e.g. 0.35x instead of 0.3x)

## v2.5 — 2026-03-31

### Added
- **QPC caller bypass for frame generation** — `_ReturnAddress()` check skips time scaling for OptiScaler, FSR Frame Generation, and Streamline/DLSS-G DLLs, preventing frame generation from disabling when speed changes

### Fixed
- **Button suppression per-controller filtering** — suppression now only applies to the detected controller index, fixing false "release" events from unused indices that caused Back to bleed through
- **Quick-tap reliability** — increased tap threshold from 150ms to 250ms and replay window from 60ms to 150ms for more consistent Back/View passthrough on quick presses

## v2.4 — 2026-03-31

### Added
- **Button suppression** — hooks the game's `XInputGetState` via MinHook; when the modifier (Back/View) is held, the modifier bit and all configured speed-button bits are stripped from `wButtons` before the game sees them, preventing accidental menu opens and in-game interactions during speed combos
- `SuppressButtons=1` INI setting (default enabled) — set to 0 to disable if needed
- `g_inJSPoll` thread-local bypass flag ensures JustSkip's own controller polling is never filtered even if the game and JustSkip resolve to the same XInput DLL (non-Steam installs)

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
