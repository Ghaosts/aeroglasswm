# AeroglassWM

A minimalist, gesture-centric Wayland compositor built on wlroots and SceneFX. It features a strict two-window mathematical grid, a highly customizable UI slider, and a fully declarative TOML configuration for both appearance and behavior.

> **Status:** `v0.1.0-alpha`. The core architecture is stable, but the API and configuration format may be subject to change in future releases.

<!-- Replace with actual screenshot -->

![AeroglassWM](docs/screenshot.png)

## Features

- **Two-Window Tiling:** A strict Top/Bottom grid that dynamically adjusts when windows are moved, floated, or closed.
- **Gesture-Centric UI:** A morphing slider located below the active window. Swipe left/right to trigger actions (e.g., fullscreen, close) or swipe up to detach the window.
- **Declarative Configuration:** Complete control over window gaps, border radii, colors, and UI assets via `~/.config/aeroglass/config.toml`.
- **Action & Hotkey Mapping:** Bind built-in actions (`fullscreen`, `close`, `detach`) or arbitrary shell commands (`exec:`) to clicks, swipes, and keyboard combinations.
- **PNG Skins:** Customizable UI elements (bar, slider, buttons) with automatic corner-radius masking. Falls back to vector shapes if assets are missing.
- **Layer Shell Support:** Out-of-the-box compatibility with `swaybg`, `waybar`, `rofi`, `wofi`, and `mako`.

## Dependencies

- `wlroots` (>= 0.19.0)
- `scenefx` (0.4.x)
- `cairo`
- `tomlc99`
- `wayland-server`
- `xkbcommon`

*(For Arch Linux users: `pacman -S base-devel meson ninja cairo wlroots scenefx tomlc99 wayland wayland-protocols xkbcommon`)*

## Build

```sh
meson setup build
ninja -C build
```

## Run

```sh
./build/aeroglasswm -s "foot"
```

The `-s` flag accepts a shell command to run immediately after the compositor starts (e.g., starting a terminal, setting a wallpaper via `swaybg`, or launching a panel).

## Configuration

AeroglassWM reads its configuration from `~/.config/aeroglass/config.toml`.

### Basic Example

```toml
gap = 10
gap_between = 20
bar_h = 30
corner_radius = 12
slider_radius = 10
btn_radius = 8
bar_radius = 12

[colors]
bg = [0.1, 0.1, 0.1, 1.0]
bar = [0.05, 0.05, 0.05, 0.95]
slider = [0.3, 0.3, 0.3, 0.95]
btn_fs = [0.2, 0.6, 1.0, 0.9]
btn_close = [1.0, 0.3, 0.3, 0.9]
slider_pull = [0.8, 0.9, 1.0, 0.9]

[actions]
click_left  = "fullscreen"
click_right = "close"
swipe_left  = "exec:rofi -show drun"
swipe_right = "close"
swipe_up    = "detach"

[hotkeys]
"Super+Return" = "exec:foot"
"Super+Q"      = "close"
"Super+F"      = "fullscreen"
```

### Actions
- `none`: Ignores the action (visual morphing still occurs).
- `fullscreen`: Toggles fullscreen for the active window.
- `close`: Closes the active window.
- `detach`: Pops the window out of the grid (floating mode).
- `exec:COMMAND`: Executes an arbitrary shell command via `sh -c`.

## License

MIT
