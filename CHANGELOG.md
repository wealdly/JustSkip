# Changelog

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
