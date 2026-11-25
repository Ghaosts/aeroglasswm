#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cairo/cairo.h>
#include <drm_fourcc.h>
#include <scenefx/types/wlr_scene.h>
#include <scenefx/types/fx/corner_location.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>
#include "slider.h"
#include "wm.h"
#include "wm_internal.h"

#define SLIDER_BTN_INSET 20
#define SLIDER_SWIPE_ZONE 80
#define SLIDER_SIDE_DEAD 10.0
#define SLIDER_PULL_START 20.0
#define SLIDER_PULL_RANGE 40.0
#define SLIDER_PULL_ABORT 30.0
#define SLIDER_PULL_DETACH 30.0
#define SLIDER_GROW 12.0f

struct ui_buffer {
    struct wlr_buffer base;
    cairo_surface_t *surface;
    uint32_t format;
};

struct ui_assets {
    struct wlr_buffer *bar;
    struct wlr_buffer *slider;
    struct wlr_buffer *slider_pull;
    struct wlr_buffer *btn_left;
    struct wlr_buffer *btn_right;
};

struct ui_overlay {
    struct wlr_scene_buffer *sprite;
    struct wlr_scene_rect *source;
    bool clear_source;
    float opacity;
};

struct ui_visuals {
    struct ui_overlay bar;
    struct ui_overlay btn_left;
    struct ui_overlay btn_right;
    struct ui_overlay slider;
    struct ui_overlay slider_pull;
    struct ui_overlay morph_left;
    struct ui_overlay morph_right;
};

enum slider_part {
    SLIDER_HIT_NONE,
    SLIDER_HIT_BAR,
    SLIDER_HIT_SLIDER,
    SLIDER_HIT_BTN_LEFT,
    SLIDER_HIT_BTN_RIGHT,
};

struct slider_view {
    bool hovered;
    bool hover_want;
    int hover_wait_ms;
    float hover_alpha;
    struct wl_list link;
    struct slider *owner;
    struct wm_toplevel *toplevel;
    struct wlr_scene_rect *bar_bg;
    struct wlr_scene_rect *slider;
    struct wlr_scene_rect *btn_left;
    struct wlr_scene_rect *btn_right;
    struct ui_visuals visuals;
    struct wm_placement place;
};

struct slider {
    struct wm_server *server;
    struct wl_event_source *hover_timer;
    struct slider_config config;
    struct ui_assets assets;
    struct wl_list views;
    struct slider_view *drag;
    bool pulling_up;
    bool moved_sideways;
    double start_x;
    double start_y;
    int base_x;
};

static void ui_parse_color(toml_table_t *table, const char *key, float out_color[4],
        float default_r, float default_g, float default_b, float default_a) {
    out_color[0] = default_r;
    out_color[1] = default_g;
    out_color[2] = default_b;
    out_color[3] = default_a;
    if (table == NULL) return;
    toml_array_t *arr = toml_array_in(table, key);
    if (arr && toml_array_nelem(arr) == 4) {
        for (int i = 0; i < 4; i++) {
            toml_datum_t v = toml_double_at(arr, i);
            if (v.ok) out_color[i] = (float)v.u.d;
        }
    }
}

static void ui_config_string(toml_table_t *table, const char *key, char *out, size_t cap) {
    if (table == NULL) return;
    toml_datum_t value = toml_string_in(table, key);
    if (value.ok == 0) return;
    snprintf(out, cap, "%s", value.u.s);
    free(value.u.s);
}

static void ui_config_int(toml_table_t *table, const char *key, int *out) {
    if (table == NULL) return;
    toml_datum_t value = toml_int_in(table, key);
    if (value.ok) *out = (int)value.u.i;
}

void slider_config_defaults(struct slider_config *config) {
    memset(config, 0, sizeof(*config));
    config->overlay_mode = true;
    config->bar_h = 30;
    config->slider_w = 200;
    config->slider_h = 20;
    config->btn_w = 16;
    config->btn_h = 16;
    config->slider_radius = 10;
    config->btn_radius = 8;
    config->bar_radius = 12;
    config->morph_radius = 100;
    ui_parse_color(NULL, "", config->bar_color, 0.04f, 0.04f, 0.05f, 0.92f);
    ui_parse_color(NULL, "", config->slider_color, 0.8f, 0.9f, 1.0f, 0.9f);
    ui_parse_color(NULL, "", config->slider_pull_color, 1.0f, 1.0f, 1.0f, 0.9f);
    ui_parse_color(NULL, "", config->btn_fs_color, 0.2f, 0.6f, 1.0f, 0.9f);
    ui_parse_color(NULL, "", config->btn_close_color, 1.0f, 0.3f, 0.3f, 0.9f);
    snprintf(config->action_left, sizeof(config->action_left), "%s", "fullscreen");
    snprintf(config->action_right, sizeof(config->action_right), "%s", "close");
    snprintf(config->swipe_left_action, sizeof(config->swipe_left_action), "%s", "fullscreen");
    snprintf(config->swipe_right_action, sizeof(config->swipe_right_action), "%s", "close");
    snprintf(config->swipe_up_action, sizeof(config->swipe_up_action), "%s", "detach");
}

void slider_config_load(struct slider_config *config, toml_table_t *conf) {
    if (conf == NULL) return;
    toml_table_t *sl = toml_table_in(conf, "slider");
    toml_table_t *src = sl ? sl : conf;
    char mode_buf[32] = {0};
    ui_config_string(src, "mode", mode_buf, sizeof(mode_buf));
    if (strcasecmp(mode_buf, "docked") == 0) config->overlay_mode = false;
    else if (strcasecmp(mode_buf, "overlay") == 0) config->overlay_mode = true;
    ui_config_int(src, "bar_h", &config->bar_h);
    ui_config_int(src, "slider_w", &config->slider_w);
    ui_config_int(src, "slider_h", &config->slider_h);
    ui_config_int(src, "btn_w", &config->btn_w);
    ui_config_int(src, "btn_h", &config->btn_h);
    ui_config_int(src, "slider_radius", &config->slider_radius);
    ui_config_int(src, "btn_radius", &config->btn_radius);
    ui_config_int(src, "bar_radius", &config->bar_radius);
    ui_config_int(src, "morph_radius", &config->morph_radius);
    toml_table_t *colors = sl ? toml_table_in(sl, "colors") : NULL;
    if (!colors) colors = toml_table_in(conf, "colors");
    if (colors) {
        ui_parse_color(colors, "bar", config->bar_color, 0.04f, 0.04f, 0.05f, 0.92f);
        ui_parse_color(colors, "slider", config->slider_color, 0.8f, 0.9f, 1.0f, 0.9f);
        ui_parse_color(colors, "slider_pull", config->slider_pull_color, 1.0f, 1.0f, 1.0f, 0.9f);
        ui_parse_color(colors, "btn_fs", config->btn_fs_color, 0.2f, 0.6f, 1.0f, 0.9f);
        ui_parse_color(colors, "btn_close", config->btn_close_color, 1.0f, 0.3f, 0.3f, 0.9f);
    }
    toml_table_t *actions = sl ? toml_table_in(sl, "actions") : NULL;
    if (!actions) actions = toml_table_in(conf, "actions");
    if (actions) {
        ui_config_string(actions, "click_left", config->action_left, sizeof(config->action_left));
        ui_config_string(actions, "click_right", config->action_right, sizeof(config->action_right));
        ui_config_string(actions, "swipe_left", config->swipe_left_action, sizeof(config->swipe_left_action));
        ui_config_string(actions, "swipe_right", config->swipe_right_action, sizeof(config->swipe_right_action));
        ui_config_string(actions, "swipe_up", config->swipe_up_action, sizeof(config->swipe_up_action));
    }
    if (config->bar_h < 4) config->bar_h = 4;
    if (config->slider_w < 10) config->slider_w = 10;
    if (config->slider_h < 2) config->slider_h = 2;
    toml_table_t *images = sl ? toml_table_in(sl, "images") : NULL;
    if (!images) images = toml_table_in(conf, "images");
    if (images) {
        ui_config_string(images, "bar", config->image_bar, sizeof(config->image_bar));
        ui_config_string(images, "slider", config->image_slider, sizeof(config->image_slider));
        ui_config_string(images, "slider_pull", config->image_slider_pull, sizeof(config->image_slider_pull));
        ui_config_string(images, "btn_left", config->image_btn_left, sizeof(config->image_btn_left));
        ui_config_string(images, "btn_right", config->image_btn_right, sizeof(config->image_btn_right));
    }
}

static bool ui_resolve_path(const char *spec, char *out, size_t cap) {
    if (spec == NULL) return false;
    while (*spec == ' ') spec++;
    if (spec[0] == '\0') return false;
    int written = 0;
    if (spec[0] == '~' && spec[1] == '/') {
        const char *home = getenv("HOME");
        if (home == NULL) return false;
        written = snprintf(out, cap, "%s%s", home, spec + 1);
    } else {
        written = snprintf(out, cap, "%s", spec);
    }
    if (written < 1) return false;
    if ((size_t)written >= cap) return false;
    return true;
}

static void ui_buffer_destroy(struct wlr_buffer *wlr_buffer) {
    struct ui_buffer *buffer = wl_container_of(wlr_buffer, buffer, base);
    cairo_surface_destroy(buffer->surface);
    free(buffer);
}

static bool ui_buffer_begin_data_ptr_access(struct wlr_buffer *wlr_buffer, uint32_t flags,
        void **data, uint32_t *format, size_t *stride) {
    struct ui_buffer *buffer = wl_container_of(wlr_buffer, buffer, base);
    (void)flags;
    *data = cairo_image_surface_get_data(buffer->surface);
    *format = buffer->format;
    *stride = (size_t)cairo_image_surface_get_stride(buffer->surface);
    return true;
}

static void ui_buffer_end_data_ptr_access(struct wlr_buffer *wlr_buffer) {
    (void)wlr_buffer;
}

static const struct wlr_buffer_impl ui_buffer_impl = {
    .destroy = ui_buffer_destroy,
    .begin_data_ptr_access = ui_buffer_begin_data_ptr_access,
    .end_data_ptr_access = ui_buffer_end_data_ptr_access,
};

static struct wlr_buffer *ui_buffer_from_png(const char *path) {
    cairo_surface_t *surface = cairo_image_surface_create_from_png(path);
    if (cairo_surface_status(surface) > CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        return NULL;
    }
    cairo_format_t format = cairo_image_surface_get_format(surface);
    uint32_t fourcc = 0;
    if (format == CAIRO_FORMAT_ARGB32) fourcc = DRM_FORMAT_ARGB8888;
    else if (format == CAIRO_FORMAT_RGB24) fourcc = DRM_FORMAT_XRGB8888;
    int width = cairo_image_surface_get_width(surface);
    int height = cairo_image_surface_get_height(surface);
    if (fourcc == 0 || width < 1 || height < 1) {
        cairo_surface_destroy(surface);
        return NULL;
    }
    struct ui_buffer *buffer = calloc(1, sizeof(*buffer));
    if (buffer == NULL) {
        cairo_surface_destroy(surface);
        return NULL;
    }
    wlr_buffer_init(&buffer->base, &ui_buffer_impl, width, height);
    buffer->surface = surface;
    buffer->format = fourcc;
    wlr_log(WLR_INFO, "slider asset: %s %dx%d", path, width, height);
    return &buffer->base;
}

static struct wlr_buffer *ui_asset_load(const char *spec) {
    char path[1024];
    if (spec == NULL) return NULL;
    if (spec[0] == '\0') return NULL;
    if (ui_resolve_path(spec, path, sizeof(path)) == false) return NULL;
    struct wlr_buffer *buffer = ui_buffer_from_png(path);
    if (buffer == NULL) {
        wlr_log(WLR_ERROR, "slider: cannot load png: %s", path);
        return NULL;
    }
    wlr_buffer_lock(buffer);
    wlr_buffer_drop(buffer);
    return buffer;
}

static void ui_asset_release(struct wlr_buffer **buffer) {
    if (*buffer == NULL) return;
    wlr_buffer_unlock(*buffer);
    *buffer = NULL;
}

static void ui_assets_load(struct ui_assets *assets, const struct slider_config *config) {
    assets->bar = ui_asset_load(config->image_bar);
    assets->slider = ui_asset_load(config->image_slider);
    assets->slider_pull = ui_asset_load(config->image_slider_pull);
    assets->btn_left = ui_asset_load(config->image_btn_left);
    assets->btn_right = ui_asset_load(config->image_btn_right);
}

static void ui_assets_release(struct ui_assets *assets) {
    ui_asset_release(&assets->bar);
    ui_asset_release(&assets->slider);
    ui_asset_release(&assets->slider_pull);
    ui_asset_release(&assets->btn_left);
    ui_asset_release(&assets->btn_right);
}

static bool ui_overlay_reject_input(struct wlr_scene_buffer *buffer, double *sx, double *sy) {
    (void)buffer;
    (void)sx;
    (void)sy;
    return false;
}

static void ui_overlay_init(struct ui_overlay *overlay, struct wlr_scene_tree *parent,
        struct wlr_buffer *asset, struct wlr_scene_rect *source, bool clear_source,
        int radius, float opacity) {
    memset(overlay, 0, sizeof(*overlay));
    if (asset == NULL) return;
    if (source == NULL) return;
    struct wlr_scene_buffer *sprite = wlr_scene_buffer_create(parent, asset);
    if (sprite == NULL) return;
    sprite->point_accepts_input = ui_overlay_reject_input;
    wlr_scene_buffer_set_corner_radius(sprite, radius, CORNER_LOCATION_ALL);
    wlr_scene_node_set_enabled(&sprite->node, false);
    overlay->sprite = sprite;
    overlay->source = source;
    overlay->clear_source = clear_source;
    overlay->opacity = opacity;
}

static void ui_overlay_sync(struct ui_overlay *overlay) {
    if (overlay->sprite == NULL) return;
    struct wlr_scene_rect *source = overlay->source;
    if (overlay->clear_source) {
        float clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        wlr_scene_rect_set_color(source, clear);
    }
    bool shown = source->node.enabled && overlay->opacity > 0.0f
        && source->width > 0 && source->height > 0;
    wlr_scene_node_set_enabled(&overlay->sprite->node, shown);
    if (shown == false) return;
    wlr_scene_node_set_position(&overlay->sprite->node, source->node.x, source->node.y);
    wlr_scene_buffer_set_dest_size(overlay->sprite, source->width, source->height);
    wlr_scene_buffer_set_opacity(overlay->sprite, overlay->opacity);
    wlr_scene_node_raise_to_top(&overlay->sprite->node);
}

static void ui_overlay_destroy(struct ui_overlay *overlay) {
    if (overlay->sprite) wlr_scene_node_destroy(&overlay->sprite->node);
    memset(overlay, 0, sizeof(*overlay));
}

static void ui_visuals_sync(struct slider_view *view) {
    struct ui_visuals *v = &view->visuals;
    ui_overlay_sync(&v->bar);
    ui_overlay_sync(&v->btn_left);
    ui_overlay_sync(&v->btn_right);
    ui_overlay_sync(&v->slider);
    ui_overlay_sync(&v->slider_pull);
    ui_overlay_sync(&v->morph_left);
    ui_overlay_sync(&v->morph_right);
}

static void ui_visuals_rest(struct slider_view *view, bool active) {
    struct ui_visuals *v = &view->visuals;
    float op = active ? 1.0f : 0.0f;
    v->bar.opacity = op;
    v->btn_left.opacity = op;
    v->btn_right.opacity = op;
    v->slider.opacity = op;
    v->slider_pull.opacity = 0.0f;
    v->morph_left.opacity = 0.0f;
    v->morph_right.opacity = 0.0f;
    ui_visuals_sync(view);
}

static void ui_visuals_destroy(struct slider_view *view) {
    struct ui_visuals *v = &view->visuals;
    ui_overlay_destroy(&v->bar);
    ui_overlay_destroy(&v->btn_left);
    ui_overlay_destroy(&v->btn_right);
    ui_overlay_destroy(&v->slider);
    ui_overlay_destroy(&v->slider_pull);
    ui_overlay_destroy(&v->morph_left);
    ui_overlay_destroy(&v->morph_right);
}

static void ui_visuals_create(struct slider_view *view) {
    struct slider *s = view->owner;
    struct wlr_scene_tree *parent = wm_overlay_tree(s->server);
    struct ui_assets *a = &s->assets;
    struct ui_visuals *v = &view->visuals;
    const struct slider_config *c = &s->config;
    ui_visuals_destroy(view);
    ui_overlay_init(&v->bar, parent, a->bar, view->bar_bg, true, c->bar_radius, 1.0f);
    ui_overlay_init(&v->btn_left, parent, a->btn_left, view->btn_left, true, c->btn_radius, 1.0f);
    ui_overlay_init(&v->btn_right, parent, a->btn_right, view->btn_right, true, c->btn_radius, 1.0f);
    ui_overlay_init(&v->slider, parent, a->slider, view->slider, true, c->slider_radius, 1.0f);
    struct wlr_buffer *pull_asset = a->slider_pull ? a->slider_pull : a->slider;
    ui_overlay_init(&v->slider_pull, parent, pull_asset, view->slider, false, c->slider_radius, 0.0f);
    ui_overlay_init(&v->morph_left, parent, a->btn_left, view->slider, false, c->slider_radius, 0.0f);
    ui_overlay_init(&v->morph_right, parent, a->btn_right, view->slider, false, c->slider_radius, 0.0f);
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

static bool ui_action_enabled(const char *action) {
    if (action == NULL) return false;
    if (action[0] == '\0') return false;
    if (strcmp(action, "none") == 0) return false;
    return true;
}

static bool ui_rect_contains(const struct wlr_scene_rect *rect, double x, double y) {
    if (rect == NULL) return false;
    if (rect->node.enabled == false) return false;
    if (x < rect->node.x || x >= rect->node.x + rect->width) return false;
    if (y < rect->node.y || y >= rect->node.y + rect->height) return false;
    return true;
}

static void view_set_alpha(struct slider_view *view, float a) {
    const struct slider_config *c = &view->owner->config;
    float bg[4] = { c->bar_color[0], c->bar_color[1], c->bar_color[2], c->bar_color[3] * a };
    float sl[4] = { c->slider_color[0], c->slider_color[1], c->slider_color[2], c->slider_color[3] * a };
    float bl[4] = { c->btn_fs_color[0], c->btn_fs_color[1], c->btn_fs_color[2], c->btn_fs_color[3] * a };
    float br[4] = { c->btn_close_color[0], c->btn_close_color[1], c->btn_close_color[2], c->btn_close_color[3] * a };
    wlr_scene_rect_set_color(view->bar_bg, bg);
    wlr_scene_rect_set_color(view->slider, sl);
    wlr_scene_rect_set_color(view->btn_left, bl);
    wlr_scene_rect_set_color(view->btn_right, br);
}

static void view_apply(struct slider_view *view) {
    const struct slider_config *c = &view->owner->config;
    const struct wm_rect *r = &view->place.content;
    bool shown = view->place.active && (!c->overlay_mode || view->hovered || view->owner->drag == view);
    float alpha = shown ? (c->overlay_mode ? (view->hover_alpha > 0.05f ? view->hover_alpha : 1.0f) : 1.0f) : 0.0f;
    int slide_offset = (c->overlay_mode && shown) ? (int)((1.0f - alpha) * 6.0f) : 0;
    int bar_y = c->overlay_mode
        ? (r->y + r->h - c->bar_h - 4 + slide_offset)
        : ((view->place.inset.y > 0) ? view->place.inset.y : (r->y + r->h));

    wlr_scene_node_set_enabled(&view->bar_bg->node, shown);
    wlr_scene_node_set_enabled(&view->btn_left->node, shown);
    wlr_scene_node_set_enabled(&view->btn_right->node, shown);
    wlr_scene_node_set_enabled(&view->slider->node, shown);
    if (shown) {
        wlr_scene_rect_set_size(view->bar_bg, r->w, c->bar_h);
        wlr_scene_node_set_position(&view->bar_bg->node, r->x, bar_y);
        wlr_scene_rect_set_size(view->btn_left, c->btn_w, c->btn_h);
        wlr_scene_node_set_position(&view->btn_left->node,
            r->x + SLIDER_BTN_INSET, bar_y + (c->bar_h - c->btn_h) / 2);
        wlr_scene_rect_set_size(view->btn_right, c->btn_w, c->btn_h);
        wlr_scene_node_set_position(&view->btn_right->node,
            r->x + r->w - SLIDER_BTN_INSET - c->btn_w, bar_y + (c->bar_h - c->btn_h) / 2);
        wlr_scene_rect_set_size(view->slider, c->slider_w, c->slider_h);
        wlr_scene_node_set_position(&view->slider->node,
            r->x + (r->w - c->slider_w) / 2, bar_y + (c->bar_h - c->slider_h) / 2);
        wlr_scene_node_raise_to_top(&view->bar_bg->node);
        wlr_scene_node_raise_to_top(&view->btn_left->node);
        wlr_scene_node_raise_to_top(&view->btn_right->node);
        wlr_scene_node_raise_to_top(&view->slider->node);
    }
    ui_visuals_rest(view, shown);
    if (shown) view_set_alpha(view, alpha);
}

static void view_hide(struct slider_view *view) {
    view->place.active = false;
    view_apply(view);
}

static void view_follow(struct slider_view *view, int new_x) {
    struct slider *s = view->owner;
    const struct slider_config *c = &s->config;
    const struct wm_rect *r = &view->place.content;
    struct ui_visuals *v = &view->visuals;

    v->slider_pull.opacity = 0.0f;
    int base_w = c->slider_w;
    int base_h = c->slider_h;
    int radius = c->morph_radius;
    int left_edge = r->x + SLIDER_BTN_INSET + c->btn_w;
    int right_edge = r->x + r->w - SLIDER_BTN_INSET - c->btn_w;
    int bar_y = c->overlay_mode ? (r->y + r->h - c->bar_h - 4) : (r->y + r->h);
    int base_y = bar_y + (c->bar_h - base_h) / 2;
    int btn_y = bar_y + (c->bar_h - c->btn_h) / 2;
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
        left_grow = (int)((1.0f - scale) * SLIDER_GROW);
        slider_h = base_h + left_grow;
        ml = ui_smoothstep(1.0f - scale);
    } else if (right_dist < radius) {
        float scale = ui_zone_scale(right_dist, radius);
        slider_w = (int)(base_w * scale);
        right_grow = (int)((1.0f - scale) * SLIDER_GROW);
        slider_h = base_h + right_grow;
        slider_x = new_x + base_w - slider_w;
        mr = ui_smoothstep(1.0f - scale);
    }

    int slider_y = base_y + (base_h - slider_h) / 2;
    wlr_scene_rect_set_size(view->slider, slider_w, slider_h);
    wlr_scene_node_set_position(&view->slider->node, slider_x, slider_y);
    wlr_scene_rect_set_size(view->btn_left, c->btn_w + left_grow, c->btn_h + left_grow);
    wlr_scene_node_set_position(&view->btn_left->node,
        r->x + SLIDER_BTN_INSET - left_grow / 2, btn_y - left_grow / 2);
    wlr_scene_rect_set_size(view->btn_right, c->btn_w + right_grow, c->btn_h + right_grow);
    wlr_scene_node_set_position(&view->btn_right->node,
        r->x + r->w - SLIDER_BTN_INSET - c->btn_w - right_grow / 2, btn_y - right_grow / 2);

    float m = ml > mr ? ml : mr;
    bool left_wins = ml >= mr;
    if (v->slider.sprite == NULL) {
        const float *target = left_wins ? c->btn_fs_color : c->btn_close_color;
        float color[4];
        for (int i = 0; i < 4; i++) {
            color[i] = c->slider_color[i] + (target[i] - c->slider_color[i]) * m;
        }
        wlr_scene_rect_set_color(view->slider, color);
        v->slider.opacity = 1.0f;
    } else {
        struct wlr_scene_buffer *target_sprite = left_wins ? v->morph_left.sprite : v->morph_right.sprite;
        v->slider.opacity = target_sprite ? 1.0f - m : 1.0f;
    }
    v->morph_left.opacity = ml;
    v->morph_right.opacity = mr;
    ui_visuals_sync(view);
}

static void view_center(struct slider_view *view) {
    const struct wm_rect *r = &view->place.content;
    view->visuals.slider_pull.opacity = 0.0f;
    view->visuals.slider.opacity = 1.0f;
    view_follow(view, r->x + (r->w - view->owner->config.slider_w) / 2);
}

static void view_pull(struct slider_view *view, double dy_up) {
    const struct slider_config *c = &view->owner->config;
    const struct wm_rect *r = &view->place.content;
    struct ui_visuals *v = &view->visuals;
    int panel_top = r->y + r->h;
    double progress = (dy_up - SLIDER_PULL_START) / SLIDER_PULL_RANGE;
    if (progress < 0.0) progress = 0.0;
    if (progress > 1.0) progress = 1.0;
    int cur_h = c->slider_h + (int)(progress * (c->bar_h - c->slider_h));
    int cx = r->x + (r->w - c->slider_w) / 2;
    int cy = panel_top - cur_h;
    wlr_scene_rect_set_size(view->slider, c->slider_w, cur_h);
    wlr_scene_node_set_position(&view->slider->node, cx, cy);
    wlr_scene_rect_set_color(view->slider, c->slider_pull_color);
    v->slider.opacity = 0.0f;
    v->morph_left.opacity = 0.0f;
    v->morph_right.opacity = 0.0f;
    v->slider_pull.opacity = 1.0f;
    ui_visuals_sync(view);
}

static struct slider_view *view_create(struct slider *s, struct wm_toplevel *t) {
    struct slider_view *view = calloc(1, sizeof(*view));
    if (view == NULL) return NULL;
    struct wlr_scene_tree *parent = wm_overlay_tree(s->server);
    const struct slider_config *c = &s->config;
    view->owner = s;
    view->toplevel = t;
    view->slider = wlr_scene_rect_create(parent, c->slider_w, c->slider_h, c->slider_color);
    view->bar_bg = wlr_scene_rect_create(parent, 10, 10, c->bar_color);
    view->btn_left = wlr_scene_rect_create(parent, c->btn_w, c->btn_h, c->btn_fs_color);
    view->btn_right = wlr_scene_rect_create(parent, c->btn_w, c->btn_h, c->btn_close_color);
    if (view->slider == NULL || view->bar_bg == NULL || view->btn_left == NULL || view->btn_right == NULL) {
        if (view->slider) wlr_scene_node_destroy(&view->slider->node);
        if (view->bar_bg) wlr_scene_node_destroy(&view->bar_bg->node);
        if (view->btn_left) wlr_scene_node_destroy(&view->btn_left->node);
        if (view->btn_right) wlr_scene_node_destroy(&view->btn_right->node);
        free(view);
        return NULL;
    }
    wlr_scene_rect_set_corner_radius(view->slider, c->slider_radius, CORNER_LOCATION_ALL);
    wlr_scene_rect_set_corner_radius(view->bar_bg, c->bar_radius, CORNER_LOCATION_ALL);
    wlr_scene_rect_set_corner_radius(view->btn_left, c->btn_radius, CORNER_LOCATION_ALL);
    wlr_scene_rect_set_corner_radius(view->btn_right, c->btn_radius, CORNER_LOCATION_ALL);
    wlr_scene_node_set_enabled(&view->slider->node, false);
    wlr_scene_node_set_enabled(&view->bar_bg->node, false);
    wlr_scene_node_set_enabled(&view->btn_left->node, false);
    wlr_scene_node_set_enabled(&view->btn_right->node, false);
    ui_visuals_create(view);
    wl_list_insert(&s->views, &view->link);
    return view;
}

static void view_destroy(struct slider_view *view) {
    ui_visuals_destroy(view);
    wlr_scene_node_destroy(&view->slider->node);
    wlr_scene_node_destroy(&view->bar_bg->node);
    wlr_scene_node_destroy(&view->btn_left->node);
    wlr_scene_node_destroy(&view->btn_right->node);
    wl_list_remove(&view->link);
    free(view);
}

static void slider_drag_reset(struct slider *s) {
    s->drag = NULL;
    s->pulling_up = false;
    s->moved_sideways = false;
}

static struct slider_view *slider_hit(struct slider *s, double x, double y, enum slider_part *part) {
    *part = SLIDER_HIT_NONE;
    struct slider_view *view;
    wl_list_for_each(view, &s->views, link) {
        if (ui_rect_contains(view->bar_bg, x, y) == false) continue;
        if (ui_rect_contains(view->btn_left, x, y)) *part = SLIDER_HIT_BTN_LEFT;
        else if (ui_rect_contains(view->btn_right, x, y)) *part = SLIDER_HIT_BTN_RIGHT;
        else if (ui_rect_contains(view->slider, x, y)) *part = SLIDER_HIT_SLIDER;
        else *part = SLIDER_HIT_BAR;
        return view;
    }
    return NULL;
}

static void slider_grab(struct slider *s, struct slider_view *view) {
    s->drag = view;
    s->pulling_up = false;
    s->moved_sideways = false;
    s->start_x = wm_cursor_x(s->server);
    s->start_y = wm_cursor_y(s->server);
    s->base_x = view->slider->node.x;
}

static bool slider_release(struct slider *s) {
    struct slider_view *view = s->drag;
    bool was_pulling = s->pulling_up;
    double dy_up = s->start_y - wm_cursor_y(s->server);
    slider_drag_reset(s);
    if (view == NULL) return true;
    struct wm_toplevel *t = view->toplevel;
    const struct wm_rect *r = &view->place.content;
    const struct slider_config *c = &s->config;
    if (was_pulling && dy_up > SLIDER_PULL_DETACH) {
        view_center(view);
        wm_execute_action(s->server, t, c->swipe_up_action);
        return true;
    }
    const char *action = NULL;
    int cur_x = view->slider->node.x;
    if (cur_x > r->x + r->w - c->slider_w - SLIDER_SWIPE_ZONE) {
        if (ui_action_enabled(c->swipe_right_action)) action = c->swipe_right_action;
    } else if (cur_x < r->x + SLIDER_SWIPE_ZONE) {
        if (ui_action_enabled(c->swipe_left_action)) action = c->swipe_left_action;
    }
    view_center(view);
    wm_execute_action(s->server, t, action);
    return true;
}

static bool slider_on_button(struct wm_server *server, const struct wlr_pointer_button_event *event, void *data) {
    struct slider *s = data;
    if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
        if (s->drag) return true;
        enum slider_part part = SLIDER_HIT_NONE;
        struct slider_view *view = slider_hit(s, wm_cursor_x(server), wm_cursor_y(server), &part);
        if (view == NULL) return false;
        if (part == SLIDER_HIT_BTN_LEFT) {
            wm_focus_toplevel_internal(view->toplevel);
            wm_execute_action(server, view->toplevel, s->config.action_left);
            return true;
        }
        if (part == SLIDER_HIT_BTN_RIGHT) {
            wm_focus_toplevel_internal(view->toplevel);
            wm_execute_action(server, view->toplevel, s->config.action_right);
            return true;
        }
        slider_grab(s, view);
        return true;
    }
    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        if (s->drag == NULL) return false;
        return slider_release(s);
    }
    return false;
}

static int slider_hover_tick(void *data) {
    struct slider *s = data;
    bool any_anim = false;
    struct slider_view *v;
    wl_list_for_each(v, &s->views, link) {
        if (!v->place.active) continue;
        if (v->hover_want) {
            if (v->hover_wait_ms < 80) {
                v->hover_wait_ms += 10;
                any_anim = true;
            } else if (v->hover_alpha < 1.0f) {
                v->hovered = true;
                v->hover_alpha += 0.10f;
                if (v->hover_alpha > 1.0f) v->hover_alpha = 1.0f;
                view_apply(v);
                if (v->hover_alpha < 1.0f) any_anim = true;
            }
        } else if (v->hovered || v->hover_alpha > 0.0f || v->hover_wait_ms > 0) {
            v->hover_wait_ms = 0;
            v->hover_alpha -= 0.14f;
            if (v->hover_alpha <= 0.0f) {
                v->hover_alpha = 0.0f;
                v->hovered = false;
            } else {
                any_anim = true;
            }
            view_apply(v);
        }
    }
    if (any_anim && s->hover_timer) wl_event_source_timer_update(s->hover_timer, 10);
    return 0;
}

static void slider_update_hover(struct slider *s, double cx, double cy) {
    if (!s->config.overlay_mode) return;
    bool need_timer = false;
    struct slider_view *v;
    wl_list_for_each(v, &s->views, link) {
        if (!v->place.active) continue;
        const struct wm_rect *r = &v->place.content;
        int bottom = r->y + r->h;
        int trig_w = s->config.slider_w > 0 ? s->config.slider_w : 200;
        int trig_x = r->x + (r->w - trig_w) / 2;
        bool in_bar_x = (cx >= r->x && cx <= r->x + r->w);
        bool in_trig_x = (cx >= trig_x && cx <= trig_x + trig_w);
        bool want = false;
        if (s->drag == v) {
            want = true;
        } else if (v->hovered) {
            want = in_bar_x && (cy >= bottom - s->config.bar_h - 8 && cy <= bottom + 16);
        } else {
            want = in_trig_x && (cy >= bottom - 18 && cy <= bottom + 16);
        }
        if (want && s->server && s->server->cursor && s->server->cursor_mgr) {
            wlr_cursor_set_xcursor(s->server->cursor, s->server->cursor_mgr, "default");
            if (s->server->seat && s->server->seat->pointer_state.focused_surface) {
                wlr_seat_pointer_clear_focus(s->server->seat);
            }
        }
        v->hover_want = want;
        if (want || v->hovered || v->hover_alpha > 0.0f) need_timer = true;
    }
    if (need_timer) {
        if (!s->hover_timer && s->server && s->server->wl_display) {
            struct wl_event_loop *loop = wl_display_get_event_loop(s->server->wl_display);
            if (loop) s->hover_timer = wl_event_loop_add_timer(loop, slider_hover_tick, s);
        }
        if (s->hover_timer) wl_event_source_timer_update(s->hover_timer, 10);
    }
}

static bool slider_on_motion(struct wm_server *server, uint32_t time_msec, void *data) {
    (void)time_msec;
    struct slider *s = data;
    slider_update_hover(s, wm_cursor_x(server), wm_cursor_y(server));
    struct slider_view *view = s->drag;
    if (view == NULL) return false;
    const struct wm_rect *r = &view->place.content;
    double cx = wm_cursor_x(server);
    double cy = wm_cursor_y(server);
    double dx = cx - s->start_x;
    double dy_up = s->start_y - cy;
    if (fabs(dx) > SLIDER_SIDE_DEAD) s->moved_sideways = true;
    bool allow_up = ui_action_enabled(s->config.swipe_up_action);
    if (s->moved_sideways == false && dy_up > SLIDER_PULL_START && allow_up) s->pulling_up = true;
    if (s->pulling_up) {
        int panel_top = r->y + r->h;
        if (fabs(dx) > SLIDER_PULL_ABORT || cy > panel_top) {
            s->pulling_up = false;
            s->start_x = cx;
            s->start_y = cy;
            s->base_x = view->slider->node.x;
            dx = 0.0;
        } else {
            double max_pull = (r->y + r->h) / 3.0;
            if (max_pull < 60.0) max_pull = 60.0;
            if (dy_up > max_pull) dy_up = max_pull;
            view_pull(view, dy_up);
            return true;
        }
    }
    int base_w = s->config.slider_w;
    int new_x = s->base_x + (int)dx;
    if (new_x < r->x) new_x = r->x;
    if (new_x > r->x + r->w - base_w) new_x = r->x + r->w - base_w;
    view_follow(view, new_x);
    return true;
}

static void slider_on_added(struct wm_toplevel *t, void *data) {
    struct slider *s = data;
    if (wm_toplevel_userdata(t)) return;
    struct slider_view *view = view_create(s, t);
    if (view == NULL) return;
    wm_toplevel_set_userdata(t, view);
}

static void slider_on_removed(struct wm_toplevel *t, void *data) {
    struct slider *s = data;
    struct slider_view *view = wm_toplevel_userdata(t);
    if (view == NULL) return;
    if (s->drag == view) slider_drag_reset(s);
    wm_toplevel_set_userdata(t, NULL);
    view_destroy(view);
}

static void slider_on_placed(struct wm_toplevel *t, const struct wm_placement *place, void *data) {
    (void)data;
    struct slider_view *view = wm_toplevel_userdata(t);
    if (view == NULL) return;
    view->place = *place;
    if (place->active == false || (!view->owner->config.overlay_mode && place->inset.h <= 0)) {
        view_hide(view);
        return;
    }
    view_apply(view);
}

static void slider_on_hidden(struct wm_toplevel *t, void *data) {
    (void)data;
    struct slider_view *view = wm_toplevel_userdata(t);
    if (view == NULL) return;
    view_hide(view);
}

static void slider_adopt(struct wm_toplevel *t, void *data) {
    slider_on_added(t, data);
}

struct slider *slider_attach(struct wm_server *server, const struct slider_config *config) {
    if (server == NULL || config == NULL) return NULL;
    struct slider *s = calloc(1, sizeof(*s));
    if (s == NULL) return NULL;
    s->server = server;
    s->config = *config;
    wl_list_init(&s->views);
    ui_assets_load(&s->assets, &s->config);
    struct wm_hooks hooks = {
        .toplevel_added = slider_on_added,
        .toplevel_removed = slider_on_removed,
        .toplevel_placed = slider_on_placed,
        .toplevel_hidden = slider_on_hidden,
        .pointer_button = slider_on_button,
        .pointer_motion = slider_on_motion,
    };
    wm_set_hooks(server, &hooks, s);
    wm_set_inset_bottom(server, s->config.overlay_mode ? 0 : s->config.bar_h);
    wm_foreach_toplevel(server, slider_adopt, s);
    wm_relayout(server);
    return s;
}

void slider_detach(struct slider *s) {
    if (s == NULL) return;
    wm_set_hooks(s->server, NULL, NULL);
    wm_set_inset_bottom(s->server, 0);
    slider_drag_reset(s);
    struct slider_view *view, *tmp;
    wl_list_for_each_safe(view, tmp, &s->views, link) {
        wm_toplevel_set_userdata(view->toplevel, NULL);
        view_destroy(view);
    }
    ui_assets_release(&s->assets);
    wm_relayout(s->server);
    free(s);
}
