# AeroglassWM

A standalone, minimalist Wayland compositor built from scratch in C on **wlroots 0.19** and **SceneFX 0.4**.

## Key Features (`v0.2.0-alpha`)
- **Deterministic Hybrid Grid (`1x1` to `3x2` / `2x3`)**:
  - Binary X-cross window splitting up to 4 windows and invisible sector placement up to 6 windows.
  - **4-Quadrant Exchange (3 windows)**: Keeps top anchor windows fixed in their corners while exchanging half/quarter proportions on orientation toggle.
  - **Symmetric Band Rotation (5–6 windows)**: Transitions cleanly between horizontal rows (`3+2` / `2+3`) and vertical columns without shuffling inner windows.
  - **Centered 1/4 Drag & Drop (`Super + LMB`)**: Detached tiled windows automatically resize to 1/4 screen area centered under the cursor for unobstructed target visibility.
- **Native SceneFX Rendering**: Real-time Kawase backdrop blur, rounded corners, smooth window shadows, configurable monitor `refresh_rate` (auto-max by default), and zero-latency fullscreen mode.
- **Unified Structured Config**: Single self-documented `~/.config/aeroglasswm/config.toml` with automatic Cyrillic layout support for all hotkeys.
- **Layer-Shell & IPC CLI**: Full support for `waybar`, `swaybg`, `mako`, `rofi`, and runtime control via `wmctl` over UNIX sockets.

## Dependencies (Arch / CachyOS)
```bash
sudo pacman -S wlroots0.19 scenefx wayland wayland-protocols libxkbcommon cairo pango pixman
```

## Installation (`v0.2.0-alpha`)
1. Download `aeroglasswm-v0.2.0-alpha-x86_64.tar.gz` from [GitHub Releases](https://github.com).
2. Extract and install binaries and default configuration:
```bash
tar -xzf aeroglasswm-v0.2.0-alpha-x86_64.tar.gz
cd aeroglasswm-v0.2.0-alpha-x86_64
sudo install -Dm755 aeroglasswm wmctl /usr/local/bin/
mkdir -p ~/.config/aeroglasswm
cp examples/config.toml ~/.config/aeroglasswm/config.toml
```

## Session Setup (SDDM / GDM)
Create `/usr/share/wayland-sessions/aeroglasswm.desktop`:
```ini
[Desktop Entry]
Name=AeroglassWM
Comment=Minimalist Wayland Compositor
Exec=aeroglasswm
Type=Application
```

## License
Business Source License 1.1 (BSL 1.1) — transitioning to Apache License 2.0. See `LICENSE` for details.
