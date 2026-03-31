# Changelog

## v2.0 — 2026-03-31

### Added
- **XInput gamepad support** — configurable modifier+button combos, auto-detection of active controller
- **Combat detection** — multi-signal fusion (vibration hooks, button mashing, AOB signature scanning) to auto-throttle speed during combat
- **On-screen display (OSD)** — GDI overlay shows current speed on toggle/hold
- **Load-screen stability** — QPC gap detection prevents time-warp on loading transitions; optional auto-reset of toggles
- **Hold-key safety** — max hold duration (default 120s) and focus-check (releases hold when game loses focus)
- **Hot-reload** — press F6 (or gamepad Back+Start) to reload INI without restarting
- **CDUMM and CDMM mod manager support** — release archives for both managers plus manual install
- **Release script** (`release.ps1`) — automated CMake build + packaging into 3 zip archives

### Changed
- Replaced `std::mutex` with SRWLock (shared-read path for QPC hot loop)
- INI trimmed from ~220 lines to ~90 with inline comments
- README converted from Markdown to plain text
- Nexus description rewritten with BBCode formatting

### Fixed
- Shadowed `DWORD now` variable in combat detection block
- Thread handle leak from hotkey polling thread
- `g_detectedPadIndex` not seeded from INI `GamepadIndex` on startup

## v1.0 — 2025-12-01

- Initial release
- Keyboard-only speed control via QueryPerformanceCounter hook
- 4 configurable speed slots (toggle or hold)
- INI-based configuration
- Debug logging
