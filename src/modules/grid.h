#ifndef AEROGLASS_GRID_H
#define AEROGLASS_GRID_H

#include <stdbool.h>
#include <stdint.h>
#include "wm.h"

struct wm_server;
struct wm_toplevel;

struct wm_grid_state {
    int preset;
    int vertical;
    int split_mode;
    int prev_count;
    int row5_top;
    uint32_t last_scroll_msec;
    struct wlr_scene_tree *hud_tree;
    struct wl_event_source *hud_timer;
};

void grid_init(struct wm_server *server);
void grid_fini(struct wm_server *server);
int grid_effective_capacity(const struct wm_server *server);
int grid_cols(const struct wm_server *server);
int grid_rows(const struct wm_server *server);
void grid_cell(const struct wm_server *server, int sw, int sh, int slot, struct wm_rect *cell);
void grid_place_at_cursor(struct wm_server *server, struct wm_toplevel *target);
void grid_assign_slots(struct wm_server *server, struct wm_toplevel **wins, int count);
void grid_restore_minimized(struct wm_server *server);
void grid_on_toplevel_remove(struct wm_server *server, struct wm_toplevel *closing);
void grid_cycle_preset(struct wm_server *server, int delta);
void grid_toggle_orientation(struct wm_server *server);
void grid_handle_scroll(struct wm_server *server, uint32_t time_msec, double delta);
void grid_refresh_hud(struct wm_server *server);
void grid_on_super_release(struct wm_server *server);
void grid_cycle_slot_stack(struct wm_server *server, int dir);
void grid_toggle_workspace_floating(struct wm_server *server);
void grid_toggle_window_floating(struct wm_server *server);
void grid_swap_windows(struct wm_server *server, struct wm_toplevel *a, struct wm_toplevel *b);
void grid_swap_direction(struct wm_server *server, int dx, int dy);
void grid_focus_direction(struct wm_server *server, int dx, int dy);
void grid_fit_rect(struct wm_rect *r, const struct wm_rect *bounds, int margin);

#endif
