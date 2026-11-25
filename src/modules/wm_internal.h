#ifndef AEROGLASS_WM_INTERNAL_H
#define AEROGLASS_WM_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output.h>
#include <scenefx/types/wlr_scene.h>
#include <scenefx/types/fx/corner_location.h>
#include "wm.h"
#include "grid.h"

struct wlr_idle_notifier_v1;
struct wlr_idle_inhibitor_v1;
struct wlr_idle_inhibit_manager_v1;
struct wlr_output_manager_v1;
struct wlr_data_control_manager_v1;
struct wlr_relative_pointer_manager_v1;
struct wlr_pointer_constraints_v1;
struct wlr_pointer_constraint_v1;

static inline int wm_output_width(struct wlr_output *output) {
    int w = 0;
    int h = 0;
    wlr_output_effective_resolution(output, &w, &h);
    return w;
}

static inline int wm_output_height(struct wlr_output *output) {
    int w = 0;
    int h = 0;
    wlr_output_effective_resolution(output, &w, &h);
    return h;
}

#define WM_MAX_SLOTS 16

enum wm_cursor_mode {
    WM_CURSOR_PASSTHROUGH,
    WM_CURSOR_RESIZE,
};

struct wm_hotkey {
    struct wl_list link;
    uint32_t modifiers;
    xkb_keysym_t keysym;
    char action[512];
};

struct wm_server {
    struct wm_config config;
    struct wm_hooks hooks;
    void *hooks_data;
    int inset_bottom;
    int inset_top;
    struct wm_grid_state grid;

    struct wl_display *wl_display;
    struct wlr_backend *backend;
    struct wlr_renderer *renderer;
    struct wlr_allocator *allocator;
    struct wlr_scene *scene;
    struct wlr_scene_output_layout *scene_layout;

    struct wlr_xdg_shell *xdg_shell;
    struct wlr_xwayland *xwayland;
    struct wlr_layer_shell_v1 *layer_shell;
    struct wl_list layer_surfaces;
    struct wlr_box usable_area;
    struct wlr_xdg_decoration_manager_v1 *deco_mgr;
    struct wl_listener new_layer_surface;
    struct wl_listener new_xwayland_surface;
    struct wl_listener xwayland_ready;
    struct wl_listener new_xdg_toplevel;
    struct wl_listener new_toplevel_decoration;
    struct wl_listener new_xdg_popup;
    struct wl_list toplevels;
    struct wl_list hotkeys;
    int active_workspace;
    struct wm_toplevel *workspace_focus[16];
    bool workspace_floating[16];
    int workspace_group_owner[16];
    struct wm_toplevel *active_toplevel;
    struct wlr_surface *active_layer_surface;
    struct wm_toplevel *dragging_toplevel;
    bool window_dragging;
    bool cursor_hidden;
    bool drag_was_tiled;
    struct wl_event_source *anim_timer;
    int ipc_fd;
    char ipc_path[108];
    struct wl_event_source *ipc_source;
    int ws_slide_dir;
    double window_drag_offset_x;
    double window_drag_offset_y;
    bool exit_fullscreen_dragging;
    double exit_fullscreen_start_y;

    struct wlr_cursor *cursor;
    struct wlr_xcursor_manager *cursor_mgr;
    struct wl_listener cursor_motion;
    struct wl_listener cursor_motion_absolute;
    struct wl_listener cursor_button;
    struct wl_listener cursor_axis;
    struct wl_listener cursor_frame;

    struct wlr_seat *seat;
    struct wl_listener new_input;
    struct wl_listener request_cursor;
    struct wl_listener request_set_selection;
    struct wl_listener request_start_drag;
    struct wl_listener start_drag;
    struct wlr_scene_tree *drag_icon_tree;
    struct wl_list keyboards;
    enum wm_cursor_mode cursor_mode;
    struct wm_toplevel *grabbed_toplevel;
    double grab_x;
    double grab_y;
    struct wlr_box grab_geobox;
    uint32_t resize_edges;

    struct wlr_output_layout *output_layout;
    struct wl_list outputs;
    struct wl_listener new_output;

    struct wlr_data_control_manager_v1 *data_control;
    struct wlr_relative_pointer_manager_v1 *relative_pointer_mgr;
    struct wlr_pointer_constraints_v1 *pointer_constraints;
    struct wlr_pointer_constraint_v1 *active_constraint;
    struct wl_listener new_pointer_constraint;
    struct wl_listener constraint_destroy;
    struct wlr_idle_notifier_v1 *idle_notifier;
    struct wlr_idle_inhibit_manager_v1 *idle_inhibit;
    struct wl_listener new_idle_inhibitor;
    struct wlr_output_manager_v1 *output_manager;
    struct wl_listener output_manager_apply;
    struct wl_listener output_manager_test;
};

struct wm_output {
    struct wl_list link;
    struct wm_server *server;
    struct wlr_output *wlr_output;
    struct wl_listener frame;
    struct wl_listener request_state;
    struct wl_listener destroy;
    struct wl_listener commit;
    int last_width;
    int last_height;
};

struct wm_toplevel {
    struct wl_list link;
    struct wm_server *server;
    struct wlr_xdg_toplevel *xdg_toplevel;
    struct wlr_xwayland_surface *xwayland_surface;
    struct wlr_scene_tree *scene_tree;
    struct wlr_scene_rect *border;
    struct wlr_scene_shadow *shadow;
    int rule_blur;
    bool is_pinned;
    bool no_anim;
    float opacity_active;
    float opacity_inactive;
    bool has_rule_border;
    float rule_border_color[4];
    bool rule_no_anim;
    float rule_op_active;
    float rule_op_inactive;
    bool rule_has_border;
    struct wlr_xdg_toplevel_decoration_v1 *decoration;
    struct wlr_addon addon;
    bool is_fullscreen;
    bool is_x11;
    bool is_floating;
    bool is_minimized;
    int workspace;
    int slot;
    int rot_from_top;
    int grid_band;
    int grid_order;
    int gx, gy, gw, gh;
    int x, y;
    int width, height;
    bool anim_active, anim_pos_init, anim_fade, anim_pop;
    int anim_cur_x, anim_cur_y, anim_start_x, anim_start_y, anim_target_x, anim_target_y, anim_dur_ms;
    uint64_t anim_start_ms;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;
    struct wl_listener request_move;
    struct wl_listener request_resize;
    struct wl_listener request_maximize;
    struct wl_listener request_fullscreen;
    struct wl_listener xwayland_associate;
    struct wl_listener xwayland_dissociate;
    struct wl_listener xwayland_destroy;
    struct wl_listener xwayland_request_configure;
    struct wl_listener surface_map;
    struct wl_listener surface_unmap;
    bool in_list;
    void *userdata;
    struct wlr_surface *x11_surface;
};

void wm_relayout_internal(struct wm_server *server);
void wm_hide_toplevel_internal(struct wm_toplevel *t);
void wm_focus_toplevel_internal(struct wm_toplevel *t);
void wm_apply_floating_geometry_internal(struct wm_toplevel *t);

void wm_ipc_start(struct wm_server *srv, const char *sock_name);
void server_new_output(struct wl_listener *listener, void *data);
void server_output_manager_apply(struct wl_listener *listener, void *data);
void server_output_manager_test(struct wl_listener *listener, void *data);
void server_new_idle_inhibitor(struct wl_listener *listener, void *data);
void wm_update_idle_inhibit(struct wm_server *server, struct wlr_idle_inhibitor_v1 *excluding);

#endif
