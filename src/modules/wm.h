#ifndef AEROGLASS_WM_H

#define AEROGLASS_WM_H

#include <stdbool.h>

#include <stdint.h>

#include <wayland-server-core.h>

#include <scenefx/types/wlr_scene.h>

#include <wlr/types/wlr_pointer.h>

#include "toml.h"

struct wm_server;

struct wm_toplevel;

struct wm_rect {

    int x;

    int y;

    int w;

    int h;

};

#define WM_MAX_AUTOSTART 32
#define WM_MAX_WINDOW_RULES 64

struct wm_window_rule {
    char app_id[128];
    char title[128];
    int floating;
    int workspace;
    int width;
    int height;
    int center;
    int blur;
    int pin;
    int fullscreen;
    int no_anim;
    float opacity_active;
    float opacity_inactive;
    bool has_border_color;
    float border_color[4];
};

struct wm_config {

    int gap;

    int gap_x;

    int gap_y;

    int corner_radius;

    int border_width;
    bool slider_enabled;
    int anim_duration;

    int workspaces;
    int refresh_rate;
    int max_windows;
    int grid_preset;
    int grid_vertical;
    int grid_lock;
    int hud_radius;
    char hud_mode[32];
    char config_path[512];

    char layout_mode[16];
    char theme[16];
    char xkb_layout[64];
    char xkb_options[128];
    int autostart_count;
    char autostart[WM_MAX_AUTOSTART][512];
    int rule_count;
    struct wm_window_rule rules[WM_MAX_WINDOW_RULES];

    bool blur_enabled;
    int blur_size;
    int blur_passes;
    bool blur_layers;
    bool shadow_enabled;
    int shadow_range;
    float shadow_color[4];
    float active_opacity;
    float inactive_opacity;

    float bg_color[4];

    float border_color_active[4];

    float border_color_inactive[4];
    bool blur;
    bool shadow;

};

struct wm_placement {

    struct wm_rect content;

    struct wm_rect inset;

    bool active;

};

struct wm_hooks {

    void (*toplevel_added)(struct wm_toplevel *t, void *data);

    void (*toplevel_removed)(struct wm_toplevel *t, void *data);

    void (*toplevel_placed)(struct wm_toplevel *t, const struct wm_placement *place, void *data);

    void (*toplevel_hidden)(struct wm_toplevel *t, void *data);

    bool (*pointer_button)(struct wm_server *server, const struct wlr_pointer_button_event *event, void *data);

    bool (*pointer_motion)(struct wm_server *server, uint32_t time_msec, void *data);

};

void wm_config_defaults(struct wm_config *config);

void wm_config_load(struct wm_config *config, toml_table_t *conf);

struct wm_server *wm_create(const struct wm_config *config);

void wm_load_hotkeys(struct wm_server *server, toml_table_t *conf);

bool wm_start(struct wm_server *server, const char *startup_cmd);

void wm_run(struct wm_server *server);

void wm_destroy(struct wm_server *server);

void wm_set_hooks(struct wm_server *server, const struct wm_hooks *hooks, void *data);

void wm_set_inset_bottom(struct wm_server *server, int pixels);
void wm_set_inset_top(struct wm_server *server, int pixels);

void wm_relayout(struct wm_server *server);

void wm_foreach_toplevel(struct wm_server *server, void (*fn)(struct wm_toplevel *t, void *data), void *data);

struct wlr_scene_tree *wm_overlay_tree(struct wm_server *server);

double wm_cursor_x(struct wm_server *server);

double wm_cursor_y(struct wm_server *server);

void *wm_toplevel_userdata(struct wm_toplevel *t);

void wm_toplevel_set_userdata(struct wm_toplevel *t, void *userdata);

void wm_toplevel_close(struct wm_toplevel *t);

void wm_toplevel_toggle_fullscreen(struct wm_toplevel *t);

void wm_toplevel_detach(struct wm_toplevel *t);

void wm_execute_action(struct wm_server *server, struct wm_toplevel *t, const char *action);

#endif
