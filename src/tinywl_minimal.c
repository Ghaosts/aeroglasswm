#include <assert.h>
#include <wlr/types/wlr_buffer.h>
#include <drm_fourcc.h>
#include <math.h>
#include <math.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
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
#include <scenefx/types/wlr_scene.h>
#include <scenefx/render/fx_renderer/fx_renderer.h>
#include <scenefx/types/fx/corner_location.h>
#include <wlr/types/wlr_buffer.h>
#include <drm_fourcc.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/xwayland.h>
#include <wlr/util/addon.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include "toml.h"
#include <cairo/cairo.h>
#include <signal.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include "wlr-layer-shell-unstable-v1-protocol.h"

#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_text_input_v3.h>
#include <wlr/types/wlr_xdg_toplevel_icon_v1.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

#define BTN_SIZE 16
#define BTN_INSET 20

enum tinywl_cursor_mode {
	TINYWL_CURSOR_PASSTHROUGH,
};

struct tinywl_config {
    int gap;
    int gap_between;
    int slider_w;
    int slider_h;
    int bar_h;
    float bg_color[4];
    float bar_color[4];
    float slider_color[4];
    float btn_fs_color[4];
    float btn_close_color[4];
    int corner_radius;
    int border_width;
    float border_color_active[4];
    float border_color_inactive[4];
    int slider_radius;
    int btn_radius;
    int bar_radius;
    int morph_radius;
    int workspaces;
    char terminal[256];
    char action_left[512];
    char action_right[512];
    char swipe_left_action[512];
    char swipe_right_action[512];
    char swipe_up_action[512];
};

struct tinywl_server {
    struct tinywl_config config;

	struct wl_display *wl_display;
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;
	struct wlr_scene *scene;
	struct wlr_scene_output_layout *scene_layout;

	struct wlr_xdg_shell *xdg_shell;
	struct wlr_xwayland *xwayland;
    struct wlr_layer_shell_v1 *layer_shell;
    struct wl_listener new_layer_surface;
	struct wl_listener new_xwayland_surface;
	struct wl_listener xwayland_ready;
	struct wl_listener new_xdg_toplevel;
	struct wl_listener new_toplevel_decoration;
	struct wl_listener new_xdg_popup;
	struct wl_list toplevels;
	int active_workspace;
    struct tinywl_toplevel *workspace_focus[16];
    struct tinywl_toplevel *active_toplevel;
	struct wlr_surface *active_layer_surface;
	struct tinywl_toplevel *dragging_toplevel;
	bool slider_dragging;
    bool slider_pulling_up;
    bool slider_moved_sideways;
    double slider_start_y;
    bool window_dragging;
    double window_drag_offset_x;
    double window_drag_offset_y;
    int drag_zone;
	bool exit_fullscreen_dragging;
	double exit_fullscreen_start_y;
	double slider_start_x;
	int slider_base_x;

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
	struct wl_list keyboards;
	enum tinywl_cursor_mode cursor_mode;
	struct tinywl_toplevel *grabbed_toplevel;

	struct wlr_output_layout *output_layout;
	struct wl_list outputs;
	struct wl_listener new_output;
};

struct tinywl_output {
	struct wl_list link;
	struct tinywl_server *server;
	struct wlr_output *wlr_output;
	struct wl_listener frame;
	struct wl_listener request_state;
	struct wl_listener destroy;
	struct wl_listener commit;
	int last_width;
	int last_height;
};

struct tinywl_toplevel {
struct wl_list link;
struct tinywl_server *server;
struct wlr_xdg_toplevel *xdg_toplevel;
    struct wlr_xwayland_surface *xwayland_surface;
struct wlr_scene_tree *scene_tree;
struct wlr_scene_rect *slider;
struct wlr_scene_rect *bar_bg;
struct wlr_scene_rect *btn_fs;
struct wlr_scene_rect *btn_close;
struct wlr_scene_rect *border;
struct wlr_xdg_toplevel_decoration_v1 *decoration;
struct wlr_addon addon;
bool is_fullscreen;
    bool is_x11;
    bool is_floating;
    int workspace;
    int x, y;
    int width, height;
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
    struct wlr_surface *x11_surface;
};

struct tinywl_popup {
	struct wlr_xdg_popup *xdg_popup;
	struct wl_listener commit;
	struct wl_listener destroy;
};

struct tinywl_keyboard {
	struct wl_list link;
	struct tinywl_server *server;
	struct wlr_keyboard *wlr_keyboard;

	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
};

static void keyboard_handle_modifiers(
		struct wl_listener *listener, void *data) {

	struct tinywl_keyboard *keyboard =
		wl_container_of(listener, keyboard, modifiers);

	wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);

	wlr_seat_keyboard_notify_modifiers(keyboard->server->seat,
		&keyboard->wlr_keyboard->modifiers);
}

static void layout_toplevels(struct tinywl_server *server);
static void toplevel_map_common(struct tinywl_toplevel *toplevel);
static void toplevel_unmap_common(struct tinywl_toplevel *toplevel);
static void toplevel_destroy_common(struct tinywl_toplevel *toplevel);
static void focus_toplevel(struct tinywl_toplevel *toplevel);
static void sync_sliders(struct tinywl_server *server);
static void toplevel_set_fullscreen(struct tinywl_toplevel *t, bool fullscreen);
static void detach_window(struct tinywl_server *server, struct tinywl_toplevel *t);
static void focus_next_toplevel(struct tinywl_server *server, bool forward);

static struct wlr_surface *toplevel_surface(struct tinywl_toplevel *t) {
    if (t->is_x11) {
        if (t->xwayland_surface == NULL) return NULL;
        return t->xwayland_surface->surface;
    }
    if (t->xdg_toplevel == NULL) return NULL;
    return t->xdg_toplevel->base->surface;
}

static void toplevel_request_close(struct tinywl_toplevel *t) {
    if (t == NULL) return;
    if (t->is_x11) {
        if (t->xwayland_surface) wlr_xwayland_surface_close(t->xwayland_surface);
        return;
    }
    if (t->xdg_toplevel) wlr_xdg_toplevel_send_close(t->xdg_toplevel);
}

static void ui_config_path(char *out, size_t cap) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg != NULL && xdg[0] != '\0') {
        snprintf(out, cap, "%s/aeroglass/config.toml", xdg);
        return;
    }
    const char *home = getenv("HOME");
    if (home == NULL) {
        snprintf(out, cap, "config.toml");
        return;
    }
    snprintf(out, cap, "%s/.config/aeroglass/config.toml", home);
}

static void ui_spawn_command(const char *cmd) {
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

static void execute_action(struct tinywl_server *server, struct tinywl_toplevel *t, const char *action) {
    (void)server;
    if (action == NULL) return;
    if (action[0] == '\0') return;
    if (strcmp(action, "none") == 0) return;
    if (strcmp(action, "close") == 0) {
        toplevel_request_close(t);
        return;
    }
    if (strcmp(action, "detach") == 0) {
        if (t != NULL) detach_window(t->server, t);
        return;
    }
    if (strcmp(action, "fullscreen") == 0) {
        if (t == NULL) return;
        toplevel_set_fullscreen(t, t->is_fullscreen == false);
        return;
    }
    if (strncmp(action, "exec:", 5) == 0) {
        ui_spawn_command(action + 5);
        return;
    }
    wlr_log(WLR_ERROR, "unknown action: %s", action);
}

static void ui_config_string(toml_table_t *table, const char *key, char *out, size_t cap) {
    if (table == NULL) return;
    toml_datum_t value = toml_string_in(table, key);
    if (value.ok == 0) return;
    snprintf(out, cap, "%s", value.u.s);
    free(value.u.s);
}

static void ui_config_defaults(struct tinywl_config *config) {
    config->morph_radius = 100;
    config->workspaces = 9;
    snprintf(config->terminal, sizeof(config->terminal), "%s", "foot");
    snprintf(config->action_left, sizeof(config->action_left), "%s", "fullscreen");
    snprintf(config->action_right, sizeof(config->action_right), "%s", "close");
    snprintf(config->swipe_left_action, sizeof(config->swipe_left_action), "%s", "fullscreen");
    snprintf(config->swipe_right_action, sizeof(config->swipe_right_action), "%s", "close");
    snprintf(config->swipe_up_action, sizeof(config->swipe_up_action), "%s", "detach");
}

static void ui_config_load(struct tinywl_config *config, toml_table_t *conf) {
    toml_table_t *actions = toml_table_in(conf, "actions");
    if (actions != NULL) {
        ui_config_string(actions, "click_left", config->action_left, sizeof(config->action_left));
        ui_config_string(actions, "click_right", config->action_right, sizeof(config->action_right));
        ui_config_string(actions, "swipe_left", config->swipe_left_action, sizeof(config->swipe_left_action));
        ui_config_string(actions, "swipe_right", config->swipe_right_action, sizeof(config->swipe_right_action));
        ui_config_string(actions, "swipe_up", config->swipe_up_action, sizeof(config->swipe_up_action));
    } else {
        ui_config_string(conf, "terminal", config->terminal, sizeof(config->terminal));
    ui_config_string(conf, "action_left", config->action_left, sizeof(config->action_left));
        ui_config_string(conf, "action_right", config->action_right, sizeof(config->action_right));
    }
    toml_datum_t radius = toml_int_in(conf, "morph_radius");
    if (radius.ok) config->morph_radius = (int)radius.u.i;
    }

static float ui_clamp01(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static float ui_smoothstep(float value) {
    float v = ui_clamp01(value);
    return v * v * (3.0f - 2.0f * v);
}

static float ui_zone_scale(int distance, int radius) {
    if (radius < 1) return 1.0f;
    if (distance < 0) return 0.0f;
    return ui_clamp01((float)distance / (float)radius);
}

static void ui_slider_follow(struct tinywl_toplevel *t, int new_x) {
    if (t->slider == NULL || t->btn_fs == NULL || t->btn_close == NULL) return;
    struct tinywl_config *config = &t->server->config;
    int base_w = config->slider_w;
    int base_h = config->slider_h;
    int radius = config->morph_radius;
    int left_edge = t->x + BTN_INSET + BTN_SIZE;
    int right_edge = t->x + t->width - BTN_INSET - BTN_SIZE;
    int bar_y = t->y + t->height;
    int base_y = bar_y + (config->bar_h - base_h) / 2;
    int btn_y = bar_y + (config->bar_h - BTN_SIZE) / 2;
    int slider_w = base_w;
    int slider_h = base_h;
    int slider_x = new_x;
    int left_grow = 0;
    int right_grow = 0;
    float ml = 0.0f;
    float mr = 0.0f;
    int left_dist = new_x - left_edge;
    int right_dist = right_edge - (new_x + base_w);

    if (left_dist < radius) {
        float scale = ui_zone_scale(left_dist, radius);
        slider_w = (int)(base_w * scale);
        left_grow = (int)((1.0f - scale) * 12.0f);
        slider_h = base_h + left_grow;
        ml = ui_smoothstep(1.0f - scale);
    } else if (right_dist < radius) {
        float scale = ui_zone_scale(right_dist, radius);
        slider_w = (int)(base_w * scale);
        right_grow = (int)((1.0f - scale) * 12.0f);
        slider_h = base_h + right_grow;
        slider_x = new_x + base_w - slider_w;
        mr = ui_smoothstep(1.0f - scale);
    }

    int slider_y = base_y + (base_h - slider_h) / 2;
    wlr_scene_rect_set_size(t->slider, slider_w, slider_h);
    wlr_scene_node_set_position(&t->slider->node, slider_x, slider_y);
    int left_size = BTN_SIZE + left_grow;
    wlr_scene_rect_set_size(t->btn_fs, left_size, left_size);
    wlr_scene_node_set_position(&t->btn_fs->node, t->x + BTN_INSET - left_grow / 2, btn_y - left_grow / 2);
    int right_size = BTN_SIZE + right_grow;
    wlr_scene_rect_set_size(t->btn_close, right_size, right_size);
    wlr_scene_node_set_position(&t->btn_close->node,
        t->x + t->width - BTN_INSET - BTN_SIZE - right_grow / 2, btn_y - right_grow / 2);

    float m = ml > mr ? ml : mr;
    bool left_wins = ml >= mr;
    const float *target = left_wins ? config->btn_fs_color : config->btn_close_color;
    float color[4];
    for (int i = 0; i < 4; i++) {
        color[i] = config->slider_color[i] + (target[i] - config->slider_color[i]) * m;
    }
    wlr_scene_rect_set_color(t->slider, color);
}


static void ui_slider_center(struct tinywl_toplevel *t) {
    ui_slider_follow(t, t->x + (t->width - t->server->config.slider_w) / 2);
}

static bool handle_keybinding(struct tinywl_server *server, xkb_keysym_t sym, uint32_t mods) {
    if (mods & WLR_MODIFIER_LOGO) {
        if (sym == XKB_KEY_Return) {
            char cmd[300];
            snprintf(cmd, sizeof(cmd), "exec:%s", server->config.terminal);
            execute_action(server, server->active_toplevel, cmd);
            return true;
        }
        if (sym == XKB_KEY_Up && server->active_layer_surface == NULL) {
            focus_next_toplevel(server, false);
            return true;
        }
        if (sym == XKB_KEY_Down && server->active_layer_surface == NULL) {
            focus_next_toplevel(server, true);
            return true;
        }
    }
    if (sym >= XKB_KEY_1 && sym <= XKB_KEY_9) {
        int ws = sym - XKB_KEY_1;
        if (mods & WLR_MODIFIER_SHIFT) {
            if (server->active_toplevel) {
                server->active_toplevel->workspace = ws;
                focus_toplevel(NULL);
                layout_toplevels(server);
            }
        } else {
            if (ws >= server->config.workspaces) return true;
            if (server->active_workspace != ws) {
                server->active_workspace = ws;
                server->active_toplevel = NULL;
                wlr_seat_keyboard_notify_clear_focus(server->seat);
                wlr_seat_pointer_clear_focus(server->seat);
                struct tinywl_toplevel *t = server->workspace_focus[ws];
                if (t && t->in_list && t->workspace == ws) focus_toplevel(t);
                else {
                    wl_list_for_each(t, &server->toplevels, link) {
                        if (t->workspace == ws) { focus_toplevel(t); break; }
                    }
                }
                layout_toplevels(server);
            }
        }
        return true;
    }
    switch (sym) {
    case XKB_KEY_Escape: wl_display_terminate(server->wl_display); break;
    case XKB_KEY_F1:
        if (wl_list_length(&server->toplevels) < 2) break;
        struct tinywl_toplevel *next_toplevel = wl_container_of(server->toplevels.prev, next_toplevel, link);
        focus_toplevel(next_toplevel);
        break;
    default: return false;
    }
    return true;
}
static void keyboard_handle_key(
		struct wl_listener *listener, void *data) {
	(void)data;

	struct tinywl_keyboard *keyboard =
		wl_container_of(listener, keyboard, key);
	struct tinywl_server *server = keyboard->server;
	struct wlr_keyboard_key_event *event = data;
	struct wlr_seat *seat = server->seat;

	uint32_t keycode = event->keycode + 8;

	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(
			keyboard->wlr_keyboard->xkb_state, keycode, &syms);

	bool handled = false;
	uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
	if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		if (modifiers & (WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO)) {
			for (int i = 0; i < nsyms; i++) {
				handled = handle_keybinding(server, syms[i], modifiers);
			}
		}
	}

	if (handled == false) {
		wlr_seat_set_keyboard(seat, keyboard->wlr_keyboard);
		wlr_seat_keyboard_notify_key(seat, event->time_msec,
			event->keycode, event->state);
	}
}

static void keyboard_handle_destroy(struct wl_listener *listener, void *data) {

	struct tinywl_keyboard *keyboard =
		wl_container_of(listener, keyboard, destroy);
	wl_list_remove(&keyboard->modifiers.link);
	wl_list_remove(&keyboard->key.link);
	wl_list_remove(&keyboard->destroy.link);
	wl_list_remove(&keyboard->link);
	free(keyboard);
}

static void server_new_keyboard(struct tinywl_server *server,
		struct wlr_input_device *device) {
	struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

	struct tinywl_keyboard *keyboard = calloc(1, sizeof(*keyboard));
	keyboard->server = server;
	keyboard->wlr_keyboard = wlr_keyboard;

	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, NULL,
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

static void server_new_pointer(struct tinywl_server *server,
		struct wlr_input_device *device) {

	wlr_cursor_attach_input_device(server->cursor, device);
}

static void server_new_input(struct wl_listener *listener, void *data) {

	struct tinywl_server *server =
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

static void seat_request_cursor(struct wl_listener *listener, void *data) {
	struct tinywl_server *server = wl_container_of(
			listener, server, request_cursor);

	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	struct wlr_seat_client *focused_client =
		server->seat->pointer_state.focused_client;

	if (focused_client == event->seat_client) {

		wlr_cursor_set_surface(server->cursor, event->surface,
				event->hotspot_x, event->hotspot_y);
	}
}

static void seat_request_set_selection(struct wl_listener *listener, void *data) {

	struct tinywl_server *server = wl_container_of(
			listener, server, request_set_selection);
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(server->seat, event->source, event->serial);
}

static struct tinywl_toplevel *desktop_toplevel_at(
		struct tinywl_server *server, double lx, double ly,
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

static void reset_cursor_mode(struct tinywl_server *server) {

	server->cursor_mode = TINYWL_CURSOR_PASSTHROUGH;
	server->grabbed_toplevel = NULL;
}

static void process_cursor_motion(struct tinywl_server *server, uint32_t time) {

	double sx, sy;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *surface = NULL;
	struct tinywl_toplevel *toplevel = desktop_toplevel_at(server,
			server->cursor->x, server->cursor->y, &surface, &sx, &sy);
	if (!toplevel) {

		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
	}
	if (surface) {

		wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
		wlr_seat_pointer_notify_motion(seat, time, sx, sy);
	} else {

		wlr_seat_pointer_clear_focus(seat);
	}
}


static void detach_window(struct tinywl_server *server, struct tinywl_toplevel *t) {
    if (t == NULL) return;
    if (!t->is_floating) {
        t->is_floating = true;
        struct tinywl_output *output = wl_container_of(server->outputs.next, output, link);
        int sw = output->wlr_output->width;
        int sh = output->wlr_output->height;
        t->width = sw * 0.7;
        t->height = sh * 0.7;
        t->x = (int)(server->cursor->x - t->width / 2);
        t->y = (int)(server->cursor->y - t->height / 2);
        if (t->x < 0) t->x = 0;
        if (t->y < 0) t->y = 0;
        if (t->is_x11) wlr_xwayland_surface_configure(t->xwayland_surface, 0, 0, t->width, t->height);
        else wlr_xdg_toplevel_set_size(t->xdg_toplevel, t->width, t->height);
        if (t->scene_tree) {
            wlr_scene_node_set_position(&t->scene_tree->node, t->x, t->y);
            wlr_scene_node_raise_to_top(&t->scene_tree->node);
        }
        if (t->slider) wlr_scene_node_set_enabled(&t->slider->node, false);
        if (t->bar_bg) wlr_scene_node_set_enabled(&t->bar_bg->node, false);
        if (t->btn_fs) wlr_scene_node_set_enabled(&t->btn_fs->node, false);
        if (t->btn_close) wlr_scene_node_set_enabled(&t->btn_close->node, false);
    }
    server->window_dragging = true;
    server->dragging_toplevel = t;
    server->window_drag_offset_x = server->cursor->x - t->x;
    server->window_drag_offset_y = server->cursor->y - t->y;
    if (t->scene_tree) wlr_scene_node_raise_to_top(&t->scene_tree->node);
    layout_toplevels(server);
}

static void server_cursor_motion(struct wl_listener *listener, void *data) {
    struct tinywl_server *server = wl_container_of(listener, server, cursor_motion);
    struct wlr_pointer_motion_event *event = data;
    wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x, event->delta_y);
    if (server->window_dragging && server->dragging_toplevel) {
        struct tinywl_toplevel *t = server->dragging_toplevel;
        int nx = (int)(server->cursor->x - server->window_drag_offset_x);
        int ny = (int)(server->cursor->y - server->window_drag_offset_y);
        t->x = nx; t->y = ny;
        if (t->scene_tree) {
            wlr_scene_node_set_position(&t->scene_tree->node, nx, ny);
            wlr_scene_node_raise_to_top(&t->scene_tree->node);
        }
        struct tinywl_output *output = wl_container_of(server->outputs.next, output, link);
        int sh = output->wlr_output->height;
        int old_zone = server->drag_zone;
        if (server->cursor->y < sh / 3) server->drag_zone = 1;
        else if (server->cursor->y > sh * 2 / 3) server->drag_zone = 3;
        else server->drag_zone = 2;
        if (old_zone != server->drag_zone) layout_toplevels(server);
        return;
    }
    if (server->exit_fullscreen_dragging && server->dragging_toplevel) {
        struct tinywl_toplevel *t = server->dragging_toplevel;
        double dy = server->cursor->y - server->exit_fullscreen_start_y;
        if (dy < 0) {
            if (t->scene_tree) wlr_scene_node_set_position(&t->scene_tree->node, 0, (int)dy);
        } else {
            if (t->scene_tree) wlr_scene_node_set_position(&t->scene_tree->node, 0, 0);
        }
        return;
    }
    if (server->slider_dragging && server->dragging_toplevel && server->dragging_toplevel->slider) {
        struct tinywl_toplevel *t = server->dragging_toplevel;
        double dx = server->cursor->x - server->slider_start_x;
        double dy_up = server->slider_start_y - server->cursor->y;
        if (fabs(dx) > 10.0) server->slider_moved_sideways = true;
        bool allow_up = strcmp(server->config.swipe_up_action, "none") != 0;
        if (!server->slider_moved_sideways && dy_up > 20.0 && allow_up) server->slider_pulling_up = true;
        if (server->slider_pulling_up) {
            int panel_top = t->y + t->height;
            if (fabs(dx) > 30.0 || server->cursor->y > panel_top) {
                server->slider_pulling_up = false;
                server->slider_start_x = server->cursor->x;
                server->slider_start_y = server->cursor->y;
                server->slider_base_x = t->slider->node.x;
            } else {
                int base_w = server->config.slider_w;
                int base_h = server->config.slider_h;
                int max_h = server->config.bar_h;
                double progress = (dy_up - 20.0) / 40.0;
                if (progress < 0.0) progress = 0.0;
                if (progress > 1.0) progress = 1.0;
                int cur_h = base_h + (int)(progress * (max_h - base_h));
                int cx = t->x + (t->width - base_w) / 2;
                int cy = panel_top - cur_h;
                wlr_scene_rect_set_size(t->slider, base_w, cur_h);
                wlr_scene_node_set_position(&t->slider->node, cx, cy);
                wlr_scene_rect_set_color(t->slider, server->config.slider_color);

                wlr_seat_pointer_notify_motion(server->seat, event->time_msec, server->cursor->x, server->cursor->y);
                return;
            }
        }
        int new_x = server->slider_base_x + (int)dx;
        int base_w = server->config.slider_w;
        if (new_x < t->x) new_x = t->x;
        if (new_x > t->x + t->width - base_w) new_x = t->x + t->width - base_w;
        ui_slider_follow(t, new_x);
        wlr_seat_pointer_notify_motion(server->seat, event->time_msec, server->cursor->x, server->cursor->y);
        return;
    }
    process_cursor_motion(server, event->time_msec);
}

static void server_cursor_motion_absolute(
		struct wl_listener *listener, void *data) {

	struct tinywl_server *server =
		wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;
	wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x,
		event->y);
	process_cursor_motion(server, event->time_msec);
}

static void server_cursor_button(struct wl_listener *listener, void *data) {
    struct tinywl_server *server = wl_container_of(listener, server, cursor_button);
    struct wlr_pointer_button_event *event = data;
    if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
        struct wlr_keyboard *kb = wlr_seat_get_keyboard(server->seat);
        if (kb && (wlr_keyboard_get_modifiers(kb) & WLR_MODIFIER_LOGO)) {
            double ssx, ssy;
            struct wlr_surface *ssurf = NULL;
            struct tinywl_toplevel *st = desktop_toplevel_at(server, server->cursor->x, server->cursor->y, &ssurf, &ssx, &ssy);
            if (st != NULL) {
                detach_window(server, st);
                return;
            }
        }
        if (server->active_toplevel && server->active_toplevel->is_fullscreen
                && wl_list_empty(&server->outputs) == 0) {
            struct tinywl_output *o = wl_container_of(server->outputs.next, o, link);
            if (server->cursor->y > o->wlr_output->height - 50) {
                server->exit_fullscreen_dragging = true;
                server->exit_fullscreen_start_y = server->cursor->y;
                server->dragging_toplevel = server->active_toplevel;
                return;
            }
        }
        double sx, sy;
        struct wlr_scene_node *node = wlr_scene_node_at(&server->scene->tree.node,
            server->cursor->x, server->cursor->y, &sx, &sy);
        if (node && node->data) {
            struct tinywl_toplevel *t = node->data;
            if (t->slider && &t->slider->node == node) {
                server->slider_dragging = true;
                server->slider_pulling_up = false;
                server->slider_moved_sideways = false;
                server->slider_start_x = server->cursor->x;
                server->slider_start_y = server->cursor->y;
                server->slider_base_x = node->x;
                server->dragging_toplevel = t;
                return;
            }
            if (t->btn_fs && &t->btn_fs->node == node) {
                execute_action(server, t, server->config.action_left);
                return;
            }
            if (t->btn_close && &t->btn_close->node == node) {
                execute_action(server, t, server->config.action_right);
                return;
            }
        }
    } else if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        if (server->window_dragging && server->dragging_toplevel) {
            struct tinywl_toplevel *t = server->dragging_toplevel;
            server->window_dragging = false;
            server->dragging_toplevel = NULL;
            server->drag_zone = 0;
            t->workspace = server->active_workspace;
            struct tinywl_output *output = wl_container_of(server->outputs.next, output, link);
            int sh = output->wlr_output->height;
            t->is_floating = false;
            if (t->in_list) { wl_list_remove(&t->link); t->in_list = false; }
            if (server->cursor->y < sh / 2) wl_list_insert(&server->toplevels, &t->link);
            else wl_list_insert(server->toplevels.prev, &t->link);
            t->in_list = true;
            layout_toplevels(server);
            focus_toplevel(t);
            return;
        }
        if (server->exit_fullscreen_dragging) {
            double dy = server->cursor->y - server->exit_fullscreen_start_y;
            server->exit_fullscreen_dragging = false;
            struct tinywl_toplevel *t = server->dragging_toplevel;
            server->dragging_toplevel = NULL;
            if (t) {
                if (dy < -40 && t->is_fullscreen) {
                    toplevel_set_fullscreen(t, false);
                } else {
                    if (t->scene_tree) wlr_scene_node_set_position(&t->scene_tree->node, 0, 0);
                }
            }
            return;
        }
        if (server->slider_dragging) {
            struct tinywl_toplevel *t = server->dragging_toplevel;
            bool was_pulling = server->slider_pulling_up;
            double dy_up = server->slider_start_y - server->cursor->y;
            server->slider_dragging = false;
            server->slider_pulling_up = false;
            server->dragging_toplevel = NULL;
            if (t == NULL || t->slider == NULL) {
                sync_sliders(server);
                return;
            }
            if (was_pulling && dy_up > 30.0) {
                detach_window(server, t);
                return;
            }
            const char *action = NULL;
            int cur_x = t->slider->node.x;
            if (cur_x > t->x + t->width - server->config.slider_w - 80) {
                if (strcmp(server->config.swipe_right_action, "none") != 0) {
                    action = server->config.swipe_right_action;
                }
            } else if (cur_x < t->x + 80) {
                if (strcmp(server->config.swipe_left_action, "none") != 0) {
                    action = server->config.swipe_left_action;
                }
            }
            wlr_log(WLR_INFO, "SWIPE release: cur_x=%d action='%s'", cur_x, action ? action : "NULL");
            ui_slider_center(t);
            execute_action(server, t, action);
            sync_sliders(server);
            return;
        }
    }
    wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);
    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        reset_cursor_mode(server);
    } else {
        double sx, sy;
        struct wlr_surface *surface = NULL;
        struct tinywl_toplevel *toplevel = desktop_toplevel_at(server, server->cursor->x,
            server->cursor->y, &surface, &sx, &sy);
        focus_toplevel(toplevel);
    }
}

static void server_cursor_axis(struct wl_listener *listener, void *data) {

	struct tinywl_server *server =
		wl_container_of(listener, server, cursor_axis);
	struct wlr_pointer_axis_event *event = data;

	wlr_seat_pointer_notify_axis(server->seat,
			event->time_msec, event->orientation, event->delta,
			event->delta_discrete, event->source, event->relative_direction);
}

static void server_cursor_frame(struct wl_listener *listener, void *data) {

	struct tinywl_server *server =
		wl_container_of(listener, server, cursor_frame);

	wlr_seat_pointer_notify_frame(server->seat);
}

static void output_frame(struct wl_listener *listener, void *data) {

	struct tinywl_output *output = wl_container_of(listener, output, frame);
	struct wlr_scene *scene = output->server->scene;

	struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(
		scene, output->wlr_output);

	wlr_scene_output_commit(scene_output, NULL);

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(scene_output, &now);
}

static void output_request_state(struct wl_listener *listener, void *data) {

	struct tinywl_output *output = wl_container_of(listener, output, request_state);
	const struct wlr_output_event_request_state *event = data;

    wlr_output_commit_state(output->wlr_output, event->state);
}

static void output_destroy(struct wl_listener *listener, void *data) {
	struct tinywl_output *output = wl_container_of(listener, output, destroy);

	wl_list_remove(&output->frame.link);
	wl_list_remove(&output->request_state.link);
	wl_list_remove(&output->destroy.link);
	wl_list_remove(&output->commit.link);
	wl_list_remove(&output->link);
	free(output);
}

static void output_handle_commit(struct wl_listener *listener, void *data) {
    (void)data;
    struct tinywl_output *output = wl_container_of(listener, output, commit);
    int w = output->wlr_output->width;
    int h = output->wlr_output->height;

    if (w <= 0 || h <= 0) return;
    if (output->last_width == w && output->last_height == h) return;

    output->last_width = w;
    output->last_height = h;

    layout_toplevels(output->server);
}

static void server_new_output(struct wl_listener *listener, void *data) {

	struct tinywl_server *server =
		wl_container_of(listener, server, new_output);
	struct wlr_output *wlr_output = data;

	wlr_output_init_render(wlr_output, server->allocator, server->renderer);

	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true);

	struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
	if (mode != NULL) {
		wlr_output_state_set_mode(&state, mode);
	}

	wlr_output_commit_state(wlr_output, &state);
	wlr_output_state_finish(&state);

	struct tinywl_output *output = calloc(1, sizeof(*output));
	output->wlr_output = wlr_output;
	output->server = server;

	output->frame.notify = output_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);

	output->request_state.notify = output_request_state;
	wl_signal_add(&wlr_output->events.request_state, &output->request_state);

	output->destroy.notify = output_destroy;
	wl_signal_add(&wlr_output->events.destroy, &output->destroy);
	output->commit.notify = output_handle_commit;
	wl_signal_add(&wlr_output->events.commit, &output->commit);
	output->last_width = wlr_output->width;
	output->last_height = wlr_output->height;

	wl_list_insert(&server->outputs, &output->link);

	struct wlr_output_layout_output *l_output = wlr_output_layout_add_auto(server->output_layout,
		wlr_output);
	struct wlr_scene_output *scene_output = wlr_scene_output_create(server->scene, wlr_output);
	wlr_scene_output_layout_add_output(server->scene_layout, l_output, scene_output);
}

static void sync_sliders(struct tinywl_server *server) {
    struct tinywl_toplevel *t;
    wl_list_for_each(t, &server->toplevels, link) {
        if (t->slider) {
            bool layer_focus = (server->active_layer_surface != NULL);
            bool should_show = (t == server->active_toplevel) && !t->is_fullscreen && !layer_focus;

            if (should_show) {
                if (t->bar_bg) wlr_scene_node_set_enabled(&t->bar_bg->node, true);
                if (t->btn_fs) wlr_scene_node_set_enabled(&t->btn_fs->node, true);
                if (t->btn_close) wlr_scene_node_set_enabled(&t->btn_close->node, true);
                wlr_scene_node_set_enabled(&t->slider->node, true);

                if (t->bar_bg) wlr_scene_node_raise_to_top(&t->bar_bg->node);
                if (t->btn_fs) wlr_scene_node_raise_to_top(&t->btn_fs->node);
                if (t->btn_close) wlr_scene_node_raise_to_top(&t->btn_close->node);
                wlr_scene_node_raise_to_top(&t->slider->node);
            } else {
                if (t->bar_bg) wlr_scene_node_set_enabled(&t->bar_bg->node, false);
                if (t->btn_fs) wlr_scene_node_set_enabled(&t->btn_fs->node, false);
                if (t->btn_close) wlr_scene_node_set_enabled(&t->btn_close->node, false);
                wlr_scene_node_set_enabled(&t->slider->node, false);
            }
        }
    }
}

static void focus_toplevel(struct tinywl_toplevel *toplevel) {
    if (toplevel == NULL) return;
    if (toplevel->in_list == false) return;
    struct tinywl_server *server = toplevel->server;

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
    layout_toplevels(server);

    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
    if (keyboard) {
        wlr_seat_keyboard_notify_enter(server->seat, surface,
            keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
    }
    if (toplevel->is_x11) wlr_xwayland_surface_activate(toplevel->xwayland_surface, true);
    else wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);
}

static void focus_next_toplevel(struct tinywl_server *server, bool forward) {
    int count = wl_list_length(&server->toplevels);
    if (count < 2) return;
    struct tinywl_toplevel *cur = server->active_toplevel;
    struct tinywl_toplevel *start;
    if (cur != NULL && cur->in_list) {
        start = wl_container_of(forward ? cur->link.next : cur->link.prev, start, link);
    } else {
        start = wl_container_of(forward ? server->toplevels.next : server->toplevels.prev, start, link);
    }
    struct tinywl_toplevel *t = start;
    for (int i = 0; i < count; i++) {
        if (t->workspace == server->active_workspace && t->in_list && t != cur) {
            focus_toplevel(t);
            return;
        }
        t = wl_container_of(forward ? t->link.next : t->link.prev, t, link);
    }
}

static void toplevel_set_fullscreen(struct tinywl_toplevel *t, bool fullscreen) {
    if (t->is_fullscreen == fullscreen) return;
    if (t->is_x11) wlr_xwayland_surface_set_fullscreen(t->xwayland_surface, fullscreen);
    else wlr_xdg_toplevel_set_fullscreen(t->xdg_toplevel, fullscreen);
    t->is_fullscreen = fullscreen;
    if (t->slider) {
        float color[4] = {0.8f, 0.9f, 1.0f, 0.9f};
        wlr_scene_rect_set_color(t->slider, color);
    }
    layout_toplevels(t->server);
}


static void toplevel_hide(struct tinywl_toplevel *t) {
    if (!t->scene_tree) return;
    wlr_scene_node_set_enabled(&t->scene_tree->node, false);
    if (t->slider) wlr_scene_node_set_enabled(&t->slider->node, false);
    if (t->bar_bg) wlr_scene_node_set_enabled(&t->bar_bg->node, false);
    if (t->btn_fs) wlr_scene_node_set_enabled(&t->btn_fs->node, false);
    if (t->btn_close) wlr_scene_node_set_enabled(&t->btn_close->node, false);
    if (t->border) wlr_scene_node_set_enabled(&t->border->node, false);
}

static void toplevel_apply_fullscreen_geometry(struct tinywl_toplevel *t, struct wlr_output *output) {
    if (!t->scene_tree) return;
    t->x = 0;
    t->y = 0;
    t->width = output->width;
    t->height = output->height;
    wlr_scene_node_set_enabled(&t->scene_tree->node, true);
    if (t->is_x11) wlr_xwayland_surface_configure(t->xwayland_surface, 0, 0, t->width, t->height);
    else wlr_xdg_toplevel_set_size(t->xdg_toplevel, t->width, t->height);
    if (t->scene_tree) wlr_scene_node_set_position(&t->scene_tree->node, 0, 0);
    wlr_scene_node_raise_to_top(&t->scene_tree->node);
    if (t->slider) wlr_scene_node_set_enabled(&t->slider->node, false);
    if (t->bar_bg) wlr_scene_node_set_enabled(&t->bar_bg->node, false);
    if (t->btn_fs) wlr_scene_node_set_enabled(&t->btn_fs->node, false);
    if (t->btn_close) wlr_scene_node_set_enabled(&t->btn_close->node, false);
    if (t->border) wlr_scene_node_set_enabled(&t->border->node, false);
}

static void toplevel_place_bar(struct tinywl_toplevel *t, bool is_active) {
    int bar_h = t->server->config.bar_h;
    int slider_w = t->server->config.slider_w;
    int slider_h = t->server->config.slider_h;
    int bar_y = t->y + t->height;

    if (t->bar_bg) {
        wlr_scene_node_set_enabled(&t->bar_bg->node, is_active);
        if (is_active) {
            wlr_scene_rect_set_size(t->bar_bg, t->width, bar_h);
            wlr_scene_node_set_position(&t->bar_bg->node, t->x, bar_y);
            wlr_log(WLR_INFO, "UI: bar placed at %d,%d (size %dx%d)", t->x, bar_y, t->width, bar_h);
        }
    }
    if (t->btn_fs) {
        wlr_scene_node_set_enabled(&t->btn_fs->node, is_active);
        if (is_active) {
            wlr_scene_rect_set_size(t->btn_fs, BTN_SIZE, BTN_SIZE);
            wlr_scene_node_set_position(&t->btn_fs->node,
                t->x + BTN_INSET, bar_y + (bar_h - BTN_SIZE) / 2);
        }
    }
    if (t->btn_close) {
        wlr_scene_node_set_enabled(&t->btn_close->node, is_active);
        if (is_active) {
            wlr_scene_rect_set_size(t->btn_close, BTN_SIZE, BTN_SIZE);
            wlr_scene_node_set_position(&t->btn_close->node,
                t->x + t->width - BTN_INSET - BTN_SIZE,
                bar_y + (bar_h - BTN_SIZE) / 2);
        }
    }
    if (t->slider) {
        wlr_scene_node_set_enabled(&t->slider->node, is_active);
        if (is_active) {
            wlr_scene_rect_set_size(t->slider, slider_w, slider_h);
            wlr_scene_node_set_position(&t->slider->node,
                t->x + (t->width - slider_w) / 2,
                bar_y + (bar_h - slider_h) / 2);
            wlr_scene_rect_set_color(t->slider, t->server->config.slider_color);
        }
    }
    if (is_active) {
        if (t->bar_bg) wlr_scene_node_raise_to_top(&t->bar_bg->node);
        if (t->btn_fs) wlr_scene_node_raise_to_top(&t->btn_fs->node);
        if (t->btn_close) wlr_scene_node_raise_to_top(&t->btn_close->node);
        if (t->slider) wlr_scene_node_raise_to_top(&t->slider->node);
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

static void toplevel_place_content(struct tinywl_toplevel *t, int x, int y, int content_w, int content_h, bool is_active) {
    if (!t->scene_tree) return;
    t->x = x;
    t->y = y;
    t->width = content_w;
    t->height = content_h;
    wlr_scene_node_set_enabled(&t->scene_tree->node, true);
    if (t->is_x11) wlr_xwayland_surface_configure(t->xwayland_surface, 0, 0, content_w, content_h);
    else wlr_xdg_toplevel_set_size(t->xdg_toplevel, content_w, content_h);
    wlr_scene_node_set_position(&t->scene_tree->node, x, y);

    apply_corner_radius_to_node(&t->scene_tree->node, t->server->config.corner_radius);

    if (t->border) {
        int bw = t->server->config.border_width;
        int br = t->server->config.corner_radius;
        wlr_scene_rect_set_size(t->border, content_w + bw * 2, content_h + bw * 2);
        wlr_scene_node_set_position(&t->border->node, x - bw, y - bw);
        wlr_scene_node_set_enabled(&t->border->node, true);
        wlr_scene_rect_set_corner_radius(t->border, br, CORNER_LOCATION_ALL);
        wlr_scene_rect_set_color(t->border, is_active ? t->server->config.border_color_active : t->server->config.border_color_inactive);
        wlr_scene_node_lower_to_bottom(&t->border->node);
    }

    toplevel_place_bar(t, is_active);
}

static void layout_toplevels(struct tinywl_server *server) {
    if (wl_list_empty(&server->outputs)) return;
    struct tinywl_output *output = wl_container_of(server->outputs.next, output, link);
    int sw = output->wlr_output->width;
    int sh = output->wlr_output->height;
    if (sw <= 0 || sh <= 0) return;
    if (wl_list_empty(&server->toplevels)) return;

    struct tinywl_toplevel *t;
    struct tinywl_toplevel *fullscreen_toplevel = NULL;
    wl_list_for_each(t, &server->toplevels, link) {
        if (t->is_fullscreen) { fullscreen_toplevel = t; break; }
    }
    if (fullscreen_toplevel) {
        toplevel_apply_fullscreen_geometry(fullscreen_toplevel, output->wlr_output);
        wl_list_for_each(t, &server->toplevels, link) {
            if (t != fullscreen_toplevel) toplevel_hide(t);
        }
        return;
    }

    int gap = server->config.gap;
    int gap_between = server->config.gap_between;
    int bar_h = server->config.bar_h;

    struct tinywl_toplevel *top = NULL, *bot = NULL;
    int idx = 0;
    wl_list_for_each(t, &server->toplevels, link) {
        bool is_dragged = (server->window_dragging && server->dragging_toplevel == t);
        if (t->workspace != server->active_workspace && !is_dragged) {
            toplevel_hide(t);
            continue;
        }
        if (t->is_floating) {
            if (t->scene_tree) {
                wlr_scene_node_set_enabled(&t->scene_tree->node, true);
                if (is_dragged) wlr_scene_node_raise_to_top(&t->scene_tree->node);
            }
            if (t->slider) wlr_scene_node_set_enabled(&t->slider->node, false);
            if (t->bar_bg) wlr_scene_node_set_enabled(&t->bar_bg->node, false);
            if (t->btn_fs) wlr_scene_node_set_enabled(&t->btn_fs->node, false);
            if (t->btn_close) wlr_scene_node_set_enabled(&t->btn_close->node, false);
            continue;
        }
        if (idx == 0) top = t;
        else if (idx == 1) bot = t;
        else toplevel_hide(t);
        idx++;
    }

    if (!top) return;

    if (server->window_dragging) {
        struct tinywl_toplevel *dragged = server->dragging_toplevel;
        struct tinywl_toplevel *other = (dragged == top) ? bot : top;
        if (other) {
            if (server->drag_zone == 1) {
                toplevel_place_content(other, gap, sh / 2, sw - 2 * gap, sh / 2 - gap - bar_h, false);
            } else if (server->drag_zone == 3) {
                toplevel_place_content(other, gap, gap, sw - 2 * gap, sh / 2 - gap - bar_h, false);
            } else {
                toplevel_place_content(other, gap, gap, sw - 2 * gap, sh - 2 * gap, false);
            }
        }
        return;
    }

    bool layer_focus = (server->active_layer_surface != NULL);
    int bar_reserve = layer_focus ? 0 : bar_h;

    if (!bot) {
        bool is_active = (server->active_toplevel == top) && !layer_focus;
        int content_h = sh - 2 * gap - bar_reserve;
        toplevel_place_content(top, gap, gap, sw - 2 * gap, content_h, is_active);
        return;
    }

    int content_h = (sh - 2 * gap - gap_between - bar_reserve) / 2;
    bool top_active = (server->active_toplevel == top) && !layer_focus;
    bool bot_active = (server->active_toplevel == bot) && !layer_focus;

    toplevel_place_content(top, gap, gap, sw - 2 * gap, content_h, top_active);
    int bot_y = gap + content_h + gap_between + (top_active ? bar_h : 0);
    toplevel_place_content(bot, gap, bot_y, sw - 2 * gap, content_h, bot_active);
}

static void toplevel_map_common(struct tinywl_toplevel *toplevel) {
    if (!toplevel->in_list) {
        wl_list_insert(toplevel->server->toplevels.prev, &toplevel->link);
        toplevel->in_list = true;
    }
    if (wl_list_length(&toplevel->server->toplevels) > 2) {
        struct tinywl_toplevel *oldest = wl_container_of(toplevel->server->toplevels.next, oldest, link);
        toplevel_request_close(oldest);
    }
    toplevel->is_fullscreen = false;
    toplevel->workspace = toplevel->server->active_workspace;
    toplevel->is_floating = false;
    if (toplevel->slider == NULL) {
        toplevel->slider = wlr_scene_rect_create(&toplevel->server->scene->tree, toplevel->server->config.slider_w, toplevel->server->config.slider_h, toplevel->server->config.slider_color);
        toplevel->slider->node.data = toplevel;
        wlr_scene_rect_set_corner_radius(toplevel->slider, toplevel->server->config.slider_radius, CORNER_LOCATION_ALL);
        wlr_scene_node_set_enabled(&toplevel->slider->node, false);
    }
    if (toplevel->bar_bg == NULL) {
        toplevel->bar_bg = wlr_scene_rect_create(&toplevel->server->scene->tree, 10, 10, toplevel->server->config.bar_color);
        toplevel->bar_bg->node.data = toplevel;
        wlr_scene_rect_set_corner_radius(toplevel->bar_bg, toplevel->server->config.bar_radius, CORNER_LOCATION_ALL);
                wlr_scene_node_set_enabled(&toplevel->bar_bg->node, false);
        toplevel->btn_fs = wlr_scene_rect_create(&toplevel->server->scene->tree, 16, 16, toplevel->server->config.btn_fs_color);
        toplevel->btn_fs->node.data = toplevel;
        wlr_scene_rect_set_corner_radius(toplevel->btn_fs, toplevel->server->config.btn_radius, CORNER_LOCATION_ALL);
        wlr_scene_node_set_enabled(&toplevel->btn_fs->node, false);
        toplevel->btn_close = wlr_scene_rect_create(&toplevel->server->scene->tree, 16, 16, toplevel->server->config.btn_close_color);
        toplevel->btn_close->node.data = toplevel;
        wlr_scene_rect_set_corner_radius(toplevel->btn_close, toplevel->server->config.btn_radius, CORNER_LOCATION_ALL);
        wlr_scene_node_set_enabled(&toplevel->btn_close->node, false);
    }
    layout_toplevels(toplevel->server);
    focus_toplevel(toplevel);
}

static void toplevel_retire(struct tinywl_toplevel *toplevel) {
    struct tinywl_server *server = toplevel->server;
    bool was_listed = toplevel->in_list;
    bool was_active = (server->active_toplevel == toplevel);

    struct wlr_surface *dying = NULL;
    if (toplevel->is_x11) dying = toplevel->x11_surface;
    else if (toplevel->xdg_toplevel) dying = toplevel->xdg_toplevel->base->surface;
    if (toplevel->slider) { wlr_scene_node_destroy(&toplevel->slider->node); toplevel->slider = NULL; }
    if (toplevel->bar_bg) { wlr_scene_node_destroy(&toplevel->bar_bg->node); toplevel->bar_bg = NULL; }
    if (toplevel->btn_fs) { wlr_scene_node_destroy(&toplevel->btn_fs->node); toplevel->btn_fs = NULL; }
    if (toplevel->btn_close) { wlr_scene_node_destroy(&toplevel->btn_close->node); toplevel->btn_close = NULL; }
    if (toplevel->border) { wlr_scene_node_destroy(&toplevel->border->node); toplevel->border = NULL; }

    if (toplevel->in_list) { wl_list_remove(&toplevel->link); toplevel->in_list = false; }
    if (was_active) server->active_toplevel = NULL;
    if (server->dragging_toplevel == toplevel) {
        server->dragging_toplevel = NULL;
        server->slider_dragging = false;
        server->exit_fullscreen_dragging = false;
    }
    if (server->grabbed_toplevel == toplevel) server->grabbed_toplevel = NULL;
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

    layout_toplevels(server);

    if (was_active && wl_list_empty(&server->toplevels) == 0) {
        struct tinywl_toplevel *next = wl_container_of(server->toplevels.prev, next, link);
        focus_toplevel(next);
    }
}

static void toplevel_unmap_common(struct tinywl_toplevel *toplevel) {
    toplevel_retire(toplevel);
}

static void toplevel_destroy_common(struct tinywl_toplevel *toplevel) {
    toplevel_retire(toplevel);
    if (toplevel->scene_tree) {
        toplevel->scene_tree->node.data = NULL;
    }
    free(toplevel);
}

static void xdg_toplevel_map(struct wl_listener *listener, void *data) {
    struct tinywl_toplevel *toplevel = wl_container_of(listener, toplevel, map);
    if (toplevel->decoration) {
        wlr_xdg_toplevel_decoration_v1_set_mode(toplevel->decoration, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
        toplevel->decoration = NULL;
    }
    toplevel_map_common(toplevel);
}

static void xdg_toplevel_unmap(struct wl_listener *listener, void *data) {
    struct tinywl_toplevel *toplevel = wl_container_of(listener, toplevel, unmap);
    toplevel_unmap_common(toplevel);
}

static void xdg_toplevel_commit(struct wl_listener *listener, void *data) {
    struct tinywl_toplevel *toplevel = wl_container_of(listener, toplevel, commit);

    if (toplevel->xdg_toplevel->base->initial_commit) {
        wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, 0, 0);
        if (toplevel->decoration) {
            wlr_xdg_toplevel_decoration_v1_set_mode(toplevel->decoration,
                WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
            toplevel->decoration = NULL;
        }
    }
}

static void xdg_toplevel_destroy(struct wl_listener *listener, void *data) {
    struct tinywl_toplevel *toplevel = wl_container_of(listener, toplevel, destroy);
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

	struct tinywl_toplevel *toplevel =
		wl_container_of(listener, toplevel, request_maximize);
	if (toplevel->xdg_toplevel->base->initialized) {
		wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
	}
}

static void xdg_toplevel_request_fullscreen(
        struct wl_listener *listener, void *data) {
    struct tinywl_toplevel *toplevel =
        wl_container_of(listener, toplevel, request_fullscreen);
    if (toplevel->xdg_toplevel->base->initialized) {
        toplevel_set_fullscreen(toplevel, toplevel->xdg_toplevel->requested.fullscreen);
    }
}

static void toplevel_addon_destroy(struct wlr_addon *addon) { (void)addon; }
static const struct wlr_addon_interface toplevel_addon_impl = {
    .name = "tinywl_toplevel",
    .destroy = toplevel_addon_destroy,
};

static void server_new_xdg_decoration(struct wl_listener *listener, void *data) {
    struct wlr_xdg_toplevel_decoration_v1 *deco = data;
    struct wlr_addon *addon = wlr_addon_find(&deco->toplevel->base->surface->addons,
        deco->toplevel, &toplevel_addon_impl);
    if (!addon) return;
    struct tinywl_toplevel *toplevel = wl_container_of(addon, toplevel, addon);
    toplevel->decoration = deco;
}

static void server_new_xdg_toplevel(struct wl_listener *listener, void *data) {

	struct tinywl_server *server = wl_container_of(listener, server, new_xdg_toplevel);
	struct wlr_xdg_toplevel *xdg_toplevel = data;

	struct tinywl_toplevel *toplevel = calloc(1, sizeof(*toplevel));
	toplevel->server = server;
	toplevel->xdg_toplevel = xdg_toplevel;
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

	struct tinywl_popup *popup = wl_container_of(listener, popup, commit);

	if (popup->xdg_popup->base->initial_commit) {

		wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
	}
}

static void xdg_popup_destroy(struct wl_listener *listener, void *data) {

	struct tinywl_popup *popup = wl_container_of(listener, popup, destroy);

	wl_list_remove(&popup->commit.link);
	wl_list_remove(&popup->destroy.link);

	free(popup);
}

static void server_new_xdg_popup(struct wl_listener *listener, void *data) {

	struct wlr_xdg_popup *xdg_popup = data;

	struct tinywl_popup *popup = calloc(1, sizeof(*popup));
	popup->xdg_popup = xdg_popup;

	struct wlr_xdg_surface *parent = wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
	assert(parent != NULL);
	struct wlr_scene_tree *parent_tree = parent->data;
	xdg_popup->base->data = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);

	popup->commit.notify = xdg_popup_commit;
	wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);

	popup->destroy.notify = xdg_popup_destroy;
	wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
}

static void xwayland_surface_map(struct wl_listener *listener, void *data) {
    struct tinywl_toplevel *toplevel = wl_container_of(listener, toplevel, surface_map);
    toplevel_map_common(toplevel);
}

static void xwayland_surface_unmap(struct wl_listener *listener, void *data) {
    struct tinywl_toplevel *toplevel = wl_container_of(listener, toplevel, surface_unmap);
    toplevel_unmap_common(toplevel);
}

static void xwayland_ready(struct wl_listener *listener, void *data) {
    struct tinywl_server *server = wl_container_of(listener, server, xwayland_ready);
    struct wlr_xcursor *xcursor = wlr_xcursor_manager_get_xcursor(server->cursor_mgr, "default", 1);
    if (xcursor && xcursor->image_count > 0) {
        struct wlr_xcursor_image *image = xcursor->images[0];
        wlr_xwayland_set_cursor(server->xwayland, image->buffer, image->width * 4,
            image->width, image->height, image->hotspot_x, image->hotspot_y);
    }
}

static void xwayland_surface_associate(struct wl_listener *listener, void *data) {
    struct tinywl_toplevel *toplevel = wl_container_of(listener, toplevel, xwayland_associate);
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
    struct tinywl_toplevel *toplevel = wl_container_of(listener, toplevel, xwayland_dissociate);
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
    struct tinywl_toplevel *toplevel = wl_container_of(listener, toplevel, xwayland_destroy);
    if (toplevel->xwayland_associate.link.next) wl_list_remove(&toplevel->xwayland_associate.link);
    if (toplevel->xwayland_dissociate.link.next) wl_list_remove(&toplevel->xwayland_dissociate.link);
    if (toplevel->xwayland_destroy.link.next) wl_list_remove(&toplevel->xwayland_destroy.link);
    if (toplevel->xwayland_request_configure.link.next) wl_list_remove(&toplevel->xwayland_request_configure.link);
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
    struct tinywl_toplevel *toplevel = wl_container_of(listener, toplevel, xwayland_request_configure);
    struct wlr_xwayland_surface_configure_event *event = data;
    if (toplevel->xwayland_surface == NULL) return;
    if (toplevel->in_list == false) {
        wlr_xwayland_surface_configure(toplevel->xwayland_surface,
            event->x, event->y, event->width, event->height);
        return;
    }
    layout_toplevels(toplevel->server);
}

static void server_new_xwayland_surface(struct wl_listener *listener, void *data) {
    struct tinywl_server *server = wl_container_of(listener, server, new_xwayland_surface);
    struct wlr_xwayland_surface *xsurface = data;
    if (xsurface->override_redirect) return;
    struct tinywl_toplevel *toplevel = calloc(1, sizeof(*toplevel));
    toplevel->server = server;
    toplevel->is_x11 = true;
    toplevel->xwayland_surface = xsurface;
    xsurface->data = toplevel;

    toplevel->xwayland_associate.notify = xwayland_surface_associate;
    wl_signal_add(&xsurface->events.associate, &toplevel->xwayland_associate);
    toplevel->xwayland_dissociate.notify = xwayland_surface_dissociate;
    wl_signal_add(&xsurface->events.dissociate, &toplevel->xwayland_dissociate);
    toplevel->xwayland_destroy.notify = xwayland_surface_destroy;
    wl_signal_add(&xsurface->events.destroy, &toplevel->xwayland_destroy);
    toplevel->xwayland_request_configure.notify = xwayland_surface_request_configure;
    wl_signal_add(&xsurface->events.request_configure, &toplevel->xwayland_request_configure);
}





struct tinywl_layer_surface {
    struct wlr_layer_surface_v1 *layer_surface;
    struct wlr_scene_layer_surface_v1 *scene_layer_surface;
    struct wl_listener commit;
    struct wl_listener destroy;
    struct wl_listener surface_map;
    struct wl_listener surface_unmap;
    struct tinywl_server *server;
};

static void layer_surface_map(struct wl_listener *listener, void *data) {
    struct tinywl_layer_surface *ls = wl_container_of(listener, ls, surface_map);
    uint32_t layer = ls->layer_surface->current.layer;
    uint32_t kbd = ls->layer_surface->current.keyboard_interactive;
    if (layer != ZWLR_LAYER_SHELL_V1_LAYER_TOP && layer != ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY) return;
    if (kbd != ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE) return;
    struct tinywl_server *server = ls->server;
    if (server->active_layer_surface == ls->layer_surface->surface) return;
    server->active_layer_surface = ls->layer_surface->surface;
    struct wlr_keyboard *kb = wlr_seat_get_keyboard(server->seat);
    if (kb != NULL) {
        wlr_seat_keyboard_notify_enter(server->seat, ls->layer_surface->surface,
            kb->keycodes, kb->num_keycodes, &kb->modifiers);
    }
    layout_toplevels(server);
    sync_sliders(server);
}

static void layer_surface_unmap(struct wl_listener *listener, void *data) {
    struct tinywl_layer_surface *ls = wl_container_of(listener, ls, surface_unmap);
    struct tinywl_server *server = ls->server;
    if (server->active_layer_surface == ls->layer_surface->surface) {
        server->active_layer_surface = NULL;
        if (server->active_toplevel != NULL && server->active_toplevel->in_list) {
            focus_toplevel(server->active_toplevel);
        } else {
            wlr_seat_keyboard_notify_clear_focus(server->seat);
        }
        layout_toplevels(server);
        sync_sliders(server);
    }
}

static void layer_surface_commit(struct wl_listener *listener, void *data) {
    struct tinywl_layer_surface *ls = wl_container_of(listener, ls, commit);
    if (ls->layer_surface->initial_commit) {
        if (wl_list_empty(&ls->server->outputs)) return;
        struct tinywl_output *output = wl_container_of(ls->server->outputs.next, output, link);
        wlr_layer_surface_v1_configure(ls->layer_surface, output->wlr_output->width, output->wlr_output->height);
    }
}

static void layer_surface_destroy(struct wl_listener *listener, void *data) {
    struct tinywl_layer_surface *ls = wl_container_of(listener, ls, destroy);
    if (ls->server->active_layer_surface == ls->layer_surface->surface) {
        ls->server->active_layer_surface = NULL;
        if (ls->server->active_toplevel != NULL && ls->server->active_toplevel->in_list) {
            focus_toplevel(ls->server->active_toplevel);
        }
        layout_toplevels(ls->server);
        sync_sliders(ls->server);
    }
    wl_list_remove(&ls->commit.link);
    wl_list_remove(&ls->destroy.link);
    wl_list_remove(&ls->surface_map.link);
    wl_list_remove(&ls->surface_unmap.link);
    free(ls);
}

static void server_new_layer_surface(struct wl_listener *listener, void *data) {
    struct tinywl_server *server = wl_container_of(listener, server, new_layer_surface);
    struct wlr_layer_surface_v1 *layer_surface = data;
    if (layer_surface->output == NULL) {
        if (wl_list_empty(&server->outputs)) return;
        struct tinywl_output *output = wl_container_of(server->outputs.next, output, link);
        layer_surface->output = output->wlr_output;
    }
    struct tinywl_layer_surface *ls = calloc(1, sizeof(*ls));
    ls->server = server;
    ls->layer_surface = layer_surface;
    ls->scene_layer_surface = wlr_scene_layer_surface_v1_create(&server->scene->tree, layer_surface);
    ls->commit.notify = layer_surface_commit;
    wl_signal_add(&layer_surface->surface->events.commit, &ls->commit);
    ls->destroy.notify = layer_surface_destroy;
    wl_signal_add(&layer_surface->events.destroy, &ls->destroy);
    ls->surface_map.notify = layer_surface_map;
    wl_signal_add(&layer_surface->surface->events.map, &ls->surface_map);
    ls->surface_unmap.notify = layer_surface_unmap;
    wl_signal_add(&layer_surface->surface->events.unmap, &ls->surface_unmap);
}

static void parse_color(toml_table_t *table, const char *key, float out_color[4], float default_r, float default_g, float default_b, float default_a) {
    out_color[0] = default_r; out_color[1] = default_g; out_color[2] = default_b; out_color[3] = default_a;
    if (table == NULL) return;
    toml_array_t *arr = toml_array_in(table, key);
    if (arr && toml_array_nelem(arr) == 4) {
        for (int i = 0; i < 4; i++) {
            toml_datum_t v = toml_double_at(arr, i);
            if (v.ok) out_color[i] = (float)v.u.d;
        }
    }
}


static void load_config(struct tinywl_server *server, const char *path) {
    struct tinywl_config *config = &server->config;
    config->gap = 10;
    config->gap_between = 20;
    config->slider_w = 200;
    config->slider_h = 20;
    config->bar_h = 30;
    
    parse_color(NULL, "", config->bg_color, 0.95f, 0.95f, 0.98f, 1.0f);
    parse_color(NULL, "", config->bar_color, 0.1f, 0.1f, 0.1f, 0.85f);
    parse_color(NULL, "", config->slider_color, 0.8f, 0.9f, 1.0f, 0.9f);
    parse_color(NULL, "", config->btn_fs_color, 0.2f, 0.6f, 1.0f, 0.9f);
    parse_color(NULL, "", config->btn_close_color, 1.0f, 0.3f, 0.3f, 0.9f);
    config->corner_radius = 10;
    config->border_width = 2;
    parse_color(NULL, "", config->border_color_active, 0.2f, 0.6f, 1.0f, 1.0f);
    parse_color(NULL, "", config->border_color_inactive, 0.5f, 0.5f, 0.5f, 0.5f);
    config->slider_radius = 10;
    config->btn_radius = 8;
    config->bar_radius = 12;
    ui_config_defaults(config);

    FILE *fp = fopen(path, "r");
    if (fp == NULL) { return; }
    char errbuf[200];
    toml_table_t *conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
    fclose(fp);
    if (conf == NULL) { wlr_log(WLR_ERROR, "Cannot parse config: %s", errbuf); return; }

    toml_datum_t gap = toml_int_in(conf, "gap");
    if (gap.ok) { config->gap = (int)gap.u.i; }
    toml_datum_t gap_between = toml_int_in(conf, "gap_between");
    if (gap_between.ok) { config->gap_between = (int)gap_between.u.i; }
    toml_datum_t slider_w = toml_int_in(conf, "slider_w");
    if (slider_w.ok) { config->slider_w = (int)slider_w.u.i; }
    toml_datum_t slider_h = toml_int_in(conf, "slider_h");
    if (slider_h.ok) { config->slider_h = (int)slider_h.u.i; }
    toml_datum_t bar_h = toml_int_in(conf, "bar_h");
    if (bar_h.ok) { config->bar_h = (int)bar_h.u.i; }

    toml_datum_t cr = toml_int_in(conf, "corner_radius");
    if (cr.ok) { config->corner_radius = (int)cr.u.i; }
    toml_datum_t bw = toml_int_in(conf, "border_width");
    if (bw.ok) { config->border_width = (int)bw.u.i; }
    toml_datum_t sr = toml_int_in(conf, "slider_radius");
    if (sr.ok) { config->slider_radius = (int)sr.u.i; }
    toml_datum_t br = toml_int_in(conf, "btn_radius");
    if (br.ok) { config->btn_radius = (int)br.u.i; }
    toml_datum_t bar_r = toml_int_in(conf, "bar_radius");
    if (bar_r.ok) { config->bar_radius = (int)bar_r.u.i; }

    toml_table_t *colors = toml_table_in(conf, "colors");
    if (colors) {
        parse_color(colors, "bg", config->bg_color, 0.95f, 0.95f, 0.98f, 1.0f);
        parse_color(colors, "bar", config->bar_color, 0.1f, 0.1f, 0.1f, 0.85f);
        parse_color(colors, "slider", config->slider_color, 0.8f, 0.9f, 1.0f, 0.9f);
        parse_color(colors, "btn_fs", config->btn_fs_color, 0.2f, 0.6f, 1.0f, 0.9f);
        parse_color(colors, "btn_close", config->btn_close_color, 1.0f, 0.3f, 0.3f, 0.9f);
        parse_color(colors, "border_active", config->border_color_active, 0.2f, 0.6f, 1.0f, 1.0f);
        parse_color(colors, "border_inactive", config->border_color_inactive, 0.5f, 0.5f, 0.5f, 0.5f);
    }
    toml_datum_t ws_datum = toml_int_in(conf, "workspaces");
    if (ws_datum.ok) { int val = (int)ws_datum.u.i; if (val >= 1 && val <= 16) config->workspaces = val; }
    ui_config_load(config, conf);
    wlr_log(WLR_INFO, "CONFIG: click_left='%s' click_right='%s' swipe_left='%s' swipe_right='%s' swipe_up='%s'",
        config->action_left, config->action_right,
        config->swipe_left_action, config->swipe_right_action, config->swipe_up_action);
        toml_free(conf);
}
static int server_signal_handle(int sig, void *data) {
    (void)sig;
    struct tinywl_server *server = data;
    if (server->wl_display != NULL) wl_display_terminate(server->wl_display);
    return 0;
}

int main(int argc, char *argv[]) {
	wlr_log_init(WLR_INFO, NULL);
	char *startup_cmd = NULL;

	int c;
	while ((c = getopt(argc, argv, "s:h")) != -1) {
		switch (c) {
		case 's':
			startup_cmd = optarg;
			break;
		default:
			printf("Usage: %s [-s startup command]\n", argv[0]);
			return 0;
		}
	}
	if (optind < argc) {
		printf("Usage: %s [-s startup command]\n", argv[0]);
		return 0;
	}

	struct tinywl_server server = {0};
	char config_path[1024];
	ui_config_path(config_path, sizeof(config_path));
	load_config(&server, config_path);

	server.wl_display = wl_display_create();
	struct wl_event_loop *loop = wl_display_get_event_loop(server.wl_display);
	wl_event_loop_add_signal(loop, SIGINT, server_signal_handle, &server);
	wl_event_loop_add_signal(loop, SIGTERM, server_signal_handle, &server);

	server.backend = wlr_backend_autocreate(wl_display_get_event_loop(server.wl_display), NULL);
	if (server.backend == NULL) {
		wlr_log(WLR_ERROR, "failed to create wlr_backend");
		return 1;
	}

	server.renderer = fx_renderer_create(server.backend);
	if (server.renderer == NULL) {
		wlr_log(WLR_ERROR, "failed to create fx_renderer");
		return 1;
	}

	wlr_renderer_init_wl_display(server.renderer, server.wl_display);

	server.allocator = wlr_allocator_autocreate(server.backend,
		server.renderer);
	if (server.allocator == NULL) {
		wlr_log(WLR_ERROR, "failed to create wlr_allocator");
		return 1;
	}

	struct wlr_compositor *compositor = wlr_compositor_create(server.wl_display, 5, server.renderer);
	wlr_subcompositor_create(server.wl_display);
	wlr_data_device_manager_create(server.wl_display);

	server.output_layout = wlr_output_layout_create(server.wl_display);

	wl_list_init(&server.outputs);
	server.new_output.notify = server_new_output;
	wl_signal_add(&server.backend->events.new_output, &server.new_output);

	server.scene = wlr_scene_create();
	server.scene_layout = wlr_scene_attach_output_layout(server.scene, server.output_layout);


	wl_list_init(&server.toplevels);
	server.xdg_shell = wlr_xdg_shell_create(server.wl_display, 3);
    server.layer_shell = wlr_layer_shell_v1_create(server.wl_display, 4);
    server.new_layer_surface.notify = server_new_layer_surface;
    wl_signal_add(&server.layer_shell->events.new_surface, &server.new_layer_surface);

    struct wlr_xdg_decoration_manager_v1 *deco_mgr = wlr_xdg_decoration_manager_v1_create(server.wl_display);
    if (deco_mgr) {
        server.new_toplevel_decoration.notify = server_new_xdg_decoration;
        wl_signal_add(&deco_mgr->events.new_toplevel_decoration, &server.new_toplevel_decoration);
    }

    wlr_primary_selection_v1_device_manager_create(server.wl_display);

    wlr_xdg_activation_v1_create(server.wl_display);

    wlr_fractional_scale_manager_v1_create(server.wl_display, 1);

    wlr_cursor_shape_manager_v1_create(server.wl_display, 1);
    wlr_text_input_manager_v3_create(server.wl_display);
    wlr_xdg_toplevel_icon_manager_v1_create(server.wl_display, 1);

	server.xwayland = wlr_xwayland_create(server.wl_display, compositor, false);
	if (server.xwayland) {
		server.new_xwayland_surface.notify = server_new_xwayland_surface;
		wl_signal_add(&server.xwayland->events.new_surface, &server.new_xwayland_surface);
		server.xwayland_ready.notify = xwayland_ready;
		wl_signal_add(&server.xwayland->events.ready, &server.xwayland_ready);
		setenv("DISPLAY", server.xwayland->display_name, true);
	}

	server.new_xdg_toplevel.notify = server_new_xdg_toplevel;
	wl_signal_add(&server.xdg_shell->events.new_toplevel, &server.new_xdg_toplevel);
	server.new_xdg_popup.notify = server_new_xdg_popup;
	wl_signal_add(&server.xdg_shell->events.new_popup, &server.new_xdg_popup);

	server.cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(server.cursor, server.output_layout);

	server.cursor_mgr = wlr_xcursor_manager_create(NULL, 24);

	server.cursor_mode = TINYWL_CURSOR_PASSTHROUGH;
	server.cursor_motion.notify = server_cursor_motion;
	wl_signal_add(&server.cursor->events.motion, &server.cursor_motion);
	server.cursor_motion_absolute.notify = server_cursor_motion_absolute;
	wl_signal_add(&server.cursor->events.motion_absolute,
			&server.cursor_motion_absolute);
	server.cursor_button.notify = server_cursor_button;
	wl_signal_add(&server.cursor->events.button, &server.cursor_button);
	server.cursor_axis.notify = server_cursor_axis;
	wl_signal_add(&server.cursor->events.axis, &server.cursor_axis);
	server.cursor_frame.notify = server_cursor_frame;
	wl_signal_add(&server.cursor->events.frame, &server.cursor_frame);

	wl_list_init(&server.keyboards);
	server.new_input.notify = server_new_input;
	wl_signal_add(&server.backend->events.new_input, &server.new_input);
	server.seat = wlr_seat_create(server.wl_display, "seat0");
	if (server.xwayland) wlr_xwayland_set_seat(server.xwayland, server.seat);
	server.request_cursor.notify = seat_request_cursor;
	wl_signal_add(&server.seat->events.request_set_cursor,
			&server.request_cursor);
	server.request_set_selection.notify = seat_request_set_selection;
	wl_signal_add(&server.seat->events.request_set_selection,
			&server.request_set_selection);

	const char *socket = wl_display_add_socket_auto(server.wl_display);
	if (!socket) {
		wlr_backend_destroy(server.backend);
		return 1;
	}

	if (!wlr_backend_start(server.backend)) {
		wlr_backend_destroy(server.backend);
		wl_display_destroy(server.wl_display);
		return 1;
	}

	setenv("WAYLAND_DISPLAY", socket, true);
	if (startup_cmd) {
		if (fork() == 0) {
			execl("/bin/sh", "/bin/sh", "-c", startup_cmd, (void *)NULL);
		}
	}

	wlr_log(WLR_INFO, "Running Wayland compositor on WAYLAND_DISPLAY=%s",
			socket);
	wl_display_run(server.wl_display);

	if (server.xwayland) {
		wl_list_remove(&server.new_xwayland_surface.link);
		wl_list_remove(&server.xwayland_ready.link);
		wlr_xwayland_destroy(server.xwayland);
		server.xwayland = NULL;
	}
	wl_display_destroy_clients(server.wl_display);

	wl_list_remove(&server.new_xdg_toplevel.link);
	wl_list_remove(&server.new_xdg_popup.link);

	wl_list_remove(&server.cursor_motion.link);
	wl_list_remove(&server.cursor_motion_absolute.link);
	wl_list_remove(&server.cursor_button.link);
	wl_list_remove(&server.cursor_axis.link);
	wl_list_remove(&server.cursor_frame.link);

	wl_list_remove(&server.new_input.link);
	wl_list_remove(&server.request_cursor.link);
	wl_list_remove(&server.request_set_selection.link);

	wl_list_remove(&server.new_output.link);

	wlr_scene_node_destroy(&server.scene->tree.node);
	wlr_xcursor_manager_destroy(server.cursor_mgr);
	wlr_cursor_destroy(server.cursor);
	wlr_allocator_destroy(server.allocator);
	wlr_renderer_destroy(server.renderer);
	wlr_backend_destroy(server.backend);
	wl_display_destroy(server.wl_display);
	return 0;
}