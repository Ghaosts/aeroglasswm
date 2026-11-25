#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <scenefx/types/wlr_scene.h>
#include <scenefx/render/fx_renderer/fx_renderer.h>
#include <scenefx/types/fx/corner_location.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/xwayland.h>
#include <wlr/util/addon.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_linux_drm_syncobj_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include "wlr-layer-shell-unstable-v1-protocol.h"
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_text_input_v3.h>
#include <wlr/types/wlr_xdg_toplevel_icon_v1.h>
#include <pixman.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>
#include "toml.h"
#include "wm_internal.h"

struct wm_popup {
    struct wlr_xdg_popup *xdg_popup;
    struct wl_listener commit;
    struct wl_listener destroy;
};

struct wm_keyboard {
    struct wl_list link;
    struct wm_server *server;
    struct wlr_keyboard *wlr_keyboard;
    struct wl_listener modifiers;
    struct wl_listener key;
    struct wl_listener destroy;
    uint8_t swallowed[96];
};

struct wm_layer_surface {
    struct wl_list link;
    struct wlr_layer_surface_v1 *layer_surface;
    struct wlr_scene_layer_surface_v1 *scene_layer_surface;
    struct wl_listener commit;
    struct wl_listener destroy;
    struct wl_listener surface_map;
    struct wl_listener surface_unmap;
    struct wm_server *server;
};

#define WM_HOTKEY_MOD_MASK (WLR_MODIFIER_LOGO | WLR_MODIFIER_ALT | WLR_MODIFIER_CTRL | WLR_MODIFIER_SHIFT)
#define WM_KEY_SWALLOW_MAX 768

struct wm_idle_inhibitor {
    struct wm_server *server;
    struct wl_listener destroy;
};

static void layout_toplevels(struct wm_server *server);
static void wm_apply_surface_fx(struct wlr_scene_node *node, bool blur_en, float opacity) {
    if (node->type == WLR_SCENE_NODE_BUFFER) {
        struct wlr_scene_buffer *sb = wlr_scene_buffer_from_node(node);
        if (sb) {
            wlr_scene_buffer_set_backdrop_blur(sb, blur_en);
            wlr_scene_buffer_set_backdrop_blur_optimized(sb, true);
            wlr_scene_buffer_set_opacity(sb, opacity);
        }
    } else if (node->type == WLR_SCENE_NODE_TREE) {
        struct wlr_scene_node *c;
        wl_list_for_each(c, &wlr_scene_tree_from_node(node)->children, link) wm_apply_surface_fx(c, blur_en, opacity);
    }
}

static void wm_update_toplevel_border(struct wm_toplevel *t, bool is_active);
static void toplevel_map_common(struct wm_toplevel *toplevel);
static void toplevel_unmap_common(struct wm_toplevel *toplevel);
static void toplevel_destroy_common(struct wm_toplevel *toplevel);

void wm_relayout_internal(struct wm_server *server) {
    layout_toplevels(server);
}

static void focus_toplevel(struct wm_toplevel *toplevel);
static uint64_t wm_now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000ULL);
}
static void wm_set_opacity(struct wlr_scene_node *n, float op) {
    if (n->type == WLR_SCENE_NODE_BUFFER) {
        struct wlr_scene_buffer *b = wlr_scene_buffer_from_node(n);
        if (b) wlr_scene_buffer_set_opacity(b, op);
    } else if (n->type == WLR_SCENE_NODE_TREE) {
        struct wlr_scene_node *c;
        wl_list_for_each(c, &wlr_scene_tree_from_node(n)->children, link) wm_set_opacity(c, op);
    }
}
static double wm_hypr_bezier(double t) {
    if (t <= 0.0) return 0.0;
    if (t >= 1.0) return 1.0;
    // Быстрый старт + лёгкий пружинный перелёт (1.035) и мягкая посадка в 1.0
    double u = 1.0 - t;
    double base = 1.0 - u * u * u * u;
    double overshoot = 0.045 * (t * (1.0 - t) * (1.0 - t) * 6.75);
    return base + overshoot;
}

static void wm_set_scale_and_opacity(struct wm_toplevel *t, float scale, float op) {
    if (!t || !t->scene_tree) return;
    wm_set_opacity(&t->scene_tree->node, op);
    int w = t->width > 0 ? t->width : 1;
    int h = t->height > 0 ? t->height : 1;
    int sw = (int)(w * scale);
    int sh = (int)(h * scale);
    if (sw < 1) sw = 1;
    if (sh < 1) sh = 1;
    int ox = (w - sw) / 2;
    int oy = (h - sh) / 2;
    wlr_scene_node_set_position(&t->scene_tree->node, t->anim_cur_x + ox, t->anim_cur_y + oy);
}

static void wm_notify_placed(struct wm_toplevel *t, const struct wm_placement *place);
static int wm_anim_tick(void *data);
static void wm_animate_to(struct wm_toplevel *t, int tx, int ty, bool fade) {
    if (!t || !t->scene_tree) return;
    int dur = t->server->config.anim_duration;
    bool drag = (t->server->window_dragging && t->server->dragging_toplevel == t) ||
                (t->server->cursor_mode == WM_CURSOR_RESIZE && t->server->grabbed_toplevel == t);
    if (dur <= 0 || drag) {
        t->anim_active = t->anim_fade = false; t->anim_pos_init = true;
        t->anim_cur_x = t->anim_target_x = tx; t->anim_cur_y = t->anim_target_y = ty;
        wlr_scene_node_set_position(&t->scene_tree->node, tx, ty);
        wm_set_opacity(&t->scene_tree->node, 1.0f);
        return;
    }
    if (!t->anim_pos_init) {
        t->anim_pos_init = true;
        int slide_dx = 0;
        if (t->server->ws_slide_dir != 0 && !wl_list_empty(&t->server->outputs)) {
            struct wm_output *o = wl_container_of(t->server->outputs.next, o, link);
            slide_dx = t->server->ws_slide_dir * (wm_output_width(o->wlr_output) / 3);
        }
        t->anim_cur_x = tx + slide_dx; t->anim_cur_y = ty;
        wlr_scene_node_set_position(&t->scene_tree->node, t->anim_cur_x, ty);
        if (!fade && slide_dx == 0) return;
    } else if (t->anim_target_x == tx && t->anim_target_y == ty && !fade && t->server->ws_slide_dir == 0) {
        if (!t->anim_active) wlr_scene_node_set_position(&t->scene_tree->node, tx, ty);
        return;
    }
    if (t->server->ws_slide_dir != 0 && !wl_list_empty(&t->server->outputs)) {
        struct wm_output *o = wl_container_of(t->server->outputs.next, o, link);
        t->anim_cur_x = tx + t->server->ws_slide_dir * (wm_output_width(o->wlr_output) / 3);
        t->anim_cur_y = ty;
    }
    t->anim_start_x = t->anim_cur_x; t->anim_start_y = t->anim_cur_y;
    t->anim_target_x = tx; t->anim_target_y = ty;
    t->anim_start_ms = wm_now_ms(); t->anim_dur_ms = dur;
    if (fade) {
        t->anim_fade = true;
        t->anim_pop = true;
        wm_set_scale_and_opacity(t, 0.88f, 0.0f);
    }
    t->anim_active = true;
    if (!t->server->anim_timer)
        t->server->anim_timer = wl_event_loop_add_timer(wl_display_get_event_loop(t->server->wl_display), wm_anim_tick, t->server);
    if (t->server->anim_timer) wl_event_source_timer_update(t->server->anim_timer, 8);
}
static int wm_anim_tick(void *data) {
    struct wm_server *srv = data; uint64_t now = wm_now_ms(); bool active = false;
    struct wm_toplevel *t;
    wl_list_for_each(t, &srv->toplevels, link) {
        if (!t->anim_active || !t->scene_tree) continue;
        double p = (double)(now - t->anim_start_ms) / (double)t->anim_dur_ms;
        if (p >= 1.0) {
            t->anim_cur_x = t->anim_target_x; t->anim_cur_y = t->anim_target_y;
            wlr_scene_node_set_position(&t->scene_tree->node, t->anim_cur_x, t->anim_cur_y);
            if (t->anim_fade || t->anim_pop) {
                wm_set_opacity(&t->scene_tree->node, 1.0f);
                t->anim_fade = false;
                t->anim_pop = false;
            }
            t->anim_active = false;
        } else {
            double ease = wm_hypr_bezier(p);
            t->anim_cur_x = t->anim_start_x + (int)((t->anim_target_x - t->anim_start_x) * ease);
            t->anim_cur_y = t->anim_start_y + (int)((t->anim_target_y - t->anim_start_y) * ease);
            if (t->anim_pop) {
                float sc = 0.88f + 0.12f * (float)ease;
                float op = (float)(p * 1.4 > 1.0 ? 1.0 : p * 1.4);
                wm_set_scale_and_opacity(t, sc, op);
            } else {
                wlr_scene_node_set_position(&t->scene_tree->node, t->anim_cur_x, t->anim_cur_y);
                if (t->anim_fade) wm_set_opacity(&t->scene_tree->node, (float)(p > 1.0 ? 1.0 : p));
            }
            active = true;
        }
        bool act = (srv->active_toplevel == t);
        wm_update_toplevel_border(t, act && !srv->active_layer_surface);
        if (act && !t->anim_fade && !t->is_floating && !t->is_fullscreen && srv->inset_bottom > 0) {
            struct wm_placement pl = {
                .content = { t->anim_cur_x, t->anim_cur_y, t->width, t->height },
                .inset = { t->anim_cur_x, t->anim_cur_y + t->height + 6, t->width, srv->inset_bottom },
                .active = true,
            };
            wm_notify_placed(t, &pl);
        }
    }
    if (active && srv->anim_timer) wl_event_source_timer_update(srv->anim_timer, 8);
    return 0;
}

void wm_apply_floating_geometry_internal(struct wm_toplevel *t) {
    if (t == NULL) return;
    if (t->scene_tree) wm_animate_to(t, t->x, t->y, false);
    if (t->is_x11) {
        if (t->xwayland_surface) wlr_xwayland_surface_configure(t->xwayland_surface, t->x, t->y, t->width, t->height);
    } else if (t->xdg_toplevel) {
        wlr_xdg_toplevel_set_tiled(t->xdg_toplevel, 0);
        wlr_xdg_toplevel_set_size(t->xdg_toplevel, t->width, t->height);
    }
}

void wm_focus_toplevel_internal(struct wm_toplevel *t) {
    focus_toplevel(t);
}

static void toplevel_map_common(struct wm_toplevel *toplevel);
static void toplevel_unmap_common(struct wm_toplevel *toplevel);
static void toplevel_destroy_common(struct wm_toplevel *toplevel);
static void toplevel_retire(struct wm_toplevel *toplevel);
static void focus_toplevel(struct wm_toplevel *toplevel);
static void toplevel_set_fullscreen(struct wm_toplevel *t, bool fullscreen);
static void detach_window(struct wm_server *server, struct wm_toplevel *t);
static void focus_next_toplevel(struct wm_server *server, bool forward);
static void toplevel_request_close(struct wm_toplevel *t);
static void wm_notify_added(struct wm_toplevel *t) {
    struct wm_server *server = t->server;
    if (server->hooks.toplevel_added) server->hooks.toplevel_added(t, server->hooks_data);
}

static void wm_notify_removed(struct wm_toplevel *t) {
    struct wm_server *server = t->server;
    if (server->hooks.toplevel_removed) server->hooks.toplevel_removed(t, server->hooks_data);
}

static void wm_notify_hidden(struct wm_toplevel *t) {
    struct wm_server *server = t->server;
    if (server->hooks.toplevel_hidden) server->hooks.toplevel_hidden(t, server->hooks_data);
}

static void wm_notify_placed(struct wm_toplevel *t, const struct wm_placement *place) {
    struct wm_server *server = t->server;
    if (server->hooks.toplevel_placed) server->hooks.toplevel_placed(t, place, server->hooks_data);
}

static void wm_notify_activity(struct wm_server *server) {
    if (server->idle_notifier == NULL) return;
    if (server->seat == NULL) return;
    wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
}

static void keyboard_handle_modifiers(
		struct wl_listener *listener, void *data) {
	(void)data;

	struct wm_keyboard *keyboard =
		wl_container_of(listener, keyboard, modifiers);

	wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);

	wlr_seat_keyboard_notify_modifiers(keyboard->server->seat,
		&keyboard->wlr_keyboard->modifiers);
	if ((wlr_keyboard_get_modifiers(keyboard->wlr_keyboard) & WLR_MODIFIER_LOGO) == 0) {
		grid_on_super_release(keyboard->server);
	}
}

static struct wlr_surface *toplevel_surface(struct wm_toplevel *t) {
    if (t->is_x11) {
        if (t->xwayland_surface == NULL) return NULL;
        return t->xwayland_surface->surface;
    }
    if (t->xdg_toplevel == NULL) return NULL;
    return t->xdg_toplevel->base->surface;
}

static void toplevel_request_close(struct wm_toplevel *t) {
    if (t == NULL) return;
    if (t->is_x11) {
        if (t->xwayland_surface) wlr_xwayland_surface_close(t->xwayland_surface);
        return;
    }
    if (t->xdg_toplevel) wlr_xdg_toplevel_send_close(t->xdg_toplevel);
}

/* Configuration & Hotkeys (implemented in src/modules/config.c) */
void wm_config_defaults(struct wm_config *config);
void wm_config_load(struct wm_config *config, toml_table_t *conf);
void wm_load_hotkeys(struct wm_server *server, toml_table_t *conf);

static void wm_spawn_command(const char *cmd) {
    if (cmd == NULL || cmd[0] == '\0') return;
    pid_t child = fork();
    if (child < 0) return;
    if (child > 0) {
        waitpid(child, NULL, 0);
        return;
    }
    pid_t grandchild = fork();
    if (grandchild < 0) _exit(1);
    if (grandchild > 0) _exit(0);
    sigset_t mask;
    sigemptyset(&mask);
    sigprocmask(SIG_SETMASK, &mask, NULL);
    setsid();
    execlp("sh", "sh", "-c", cmd, (char *)NULL);
    _exit(127);
}

static void wm_center_floating(struct wm_server *server, struct wm_toplevel *t) {
    if (t == NULL) return;
    if (t->is_floating == false || t->is_fullscreen) return;
    struct wlr_output *out = wlr_output_layout_output_at(server->output_layout,
        t->x + t->width / 2, t->y + t->height / 2);
    if (out == NULL && wl_list_empty(&server->outputs) == 0) {
        struct wm_output *first = wl_container_of(server->outputs.next, first, link);
        out = first->wlr_output;
    }
    if (out == NULL) return;
    struct wlr_box box;
    wlr_output_layout_get_box(server->output_layout, out, &box);
    if (box.width < 1 || box.height < 1) return;
    struct wm_rect bounds = { box.x, box.y, box.width, box.height };
    struct wm_rect r = { t->x, t->y, t->width, t->height };
    grid_fit_rect(&r, &bounds, server->config.gap);
    t->x = r.x;
    t->y = r.y;
    t->width = r.w;
    t->height = r.h;
    wm_apply_floating_geometry_internal(t);
    wm_update_toplevel_border(t, true);
    if (t->scene_tree) wlr_scene_node_raise_to_top(&t->scene_tree->node);
}

static void wm_warp_cursor_to_active(struct wm_server *server) {
    struct wm_toplevel *t = server->active_toplevel;
    if (t == NULL || server->cursor == NULL) return;
    double cx = t->x + t->width * 0.5;
    double cy = t->y + t->height * 0.5;
    wlr_cursor_warp(server->cursor, NULL, cx, cy);
}

void wm_execute_action(struct wm_server *server, struct wm_toplevel *t, const char *action) {
    (void)server;
    if (action == NULL) return;
    if (action[0] == '\0') return;
    if (strcmp(action, "none") == 0) return;
    if (strcmp(action, "close") == 0 || strcmp(action, "win_close") == 0) {
        struct wm_toplevel *target = t != NULL ? t : server->active_toplevel;
        if (target != NULL) toplevel_request_close(target);
        return;
    }
    if (strcmp(action, "detach") == 0) {
        if (t) detach_window(t->server, t);
        return;
    }
    if (strcmp(action, "fullscreen") == 0 || strcmp(action, "win_fullscreen") == 0) {
        struct wm_toplevel *target = t != NULL ? t : server->active_toplevel;
        if (target == NULL) return;
        toplevel_set_fullscreen(target, target->is_fullscreen == false);
        return;
    }
    if (strcmp(action, "grid_next") == 0) {
        grid_cycle_preset(server, 1);
        return;
    }
    if (strcmp(action, "grid_prev") == 0) {
        grid_cycle_preset(server, -1);
        return;
    }
    if (strcmp(action, "grid_unlimited") == 0) {
        server->config.max_windows = (server->config.max_windows < 0) ? 0 : -1;
        wm_relayout_internal(server);
        return;
    }
    if (strcmp(action, "grid_flip") == 0) {
        grid_toggle_orientation(server);
        return;
    }
    if (strcmp(action, "stack_next") == 0) {
        grid_cycle_slot_stack(server, 1);
        return;
    }
    if (strcmp(action, "stack_prev") == 0) {
        grid_cycle_slot_stack(server, -1);
        return;
    }
    if (strcmp(action, "ws_floating") == 0) {
        grid_toggle_workspace_floating(server);
        return;
    }
    if (strcmp(action, "win_floating") == 0) {
        grid_toggle_window_floating(server);
        return;
    }
    if (strcmp(action, "swap_left") == 0) {
        grid_swap_direction(server, -1, 0);
        return;
    }
    if (strcmp(action, "swap_right") == 0) {
        grid_swap_direction(server, 1, 0);
        return;
    }
    if (strcmp(action, "swap_up") == 0) {
        grid_swap_direction(server, 0, -1);
        return;
    }
    if (strcmp(action, "swap_down") == 0) {
        grid_swap_direction(server, 0, 1);
        return;
    }
    if (strcmp(action, "focus_prev") == 0) {
        if (server->active_layer_surface == NULL) focus_next_toplevel(server, false);
        return;
    }
    if (strcmp(action, "focus_next") == 0) {
        if (server->active_layer_surface == NULL) focus_next_toplevel(server, true);
        return;
    }
    if (strcmp(action, "focus_left") == 0) {
        if (server->active_layer_surface == NULL) {
            struct wm_toplevel *prev = server->active_toplevel;
            grid_focus_direction(server, -1, 0);
            if (server->active_toplevel != prev) wm_warp_cursor_to_active(server);
        }
        return;
    }
    if (strcmp(action, "focus_right") == 0) {
        if (server->active_layer_surface == NULL) {
            struct wm_toplevel *prev = server->active_toplevel;
            grid_focus_direction(server, 1, 0);
            if (server->active_toplevel != prev) wm_warp_cursor_to_active(server);
        }
        return;
    }
    if (strcmp(action, "focus_up") == 0) {
        if (server->active_layer_surface == NULL) {
            struct wm_toplevel *prev = server->active_toplevel;
            grid_focus_direction(server, 0, -1);
            if (server->active_toplevel != prev) wm_warp_cursor_to_active(server);
        }
        return;
    }
    if (strcmp(action, "focus_down") == 0) {
        if (server->active_layer_surface == NULL) {
            struct wm_toplevel *prev = server->active_toplevel;
            grid_focus_direction(server, 0, 1);
            if (server->active_toplevel != prev) wm_warp_cursor_to_active(server);
        }
        return;
    }
    if (strcmp(action, "win_center") == 0) {
        wm_center_floating(server, t != NULL ? t : server->active_toplevel);
        return;
    }
    if (strncmp(action, "ws_", 3) == 0) {
        int ws = atoi(action + 3) - 1;
        if (ws >= 0 && ws < server->config.workspaces && server->active_workspace != ws) {
            server->ws_slide_dir = (ws > server->active_workspace) ? 1 : -1;
            server->active_workspace = ws;
            layout_toplevels(server);
            server->ws_slide_dir = 0;
            struct wm_toplevel *wf = server->workspace_focus[ws];
            if (wf != NULL && wf->in_list && wf->workspace == ws) {
                focus_toplevel(wf);
            } else {
                struct wm_toplevel *cand;
                wl_list_for_each(cand, &server->toplevels, link) {
                    if (cand->in_list && cand->workspace == ws && !cand->is_minimized) {
                        focus_toplevel(cand);
                        break;
                    }
                }
            }
        }
        return;
    }
    if (strncmp(action, "move_ws_", 8) == 0) {
        int ws = atoi(action + 8) - 1;
        if (ws >= 0 && ws < server->config.workspaces && server->active_toplevel != NULL) {
            struct wm_toplevel *moved = server->active_toplevel;
            moved->workspace = ws;
            server->active_toplevel = NULL;
            layout_toplevels(server);
            struct wm_toplevel *cand;
            wl_list_for_each(cand, &server->toplevels, link) {
                if (cand->in_list && cand->workspace == server->active_workspace && cand->is_minimized == false) {
                    focus_toplevel(cand);
                    break;
                }
            }
        }
        return;
    }
    if (strncmp(action, "exec:", 5) == 0) {
        wm_spawn_command(action + 5);
        return;
    }
    wlr_log(WLR_ERROR, "unknown action: %s", action);
}

void wm_set_hooks(struct wm_server *server, const struct wm_hooks *hooks, void *data) {
    if (hooks == NULL) {
        memset(&server->hooks, 0, sizeof(server->hooks));
        server->hooks_data = NULL;
        return;
    }
    server->hooks = *hooks;
    server->hooks_data = data;
}

void wm_set_inset_bottom(struct wm_server *server, int pixels) {
    server->inset_bottom = pixels < 0 ? 0 : pixels;
}

void wm_set_inset_top(struct wm_server *server, int pixels) {
    server->inset_top = pixels < 0 ? 0 : pixels;
}

void wm_relayout(struct wm_server *server) {
    layout_toplevels(server);
}

void wm_foreach_toplevel(struct wm_server *server, void (*fn)(struct wm_toplevel *t, void *data), void *data) {
    struct wm_toplevel *t, *tmp;
    wl_list_for_each_safe(t, tmp, &server->toplevels, link) {
        fn(t, data);
    }
}

struct wlr_scene_tree *wm_overlay_tree(struct wm_server *server) {
    return &server->scene->tree;
}

double wm_cursor_x(struct wm_server *server) {
    return server->cursor->x;
}

double wm_cursor_y(struct wm_server *server) {
    return server->cursor->y;
}

void *wm_toplevel_userdata(struct wm_toplevel *t) {
    return t->userdata;
}

void wm_toplevel_set_userdata(struct wm_toplevel *t, void *userdata) {
    t->userdata = userdata;
}

void wm_toplevel_close(struct wm_toplevel *t) {
    toplevel_request_close(t);
}

void wm_toplevel_toggle_fullscreen(struct wm_toplevel *t) {
    if (t == NULL) return;
    toplevel_set_fullscreen(t, t->is_fullscreen == false);
}

void wm_toplevel_detach(struct wm_toplevel *t) {
    if (t == NULL) return;
    detach_window(t->server, t);
}

static void keyboard_handle_key(struct wl_listener *listener, void *data) {
    struct wm_keyboard *keyboard = wl_container_of(listener, keyboard, key);
    struct wm_server *server = keyboard->server;
    struct wlr_keyboard_key_event *event = data;
    struct wlr_seat *seat = server->seat;

    wm_notify_activity(server);

    uint32_t keycode = event->keycode + 8;
    const xkb_keysym_t *syms;
    int nsyms = xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state, keycode, &syms);

    bool handled = false;
    uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard) & WM_HOTKEY_MOD_MASK;
    uint32_t code = event->keycode;

    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        if (code < WM_KEY_SWALLOW_MAX && (keyboard->swallowed[code / 8] & (uint8_t)(1u << (code % 8)))) {
            handled = true;
        }
        for (int i = 0; i < nsyms && handled == false; i++) {
            struct wm_hotkey *hk;
            xkb_keysym_t base_sym = xkb_keysym_to_lower(syms[i]);
            struct xkb_keymap *km = xkb_state_get_keymap(keyboard->wlr_keyboard->xkb_state);
            const xkb_keysym_t *layout0_syms = NULL;
            if (km != NULL && xkb_keymap_key_get_syms_by_level(km, keycode, 0, 0, &layout0_syms) > 0 && layout0_syms != NULL) {
                base_sym = xkb_keysym_to_lower(layout0_syms[0]);
            }
            if (code >= 2 && code <= 10) {
                base_sym = XKB_KEY_1 + (code - 2);
            }
            wl_list_for_each(hk, &server->hotkeys, link) {
                xkb_keysym_t target = xkb_keysym_to_lower(hk->keysym);
                if ((target == xkb_keysym_to_lower(syms[i]) || target == base_sym) && modifiers == hk->modifiers) {
                    wm_execute_action(server, server->active_toplevel, hk->action);
                    handled = true;
                    break;
                }
            }
        }
        if (handled && code < WM_KEY_SWALLOW_MAX) {
            keyboard->swallowed[code / 8] |= (uint8_t)(1u << (code % 8));
        }
    } else if (event->state == WL_KEYBOARD_KEY_STATE_RELEASED) {
        if (code < WM_KEY_SWALLOW_MAX) {
            uint8_t bit = (uint8_t)(1u << (code % 8));
            if (keyboard->swallowed[code / 8] & bit) {
                keyboard->swallowed[code / 8] &= (uint8_t)~bit;
                handled = true;
            }
        }
    }

    if (handled == false) {
        wlr_seat_set_keyboard(seat, keyboard->wlr_keyboard);
        wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode, event->state);
    }
}

static void keyboard_handle_destroy(struct wl_listener *listener, void *data) {
	(void)data;

	struct wm_keyboard *keyboard =
		wl_container_of(listener, keyboard, destroy);
	wl_list_remove(&keyboard->modifiers.link);
	wl_list_remove(&keyboard->key.link);
	wl_list_remove(&keyboard->destroy.link);
	wl_list_remove(&keyboard->link);
	free(keyboard);
}

static void server_new_keyboard(struct wm_server *server,
		struct wlr_input_device *device) {
	struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

	struct wm_keyboard *keyboard = calloc(1, sizeof(*keyboard));
	if (keyboard == NULL) return;
	keyboard->server = server;
	keyboard->wlr_keyboard = wlr_keyboard;
	wl_list_init(&keyboard->modifiers.link);
	wl_list_init(&keyboard->key.link);
	wl_list_init(&keyboard->destroy.link);

	struct xkb_rule_names rules = {
		.rules = NULL,
		.model = NULL,
		.layout = server->config.xkb_layout[0] ? server->config.xkb_layout : NULL,
		.variant = NULL,
		.options = server->config.xkb_options[0] ? server->config.xkb_options : NULL,
	};
	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, &rules,
		XKB_KEYMAP_COMPILE_NO_FLAGS);

	wlr_keyboard_set_keymap(wlr_keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);
	wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);

	keyboard->modifiers.notify = keyboard_handle_modifiers;
	wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);
	keyboard->key.notify = keyboard_handle_key;
	wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);
	keyboard->destroy.notify = keyboard_handle_destroy;
	wl_signal_add(&device->events.destroy, &keyboard->destroy);

	wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);

	wl_list_insert(&server->keyboards, &keyboard->link);
}

static void server_new_pointer(struct wm_server *server,
		struct wlr_input_device *device) {

	wlr_cursor_attach_input_device(server->cursor, device);
}

static void server_new_input(struct wl_listener *listener, void *data) {
	(void)data;

	struct wm_server *server =
		wl_container_of(listener, server, new_input);
	struct wlr_input_device *device = data;
	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		server_new_keyboard(server, device);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		server_new_pointer(server, device);
		break;
	default:
		break;
	}

	uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&server->keyboards)) {
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	}
	wlr_seat_set_capabilities(server->seat, caps);
}

static void seat_request_set_cursor_shape(struct wl_listener *listener, void *data) {
    (void)listener;
    struct wlr_cursor_shape_manager_v1_request_set_shape_event *event = data;
    struct wlr_seat_client *focused_client = event->seat_client->seat->pointer_state.focused_client;
    if (focused_client == event->seat_client) {
        struct wm_server *srv = event->seat_client->seat->data;
        if (srv != NULL) {
            if (event->shape == 0) {
                srv->cursor_hidden = true;
                wlr_cursor_unset_image(srv->cursor);
            } else {
                srv->cursor_hidden = false;
                wlr_cursor_set_xcursor(srv->cursor, srv->cursor_mgr, wlr_cursor_shape_v1_name(event->shape));
            }
        }
    }
}

static void seat_request_cursor(struct wl_listener *listener, void *data) {
    struct wm_server *server = wl_container_of(listener, server, request_cursor);
    struct wlr_seat_pointer_request_set_cursor_event *event = data;
    struct wlr_seat_client *focused_client = server->seat->pointer_state.focused_client;
    if (focused_client == event->seat_client) {
        if (event->surface == NULL) {
            server->cursor_hidden = true;
            wlr_cursor_unset_image(server->cursor);
        } else {
            server->cursor_hidden = false;
            wlr_cursor_set_surface(server->cursor, event->surface, event->hotspot_x, event->hotspot_y);
        }
    }
}

static void seat_request_start_drag(struct wl_listener *listener, void *data) {
    struct wm_server *server = wl_container_of(listener, server, request_start_drag);
    struct wlr_seat_request_start_drag_event *event = data;
    if (wlr_seat_validate_pointer_grab_serial(server->seat, event->origin, event->serial)) {
        wlr_seat_start_pointer_drag(server->seat, event->drag, event->serial);
    } else if (server->seat->pointer_state.button_count > 0) {
        wlr_seat_start_pointer_drag(server->seat, event->drag, server->seat->pointer_state.grab_serial);
    } else {
        wlr_data_source_destroy(event->drag->source);
    }
}

static void seat_start_drag(struct wl_listener *listener, void *data) {
    struct wm_server *server = wl_container_of(listener, server, start_drag);
    struct wlr_drag *drag = data;
    if (drag->icon != NULL && server->drag_icon_tree != NULL) {
        wlr_scene_drag_icon_create(server->drag_icon_tree, drag->icon);
        wlr_scene_node_set_position(&server->drag_icon_tree->node, (int)server->cursor->x, (int)server->cursor->y);
        wlr_scene_node_raise_to_top(&server->drag_icon_tree->node);
    }
}

static void seat_request_set_selection(struct wl_listener *listener, void *data) {
	(void)data;

	struct wm_server *server = wl_container_of(
			listener, server, request_set_selection);
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(server->seat, event->source, event->serial);
}

static struct wm_toplevel *desktop_toplevel_at(
		struct wm_server *server, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy) {

	struct wlr_scene_node *node = wlr_scene_node_at(
		&server->scene->tree.node, lx, ly, sx, sy);
	if (node == NULL) return NULL;
	if (node->type == WLR_SCENE_NODE_BUFFER) {
		struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
		struct wlr_scene_surface *scene_surface =
			wlr_scene_surface_try_from_buffer(scene_buffer);
		if (scene_surface == NULL) return NULL;
		struct wlr_scene_tree *tree = node->parent;
		while (tree && tree->node.data == NULL) {
			tree = tree->node.parent;
		}
		if (tree == NULL) return NULL;
		*surface = scene_surface->surface;
		return tree->node.data;
	}
	return NULL;
}

static void reset_cursor_mode(struct wm_server *server) {
	server->cursor_mode = WM_CURSOR_PASSTHROUGH;
	server->grabbed_toplevel = NULL;
	wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
}

static uint32_t wm_detect_resize_edges(const struct wm_toplevel *t, double cx, double cy) {
    if (t == NULL || t->is_floating == false) return 0;
    int m = 10;
    int x = t->scene_tree ? t->scene_tree->node.x : t->x;
    int y = t->scene_tree ? t->scene_tree->node.y : t->y;
    int w = t->width;
    int h = t->height;
    if (cx < x - m || cx > x + w + m || cy < y - m || cy > y + h + m) return 0;
    uint32_t edges = 0;
    if (cx <= x + m) edges |= WLR_EDGE_LEFT;
    else if (cx >= x + w - m) edges |= WLR_EDGE_RIGHT;
    if (cy <= y + m) edges |= WLR_EDGE_TOP;
    else if (cy >= y + h - m) edges |= WLR_EDGE_BOTTOM;
    return edges;
}

static const char *wm_resize_cursor_name(uint32_t edges) {
    if ((edges & WLR_EDGE_TOP) && (edges & WLR_EDGE_LEFT)) return "top_left_corner";
    if ((edges & WLR_EDGE_TOP) && (edges & WLR_EDGE_RIGHT)) return "top_right_corner";
    if ((edges & WLR_EDGE_BOTTOM) && (edges & WLR_EDGE_LEFT)) return "bottom_left_corner";
    if ((edges & WLR_EDGE_BOTTOM) && (edges & WLR_EDGE_RIGHT)) return "bottom_right_corner";
    if (edges & WLR_EDGE_LEFT) return "left_side";
    if (edges & WLR_EDGE_RIGHT) return "right_side";
    if (edges & WLR_EDGE_TOP) return "top_side";
    if (edges & WLR_EDGE_BOTTOM) return "bottom_side";
    return "default";
}

static struct wm_toplevel *wm_floating_border_at(struct wm_server *server, double cx, double cy, uint32_t *out_edges) {
    struct wm_toplevel *t;
    wl_list_for_each_reverse(t, &server->toplevels, link) {
        if (t->in_list == false || t->is_floating == false || t->is_minimized || t->is_fullscreen) continue;
        if (t->workspace != server->active_workspace) continue;
        uint32_t e = wm_detect_resize_edges(t, cx, cy);
        if (e != 0) {
            if (out_edges) *out_edges = e;
            return t;
        }
    }
    if (out_edges) *out_edges = 0;
    return NULL;
}

static struct wlr_surface *wm_layer_surface_at(struct wm_server *server, double lx, double ly, double *sx, double *sy) {
    struct wlr_scene_node *node = wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
    if (node == NULL || node->type != WLR_SCENE_NODE_BUFFER) return NULL;
    struct wlr_scene_buffer *sb = wlr_scene_buffer_from_node(node);
    struct wlr_scene_surface *ss = wlr_scene_surface_try_from_buffer(sb);
    if (ss == NULL) return NULL;
    struct wlr_scene_tree *tree = node->parent;
    while (tree && tree->node.data == NULL) {
        tree = tree->node.parent;
    }
    if (tree != NULL) return NULL;
    return ss->surface;
}

static void process_cursor_motion(struct wm_server *server, uint32_t time) {
    if (server->cursor_mode == WM_CURSOR_RESIZE && server->grabbed_toplevel != NULL) {
        struct wm_toplevel *t = server->grabbed_toplevel;
        double dx = server->cursor->x - server->grab_x;
        double dy = server->cursor->y - server->grab_y;
        int new_x = server->grab_geobox.x;
        int new_y = server->grab_geobox.y;
        int new_w = server->grab_geobox.width;
        int new_h = server->grab_geobox.height;
        if (server->resize_edges & WLR_EDGE_LEFT) {
            new_x += (int)dx;
            new_w -= (int)dx;
        } else if (server->resize_edges & WLR_EDGE_RIGHT) {
            new_w += (int)dx;
        }
        if (server->resize_edges & WLR_EDGE_TOP) {
            new_y += (int)dy;
            new_h -= (int)dy;
        } else if (server->resize_edges & WLR_EDGE_BOTTOM) {
            new_h += (int)dy;
        }
        if (new_w < 120) {
            if (server->resize_edges & WLR_EDGE_LEFT) new_x -= (120 - new_w);
            new_w = 120;
        }
        if (new_h < 80) {
            if (server->resize_edges & WLR_EDGE_TOP) new_y -= (80 - new_h);
            new_h = 80;
        }
        t->x = new_x;
        t->y = new_y;
        t->width = new_w;
        t->height = new_h;
        if (t->is_x11) wlr_xwayland_surface_configure(t->xwayland_surface, new_x, new_y, new_w, new_h);
        else wlr_xdg_toplevel_set_size(t->xdg_toplevel, new_w, new_h);
        if (t->scene_tree) wlr_scene_node_set_position(&t->scene_tree->node, new_x, new_y);
        wm_update_toplevel_border(t, t == server->active_toplevel);
        return;
    }

    uint32_t hover_edges = 0;
    struct wm_toplevel *edge_tl = wm_floating_border_at(server, server->cursor->x, server->cursor->y, &hover_edges);
    if (edge_tl != NULL && hover_edges != 0) {
        wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, wm_resize_cursor_name(hover_edges));
        wlr_seat_pointer_clear_focus(server->seat);
        return;
    }

    double sx, sy;
    struct wlr_seat *seat = server->seat;
    if (server->drag_icon_tree != NULL && seat->drag != NULL) {
        wlr_scene_node_set_position(&server->drag_icon_tree->node, (int)server->cursor->x, (int)server->cursor->y);
        wlr_scene_node_raise_to_top(&server->drag_icon_tree->node);
    }
    if (seat->drag == NULL && seat->pointer_state.button_count > 0 && seat->pointer_state.focused_surface != NULL) {
        struct wlr_surface *grab_surf = seat->pointer_state.focused_surface;
        struct wlr_surface *root_surf = wlr_surface_get_root_surface(grab_surf);
        struct wlr_xdg_surface *xdg_s = wlr_xdg_surface_try_from_wlr_surface(root_surf);
        if (xdg_s != NULL && xdg_s->data != NULL) {
            struct wlr_scene_tree *tree = xdg_s->data;
            struct wm_toplevel *gt = tree->node.data;
            if (gt != NULL) {
                sx = server->cursor->x - (gt->scene_tree ? gt->scene_tree->node.x : gt->x);
                sy = server->cursor->y - (gt->scene_tree ? gt->scene_tree->node.y : gt->y);
                wlr_seat_pointer_notify_motion(seat, time, sx, sy);
                return;
            }
        }
    }

    struct wlr_surface *surface = NULL;
    struct wm_toplevel *toplevel = desktop_toplevel_at(server,
            server->cursor->x, server->cursor->y, &surface, &sx, &sy);
    if (!toplevel) {
        bool in_fs = (server->active_toplevel && server->active_toplevel->is_fullscreen &&
                      server->active_toplevel->workspace == server->active_workspace);
        if (!in_fs && (seat->pointer_state.focused_surface != NULL || server->cursor_hidden)) {
            server->cursor_hidden = false;
            wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
        }
    } else {
        if (seat->drag == NULL && toplevel != server->active_toplevel && server->active_layer_surface == NULL) {
            focus_toplevel(toplevel);
        }
    }
    if (surface == NULL) {
        surface = wm_layer_surface_at(server, server->cursor->x, server->cursor->y, &sx, &sy);
    }
    if (surface) {
        if (seat->pointer_state.focused_surface != surface) {
            wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
        }
        wlr_seat_pointer_notify_motion(seat, time, sx, sy);
    } else {
        wlr_seat_pointer_clear_focus(seat);
    }
}

static void detach_window(struct wm_server *server, struct wm_toplevel *t) {
    if (t == NULL) return;
    if (wl_list_empty(&server->outputs)) return;
    server->drag_was_tiled = (t->is_floating == false);
    if (t->is_floating == false) {
        t->is_floating = true;
        struct wm_output *out = wl_container_of(server->outputs.next, out, link);
        int uw = server->usable_area.width > 0 ? server->usable_area.width : wm_output_width(out->wlr_output);
        int uh = server->usable_area.height > 0 ? server->usable_area.height : wm_output_height(out->wlr_output);
        t->width = uw / 2;
        t->height = uh / 2;
        t->x = (int)(server->cursor->x - t->width * 0.5);
        t->y = (int)(server->cursor->y - t->height * 0.5);
        if (t->is_x11) wlr_xwayland_surface_configure(t->xwayland_surface, t->x, t->y, t->width, t->height);
        else {
            wlr_xdg_toplevel_set_tiled(t->xdg_toplevel, 0);
            wlr_xdg_toplevel_set_size(t->xdg_toplevel, t->width, t->height);
        }
        if (t->scene_tree) {
            wlr_scene_node_set_position(&t->scene_tree->node, t->x, t->y);
            wlr_scene_node_raise_to_top(&t->scene_tree->node);
        }
        wm_notify_hidden(t);
    }
    if (t->scene_tree) {
        t->x = t->scene_tree->node.x;
        t->y = t->scene_tree->node.y;
    }
    server->window_dragging = true;
    server->dragging_toplevel = t;
    server->grab_x = server->cursor->x;
    server->grab_y = server->cursor->y;
    server->window_drag_offset_x = server->cursor->x - t->x;
    server->window_drag_offset_y = server->cursor->y - t->y;
    if (t->scene_tree) wlr_scene_node_raise_to_top(&t->scene_tree->node);
    layout_toplevels(server);
}

static void cursor_dispatch(struct wm_server *server, uint32_t time_msec) {
    wm_notify_activity(server);
    if (server->cursor_mode == WM_CURSOR_RESIZE && server->grabbed_toplevel != NULL) {
        process_cursor_motion(server, time_msec);
        return;
    }
    if (server->window_dragging && server->dragging_toplevel) {
        struct wm_toplevel *t = server->dragging_toplevel;
        int nx = (int)(server->cursor->x - server->window_drag_offset_x);
        int ny = (int)(server->cursor->y - server->window_drag_offset_y);
        t->x = t->anim_cur_x = t->anim_target_x = nx;
        t->y = t->anim_cur_y = t->anim_target_y = ny;
        t->anim_active = false; t->anim_pos_init = true;
        if (t->scene_tree) {
            wlr_scene_node_set_position(&t->scene_tree->node, nx, ny);
            wlr_scene_node_raise_to_top(&t->scene_tree->node);
        }
        wm_update_toplevel_border(t, t == server->active_toplevel);
        return;
    }
    if (server->exit_fullscreen_dragging && server->dragging_toplevel) {
        struct wm_toplevel *t = server->dragging_toplevel;
        double dy = server->cursor->y - server->exit_fullscreen_start_y;
        int max_lift = (t->height > 0) ? (t->height / 3) : 240;
        if (dy < -max_lift) dy = -max_lift;
        if (t->scene_tree) {
            if (dy < 0) wlr_scene_node_set_position(&t->scene_tree->node, 0, (int)dy);
            else wlr_scene_node_set_position(&t->scene_tree->node, 0, 0);
        }
        return;
    }
    if (server->hooks.pointer_motion) {
        if (server->hooks.pointer_motion(server, time_msec, server->hooks_data)) return;
    }
    process_cursor_motion(server, time_msec);
}

static bool wm_constraint_matches_focus(struct wm_server *server) {
    if (server->active_constraint == NULL) return false;
    struct wlr_surface *focused = server->seat->pointer_state.focused_surface;
    if (focused == NULL) return false;
    return wlr_surface_get_root_surface(focused) == wlr_surface_get_root_surface(server->active_constraint->surface);
}

static void pointer_constraint_destroy(struct wl_listener *listener, void *data) {
    struct wm_server *server = wl_container_of(listener, server, constraint_destroy);
    struct wlr_pointer_constraint_v1 *constraint = data;
    wl_list_remove(&server->constraint_destroy.link);
    if (server->active_constraint == constraint) {
        if (constraint->current.committed & WLR_POINTER_CONSTRAINT_V1_STATE_CURSOR_HINT) {
            struct wm_toplevel *t = server->active_toplevel;
            double ox = (t && t->scene_tree) ? t->scene_tree->node.x : (t ? t->x : 0);
            double oy = (t && t->scene_tree) ? t->scene_tree->node.y : (t ? t->y : 0);
            wlr_cursor_warp(server->cursor, NULL, ox + constraint->current.cursor_hint.x, oy + constraint->current.cursor_hint.y);
        }
        server->active_constraint = NULL;
        server->cursor_hidden = false;
        wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
    }
}

static void server_new_pointer_constraint(struct wl_listener *listener, void *data) {
    struct wm_server *server = wl_container_of(listener, server, new_pointer_constraint);
    struct wlr_pointer_constraint_v1 *constraint = data;
    if (server->active_constraint != NULL && server->active_constraint != constraint) {
        wlr_pointer_constraint_v1_send_deactivated(server->active_constraint);
        wl_list_remove(&server->constraint_destroy.link);
    }
    server->active_constraint = constraint;
    wl_list_init(&server->constraint_destroy.link);
    server->constraint_destroy.notify = pointer_constraint_destroy;
    wl_signal_add(&constraint->events.destroy, &server->constraint_destroy);
    wlr_pointer_constraint_v1_send_activated(constraint);
    if (constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
        server->cursor_hidden = true;
        wlr_cursor_unset_image(server->cursor);
    }
}

static void server_cursor_motion(struct wl_listener *listener, void *data) {
    struct wm_server *server = wl_container_of(listener, server, cursor_motion);
    struct wlr_pointer_motion_event *event = data;
    wm_notify_activity(server);
    if (server->relative_pointer_mgr != NULL) {
        uint64_t time_usec = (uint64_t)event->time_msec * 1000ULL;
        wlr_relative_pointer_manager_v1_send_relative_motion(
            server->relative_pointer_mgr, server->seat, time_usec,
            event->delta_x, event->delta_y,
            event->unaccel_dx, event->unaccel_dy);
    }
    if (wm_constraint_matches_focus(server)) {
        if (server->active_constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
            if (!server->cursor_hidden) {
                server->cursor_hidden = true;
                wlr_cursor_unset_image(server->cursor);
            }
            return;
        }
        if (server->active_constraint->type == WLR_POINTER_CONSTRAINT_V1_CONFINED && server->active_toplevel) {
            struct wm_toplevel *at = server->active_toplevel;
            double nx = server->cursor->x + event->delta_x;
            double ny = server->cursor->y + event->delta_y;
            double min_x = at->x, max_x = at->x + (at->width > 1 ? at->width - 1 : 0);
            double min_y = at->y, max_y = at->y + (at->height > 1 ? at->height - 1 : 0);
            if (nx < min_x) nx = min_x;
            if (nx > max_x) nx = max_x;
            if (ny < min_y) ny = min_y;
            if (ny > max_y) ny = max_y;
            wlr_cursor_warp(server->cursor, &event->pointer->base, nx, ny);
            cursor_dispatch(server, event->time_msec);
            return;
        }
    }
    wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x, event->delta_y);
    cursor_dispatch(server, event->time_msec);
}

static void server_cursor_motion_absolute(struct wl_listener *listener, void *data) {
    struct wm_server *server = wl_container_of(listener, server, cursor_motion_absolute);
    struct wlr_pointer_motion_absolute_event *event = data;
    wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
    cursor_dispatch(server, event->time_msec);
}


static void server_cursor_button(struct wl_listener *listener, void *data) {
    struct wm_server *server = wl_container_of(listener, server, cursor_button);
    struct wlr_pointer_button_event *event = data;
    wm_notify_activity(server);
    if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
        if (event->button == 272) {
            uint32_t edges = 0;
            struct wm_toplevel *et = wm_floating_border_at(server, server->cursor->x, server->cursor->y, &edges);
            if (et != NULL && edges != 0) {
                focus_toplevel(et);
                if (et->scene_tree) {
                    et->x = et->scene_tree->node.x;
                    et->y = et->scene_tree->node.y;
                    wlr_scene_node_raise_to_top(&et->scene_tree->node);
                }
                server->cursor_mode = WM_CURSOR_RESIZE;
                server->grabbed_toplevel = et;
                server->grab_x = server->cursor->x;
                server->grab_y = server->cursor->y;
                server->grab_geobox.x = et->x;
                server->grab_geobox.y = et->y;
                server->grab_geobox.width = et->width;
                server->grab_geobox.height = et->height;
                server->resize_edges = edges;
                wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, wm_resize_cursor_name(edges));
                return;
            }
        }
        struct wlr_keyboard *kb = wlr_seat_get_keyboard(server->seat);
        if (kb && (wlr_keyboard_get_modifiers(kb) & WLR_MODIFIER_LOGO)) {
            double ssx, ssy;
            struct wlr_surface *ssurf = NULL;
            struct wm_toplevel *st = desktop_toplevel_at(server, server->cursor->x,
                server->cursor->y, &ssurf, &ssx, &ssy);
            if (st) {
                focus_toplevel(st);
                if (event->button == 273) {
                    if (st->is_floating == false) {
                        st->is_floating = true;
                        if (st->is_x11 == false && st->xdg_toplevel != NULL) {
                            wlr_xdg_toplevel_set_tiled(st->xdg_toplevel, 0);
                        }
                        if (st->scene_tree) wlr_scene_node_raise_to_top(&st->scene_tree->node);
                        wm_notify_hidden(st);
                        layout_toplevels(server);
                    }
                    if (st->scene_tree) {
                        st->x = st->scene_tree->node.x;
                        st->y = st->scene_tree->node.y;
                    }
                    server->cursor_mode = WM_CURSOR_RESIZE;
                    server->grabbed_toplevel = st;
                    server->grab_x = server->cursor->x;
                    server->grab_y = server->cursor->y;
                    server->grab_geobox.x = st->x;
                    server->grab_geobox.y = st->y;
                    server->grab_geobox.width = st->width;
                    server->grab_geobox.height = st->height;
                    uint32_t edges = 0;
                    edges |= (server->cursor->x < st->x + st->width / 2.0) ? WLR_EDGE_LEFT : WLR_EDGE_RIGHT;
                    edges |= (server->cursor->y < st->y + st->height / 2.0) ? WLR_EDGE_TOP : WLR_EDGE_BOTTOM;
                    server->resize_edges = edges;
                    return;
                }
                detach_window(server, st);
                return;
            }
        }
        if (server->active_toplevel && server->active_toplevel->is_fullscreen
                && wl_list_empty(&server->outputs) == 0) {
            struct wm_output *o = wl_container_of(server->outputs.next, o, link);
            if (server->cursor->y > wm_output_height(o->wlr_output) - 50) {
                server->exit_fullscreen_dragging = true;
                server->exit_fullscreen_start_y = server->cursor->y;
                server->dragging_toplevel = server->active_toplevel;
                return;
            }
        }
        if (server->hooks.pointer_button) {
            if (server->hooks.pointer_button(server, event, server->hooks_data)) return;
        }
    } else if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        if (server->cursor_mode == WM_CURSOR_RESIZE) {
            reset_cursor_mode(server);
            return;
        }
        if (server->window_dragging && server->dragging_toplevel) {
            struct wm_toplevel *t = server->dragging_toplevel;
            bool was_tiled = server->drag_was_tiled;
            server->window_dragging = false; server->dragging_toplevel = NULL; server->drag_was_tiled = false;
            if (was_tiled && !server->workspace_floating[server->active_workspace]) {
                t->workspace = server->active_workspace; t->is_floating = false;
                if (t->is_x11 == false && t->xdg_toplevel != NULL) {
                    wlr_xdg_toplevel_set_tiled(t->xdg_toplevel,
                        WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
                }
                struct wm_toplevel *drop_target = NULL, *cand;
                wl_list_for_each(cand, &server->toplevels, link) {
                    if (cand == t || !cand->in_list || cand->is_floating || cand->is_minimized) continue;
                    if (cand->workspace != server->active_workspace) continue;
                    if (server->cursor->x >= cand->x && server->cursor->x <= cand->x + cand->width &&
                        server->cursor->y >= cand->y && server->cursor->y <= cand->y + cand->height) {
                        drop_target = cand;
                        break;
                    }
                }
                if (drop_target) {
                    grid_swap_windows(server, t, drop_target);
                } else {
                    grid_place_at_cursor(server, t);
                }
                layout_toplevels(server); focus_toplevel(t);
            }
            return;
        }
        if (server->exit_fullscreen_dragging) {
            double dy = server->cursor->y - server->exit_fullscreen_start_y;
            server->exit_fullscreen_dragging = false;
            struct wm_toplevel *t = server->dragging_toplevel;
            server->dragging_toplevel = NULL;
            if (t) {
                if (dy < -40 && t->is_fullscreen) {
                    toplevel_set_fullscreen(t, false);
                } else if (t->scene_tree) {
                    wlr_scene_node_set_position(&t->scene_tree->node, 0, 0);
                }
            }
            return;
        }
        if (server->hooks.pointer_button) {
            if (server->hooks.pointer_button(server, event, server->hooks_data)) return;
        }
    }
    wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);
    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        reset_cursor_mode(server);
    } else {
        double sx, sy;
        struct wlr_surface *surface = NULL;
        struct wm_toplevel *toplevel = desktop_toplevel_at(server, server->cursor->x,
            server->cursor->y, &surface, &sx, &sy);
        if (toplevel != NULL) {
            if (server->active_layer_surface != NULL) server->active_layer_surface = NULL;
            focus_toplevel(toplevel);
        } else {
            struct wlr_surface *lsurf = wm_layer_surface_at(server, server->cursor->x, server->cursor->y, &sx, &sy);
            if (lsurf != NULL) {
                wlr_seat_pointer_notify_enter(server->seat, lsurf, sx, sy);
                struct wlr_layer_surface_v1 *wls = wlr_layer_surface_v1_try_from_wlr_surface(wlr_surface_get_root_surface(lsurf));
                if (wls != NULL && wls->current.keyboard_interactive != ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE) {
                    server->active_layer_surface = wls->surface;
                    struct wlr_keyboard *kb = wlr_seat_get_keyboard(server->seat);
                    if (kb != NULL) wlr_seat_keyboard_notify_enter(server->seat, wls->surface, kb->keycodes, kb->num_keycodes, &kb->modifiers);
                }
            }
        }
    }
}

static void server_cursor_axis(struct wl_listener *listener, void *data) {
    struct wm_server *server = wl_container_of(listener, server, cursor_axis);
    struct wlr_pointer_axis_event *event = data;
    wm_notify_activity(server);
    struct wlr_keyboard *kb = wlr_seat_get_keyboard(server->seat);
    if (kb != NULL && (wlr_keyboard_get_modifiers(kb) & WLR_MODIFIER_LOGO)) {
        if (event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL) {
            grid_handle_scroll(server, event->time_msec, event->delta);
        }
        return;
    }
    wlr_seat_pointer_notify_axis(server->seat,
        event->time_msec, event->orientation, event->delta,
        event->delta_discrete, event->source, event->relative_direction);
}

static void server_cursor_frame(struct wl_listener *listener, void *data) {
	(void)data;

	struct wm_server *server =
		wl_container_of(listener, server, cursor_frame);

	wlr_seat_pointer_notify_frame(server->seat);
}

/* Output & Idle subsystem moved to src/modules/output.c */

static void wm_update_toplevel_border(struct wm_toplevel *t, bool is_active);
static void toplevel_map_common(struct wm_toplevel *toplevel);
static void toplevel_unmap_common(struct wm_toplevel *toplevel);
static void toplevel_destroy_common(struct wm_toplevel *toplevel);

/* Output & Idle subsystem moved to src/modules/output.c */

static void wm_update_toplevel_border(struct wm_toplevel *t, bool is_active) {
    if (t == NULL || t->border == NULL || t->scene_tree == NULL) return;
    if (t->is_fullscreen || t->is_minimized || (t->workspace != t->server->active_workspace && !t->is_pinned)) {
        wlr_scene_node_set_enabled(&t->border->node, false);
        if (t->shadow) wlr_scene_node_set_enabled(&t->shadow->node, false);
        return;
    }
    int bw = t->server->config.border_width;
    if (bw <= 0) {
        wlr_scene_node_set_enabled(&t->border->node, false);
        return;
    }
    int bx = t->scene_tree->node.x;
    int by = t->scene_tree->node.y;
    int br = t->server->config.corner_radius;
    wlr_scene_rect_set_size(t->border, t->width + bw * 2, t->height + bw * 2);
    wlr_scene_node_set_position(&t->border->node, bx - bw, by - bw);
    wlr_scene_node_set_enabled(&t->border->node, true);
    wlr_scene_rect_set_corner_radius(t->border, br > 0 ? br : 0, CORNER_LOCATION_ALL);
    const float *bcol = (is_active && t->has_rule_border) ? t->rule_border_color : (is_active ? t->server->config.border_color_active : t->server->config.border_color_inactive);
    wlr_scene_rect_set_color(t->border, bcol);
    if (t->shadow) {
        int sr = t->server->config.shadow_range;
        wlr_scene_node_set_enabled(&t->shadow->node, t->server->config.shadow && !t->is_fullscreen);
        wlr_scene_shadow_set_size(t->shadow, t->width + sr, t->height + sr);
        wlr_scene_node_set_position(&t->shadow->node, bx - sr / 2, by - sr / 2 + 4);
        wlr_scene_node_place_below(&t->shadow->node, &t->border->node);
    }
    if (!t->anim_fade) {
        float op = is_active ? (t->opacity_active > 0.0f ? t->opacity_active : t->server->config.active_opacity)
                             : (t->opacity_inactive > 0.0f ? t->opacity_inactive : t->server->config.inactive_opacity);
        bool bl = (t->rule_blur >= 0) ? (t->rule_blur == 1) : t->server->config.blur;
        wm_apply_surface_fx(&t->scene_tree->node, bl, op);
    }
    struct clipped_region hole = {
        .area = { bw, bw, t->width, t->height },
        .corner_radius = br,
        .corners = CORNER_LOCATION_ALL,
    };
    wlr_scene_rect_set_clipped_region(t->border, hole);
    wlr_scene_node_place_below(&t->border->node, &t->scene_tree->node);
}

static void focus_toplevel(struct wm_toplevel *toplevel) {
    if (toplevel == NULL) return;
    if (toplevel->in_list == false) return;
    struct wm_server *server = toplevel->server;

    struct wlr_surface *surface = toplevel_surface(toplevel);
    if (surface == NULL) return;
    struct wlr_surface *prev_surface = server->seat->keyboard_state.focused_surface;
    bool same_surface = (prev_surface == surface);

    if (prev_surface && same_surface == false) {
        struct wlr_xdg_toplevel *prev = wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
        if (prev) wlr_xdg_toplevel_set_activated(prev, false);
        struct wlr_xwayland_surface *prev_x11 = wlr_xwayland_surface_try_from_wlr_surface(prev_surface);
        if (prev_x11) wlr_xwayland_surface_activate(prev_x11, false);
    }

    server->active_toplevel = toplevel;
    if (toplevel->workspace < 16) server->workspace_focus[toplevel->workspace] = toplevel;
    if (toplevel->is_floating && toplevel->scene_tree != NULL) {
        wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
    }
    struct wm_toplevel *bt;
    wl_list_for_each(bt, &server->toplevels, link) {
        bool act = (bt == toplevel && server->active_layer_surface == NULL);
        wm_update_toplevel_border(bt, act);
    }

    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
    if (keyboard) {
        wlr_seat_keyboard_notify_enter(server->seat, surface,
            keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
    }
    if (toplevel->is_x11) wlr_xwayland_surface_activate(toplevel->xwayland_surface, true);
    else wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);
    if (same_surface == false && server->active_constraint != NULL && !wm_constraint_matches_focus(server)) {
        wlr_pointer_constraint_v1_send_deactivated(server->active_constraint);
        wl_list_remove(&server->constraint_destroy.link);
        server->active_constraint = NULL;
        server->cursor_hidden = false;
        wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
    }
    if (server->inset_bottom > 0 && same_surface == false) {
        layout_toplevels(server);
    }
}

static void focus_next_toplevel(struct wm_server *server, bool forward) {
    int count = wl_list_length(&server->toplevels);
    if (count < 2) return;
    struct wm_toplevel *cur = server->active_toplevel;
    struct wm_toplevel *start;
    if (cur != NULL && cur->in_list) {
        start = wl_container_of(forward ? cur->link.next : cur->link.prev, start, link);
    } else {
        start = wl_container_of(forward ? server->toplevels.next : server->toplevels.prev, start, link);
    }
    struct wm_toplevel *t = start;
    for (int i = 0; i < count; i++) {
        if (t->workspace == server->active_workspace && t->in_list && t != cur) {
            focus_toplevel(t);
            return;
        }
        t = wl_container_of(forward ? t->link.next : t->link.prev, t, link);
    }
}


static void apply_corner_radius_to_node(struct wlr_scene_node *node, int radius) {
    if (node->type == WLR_SCENE_NODE_BUFFER) {
        struct wlr_scene_buffer *buffer = wlr_scene_buffer_from_node(node);
        if (buffer) {
            wlr_scene_buffer_set_corner_radius(buffer, radius, CORNER_LOCATION_ALL);
        }
    } else if (node->type == WLR_SCENE_NODE_TREE) {
        struct wlr_scene_tree *tree = wlr_scene_tree_from_node(node);
        struct wlr_scene_node *child;
        wl_list_for_each(child, &tree->children, link) {
            apply_corner_radius_to_node(child, radius);
        }
    }
}

static void toplevel_set_fullscreen(struct wm_toplevel *t, bool fullscreen) {
    if (!t || t->is_fullscreen == fullscreen) return;
    if (t->is_x11 && !t->xwayland_surface) return;
    if (t->is_x11) wlr_xwayland_surface_set_fullscreen(t->xwayland_surface, fullscreen);
    else wlr_xdg_toplevel_set_fullscreen(t->xdg_toplevel, fullscreen);
    t->is_fullscreen = fullscreen;
    layout_toplevels(t->server);
}

void wm_hide_toplevel_internal(struct wm_toplevel *t) {
    if (t->scene_tree) wlr_scene_node_set_enabled(&t->scene_tree->node, false);
    if (t->border) wlr_scene_node_set_enabled(&t->border->node, false);
    if (t->shadow) wlr_scene_node_set_enabled(&t->shadow->node, false);
}

static void toplevel_hide(struct wm_toplevel *t) {
    if (t->scene_tree == NULL) return;
    wlr_scene_node_set_enabled(&t->scene_tree->node, false);
    if (t->border) wlr_scene_node_set_enabled(&t->border->node, false);
    if (t->shadow) wlr_scene_node_set_enabled(&t->shadow->node, false);
    wm_notify_hidden(t);
}

static void toplevel_apply_fullscreen_geometry(struct wm_toplevel *t, struct wlr_output *output) {
    if (t->scene_tree == NULL) return;
    t->x = 0;
    t->y = 0;
    t->width = wm_output_width(output);
    t->height = wm_output_height(output);
    wlr_scene_node_set_enabled(&t->scene_tree->node, true);
    if (t->is_x11) wlr_xwayland_surface_configure(t->xwayland_surface, 0, 0, t->width, t->height);
    else wlr_xdg_toplevel_set_size(t->xdg_toplevel, t->width, t->height);
    wlr_scene_node_set_position(&t->scene_tree->node, 0, 0);
    wlr_scene_node_raise_to_top(&t->scene_tree->node);
    apply_corner_radius_to_node(&t->scene_tree->node, 0);
    wm_apply_surface_fx(&t->scene_tree->node, false, 1.0f);
    if (t->border) wlr_scene_node_set_enabled(&t->border->node, false);
    if (t->shadow) wlr_scene_node_set_enabled(&t->shadow->node, false);
    wm_notify_hidden(t);
}

static void toplevel_place_content(struct wm_toplevel *t, const struct wm_rect *cell, bool is_active) {
    if (t->scene_tree == NULL) return;
    int top_gap = (t->server->inset_top > 0) ? 6 : 0;
    int top_in = t->server->inset_top;
    int bot_gap = (t->server->inset_bottom > 0) ? 6 : 0;
    int bot_in = t->server->inset_bottom;
    int content_y = cell->y + top_in + top_gap;
    int content_h = cell->h - top_in - top_gap - bot_in - bot_gap;
    if (content_h < 1) {
        content_y = cell->y;
        content_h = 1;
        bot_in = 0;
    }
    bool size_changed = (t->width != cell->w || t->height != content_h);
    t->x = cell->x;
    t->y = content_y;
    t->width = cell->w;
    t->height = content_h;
    wlr_scene_node_set_enabled(&t->scene_tree->node, true);
    if (t->is_x11) {
        wlr_xwayland_surface_configure(t->xwayland_surface, cell->x, content_y, cell->w, content_h);
    } else {
        wlr_xdg_toplevel_set_tiled(t->xdg_toplevel, WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
        if (size_changed) wlr_xdg_toplevel_set_size(t->xdg_toplevel, cell->w, content_h);
    }
    wm_animate_to(t, cell->x, content_y, false);

    apply_corner_radius_to_node(&t->scene_tree->node, t->server->config.corner_radius);

    wm_update_toplevel_border(t, is_active);

    struct wm_placement place = {
        .content = { cell->x, content_y, cell->w, content_h },
        .inset = { cell->x, content_y + content_h + bot_gap, cell->w, bot_in },
        .active = is_active,
    };
    wm_notify_placed(t, &place);
}

static void layout_toplevels(struct wm_server *server) {
    if (wl_list_empty(&server->outputs)) return;
    struct wm_output *output = wl_container_of(server->outputs.next, output, link);
    int sw = wm_output_width(output->wlr_output);
    int sh = wm_output_height(output->wlr_output);
    if (sw <= 0 || sh <= 0) return;

    struct wm_toplevel *t;
    struct wm_toplevel *fullscreen_toplevel = NULL;
    wl_list_for_each(t, &server->toplevels, link) {
        if (t->is_fullscreen && !t->is_minimized && (t->workspace == server->active_workspace || t->is_pinned)) {
            fullscreen_toplevel = t;
            break;
        }
    }
    if (fullscreen_toplevel) {
        toplevel_apply_fullscreen_geometry(fullscreen_toplevel, output->wlr_output);
        wl_list_for_each(t, &server->toplevels, link) {
            if (t == fullscreen_toplevel) continue;
            toplevel_hide(t);
        }
        return;
    }

    int capacity = grid_effective_capacity(server);
    if (capacity > 256) capacity = 256;
    struct wm_toplevel *wins[256];
    memset(wins, 0, sizeof(wins));
    int count = 0;
    wl_list_for_each(t, &server->toplevels, link) {
        bool is_dragged = (server->window_dragging && server->dragging_toplevel == t);
        bool on_workspace = (t->workspace == server->active_workspace || t->is_pinned);
        if (on_workspace == false && is_dragged == false) {
            toplevel_hide(t);
            continue;
        }
        if (t->is_minimized) {
            toplevel_hide(t);
            continue;
        }
        if (t->is_floating) {
            if (t->scene_tree) {
                wlr_scene_node_set_enabled(&t->scene_tree->node, true);
                if (is_dragged) wlr_scene_node_raise_to_top(&t->scene_tree->node);
            }
            bool act = (server->active_toplevel == t && server->active_layer_surface == NULL);
            wm_update_toplevel_border(t, act);
            wm_notify_hidden(t);
            continue;
        }
        if (count < capacity) {
            wins[count] = t;
            count++;
        } else {
            toplevel_hide(t);
        }
    }

    grid_assign_slots(server, wins, count);
    grid_refresh_hud(server);

    (void)server->active_layer_surface;
    int ux = server->usable_area.width > 0 ? server->usable_area.x : 0;
    int uy = server->usable_area.height > 0 ? server->usable_area.y : 0;
    int uw = server->usable_area.width > 0 ? server->usable_area.width : sw;
    int uh = server->usable_area.height > 0 ? server->usable_area.height : sh;
    for (int i = 0; i < count; i++) {
        struct wm_rect cell;
        grid_cell(server, uw, uh, wins[i]->slot, &cell);
        cell.x += ux;
        cell.y += uy;
        bool active = (server->active_toplevel == wins[i]) && (wins[i]->anim_fade == false);
        toplevel_place_content(wins[i], &cell, active);
    }
}

static void wm_apply_window_rules(struct wm_server *server, struct wm_toplevel *t) {
    if (!server || !t) return;
    t->rule_blur = -1; t->opacity_active = -1.0f; t->opacity_inactive = -1.0f;
    if (server->config.rule_count <= 0) return;
    const char *app_id = "", *title = "";
    if (t->is_x11 && t->xwayland_surface) {
        if (t->xwayland_surface->class) app_id = t->xwayland_surface->class;
        if (t->xwayland_surface->title) title = t->xwayland_surface->title;
    } else if (t->xdg_toplevel) {
        if (t->xdg_toplevel->app_id) app_id = t->xdg_toplevel->app_id;
        if (t->xdg_toplevel->title) title = t->xdg_toplevel->title;
    }
    int sw = 1280, sh = 720;
    if (!wl_list_empty(&server->outputs)) {
        struct wm_output *out = wl_container_of(server->outputs.next, out, link);
        sw = wm_output_width(out->wlr_output); sh = wm_output_height(out->wlr_output);
    }
    for (int i = 0; i < server->config.rule_count; i++) {
        const struct wm_window_rule *r = &server->config.rules[i];
        if (r->app_id[0] != 0 && strstr(app_id, r->app_id) == NULL) continue;
        if (r->title[0] != 0 && strstr(title, r->title) == NULL) continue;
        if (r->workspace >= 0 && r->workspace < server->config.workspaces) t->workspace = r->workspace;
        if (r->blur >= 0) t->rule_blur = r->blur;
        if (r->pin == 1) { t->is_pinned = true; t->is_floating = true; }
        if (r->no_anim == 1) t->no_anim = true;
        if (r->opacity_active > 0.0f) t->opacity_active = r->opacity_active;
        if (r->opacity_inactive > 0.0f) t->opacity_inactive = r->opacity_inactive;
        if (r->has_border_color) { t->has_rule_border = true; memcpy(t->rule_border_color, r->border_color, sizeof(t->rule_border_color)); }
        if (r->floating == 1) {
            t->is_floating = true; t->slot = -1;
            if (r->width > 40) t->width = r->width; else if (t->width < 120) t->width = sw / 2;
            if (r->height > 40) t->height = r->height; else if (t->height < 80) t->height = sh / 2;
            if (r->center != 0) { t->x = (sw - t->width) / 2; t->y = (sh - t->height) / 2; }
            wm_apply_floating_geometry_internal(t);
        } else if (r->floating == 0) {
            t->is_floating = false;
        }
        if (r->fullscreen == 1) toplevel_set_fullscreen(t, true);
    }
}

static bool wm_toplevel_is_dialog_or_fixed(struct wm_toplevel *t, int *out_w, int *out_h) {
    if (!t) return false;
    if (!t->is_x11 && t->xdg_toplevel) {
        if (t->xdg_toplevel->parent != NULL) {
            if (out_w) *out_w = t->xdg_toplevel->base->current.geometry.width;
            if (out_h) *out_h = t->xdg_toplevel->base->current.geometry.height;
            return true;
        }
        int min_w = t->xdg_toplevel->current.min_width;
        int max_w = t->xdg_toplevel->current.max_width;
        int min_h = t->xdg_toplevel->current.min_height;
        int max_h = t->xdg_toplevel->current.max_height;
        if (min_w > 0 && max_w > 0 && min_w == max_w && min_h > 0 && max_h > 0 && min_h == max_h) {
            if (out_w) *out_w = min_w;
            if (out_h) *out_h = min_h;
            return true;
        }
    } else if (t->is_x11 && t->xwayland_surface) {
        struct wlr_xwayland_surface *xs = t->xwayland_surface;
        if (xs->parent != NULL || xs->modal) {
            if (out_w) *out_w = xs->width;
            if (out_h) *out_h = xs->height;
            return true;
        }
        if (xs->size_hints && xs->size_hints->min_width > 0 && xs->size_hints->max_width > 0 &&
            xs->size_hints->min_width == xs->size_hints->max_width &&
            xs->size_hints->min_height > 0 && xs->size_hints->max_height > 0 &&
            xs->size_hints->min_height == xs->size_hints->max_height) {
            if (out_w) *out_w = xs->size_hints->min_width;
            if (out_h) *out_h = xs->size_hints->min_height;
            return true;
        }
    }
    return false;
}

static void toplevel_map_common(struct wm_toplevel *toplevel) {
    struct wm_server *server = toplevel->server;
    if (toplevel->in_list == false) {
        wl_list_insert(server->toplevels.prev, &toplevel->link);
        toplevel->in_list = true;
    }
    if (toplevel->shadow == NULL && server->config.shadow) {
        toplevel->shadow = wlr_scene_shadow_create(&server->scene->tree, 1, 1, server->config.corner_radius, (float)server->config.shadow_range, server->config.shadow_color);
    }
    if (toplevel->border == NULL && server->config.border_width > 0) {
        toplevel->border = wlr_scene_rect_create(&server->scene->tree, 1, 1, server->config.border_color_inactive);
    }
    toplevel->is_fullscreen = false;
    toplevel->workspace = server->active_workspace;
    int req_w = 0, req_h = 0;
    bool is_dialog = wm_toplevel_is_dialog_or_fixed(toplevel, &req_w, &req_h);
    if (server->workspace_floating[server->active_workspace] || is_dialog) {
        toplevel->is_floating = true;
        toplevel->slot = -1;
        if (wl_list_empty(&server->outputs) == false) {
            struct wm_output *out = wl_container_of(server->outputs.next, out, link);
            int sw = wm_output_width(out->wlr_output);
            int sh = wm_output_height(out->wlr_output);
            if (is_dialog && req_w > 60 && req_h > 40 && req_w < sw && req_h < sh) {
                toplevel->width = req_w;
                toplevel->height = req_h;
            } else {
                toplevel->width = is_dialog ? (sw * 45 / 100) : (sw * 65 / 100);
                toplevel->height = is_dialog ? (sh * 45 / 100) : (sh * 65 / 100);
            }
            toplevel->x = (sw - toplevel->width) / 2;
            toplevel->y = (sh - toplevel->height) / 2;
            wm_apply_floating_geometry_internal(toplevel);
        }
    } else {
        toplevel->is_floating = false;
        toplevel->slot = -1;
    }
    wm_apply_window_rules(server, toplevel);
    bool want_fs = false;
    if (!toplevel->is_x11 && toplevel->xdg_toplevel && toplevel->xdg_toplevel->requested.fullscreen) want_fs = true;
    if (toplevel->is_x11 && toplevel->xwayland_surface && toplevel->xwayland_surface->fullscreen) want_fs = true;
    if (toplevel->is_floating == false && !want_fs) {
        if (toplevel->workspace == server->active_workspace) grid_place_at_cursor(server, toplevel);
        else toplevel->slot = -1;
    }
    wm_notify_added(toplevel);
    toplevel->anim_fade = !want_fs;
    if (want_fs) {
        toplevel_set_fullscreen(toplevel, true);
    } else {
        layout_toplevels(server);
        wm_animate_to(toplevel, toplevel->x, toplevel->y, !toplevel->no_anim);
    }
    focus_toplevel(toplevel);
}

static void toplevel_retire(struct wm_toplevel *toplevel) {
    struct wm_server *server = toplevel->server;
    bool was_listed = toplevel->in_list;
    bool was_active = (server->active_toplevel == toplevel);

    struct wlr_surface *dying = NULL;
    if (toplevel->is_x11) dying = toplevel->x11_surface;
    else if (toplevel->xdg_toplevel) dying = toplevel->xdg_toplevel->base->surface;

    if (was_listed) {
        grid_on_toplevel_remove(server, toplevel);
        wm_notify_removed(toplevel);
    }
    if (toplevel->shadow) {
        wlr_scene_node_destroy(&toplevel->shadow->node);
        toplevel->shadow = NULL;
    }
    if (toplevel->border) {
        wlr_scene_node_destroy(&toplevel->border->node);
        toplevel->border = NULL;
    }

    if (toplevel->in_list) {
        wl_list_remove(&toplevel->link);
        toplevel->in_list = false;
    }
    if (was_active) server->active_toplevel = NULL;
    for (int i = 0; i < 16; i++) {
        if (server->workspace_focus[i] == toplevel) server->workspace_focus[i] = NULL;
    }
    if (server->dragging_toplevel == toplevel) {
        server->dragging_toplevel = NULL;
        server->window_dragging = false;
        server->exit_fullscreen_dragging = false;
    }
    if (server->grabbed_toplevel == toplevel) {
        server->grabbed_toplevel = NULL;
        server->cursor_mode = WM_CURSOR_PASSTHROUGH;
        if (server->cursor && server->cursor_mgr) wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
    }
    toplevel->is_fullscreen = false;

    if (dying) {
        if (server->seat->keyboard_state.focused_surface == dying) {
            wlr_seat_keyboard_notify_clear_focus(server->seat);
        }
        if (server->seat->pointer_state.focused_surface == dying) {
            wlr_seat_pointer_clear_focus(server->seat);
        }
    }

    if (was_listed == false) return;

    grid_restore_minimized(server);
    layout_toplevels(server);

    if (was_active && wl_list_empty(&server->toplevels) == 0) {
        struct wm_toplevel *next = wl_container_of(server->toplevels.prev, next, link);
        focus_toplevel(next);
    }
}

static void toplevel_unmap_common(struct wm_toplevel *toplevel) {
    toplevel_retire(toplevel);
}

static void toplevel_destroy_common(struct wm_toplevel *toplevel) {
    toplevel_retire(toplevel);
    if (toplevel->scene_tree) {
        toplevel->scene_tree->node.data = NULL;
    }
    free(toplevel);
}

static void xdg_toplevel_map(struct wl_listener *listener, void *data) {
	(void)data;
    struct wm_toplevel *toplevel = wl_container_of(listener, toplevel, map);
    if (toplevel->decoration) {
        wlr_xdg_toplevel_decoration_v1_set_mode(toplevel->decoration, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
        toplevel->decoration = NULL;
    }
    toplevel_map_common(toplevel);
}

static void xdg_toplevel_unmap(struct wl_listener *listener, void *data) {
	(void)data;
    struct wm_toplevel *toplevel = wl_container_of(listener, toplevel, unmap);
    toplevel_unmap_common(toplevel);
}

static void xdg_toplevel_commit(struct wl_listener *listener, void *data) {
	(void)data;
    struct wm_toplevel *toplevel = wl_container_of(listener, toplevel, commit);

    if (toplevel->scene_tree != NULL) {
        int rad = toplevel->is_fullscreen ? 0 : toplevel->server->config.corner_radius;
        apply_corner_radius_to_node(&toplevel->scene_tree->node, rad);
    }

    if (toplevel->xdg_toplevel->base->initial_commit) {
        wlr_xdg_toplevel_set_tiled(toplevel->xdg_toplevel, WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
        wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, 0, 0);
        if (toplevel->decoration) {
            wlr_xdg_toplevel_decoration_v1_set_mode(toplevel->decoration,
                WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
            toplevel->decoration = NULL;
        }
    }
}

static void xdg_toplevel_destroy(struct wl_listener *listener, void *data) {
	(void)data;
    struct wm_toplevel *toplevel = wl_container_of(listener, toplevel, destroy);
    wlr_addon_finish(&toplevel->addon);
    if (toplevel->map.link.next) wl_list_remove(&toplevel->map.link);
    if (toplevel->unmap.link.next) wl_list_remove(&toplevel->unmap.link);
    if (toplevel->commit.link.next) wl_list_remove(&toplevel->commit.link);
    if (toplevel->destroy.link.next) wl_list_remove(&toplevel->destroy.link);
    if (toplevel->request_maximize.link.next) wl_list_remove(&toplevel->request_maximize.link);
    if (toplevel->request_fullscreen.link.next) wl_list_remove(&toplevel->request_fullscreen.link);
    toplevel_destroy_common(toplevel);
}

static void xdg_toplevel_request_maximize(
		struct wl_listener *listener, void *data) {
	(void)data;

	struct wm_toplevel *toplevel =
		wl_container_of(listener, toplevel, request_maximize);
	if (toplevel->xdg_toplevel->base->initialized) {
		wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
	}
}

static void xdg_toplevel_request_fullscreen(
        struct wl_listener *listener, void *data) {
	(void)data;
    struct wm_toplevel *toplevel =
        wl_container_of(listener, toplevel, request_fullscreen);
    if (toplevel->is_x11) {
        if (toplevel->xwayland_surface) toplevel_set_fullscreen(toplevel, toplevel->xwayland_surface->fullscreen);
        return;
    }
    if (toplevel->xdg_toplevel->base->initialized) {
        toplevel_set_fullscreen(toplevel, toplevel->xdg_toplevel->requested.fullscreen);
    }
}

static void toplevel_addon_destroy(struct wlr_addon *addon) { (void)addon; }

static const struct wlr_addon_interface toplevel_addon_impl = {
    .name = "wm_toplevel",
    .destroy = toplevel_addon_destroy,
};

static void server_new_xdg_decoration(struct wl_listener *listener, void *data) {
	(void)listener;
	(void)data;
    struct wlr_xdg_toplevel_decoration_v1 *deco = data;
    struct wlr_addon *addon = wlr_addon_find(&deco->toplevel->base->surface->addons,
        deco->toplevel, &toplevel_addon_impl);
    if (!addon) return;
    struct wm_toplevel *toplevel = wl_container_of(addon, toplevel, addon);
    toplevel->decoration = deco;
}

static void server_new_xdg_toplevel(struct wl_listener *listener, void *data) {
	(void)data;

	struct wm_server *server = wl_container_of(listener, server, new_xdg_toplevel);
	struct wlr_xdg_toplevel *xdg_toplevel = data;

	struct wm_toplevel *toplevel = calloc(1, sizeof(*toplevel));
	toplevel->server = server;
	toplevel->xdg_toplevel = xdg_toplevel;
	wl_list_init(&toplevel->map.link);
	wl_list_init(&toplevel->unmap.link);
	wl_list_init(&toplevel->commit.link);
	wl_list_init(&toplevel->destroy.link);
	wl_list_init(&toplevel->request_move.link);
	wl_list_init(&toplevel->request_resize.link);
	wl_list_init(&toplevel->request_maximize.link);
	wl_list_init(&toplevel->request_fullscreen.link);
	wl_list_init(&toplevel->xwayland_associate.link);
	wl_list_init(&toplevel->xwayland_dissociate.link);
	wl_list_init(&toplevel->xwayland_destroy.link);
	wl_list_init(&toplevel->xwayland_request_configure.link);
	wl_list_init(&toplevel->surface_map.link);
	wl_list_init(&toplevel->surface_unmap.link);
    wlr_addon_init(&toplevel->addon, &xdg_toplevel->base->surface->addons, xdg_toplevel, &toplevel_addon_impl);
	toplevel->scene_tree =
		wlr_scene_xdg_surface_create(&toplevel->server->scene->tree, xdg_toplevel->base);
	toplevel->scene_tree->node.data = toplevel;
	xdg_toplevel->base->data = toplevel->scene_tree;

	toplevel->map.notify = xdg_toplevel_map;
	wl_signal_add(&xdg_toplevel->base->surface->events.map, &toplevel->map);
	toplevel->unmap.notify = xdg_toplevel_unmap;
	wl_signal_add(&xdg_toplevel->base->surface->events.unmap, &toplevel->unmap);
	toplevel->commit.notify = xdg_toplevel_commit;
	wl_signal_add(&xdg_toplevel->base->surface->events.commit, &toplevel->commit);

	toplevel->destroy.notify = xdg_toplevel_destroy;
	wl_signal_add(&xdg_toplevel->events.destroy, &toplevel->destroy);

	toplevel->request_maximize.notify = xdg_toplevel_request_maximize;
	wl_signal_add(&xdg_toplevel->events.request_maximize, &toplevel->request_maximize);
	toplevel->request_fullscreen.notify = xdg_toplevel_request_fullscreen;
	wl_signal_add(&xdg_toplevel->events.request_fullscreen, &toplevel->request_fullscreen);
}

static void xdg_popup_commit(struct wl_listener *listener, void *data) {
	(void)data;

	struct wm_popup *popup = wl_container_of(listener, popup, commit);

	if (popup->xdg_popup->base->initial_commit) {

		wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
	}
}

static void xdg_popup_destroy(struct wl_listener *listener, void *data) {
	(void)data;

	struct wm_popup *popup = wl_container_of(listener, popup, destroy);

	wl_list_remove(&popup->commit.link);
	wl_list_remove(&popup->destroy.link);

	free(popup);
}

static void server_new_xdg_popup(struct wl_listener *listener, void *data) {
	struct wm_server *server = wl_container_of(listener, server, new_xdg_popup);
	struct wlr_xdg_popup *xdg_popup = data;

	struct wm_popup *popup = calloc(1, sizeof(*popup));
	if (popup == NULL) return;
	popup->xdg_popup = xdg_popup;
	wl_list_init(&popup->commit.link);
	wl_list_init(&popup->destroy.link);

	struct wlr_scene_tree *parent_tree = &server->scene->tree;
	if (xdg_popup->parent != NULL) {
		struct wlr_xdg_surface *parent = wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
		if (parent != NULL && parent->data != NULL) {
			parent_tree = parent->data;
		}
	}
	xdg_popup->base->data = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);

	popup->commit.notify = xdg_popup_commit;
	wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);

	popup->destroy.notify = xdg_popup_destroy;
	wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
}

static void xwayland_surface_map(struct wl_listener *listener, void *data) {
	(void)data;
    struct wm_toplevel *toplevel = wl_container_of(listener, toplevel, surface_map);
    toplevel_map_common(toplevel);
}

static void xwayland_surface_unmap(struct wl_listener *listener, void *data) {
	(void)data;
    struct wm_toplevel *toplevel = wl_container_of(listener, toplevel, surface_unmap);
    toplevel_unmap_common(toplevel);
}

static void xwayland_ready(struct wl_listener *listener, void *data) {
	(void)data;
    struct wm_server *server = wl_container_of(listener, server, xwayland_ready);
    struct wlr_xcursor *xcursor = wlr_xcursor_manager_get_xcursor(server->cursor_mgr, "default", 1);
    if (xcursor && xcursor->image_count > 0) {
        struct wlr_xcursor_image *image = xcursor->images[0];
        wlr_xwayland_set_cursor(server->xwayland, image->buffer, image->width * 4,
            image->width, image->height, image->hotspot_x, image->hotspot_y);
    }
}

static void xwayland_surface_associate(struct wl_listener *listener, void *data) {
	(void)data;
    struct wm_toplevel *toplevel = wl_container_of(listener, toplevel, xwayland_associate);
    struct wlr_xwayland_surface *xsurface = toplevel->xwayland_surface;
    if (xsurface == NULL) return;
    if (xsurface->surface == NULL) return;

    if (toplevel->surface_map.link.next) wl_list_remove(&toplevel->surface_map.link);
    if (toplevel->surface_unmap.link.next) wl_list_remove(&toplevel->surface_unmap.link);
    if (toplevel->scene_tree) {
        wlr_scene_node_destroy(&toplevel->scene_tree->node);
        toplevel->scene_tree = NULL;
    }

    toplevel->x11_surface = xsurface->surface;
    toplevel->scene_tree = wlr_scene_tree_create(&toplevel->server->scene->tree);
    toplevel->scene_tree->node.data = toplevel;
    wlr_scene_surface_create(toplevel->scene_tree, xsurface->surface);

    toplevel->surface_map.notify = xwayland_surface_map;
    wl_signal_add(&xsurface->surface->events.map, &toplevel->surface_map);
    toplevel->surface_unmap.notify = xwayland_surface_unmap;
    wl_signal_add(&xsurface->surface->events.unmap, &toplevel->surface_unmap);
}

static void xwayland_surface_dissociate(struct wl_listener *listener, void *data) {
	(void)data;
    struct wm_toplevel *toplevel = wl_container_of(listener, toplevel, xwayland_dissociate);
    if (toplevel->surface_map.link.next) wl_list_remove(&toplevel->surface_map.link);
    if (toplevel->surface_unmap.link.next) wl_list_remove(&toplevel->surface_unmap.link);
    toplevel_retire(toplevel);
    toplevel->x11_surface = NULL;
    if (toplevel->scene_tree) {
        wlr_scene_node_destroy(&toplevel->scene_tree->node);
        toplevel->scene_tree = NULL;
    }
}

static void xwayland_surface_destroy(struct wl_listener *listener, void *data) {
	(void)data;
    struct wm_toplevel *toplevel = wl_container_of(listener, toplevel, xwayland_destroy);
    if (toplevel->xwayland_associate.link.next) wl_list_remove(&toplevel->xwayland_associate.link);
    if (toplevel->xwayland_dissociate.link.next) wl_list_remove(&toplevel->xwayland_dissociate.link);
    if (toplevel->xwayland_destroy.link.next) wl_list_remove(&toplevel->xwayland_destroy.link);
    if (toplevel->xwayland_request_configure.link.next) wl_list_remove(&toplevel->xwayland_request_configure.link);
    if (toplevel->request_fullscreen.link.next) wl_list_remove(&toplevel->request_fullscreen.link);
    if (toplevel->surface_map.link.next) wl_list_remove(&toplevel->surface_map.link);
    if (toplevel->surface_unmap.link.next) wl_list_remove(&toplevel->surface_unmap.link);

    toplevel_retire(toplevel);
    toplevel->x11_surface = NULL;
    if (toplevel->scene_tree) {
        wlr_scene_node_destroy(&toplevel->scene_tree->node);
        toplevel->scene_tree = NULL;
    }
    if (toplevel->xwayland_surface) toplevel->xwayland_surface->data = NULL;
    toplevel->xwayland_surface = NULL;
    toplevel_destroy_common(toplevel);
}

static void xwayland_surface_request_configure(struct wl_listener *listener, void *data) {
	(void)data;
    struct wm_toplevel *toplevel = wl_container_of(listener, toplevel, xwayland_request_configure);
    struct wlr_xwayland_surface_configure_event *event = data;
    if (toplevel->xwayland_surface == NULL) return;
    if (event->width <= 0 || event->height <= 0) return;
    if (toplevel->in_list == false) {
        wlr_xwayland_surface_configure(toplevel->xwayland_surface,
            event->x, event->y, event->width, event->height);
        return;
    }
    layout_toplevels(toplevel->server);
}

static void server_new_xwayland_surface(struct wl_listener *listener, void *data) {
	(void)data;
    struct wm_server *server = wl_container_of(listener, server, new_xwayland_surface);
    struct wlr_xwayland_surface *xsurface = data;
    if (xsurface->override_redirect) return;
    struct wm_toplevel *toplevel = calloc(1, sizeof(*toplevel));
    toplevel->server = server;
    toplevel->is_x11 = true;
    toplevel->xwayland_surface = xsurface;
    xsurface->data = toplevel;
    wl_list_init(&toplevel->map.link);
    wl_list_init(&toplevel->unmap.link);
    wl_list_init(&toplevel->commit.link);
    wl_list_init(&toplevel->destroy.link);
    wl_list_init(&toplevel->request_move.link);
    wl_list_init(&toplevel->request_resize.link);
    wl_list_init(&toplevel->request_maximize.link);
    wl_list_init(&toplevel->request_fullscreen.link);
    wl_list_init(&toplevel->xwayland_associate.link);
    wl_list_init(&toplevel->xwayland_dissociate.link);
    wl_list_init(&toplevel->xwayland_destroy.link);
    wl_list_init(&toplevel->xwayland_request_configure.link);
    wl_list_init(&toplevel->surface_map.link);
    wl_list_init(&toplevel->surface_unmap.link);

    toplevel->xwayland_associate.notify = xwayland_surface_associate;
    wl_signal_add(&xsurface->events.associate, &toplevel->xwayland_associate);
    toplevel->xwayland_dissociate.notify = xwayland_surface_dissociate;
    wl_signal_add(&xsurface->events.dissociate, &toplevel->xwayland_dissociate);
    toplevel->xwayland_destroy.notify = xwayland_surface_destroy;
    wl_signal_add(&xsurface->events.destroy, &toplevel->xwayland_destroy);
    toplevel->xwayland_request_configure.notify = xwayland_surface_request_configure;
    wl_signal_add(&xsurface->events.request_configure, &toplevel->xwayland_request_configure);
    toplevel->request_fullscreen.notify = xdg_toplevel_request_fullscreen;
    wl_signal_add(&xsurface->events.request_fullscreen, &toplevel->request_fullscreen);
}

static void layer_surface_map(struct wl_listener *listener, void *data) {
	(void)data;
    struct wm_layer_surface *ls = wl_container_of(listener, ls, surface_map);
    uint32_t layer = ls->layer_surface->current.layer;
    uint32_t kbd = ls->layer_surface->current.keyboard_interactive;
    if (layer != ZWLR_LAYER_SHELL_V1_LAYER_TOP && layer != ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY) return;
    if (kbd != ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE) return;
    struct wm_server *server = ls->server;
    if (server->active_layer_surface == ls->layer_surface->surface) return;
    server->active_layer_surface = ls->layer_surface->surface;
    struct wlr_keyboard *kb = wlr_seat_get_keyboard(server->seat);
    if (kb != NULL) {
        wlr_seat_keyboard_notify_enter(server->seat, ls->layer_surface->surface,
            kb->keycodes, kb->num_keycodes, &kb->modifiers);
    }
    layout_toplevels(server);
}

static void layer_surface_unmap(struct wl_listener *listener, void *data) {
	(void)data;
    struct wm_layer_surface *ls = wl_container_of(listener, ls, surface_unmap);
    struct wm_server *server = ls->server;
    if (server->active_layer_surface == ls->layer_surface->surface) {
        server->active_layer_surface = NULL;
        if (server->active_toplevel != NULL && server->active_toplevel->in_list) {
            focus_toplevel(server->active_toplevel);
        } else {
            wlr_seat_keyboard_notify_clear_focus(server->seat);
        }
        layout_toplevels(server);
    }
}

static void wm_arrange_layers(struct wm_server *server) {
    if (wl_list_empty(&server->outputs)) return;
    struct wm_output *output = wl_container_of(server->outputs.next, output, link);
    struct wlr_box full = {0};
    wlr_output_effective_resolution(output->wlr_output, &full.width, &full.height);
    struct wlr_box usable = full;
    uint32_t layers[] = {ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, ZWLR_LAYER_SHELL_V1_LAYER_TOP, ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM, ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND};
    for (int i = 0; i < 4; i++) {
        struct wm_layer_surface *ls;
        wl_list_for_each(ls, &server->layer_surfaces, link) {
            struct wlr_layer_surface_v1 *surf = ls->layer_surface;
            if (surf->current.layer != layers[i]) continue;
            if (surf->initial_commit || surf->current.exclusive_zone > 0) {
                wlr_scene_layer_surface_v1_configure(ls->scene_layer_surface, &full, &usable);
            }
            if (surf->current.exclusive_zone == 0 && surf->surface->mapped &&
                (layers[i] == ZWLR_LAYER_SHELL_V1_LAYER_TOP || layers[i] == ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM)) {
                uint32_t a = surf->current.anchor;
                int w = surf->current.actual_width;
                int h = surf->current.actual_height;
                bool top = (a & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP) != 0;
                bool bot = (a & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM) != 0;
                bool lft = (a & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT) != 0;
                bool rgt = (a & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT) != 0;
                if (top && !bot && h > 0 && h < usable.height) {
                    usable.y += h;
                    usable.height -= h;
                } else if (bot && !top && h > 0 && h < usable.height) {
                    usable.height -= h;
                } else if (lft && !rgt && w > 0 && w < usable.width) {
                    usable.x += w;
                    usable.width -= w;
                } else if (rgt && !lft && w > 0 && w < usable.width) {
                    usable.width -= w;
                }
            }
        }
    }
    server->usable_area = usable;
    layout_toplevels(server);
}

static void layer_surface_commit(struct wl_listener *listener, void *data) {
    (void)data;
    struct wm_layer_surface *ls = wl_container_of(listener, ls, commit);
    if (ls->layer_surface->initial_commit || ls->layer_surface->current.committed != 0 || ls->layer_surface->surface->mapped)
        wm_arrange_layers(ls->server);
}

static void layer_surface_destroy(struct wl_listener *listener, void *data) {
	(void)data;
    struct wm_layer_surface *ls = wl_container_of(listener, ls, destroy);
    if (ls->server->active_layer_surface == ls->layer_surface->surface) {
        ls->server->active_layer_surface = NULL;
        if (ls->server->active_toplevel != NULL && ls->server->active_toplevel->in_list) {
            focus_toplevel(ls->server->active_toplevel);
        }
        layout_toplevels(ls->server);
    }
    struct wm_server *srv = ls->server;
    wl_list_remove(&ls->link);
    wl_list_remove(&ls->commit.link);
    wl_list_remove(&ls->destroy.link);
    wl_list_remove(&ls->surface_map.link);
    wl_list_remove(&ls->surface_unmap.link);
    free(ls);
    wm_arrange_layers(srv);
}

static void server_new_layer_surface(struct wl_listener *listener, void *data) {
	(void)data;
    struct wm_server *server = wl_container_of(listener, server, new_layer_surface);
    struct wlr_layer_surface_v1 *layer_surface = data;
    if (layer_surface->output == NULL) {
        if (wl_list_empty(&server->outputs)) return;
        struct wm_output *output = wl_container_of(server->outputs.next, output, link);
        layer_surface->output = output->wlr_output;
    }
    struct wm_layer_surface *ls = calloc(1, sizeof(*ls));
    if (ls == NULL) return;
    ls->server = server;
    ls->layer_surface = layer_surface;
    wl_list_insert(&server->layer_surfaces, &ls->link);
    wl_list_init(&ls->commit.link);
    wl_list_init(&ls->destroy.link);
    wl_list_init(&ls->surface_map.link);
    wl_list_init(&ls->surface_unmap.link);
    ls->scene_layer_surface = wlr_scene_layer_surface_v1_create(&server->scene->tree, layer_surface);
    if (server->config.blur && server->config.blur_layers && ls->scene_layer_surface) wm_apply_surface_fx(&ls->scene_layer_surface->tree->node, true, 1.0f);
    ls->commit.notify = layer_surface_commit;
    wl_signal_add(&layer_surface->surface->events.commit, &ls->commit);
    ls->destroy.notify = layer_surface_destroy;
    wl_signal_add(&layer_surface->events.destroy, &ls->destroy);
    ls->surface_map.notify = layer_surface_map;
    wl_signal_add(&layer_surface->surface->events.map, &ls->surface_map);
    ls->surface_unmap.notify = layer_surface_unmap;
    wl_signal_add(&layer_surface->surface->events.unmap, &ls->surface_unmap);
}

static int server_signal_handle(int sig, void *data) {
    (void)sig;
    struct wm_server *server = data;
    if (server->wl_display != NULL) wl_display_terminate(server->wl_display);
    return 0;
}

static struct wl_listener shape_listener;
static bool shape_listener_active = false;

static void wm_abort_create(struct wm_server *server) {
    if (server->allocator) wlr_allocator_destroy(server->allocator);
    if (server->renderer) wlr_renderer_destroy(server->renderer);
    if (server->backend) wlr_backend_destroy(server->backend);
    if (server->wl_display) if (shape_listener_active) { wl_list_remove(&shape_listener.link); shape_listener_active = false; }
    wl_display_destroy(server->wl_display);
    free(server);
}

struct wm_server *wm_create(const struct wm_config *config) {
    struct wm_server *server = calloc(1, sizeof(*server));
    if (server == NULL) return NULL;
    server->config = *config;
    grid_init(server);
    wl_list_init(&server->hotkeys);
    wl_list_init(&server->toplevels);
    wl_list_init(&server->layer_surfaces);
    wl_list_init(&server->outputs);
    wl_list_init(&server->keyboards);

    server->wl_display = wl_display_create();
    if (server->wl_display == NULL) {
        free(server);
        return NULL;
    }
    struct wl_event_loop *loop = wl_display_get_event_loop(server->wl_display);
    wl_event_loop_add_signal(loop, SIGINT, server_signal_handle, server);
    wl_event_loop_add_signal(loop, SIGTERM, server_signal_handle, server);

    server->backend = wlr_backend_autocreate(loop, NULL);
    if (server->backend == NULL) {
        wlr_log(WLR_ERROR, "failed to create wlr_backend");
        wm_abort_create(server);
        return NULL;
    }

    server->renderer = fx_renderer_create(server->backend);
    if (server->renderer == NULL) {
        wlr_log(WLR_ERROR, "failed to create fx_renderer");
        wm_abort_create(server);
        return NULL;
    }
    wlr_renderer_init_wl_display(server->renderer, server->wl_display);

    server->allocator = wlr_allocator_autocreate(server->backend, server->renderer);
    if (server->allocator == NULL) {
        wlr_log(WLR_ERROR, "failed to create wlr_allocator");
        wm_abort_create(server);
        return NULL;
    }

    struct wlr_compositor *compositor = wlr_compositor_create(server->wl_display, 5, server->renderer);
    wlr_subcompositor_create(server->wl_display);
    wlr_data_device_manager_create(server->wl_display);

    server->output_layout = wlr_output_layout_create(server->wl_display);
    wlr_xdg_output_manager_v1_create(server->wl_display, server->output_layout);

    server->new_output.notify = server_new_output;
    wl_signal_add(&server->backend->events.new_output, &server->new_output);

    server->scene = wlr_scene_create();
    if (server->config.blur) {
        wlr_scene_set_blur_data(server->scene, server->config.blur_passes, server->config.blur_size, 0.01f, 0.95f, 1.0f, 1.1f);
    }
    server->scene_layout = wlr_scene_attach_output_layout(server->scene, server->output_layout);

    server->xdg_shell = wlr_xdg_shell_create(server->wl_display, 3);
    server->layer_shell = wlr_layer_shell_v1_create(server->wl_display, 4);
    server->new_layer_surface.notify = server_new_layer_surface;
    wl_signal_add(&server->layer_shell->events.new_surface, &server->new_layer_surface);

    server->deco_mgr = wlr_xdg_decoration_manager_v1_create(server->wl_display);
    if (server->deco_mgr) {
        server->new_toplevel_decoration.notify = server_new_xdg_decoration;
        wl_signal_add(&server->deco_mgr->events.new_toplevel_decoration, &server->new_toplevel_decoration);
    }

    if (wlr_renderer_get_texture_formats(server->renderer, WLR_BUFFER_CAP_DMABUF) != NULL) {
        wlr_linux_dmabuf_v1_create_with_renderer(server->wl_display, 4, server->renderer);
        int drm_fd = wlr_renderer_get_drm_fd(server->renderer);
        if (drm_fd >= 0) wlr_linux_drm_syncobj_manager_v1_create(server->wl_display, 1, drm_fd);
    }
    wlr_presentation_create(server->wl_display, server->backend, 2);
    wlr_viewporter_create(server->wl_display);
    wlr_single_pixel_buffer_manager_v1_create(server->wl_display);
    server->drag_icon_tree = wlr_scene_tree_create(&server->scene->tree);
    wlr_screencopy_manager_v1_create(server->wl_display);
    wlr_export_dmabuf_manager_v1_create(server->wl_display);
    wlr_primary_selection_v1_device_manager_create(server->wl_display);
    wlr_xdg_activation_v1_create(server->wl_display);
    wlr_fractional_scale_manager_v1_create(server->wl_display, 1);
    struct wlr_cursor_shape_manager_v1 *cursor_shape_mgr = wlr_cursor_shape_manager_v1_create(server->wl_display, 1);
    if (cursor_shape_mgr != NULL) {

        shape_listener.notify = seat_request_set_cursor_shape;
        wl_signal_add(&cursor_shape_mgr->events.request_set_shape, &shape_listener); shape_listener_active = true;
    }
    wlr_text_input_manager_v3_create(server->wl_display);
    wlr_xdg_toplevel_icon_manager_v1_create(server->wl_display, 1);

    server->data_control = wlr_data_control_manager_v1_create(server->wl_display);
    server->relative_pointer_mgr = wlr_relative_pointer_manager_v1_create(server->wl_display);
    server->pointer_constraints = wlr_pointer_constraints_v1_create(server->wl_display);
    if (server->pointer_constraints != NULL) {
        server->new_pointer_constraint.notify = server_new_pointer_constraint;
        wl_signal_add(&server->pointer_constraints->events.new_constraint, &server->new_pointer_constraint);
    }
    server->idle_notifier = wlr_idle_notifier_v1_create(server->wl_display);
    server->idle_inhibit = wlr_idle_inhibit_v1_create(server->wl_display);
    if (server->idle_inhibit != NULL) {
        server->new_idle_inhibitor.notify = server_new_idle_inhibitor;
        wl_signal_add(&server->idle_inhibit->events.new_inhibitor, &server->new_idle_inhibitor);
    }
    server->output_manager = wlr_output_manager_v1_create(server->wl_display);
    if (server->output_manager != NULL) {
        server->output_manager_apply.notify = server_output_manager_apply;
        wl_signal_add(&server->output_manager->events.apply, &server->output_manager_apply);
        server->output_manager_test.notify = server_output_manager_test;
        wl_signal_add(&server->output_manager->events.test, &server->output_manager_test);
    }

    server->xwayland = wlr_xwayland_create(server->wl_display, compositor, false);
    if (server->xwayland) {
        server->new_xwayland_surface.notify = server_new_xwayland_surface;
        wl_signal_add(&server->xwayland->events.new_surface, &server->new_xwayland_surface);
        server->xwayland_ready.notify = xwayland_ready;
        wl_signal_add(&server->xwayland->events.ready, &server->xwayland_ready);
        setenv("DISPLAY", server->xwayland->display_name, true);
    }

    server->new_xdg_toplevel.notify = server_new_xdg_toplevel;
    wl_signal_add(&server->xdg_shell->events.new_toplevel, &server->new_xdg_toplevel);
    server->new_xdg_popup.notify = server_new_xdg_popup;
    wl_signal_add(&server->xdg_shell->events.new_popup, &server->new_xdg_popup);

    server->cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(server->cursor, server->output_layout);
    const char *xcur_theme = getenv("XCURSOR_THEME");
    const char *xcur_size_s = getenv("XCURSOR_SIZE");
    uint32_t xcur_size = (xcur_size_s && atoi(xcur_size_s) > 0) ? (uint32_t)atoi(xcur_size_s) : 24;
    if (!xcur_size_s) {
        char sz_buf[16]; snprintf(sz_buf, sizeof(sz_buf), "%u", xcur_size);
        setenv("XCURSOR_SIZE", sz_buf, true);
    }
    server->cursor_mgr = wlr_xcursor_manager_create(xcur_theme && xcur_theme[0] ? xcur_theme : NULL, xcur_size);
    wlr_xcursor_manager_load(server->cursor_mgr, 1.0f);

    server->cursor_mode = WM_CURSOR_PASSTHROUGH;
    server->cursor_motion.notify = server_cursor_motion;
    wl_signal_add(&server->cursor->events.motion, &server->cursor_motion);
    server->cursor_motion_absolute.notify = server_cursor_motion_absolute;
    wl_signal_add(&server->cursor->events.motion_absolute, &server->cursor_motion_absolute);
    server->cursor_button.notify = server_cursor_button;
    wl_signal_add(&server->cursor->events.button, &server->cursor_button);
    server->cursor_axis.notify = server_cursor_axis;
    wl_signal_add(&server->cursor->events.axis, &server->cursor_axis);
    server->cursor_frame.notify = server_cursor_frame;
    wl_signal_add(&server->cursor->events.frame, &server->cursor_frame);

    server->new_input.notify = server_new_input;
    wl_signal_add(&server->backend->events.new_input, &server->new_input);
    server->seat = wlr_seat_create(server->wl_display, "seat0");
    if (server->seat == NULL || server->xdg_shell == NULL || server->layer_shell == NULL || server->cursor == NULL) {
        wm_abort_create(server);
        return NULL;
    }
    if (server->seat != NULL) server->seat->data = server;
    if (server->xwayland) wlr_xwayland_set_seat(server->xwayland, server->seat);
    server->request_cursor.notify = seat_request_cursor;
    wl_signal_add(&server->seat->events.request_set_cursor, &server->request_cursor);
    server->request_set_selection.notify = seat_request_set_selection;
    wl_signal_add(&server->seat->events.request_set_selection, &server->request_set_selection);
    server->request_start_drag.notify = seat_request_start_drag;
    wl_signal_add(&server->seat->events.request_start_drag, &server->request_start_drag);
    server->start_drag.notify = seat_start_drag;
    wl_signal_add(&server->seat->events.start_drag, &server->start_drag);

    return server;
}

static void wm_apply_system_theme(const char *theme) {
    bool is_dark = (theme == NULL || strcasecmp(theme, "light") != 0);
    if (is_dark) {
        setenv("GTK_THEME", "Adwaita:dark", true);
    setenv("MOZ_GTK_TITLEBAR_DECORATION", "client", true);
        setenv("QT_QPA_PLATFORMTHEME", "kde", true);
        setenv("QT_STYLE_OVERRIDE", "Breeze", true);
        wm_spawn_command("gsettings set org.gnome.desktop.interface color-scheme 'prefer-dark' >/dev/null 2>&1; kwriteconfig6 --file ~/.config/kdeglobals --group General --key ColorScheme BreezeDark >/dev/null 2>&1");
    } else {
        setenv("GTK_THEME", "Adwaita", true);
        setenv("QT_QPA_PLATFORMTHEME", "kde", true);
        setenv("QT_STYLE_OVERRIDE", "Breeze", true);
        wm_spawn_command("gsettings set org.gnome.desktop.interface color-scheme 'default' >/dev/null 2>&1; kwriteconfig6 --file ~/.config/kdeglobals --group General --key ColorScheme BreezeLight >/dev/null 2>&1");
    }
}


/* IPC subsystem moved to src/modules/ipc.c */

bool wm_start(struct wm_server *server, const char *startup_cmd) {
    const char *socket = wl_display_add_socket_auto(server->wl_display);
    if (socket == NULL) {
        wlr_log(WLR_ERROR, "failed to add wayland socket");
        return false;
    }
    if (wlr_backend_start(server->backend) == false) {
        wlr_log(WLR_ERROR, "failed to start backend");
        return false;
    }
    setenv("WAYLAND_DISPLAY", socket, true);
    wm_ipc_start(server, socket);
    wm_apply_system_theme(server->config.theme);
    if (startup_cmd) wm_spawn_command(startup_cmd);
    for (int i = 0; i < server->config.autostart_count; i++) {
        wlr_log(WLR_INFO, "AUTOSTART: %s", server->config.autostart[i]);
        wm_spawn_command(server->config.autostart[i]);
    }
    wlr_log(WLR_INFO, "Running Wayland compositor on WAYLAND_DISPLAY=%s", socket);
    wlr_log(WLR_INFO, "LAYOUT: mode=%s grid=%dx%d capacity=%d",
        server->config.layout_mode, grid_cols(server), grid_rows(server), grid_effective_capacity(server));
    return true;
}

void wm_run(struct wm_server *server) {
    wl_display_run(server->wl_display);
}

void wm_destroy(struct wm_server *server) {
    if (server == NULL) return;
    if (server->xwayland) {
        wl_list_remove(&server->new_xwayland_surface.link);
        wl_list_remove(&server->xwayland_ready.link);
        wlr_xwayland_destroy(server->xwayland);
        server->xwayland = NULL;
    }
    wl_display_destroy_clients(server->wl_display);

    wl_list_remove(&server->new_xdg_toplevel.link);
    wl_list_remove(&server->new_xdg_popup.link);

    wl_list_remove(&server->cursor_motion.link);
    wl_list_remove(&server->cursor_motion_absolute.link);
    wl_list_remove(&server->cursor_button.link);
    wl_list_remove(&server->cursor_axis.link);
    wl_list_remove(&server->cursor_frame.link);

    wl_list_remove(&server->new_input.link);
    wl_list_remove(&server->request_cursor.link);
    wl_list_remove(&server->request_set_selection.link);
    wl_list_remove(&server->request_start_drag.link);
    wl_list_remove(&server->start_drag.link);

    wl_list_remove(&server->new_output.link);
    if (server->idle_inhibit != NULL) wl_list_remove(&server->new_idle_inhibitor.link);
    if (server->active_constraint != NULL) {
        wl_list_remove(&server->constraint_destroy.link);
        server->active_constraint = NULL;
    }
    if (server->pointer_constraints != NULL) wl_list_remove(&server->new_pointer_constraint.link);
    if (server->output_manager != NULL) {
        wl_list_remove(&server->output_manager_apply.link);
        wl_list_remove(&server->output_manager_test.link);
    }

    if (server->ipc_source) wl_event_source_remove(server->ipc_source);
    if (server->ipc_fd >= 0) { close(server->ipc_fd); unlink(server->ipc_path); }
    if (server->anim_timer) wl_event_source_remove(server->anim_timer);
    grid_fini(server);
    wlr_scene_node_destroy(&server->scene->tree.node);
    wlr_xcursor_manager_destroy(server->cursor_mgr);
    wlr_cursor_destroy(server->cursor);
    wlr_allocator_destroy(server->allocator);
    wlr_renderer_destroy(server->renderer);
    wlr_backend_destroy(server->backend);
    if (server->layer_shell) wl_list_remove(&server->new_layer_surface.link);
    if (server->deco_mgr) wl_list_remove(&server->new_toplevel_decoration.link);

    struct wm_hotkey *hk, *hk_tmp;
    wl_list_for_each_safe(hk, hk_tmp, &server->hotkeys, link) {
        wl_list_remove(&hk->link);
        free(hk);
    }
    if (shape_listener_active) { wl_list_remove(&shape_listener.link); shape_listener_active = false; }
    wl_display_destroy(server->wl_display);
    free(server);
}
