# AeroglassWM

A standalone, minimalist Wayland compositor built from scratch in C on **wlroots 0.19** and **SceneFX 0.4**, featuring a cursor-aware hybrid tiling grid (`1x1` to `4x2`), real-time layout HUD, native Kawase blur & window shadows, Quickshell/Waybar layer-shell support, and a UNIX-socket IPC CLI (`wmctl`).

**Status:** `v0.2.0-desktop` (Closed-source binary release).

## Key Features

- **Hybrid Cursor-Aware Grid (`1x1` to `4x2`):**
  - Default `3x2` band layout with automatic `2x2` progression (`1 -> 2 -> 3 -> 4 -> 5..6`).
  - Binary 4-sector mode (`4x2`, up to 8 windows) where each quadrant splits `1/2` and automatically expands back to `1/4` when a neighbor closes.
  - Real-time translucent layout HUD on `Super + Scroll` and `Super + R` (orientation flip).
  - Automatic overflow to free workspaces ("penalty workspaces") with instant reclaim when a slot frees up.
- **SceneFX Visuals & Animations:**
  - Native Kawase backdrop blur (`blur`, `blur_size`, `blur_passes`, `blur_layers`) for transparent terminals, Waybar, and Quickshell.
  - Soft window shadows (`shadow`, `shadow_range`), rounded corners, active/inactive window opacity, and Hyprland-style spring animations.
- **Full Layer-Shell & Quickshell Support:**
  - Supports `EXCLUSIVE` and `ON_DEMAND` keyboard interactivity, negative `exclusive_zone` overlays, and automatic `usable_area` recalculation.
- **Declarative TOML Config & Window Rules:**
  - Configured via `~/.config/aeroglass/config.toml`.
  - `[[windowrule]]` matching by `app_id` and `title` with `float`, `workspace`, `size`, `center`, `blur`, `pin` (`Super + P`), `fullscreen`, `no_anim`, `opacity`, and `border_color`.
- **IPC Control (`wmctl`):**
  - Query live JSON state (`wmctl status`, `wmctl clients`) or trigger any compositor action (`wmctl dispatch <action>`) via `$WM_IPC_SOCK`.

## Runtime Dependencies

- `wlroots` (>= 0.19.0)
- `scenefx` (0.4.x)
- `cairo`, `pixman`, `wayland`, `xkbcommon`

## Installation & Session Setup

1. **Install binaries and default config:**
```sh
tar -xzf aeroglasswm-v0.2.0-desktop-x86_64.tar.gz
sudo install -Dm755 aeroglasswm /usr/local/bin/aeroglasswm
sudo install -Dm755 wmctl /usr/local/bin/wmctl
mkdir -p ~/.config/aeroglass
cp config.example.toml ~/.config/aeroglass/config.toml
```

2. **Add Display Manager entry (SDDM / GDM / greetd):**
```sh
sudo tee /usr/share/wayland-sessions/aeroglasswm.desktop > /dev/null << "DESKTOP"
[Desktop Entry]
Name=AeroglassWM
Comment=Minimalist Wayland Compositor with Hybrid Tiling & SceneFX
Exec=aeroglasswm
Type=Application
DesktopNames=wlroots
DESKTOP
```

3. **Start session:**
Select **AeroglassWM** in your display manager (SDDM/GDM), or launch directly from a clean TTY (`tty1`) via `exec aeroglasswm`.

## License

Proprietary. All rights reserved. The source code is closed and confidential. Binaries are provided for testing and preview purposes only. Reverse engineering, modification, or redistribution is strictly prohibited.
