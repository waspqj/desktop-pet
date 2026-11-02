# Changelog

All notable changes to this project are documented here.

## [1.0.0] — 2026-11-02

First public release.

### Features
- Idle animation (4-frame breathing loop + random blinks)
- Autonomous wandering in four directions
- 7 expression sets, switched by clicking the pet
- Directional drag reactions (head → dizzy, tail → squirm, body → wobble)
- Typing detection (55-key whitelist) — the pet types along with you
- Sitting-too-long reminder: walks to screen centre and holds up a sign (30/45/60 min)
- Hourly walk across the screen
- Auto walk-home and sleep after 5 idle minutes
- Tray menu: auto-start, reminder interval, action test switches, quit

### Engineering
- Single static binary, ~2.23 MB, zero runtime dependencies, 64-bit Windows
- Win32 GDI layered window (`UpdateLayeredWindow` + premultiplied-alpha BGRA frames)
- Per-frame render cost 83.2 µs → 8.9 µs (persistent render surfaces, no per-frame DC/DIB churn)
- Resident memory (Private) 6.13 MB → 4.72 MB (on-demand frame loading + LRU, 16-frame warm-up)
- CPU while dragging 4.64 % → 2.16 % of a core
- `WS_EX_NOACTIVATE` + `WM_MOUSEACTIVATE`: clicking never steals focus
- `ImmAssociateContext`: no IME injection into other applications
- Single instance via named mutex; tray icon recovers after Explorer restarts
- Sign frames read from `.dat` resources (110×110, premultiplied BGRA) so the binary stays self-contained

### Tooling
- `build.bat` — portable MinGW-w64 build (auto-detects `D:\mingw64` or `PATH`)
- `install.cmd` / `uninstall.cmd` — portable installer with optional HKCU auto-start
- `make_frames.ps1` — reskin pipeline: PNG → premultiplied `.dat`, no code changes needed
