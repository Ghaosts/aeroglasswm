#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <cairo.h>
#include <drm_fourcc.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_seat.h>
#include "wm_internal.h"

static bool grid_is_desktop(const struct wm_server *s) { return strcasecmp(s->config.layout_mode, "desktop") == 0; }
static int grid_count_on_ws(const struct wm_server *s, int ws);
static int grid_find_penalty_ws(struct wm_server *s, int owner_ws, int cap);
static int grid_find_free_normal_ws(struct wm_server *s, int from_ws, int cap);
static void grid_reclaim_penalty(struct wm_server *s, int owner_ws);

static void grid_base_dims(const struct wm_server *s, int *c, int *r) {
    if (!grid_is_desktop(s)) { *c = 1; *r = 2; return; }
    int p = s->grid.preset;
    if (p == 0) { *c = 1; *r = 1; }
    else if (p == 1) { *c = 2; *r = 1; }
    else if (p == 2) { *c = 2; *r = 2; }
    else { *c = 3; *r = 2; }
}

int grid_cols(const struct wm_server *s) { int c, r; grid_base_dims(s, &c, &r); return s->grid.vertical ? r : c; }
int grid_rows(const struct wm_server *s) { int c, r; grid_base_dims(s, &c, &r); return s->grid.vertical ? c : r; }

int grid_effective_capacity(const struct wm_server *s) {
    if (s->config.max_windows < 0) return WM_MAX_SLOTS;
    if (s->config.max_windows > 0) return s->config.max_windows > WM_MAX_SLOTS ? WM_MAX_SLOTS : s->config.max_windows;
    int cap = grid_cols(s) * grid_rows(s);
    return cap > WM_MAX_SLOTS ? WM_MAX_SLOTS : (cap < 1 ? 1 : cap);
}

static void grid_usable_box(const struct wm_server *s, int *ux, int *uy, int *uw, int *uh) {
    int sw = 1280, sh = 720;
    if (!wl_list_empty(&s->outputs)) {
        struct wm_output *o = wl_container_of(s->outputs.next, o, link);
        sw = wm_output_width(o->wlr_output); sh = wm_output_height(o->wlr_output);
    }
    *ux = s->usable_area.width > 0 ? s->usable_area.x : 0;
    *uy = s->usable_area.height > 0 ? s->usable_area.y : 0;
    *uw = s->usable_area.width > 0 ? s->usable_area.width : sw;
    *uh = s->usable_area.height > 0 ? s->usable_area.height : sh;
}

static int grid_count_tiled_ws(const struct wm_server *s, int ws) {
    int n = 0; struct wm_toplevel *t;
    wl_list_for_each(t, &s->toplevels, link)
        if (t->in_list && !t->is_floating && !t->is_minimized && t->workspace == ws) n++;
    return n;
}

void grid_cell(const struct wm_server *s, int sw, int sh, int slot, struct wm_rect *cell) {
    struct wm_toplevel *cur = NULL, *t; int idx = 0, ws = s->active_workspace;
    wl_list_for_each(t, &s->toplevels, link)
        if (t->in_list && !t->is_floating && !t->is_minimized && t->workspace == ws) { if (idx++ == slot) { cur = t; break; } }
    int gap = s->config.gap, gx = s->config.gap_x, gy = s->config.gap_y;
    int tw = sw - 2 * gap, th = sh - 2 * gap;
    if (tw < 1) tw = 1;
    if (th < 1) th = 1;
    if (!cur || grid_count_tiled_ws(s, ws) <= 1 || s->grid.preset == 0 || cur->gw <= 0 || cur->gh <= 0) {
        *cell = (struct wm_rect){ gap, gap, tw, th };
        return;
    }

    int x0 = gap + (cur->gx * tw + 500) / 1000;
    int y0 = gap + (cur->gy * th + 500) / 1000;
    int x1 = gap + ((cur->gx + cur->gw) * tw + 500) / 1000;
    int y1 = gap + ((cur->gy + cur->gh) * th + 500) / 1000;

    if (cur->gx > 0) x0 += gx / 2;
    if (cur->gx + cur->gw < 1000) x1 -= (gx - gx / 2);
    if (cur->gy > 0) y0 += gy / 2;
    if (cur->gy + cur->gh < 1000) y1 -= (gy - gy / 2);

    cell->x = x0;
    cell->y = y0;
    cell->w = (x1 - x0 > 1) ? (x1 - x0) : 1;
    cell->h = (y1 - y0 > 1) ? (y1 - y0) : 1;
}

static void grid_sort_band(struct wm_toplevel **arr, int n, bool vert) {
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++) {
            int va = vert ? arr[i]->gy : arr[i]->gx;
            int vb = vert ? arr[j]->gy : arr[j]->gx;
            if (va > vb) { struct wm_toplevel *tmp = arr[i]; arr[i] = arr[j]; arr[j] = tmp; }
        }
}

void grid_normalize_3x2(struct wm_server *s, int ws) {
    typedef struct wm_toplevel *wptr;
    wptr *b0 = __builtin_alloca(16 * sizeof(wptr));
    wptr *b1 = __builtin_alloca(16 * sizeof(wptr));
    struct wm_toplevel *t;
    int n0 = 0, n1 = 0;
    bool vert = (s->grid.vertical != 0);
    wl_list_for_each(t, &s->toplevels, link) {
        if (!t->in_list || t->is_floating || t->is_minimized || t->workspace != ws) continue;
        int mid = vert ? (t->gx + t->gw / 2) : (t->gy + t->gh / 2);
        if (mid < 500) { if (n0 < 16) *(b0 + n0++) = t; }
        else { if (n1 < 16) *(b1 + n1++) = t; }
    }
    grid_sort_band(b0, n0, vert);
    grid_sort_band(b1, n1, vert);
    if (n0 + n1 == 4 && s->grid.preset >= 2) {
        if (n0 == 3 && n1 == 1) { *(b1 + n1++) = *(b0 + --n0); grid_sort_band(b1, n1, vert); }
        else if (n1 == 3 && n0 == 1) { *(b0 + n0++) = *(b1 + --n1); grid_sort_band(b0, n0, vert); }
    }
    if (n0 > 0) {
        int step = 1000 / n0;
        for (int i = 0; i < n0; i++) {
            wptr w = *(b0 + i);
            int pos = i * step, sz = (i == n0 - 1) ? (1000 - pos) : step;
            if (!vert) { w->gx = pos; w->gy = 0; w->gw = sz; w->gh = 500; }
            else { w->gx = 0; w->gy = pos; w->gw = 500; w->gh = sz; }
        }
    }
    if (n1 > 0) {
        int step = 1000 / n1;
        for (int i = 0; i < n1; i++) {
            wptr w = *(b1 + i);
            int pos = i * step, sz = (i == n1 - 1) ? (1000 - pos) : step;
            if (!vert) { w->gx = pos; w->gy = 500; w->gw = sz; w->gh = 500; }
            else { w->gx = 500; w->gy = pos; w->gw = 500; w->gh = sz; }
        }
    }
}

static bool grid_try_merge_hole(struct wm_server *s, int ws, int hx, int hy, int hw, int hh) {
    struct wm_toplevel *t;
    wl_list_for_each(t, &s->toplevels, link) {
        if (!t->in_list || t->is_floating || t->is_minimized || t->workspace != ws) continue;
        if (t->gy == hy && t->gh == hh) {
            if (t->gx + t->gw == hx) { t->gw += hw; return true; }
            if (hx + hw == t->gx) { t->gx = hx; t->gw += hw; return true; }
        }
        if (t->gx == hx && t->gw == hw) {
            if (t->gy + t->gh == hy) { t->gh += hh; return true; }
            if (hy + hh == t->gy) { t->gy = hy; t->gh += hh; return true; }
        }
    }
    int sum_h_left = 0, sum_h_right = 0, sum_w_top = 0, sum_w_bot = 0;
    wl_list_for_each(t, &s->toplevels, link) {
        if (!t->in_list || t->is_floating || t->is_minimized || t->workspace != ws) continue;
        if (t->gy >= hy && t->gy + t->gh <= hy + hh) {
            if (t->gx + t->gw == hx) sum_h_left += t->gh;
            if (hx + hw == t->gx) sum_h_right += t->gh;
        }
        if (t->gx >= hx && t->gx + t->gw <= hx + hw) {
            if (t->gy + t->gh == hy) sum_w_top += t->gw;
            if (hy + hh == t->gy) sum_w_bot += t->gw;
        }
    }
    if (sum_h_left == hh) {
        wl_list_for_each(t, &s->toplevels, link)
            if (t->in_list && !t->is_floating && !t->is_minimized && t->workspace == ws &&
                t->gy >= hy && t->gy + t->gh <= hy + hh && t->gx + t->gw == hx) t->gw += hw;
        return true;
    }
    if (sum_h_right == hh) {
        wl_list_for_each(t, &s->toplevels, link)
            if (t->in_list && !t->is_floating && !t->is_minimized && t->workspace == ws &&
                t->gy >= hy && t->gy + t->gh <= hy + hh && hx + hw == t->gx) { t->gx = hx; t->gw += hw; }
        return true;
    }
    if (sum_w_top == hw) {
        wl_list_for_each(t, &s->toplevels, link)
            if (t->in_list && !t->is_floating && !t->is_minimized && t->workspace == ws &&
                t->gx >= hx && t->gx + t->gw <= hx + hw && t->gy + t->gh == hy) t->gh += hh;
        return true;
    }
    if (sum_w_bot == hw) {
        wl_list_for_each(t, &s->toplevels, link)
            if (t->in_list && !t->is_floating && !t->is_minimized && t->workspace == ws &&
                t->gx >= hx && t->gx + t->gw <= hx + hw && hy + hh == t->gy) { t->gy = hy; t->gh += hh; }
        return true;
    }
    return false;
}

void grid_place_at_cursor(struct wm_server *s, struct wm_toplevel *target) {
    int ux, uy, uw, uh; grid_usable_box(s, &ux, &uy, &uw, &uh);
    int cap = grid_effective_capacity(s), ws = target->workspace, vis = 0; struct wm_toplevel *t;
    wl_list_for_each(t, &s->toplevels, link)
        if (t != target && t->in_list && !t->is_floating && !t->is_minimized && t->workspace == ws) vis++;
    if (vis >= cap) {
        int fws = grid_find_free_normal_ws(s, ws, cap);
        if (fws >= 0) { target->workspace = fws; s->active_workspace = fws; ws = fws; vis = 0; }
    }
    if (vis == 0) {
        target->gx = 0; target->gy = 0; target->gw = 1000; target->gh = 1000;
        return;
    }

    int cx_n = (int)(((s->cursor->x - ux) * 1000.0) / (uw > 0 ? uw : 1));
    int cy_n = (int)(((s->cursor->y - uy) * 1000.0) / (uh > 0 ? uh : 1));
    if (cx_n < 0) cx_n = 0;
    if (cx_n > 999) cx_n = 999;
    if (cy_n < 0) cy_n = 0;
    if (cy_n > 999) cy_n = 999;

    if (s->grid.preset == 3 && vis >= 4) {
        bool vert = (s->grid.vertical != 0);
        int band_pos = vert ? cx_n : cy_n;
        int sec_pos = vert ? cy_n : cx_n;
        int sec = (sec_pos * 3) / 1000;
        if (sec < 0) sec = 0;
        if (sec > 2) sec = 2;

        struct wm_toplevel *b0[8], *b1[8];
        int n0 = 0, n1 = 0;
        wl_list_for_each(t, &s->toplevels, link) {
            if (t == target || !t->in_list || t->is_floating || t->is_minimized || t->workspace != ws) continue;
            int mid = vert ? (t->gx + t->gw / 2) : (t->gy + t->gh / 2);
            if (mid < 500) b0[n0++] = t; else b1[n1++] = t;
        }
        bool in_b0 = (band_pos < 500);
        if (n0 >= 3) in_b0 = false;
        else if (n1 >= 3) in_b0 = true;

        struct wm_toplevel **band = in_b0 ? b0 : b1;
        int bn = in_b0 ? n0 : n1;
        grid_sort_band(band, bn, vert);

        for (int i = 0; i < bn; i++) {
            int slot_i = (i < sec) ? i : (i + 1);
            int coord = slot_i * 333 + 10;
            if (!vert) band[i]->gx = coord; else band[i]->gy = coord;
        }
        int t_coord = sec * 333 + 10;
        if (!vert) { target->gx = t_coord; target->gy = in_b0 ? 0 : 500; target->gw = 333; target->gh = 500; }
        else { target->gx = in_b0 ? 0 : 500; target->gy = t_coord; target->gw = 500; target->gh = 333; }
        grid_normalize_3x2(s, ws);
        return;
    }

    struct wm_toplevel *hit = NULL;
    if (vis == 3) {
        wl_list_for_each(t, &s->toplevels, link) {
            if (t == target || !t->in_list || t->is_floating || t->is_minimized || t->workspace != ws) continue;
            if (t->gw > 550 || t->gh > 550) { hit = t; break; }
        }
    }
    if (!hit) {
        wl_list_for_each(t, &s->toplevels, link) {
            if (t == target || !t->in_list || t->is_floating || t->is_minimized || t->workspace != ws) continue;
            if (cx_n >= t->gx && cx_n < t->gx + t->gw && cy_n >= t->gy && cy_n < t->gy + t->gh) { hit = t; break; }
        }
    }
    if (!hit) {
        wl_list_for_each(t, &s->toplevels, link)
            if (t != target && t->in_list && !t->is_floating && !t->is_minimized && t->workspace == ws) { hit = t; break; }
    }
    if (!hit) { target->gx = 0; target->gy = 0; target->gw = 1000; target->gh = 1000; return; }

    double rx = (double)(cx_n - hit->gx) / (hit->gw > 0 ? hit->gw : 1);
    double ry = (double)(cy_n - hit->gy) / (hit->gh > 0 ? hit->gh : 1);
    double dx = rx - 0.5, dy = ry - 0.5;

    int px_w = (hit->gw * (uw > 0 ? uw : 1920)) / 1000;
    int px_h = (hit->gh * (uh > 0 ? uh : 1080)) / 1000;
    bool split_v = (vis == 1) ? (dx * dx >= dy * dy) : (px_w >= px_h);
    if (vis == 2 && s->grid.preset >= 2) {
        if (hit->gw <= 550 && hit->gh >= 900) split_v = false;
        else if (hit->gh <= 550 && hit->gw >= 900) split_v = true;
    }
    if (s->grid.preset == 1) split_v = !s->grid.vertical;

    if (split_v) {
        int w1 = hit->gw / 2, w2 = hit->gw - w1;
        int ax = hit->gx + w1 / 2, bx = hit->gx + w1 + w2 / 2;
        bool put_first = (abs(cx_n - ax) <= abs(cx_n - bx));
        if (put_first) {
            target->gx = hit->gx; target->gy = hit->gy; target->gw = w1; target->gh = hit->gh;
            hit->gx += w1; hit->gw = w2;
        } else {
            target->gx = hit->gx + w1; target->gy = hit->gy; target->gw = w2; target->gh = hit->gh;
            hit->gw = w1;
        }
    } else {
        int h1 = hit->gh / 2, h2 = hit->gh - h1;
        int ay = hit->gy + h1 / 2, by = hit->gy + h1 + h2 / 2;
        bool put_first = (abs(cy_n - ay) <= abs(cy_n - by));
        if (put_first) {
            target->gx = hit->gx; target->gy = hit->gy; target->gw = hit->gw; target->gh = h1;
            hit->gy += h1; hit->gh = h2;
        } else {
            target->gx = hit->gx; target->gy = hit->gy + h1; target->gw = hit->gw; target->gh = h2;
            hit->gh = h1;
        }
    }
    if (vis + 1 == 4 && s->grid.preset >= 2) grid_normalize_3x2(s, ws);
}

struct grid_text_buf { struct wlr_buffer base; cairo_surface_t *surface; void *data; size_t stride; };
static void grid_text_buf_destroy(struct wlr_buffer *b) { struct grid_text_buf *tb = wl_container_of(b, tb, base); if (tb->surface) cairo_surface_destroy(tb->surface); free(tb); }
static bool grid_text_buf_begin(struct wlr_buffer *b, uint32_t f, void **d, uint32_t *fmt, size_t *s) { (void)f; struct grid_text_buf *tb = wl_container_of(b, tb, base); *d = tb->data; *fmt = DRM_FORMAT_ARGB8888; *s = tb->stride; return true; }
static void grid_text_buf_end(struct wlr_buffer *b) { (void)b; }
static const struct wlr_buffer_impl grid_text_buf_impl = { .destroy = grid_text_buf_destroy, .begin_data_ptr_access = grid_text_buf_begin, .end_data_ptr_access = grid_text_buf_end };

static struct wlr_buffer *grid_render_label_buf(const char *text, int w, int h) {
    cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) { cairo_surface_destroy(surf); return NULL; }
    cairo_t *cr = cairo_create(surf);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR); cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 13.0);
    cairo_text_extents_t ext; cairo_text_extents(cr, text, &ext);
    cairo_set_source_rgba(cr, 0.95, 0.96, 0.98, 0.96);
    cairo_move_to(cr, (w - ext.width) * 0.5 - ext.x_bearing, (h - ext.height) * 0.5 - ext.y_bearing);
    cairo_show_text(cr, text); cairo_destroy(cr); cairo_surface_flush(surf);
    struct grid_text_buf *buf = calloc(1, sizeof(*buf));
    if (!buf) { cairo_surface_destroy(surf); return NULL; }
    wlr_buffer_init(&buf->base, &grid_text_buf_impl, w, h);
    buf->surface = surf; buf->data = cairo_image_surface_get_data(surf); buf->stride = (size_t)cairo_image_surface_get_stride(surf);
    return &buf->base;
}

static int grid_hud_hide_cb(void *data) { struct wm_server *s = data; if (s->grid.hud_tree) { wlr_scene_node_destroy(&s->grid.hud_tree->node); s->grid.hud_tree = NULL; } return 0; }
static void grid_show_hud(struct wm_server *s) {
    if (wl_list_empty(&s->outputs)) return;
    struct wm_output *out = wl_container_of(s->outputs.next, out, link);
    int sw = wm_output_width(out->wlr_output), sh = wm_output_height(out->wlr_output);
    if (sw <= 0 || sh <= 0) return;
    if (s->grid.hud_tree) { wlr_scene_node_destroy(&s->grid.hud_tree->node); s->grid.hud_tree = NULL; }
    int hud_w = 204, hud_h = 148, pad = 12, inner_w = hud_w - 2 * pad, inner_h = hud_h - 2 * pad - 26;
    int outer_r = (s->config.hud_radius >= 0) ? s->config.hud_radius : s->config.corner_radius;
    int cell_r = (s->config.hud_radius >= 0) ? s->config.hud_radius : (s->config.corner_radius / 2);
    s->grid.hud_tree = wlr_scene_tree_create(&s->scene->tree);
    if (!s->grid.hud_tree) return;
    wlr_scene_node_set_position(&s->grid.hud_tree->node, (sw - hud_w) / 2, (sh - hud_h) / 2);
    wlr_scene_node_raise_to_top(&s->grid.hud_tree->node);
    float bcol[4] = {0.42f, 0.43f, 0.46f, 0.90f}, bgcol[4] = {0.08f, 0.08f, 0.09f, 0.95f};
    struct wlr_scene_rect *brd = wlr_scene_rect_create(s->grid.hud_tree, hud_w + 4, hud_h + 4, bcol);
    if (brd) { wlr_scene_node_set_position(&brd->node, -2, -2); wlr_scene_rect_set_corner_radius(brd, outer_r > 0 ? outer_r + 2 : 0, CORNER_LOCATION_ALL); }
    struct wlr_scene_rect *bg = wlr_scene_rect_create(s->grid.hud_tree, hud_w, hud_h, bgcol);
    if (bg) wlr_scene_rect_set_corner_radius(bg, outer_r > 0 ? outer_r : 0, CORNER_LOCATION_ALL);
    int tiled = grid_count_tiled_ws(s, s->active_workspace);
    int act_s = (s->active_toplevel && s->active_toplevel->workspace == s->active_workspace && !s->active_toplevel->is_minimized) ? s->active_toplevel->slot : -1;
    if (tiled > 0) {
        for (int i = 0; i < tiled; i++) {
            struct wm_rect r; grid_cell(s, 1000, 1000, i, &r);
            int rw = (r.w * inner_w) / 1000 - 8, rh = (r.h * inner_h) / 1000 - 8;
            float ccol[4] = {0.74f, 0.76f, 0.79f, 0.75f};
            if (i == act_s || tiled == 1) { ccol[0] = 1.0f; ccol[1] = 1.0f; ccol[2] = 1.0f; ccol[3] = 1.0f; }
            struct wlr_scene_rect *rc = wlr_scene_rect_create(s->grid.hud_tree, rw < 4 ? 4 : rw, rh < 4 ? 4 : rh, ccol);
            if (rc) { wlr_scene_node_set_position(&rc->node, pad + (r.x * inner_w) / 1000 + 4, pad + (r.y * inner_h) / 1000 + 4); wlr_scene_rect_set_corner_radius(rc, cell_r > 0 ? cell_r : 0, CORNER_LOCATION_ALL); }
        }
    } else {
        int cols = grid_cols(s), rows = grid_rows(s);
        for (int r = 0; r < rows; r++) for (int c = 0; c < cols; c++) {
            int rw = inner_w / cols - 6, rh = inner_h / rows - 6;
            float ccol[4] = {0.74f, 0.76f, 0.79f, 0.75f};
            struct wlr_scene_rect *rc = wlr_scene_rect_create(s->grid.hud_tree, rw < 4 ? 4 : rw, rh < 4 ? 4 : rh, ccol);
            if (rc) { wlr_scene_node_set_position(&rc->node, pad + c * (inner_w / cols) + 3, pad + r * (inner_h / rows) + 3); wlr_scene_rect_set_corner_radius(rc, cell_r > 0 ? cell_r : 0, CORNER_LOCATION_ALL); }
        }
    }
    char lbl[32]; snprintf(lbl, sizeof(lbl), "%d x %d", grid_cols(s), grid_rows(s));
    struct wlr_buffer *tb = grid_render_label_buf(lbl, inner_w, 18);
    if (tb) { struct wlr_scene_buffer *sb = wlr_scene_buffer_create(s->grid.hud_tree, tb); if (sb) wlr_scene_node_set_position(&sb->node, pad, hud_h - pad - 18); wlr_buffer_drop(tb); }
    struct wlr_keyboard *kb = wlr_seat_get_keyboard(s->seat);
    if (kb && (wlr_keyboard_get_modifiers(kb) & WLR_MODIFIER_LOGO)) { if (s->grid.hud_timer) wl_event_source_timer_update(s->grid.hud_timer, 0); }
    else { if (!s->grid.hud_timer) s->grid.hud_timer = wl_event_loop_add_timer(wl_display_get_event_loop(s->wl_display), grid_hud_hide_cb, s); if (s->grid.hud_timer) wl_event_source_timer_update(s->grid.hud_timer, 1500); }
}
void grid_refresh_hud(struct wm_server *s) { if (s && s->grid.hud_tree) grid_show_hud(s); }
void grid_on_super_release(struct wm_server *s) { if (s->grid.hud_timer) wl_event_source_timer_update(s->grid.hud_timer, 0); if (s->grid.hud_tree) { wlr_scene_node_destroy(&s->grid.hud_tree->node); s->grid.hud_tree = NULL; } }

static void grid_apply_bounds(struct wm_server *s) {
    int cap = grid_effective_capacity(s), shown = 0; struct wm_toplevel *t;
    wl_list_for_each(t, &s->toplevels, link) {
        if (!t->in_list || t->is_floating || t->workspace != s->active_workspace) continue;
        if (!t->is_minimized) {
            if (shown < cap) shown++;
            else {
                int pws = grid_find_penalty_ws(s, s->active_workspace, cap);
                if (pws >= 0) { t->workspace = pws; t->slot = -1; } else t->is_minimized = true;
                wm_hide_toplevel_internal(t);
            }
        }
    }
    if (shown < cap) wl_list_for_each(t, &s->toplevels, link) {
        if (shown >= cap) break;
        if (t->in_list && !t->is_floating && t->workspace == s->active_workspace && t->is_minimized) {
            t->is_minimized = false; t->slot = -1; shown++;
        }
    }
    grid_reclaim_penalty(s, s->active_workspace);
    int idx = 0, rows = grid_rows(s), cols = grid_cols(s);
    if (rows < 1) rows = 1;
    if (cols < 1) cols = 1;
    wl_list_for_each(t, &s->toplevels, link) {
        if (!t->in_list || t->is_floating || t->is_minimized || t->workspace != s->active_workspace) continue;
        int r = (idx / cols) % rows, c = idx % cols;
        t->gx = (c * 1000) / cols; t->gw = ((c + 1) * 1000) / cols - t->gx;
        t->gy = (r * 1000) / rows; t->gh = ((r + 1) * 1000) / rows - t->gy;
        idx++;
    }
    grid_normalize_3x2(s, s->active_workspace);
    wm_relayout_internal(s); grid_show_hud(s);
}

void grid_cycle_preset(struct wm_server *s, int delta) {
    if (s->config.grid_lock) { grid_show_hud(s); return; }
    int next = s->grid.preset + (delta > 0 ? 1 : -1);
    if (next < 0) next = 0;
    if (next > 3) next = 3;
    if (next == s->grid.preset) { grid_show_hud(s); return; }
    s->grid.preset = next; grid_apply_bounds(s);
}

void grid_toggle_orientation(struct wm_server *s) {
    if (s->config.grid_lock) { grid_show_hud(s); return; }
    typedef struct wm_toplevel *wptr;
    int ws = s->active_workspace;
    wptr *wins = __builtin_alloca(32 * sizeof(wptr));
    struct wm_toplevel *t;
    int n = 0;
    wl_list_for_each(t, &s->toplevels, link) {
        if (!t->in_list || t->is_floating || t->is_minimized || t->workspace != ws) continue;
        if (n < 32) *(wins + n++) = t;
    }
    bool to_vert = (s->grid.vertical == 0);
    s->grid.vertical = to_vert ? 1 : 0;

    if (n <= 1 || n == 4) {

    } else if (n == 2) {
        grid_sort_band(wins, 2, !to_vert);
        wptr w0 = *(wins + 0), w1 = *(wins + 1);
        if (to_vert) {
            w0->gx = 0; w0->gy = 0; w0->gw = 1000; w0->gh = 500;
            w1->gx = 0; w1->gy = 500; w1->gw = 1000; w1->gh = 500;
        } else {
            w0->gx = 0; w0->gy = 0; w0->gw = 500; w0->gh = 1000;
            w1->gx = 500; w1->gy = 0; w1->gw = 500; w1->gh = 1000;
        }
    } else if (n == 3) {
        wptr win_a = NULL, win_b = NULL, win_c = NULL;
        for (int i = 0; i < n; i++) {
            wptr w = *(wins + i);
            if (w->gx < 250 && w->gy < 250) win_a = w;
            else if (w->gx >= 250 && w->gy < 250) win_b = w;
            else win_c = w;
        }
        if (!win_a) win_a = *(wins + 0);
        if (!win_b) win_b = (*(wins + 0) != win_a) ? *(wins + 0) : *(wins + 1);
        if (!win_c) {
            for (int i = 0; i < n; i++) {
                wptr w = *(wins + i);
                if (w != win_a && w != win_b) { win_c = w; break; }
            }
        }
        if (to_vert) {
            win_a->gx = 0; win_a->gy = 0; win_a->gw = 500; win_a->gh = 500;
            win_b->gx = 500; win_b->gy = 0; win_b->gw = 500; win_b->gh = 500;
            win_c->gx = 0; win_c->gy = 500; win_c->gw = 1000; win_c->gh = 500;
        } else {
            win_a->gx = 0; win_a->gy = 0; win_a->gw = 500; win_a->gh = 1000;
            win_b->gx = 500; win_b->gy = 0; win_b->gw = 500; win_b->gh = 500;
            win_c->gx = 500; win_c->gy = 500; win_c->gw = 500; win_c->gh = 500;
        }
    } else if (n == 5 || n == 6) {
        wptr *b0 = __builtin_alloca(16 * sizeof(wptr));
        wptr *b1 = __builtin_alloca(16 * sizeof(wptr));
        int n0 = 0, n1 = 0;
        for (int i = 0; i < n; i++) {
            wptr w = *(wins + i);
            int mid = to_vert ? (w->gy + w->gh / 2) : (w->gx + w->gw / 2);
            if (mid < 500) *(b0 + n0++) = w; else *(b1 + n1++) = w;
        }
        grid_sort_band(b0, n0, !to_vert);
        grid_sort_band(b1, n1, !to_vert);

        if (to_vert) {
            wptr *c0 = __builtin_alloca(16 * sizeof(wptr));
            wptr *c1 = __builtin_alloca(16 * sizeof(wptr));
            int nc0 = 0, nc1 = 0;
            if (n0 > 0) *(c0 + nc0++) = *(b0 + 0);
            if (n1 > 0) *(c0 + nc0++) = *(b1 + 0);
            if (n0 > 1) *(c1 + nc1++) = *(b0 + 1);
            if (n1 > 1) *(c1 + nc1++) = *(b1 + 1);
            if (n0 >= 3) *(c1 + nc1++) = *(b0 + 2);
            if (n1 >= 3) *(c0 + nc0++) = *(b1 + 2);
            for (int i = 0; i < nc0; i++) {
                wptr w = *(c0 + i);
                int step = 1000 / nc0, pos = i * step;
                w->gx = 0; w->gy = pos; w->gw = 500; w->gh = (i == nc0 - 1) ? (1000 - pos) : step;
            }
            for (int i = 0; i < nc1; i++) {
                wptr w = *(c1 + i);
                int step = 1000 / nc1, pos = i * step;
                w->gx = 500; w->gy = pos; w->gw = 500; w->gh = (i == nc1 - 1) ? (1000 - pos) : step;
            }
        } else {
            wptr *r0 = __builtin_alloca(16 * sizeof(wptr));
            wptr *r1 = __builtin_alloca(16 * sizeof(wptr));
            int nr0 = 0, nr1 = 0;
            if (n0 > 0) *(r0 + nr0++) = *(b0 + 0);
            if (n1 > 0) *(r0 + nr0++) = *(b1 + 0);
            if (n0 > 1) *(r1 + nr1++) = *(b0 + 1);
            if (n1 > 1) *(r1 + nr1++) = *(b1 + 1);
            if (n1 >= 3) *(r0 + nr0++) = *(b1 + 2);
            if (n0 >= 3) *(r1 + nr1++) = *(b0 + 2);
            for (int i = 0; i < nr0; i++) {
                wptr w = *(r0 + i);
                int step = 1000 / nr0, pos = i * step;
                w->gx = pos; w->gy = 0; w->gw = (i == nr0 - 1) ? (1000 - pos) : step; w->gh = 500;
            }
            for (int i = 0; i < nr1; i++) {
                wptr w = *(r1 + i);
                int step = 1000 / nr1, pos = i * step;
                w->gx = pos; w->gy = 500; w->gw = (i == nr1 - 1) ? (1000 - pos) : step; w->gh = 500;
            }
        }
    } else {
        for (int i = 0; i < n; i++) {
            wptr w = *(wins + i);
            int ox = w->gx, ow = w->gw;
            w->gx = w->gy; w->gw = w->gh;
            w->gy = ox; w->gh = ow;
        }
    }
    wm_relayout_internal(s); grid_show_hud(s);
}

void grid_handle_scroll(struct wm_server *s, uint32_t time_msec, double delta) {
    if (delta == 0.0 || time_msec - s->grid.last_scroll_msec < 180) return;
    s->grid.last_scroll_msec = time_msec; grid_cycle_preset(s, delta < 0.0 ? 1 : -1);
}

void grid_init(struct wm_server *s) {
    s->grid.prev_count = 0;
    s->grid.preset = (s->config.grid_preset >= 0 && s->config.grid_preset <= 3) ? s->config.grid_preset : 3;
    s->grid.vertical = s->config.grid_vertical ? 1 : 0;
}
void grid_fini(struct wm_server *s) {
    if (s->grid.hud_timer) wl_event_source_remove(s->grid.hud_timer);
    if (s->grid.hud_tree) wlr_scene_node_destroy(&s->grid.hud_tree->node);
}
void grid_assign_slots(struct wm_server *s, struct wm_toplevel **wins, int count) { (void)s; for (int i = 0; i < count; i++) wins[i]->slot = i; }

void grid_on_toplevel_remove(struct wm_server *s, struct wm_toplevel *closing) {
    if (!s || !closing) return;
    int ws = closing->workspace, hx = closing->gx, hy = closing->gy, hw = closing->gw, hh = closing->gh;
    bool was_tiled = (!closing->is_floating && !closing->is_minimized);
    if (!closing->is_floating) {
        wl_list_remove(&closing->link); closing->in_list = false;
        grid_reclaim_penalty(s, ws);
    }
    if (!was_tiled) return;
    int rem = grid_count_tiled_ws(s, ws);
    if (rem <= 0) return;
    if (rem == 1) {
        struct wm_toplevel *t;
        wl_list_for_each(t, &s->toplevels, link)
            if (t->in_list && !t->is_floating && !t->is_minimized && t->workspace == ws) { t->gx = 0; t->gy = 0; t->gw = 1000; t->gh = 1000; break; }
        return;
    }
    if ((s->grid.preset == 3 && rem >= 4) || !grid_try_merge_hole(s, ws, hx, hy, hw, hh)) {
        grid_normalize_3x2(s, ws);
    }
}

static int grid_count_on_ws(const struct wm_server *s, int ws) { int n = 0; struct wm_toplevel *t; wl_list_for_each(t, &s->toplevels, link) if (t->in_list && t->workspace == ws) n++; return n; }

static int grid_find_penalty_ws(struct wm_server *s, int owner_ws, int cap) {
    int total = s->config.workspaces > 16 ? 16 : s->config.workspaces;
    if (total < 2) return -1;
    for (int ws = total - 1; ws >= 0; ws--) if (ws != owner_ws && s->workspace_group_owner[ws] == owner_ws + 1 && grid_count_on_ws(s, ws) < cap) return ws;
    for (int ws = total - 1; ws >= 0; ws--) if (ws != owner_ws && grid_count_on_ws(s, ws) == 0 && s->workspace_group_owner[ws] == 0) { s->workspace_group_owner[ws] = owner_ws + 1; return ws; }
    return -1;
}

static int grid_find_free_normal_ws(struct wm_server *s, int from_ws, int cap) {
    int total = s->config.workspaces > 16 ? 16 : s->config.workspaces;
    for (int step = 1; step < total; step++) {
        int ws = (from_ws + step) % total;
        if (s->workspace_group_owner[ws] == 0 && !s->workspace_floating[ws] && grid_count_on_ws(s, ws) < cap) return ws;
    }
    return -1;
}

static void grid_reclaim_penalty(struct wm_server *s, int owner_ws) {
    int cap = grid_effective_capacity(s), total = s->config.workspaces > 16 ? 16 : s->config.workspaces;
    while (grid_count_on_ws(s, owner_ws) < cap) {
        struct wm_toplevel *cand = NULL, *t; int found_ws = -1;
        for (int w = total - 1; w >= 0 && !cand; w--) {
            if (w == owner_ws || s->workspace_group_owner[w] != owner_ws + 1) continue;
            wl_list_for_each(t, &s->toplevels, link) if (t->in_list && t->workspace == w) { cand = t; found_ws = w; break; }
        }
        if (!cand) break;
        cand->workspace = owner_ws; cand->is_floating = false; cand->is_minimized = false;
        grid_place_at_cursor(s, cand);
        if (grid_count_on_ws(s, found_ws) == 0) s->workspace_group_owner[found_ws] = 0;
    }
}

void grid_cycle_slot_stack(struct wm_server *s, int dir) {
    int ws = s->active_workspace, total = s->config.workspaces > 16 ? 16 : s->config.workspaces, pen_ws = -1;
    if (dir >= 0) { for (int w = total - 1; w >= 0; w--) if (w != ws && s->workspace_group_owner[w] == ws + 1 && grid_count_on_ws(s, w) > 0) { pen_ws = w; break; } }
    else { for (int w = 0; w < total; w++) if (w != ws && s->workspace_group_owner[w] == ws + 1 && grid_count_on_ws(s, w) > 0) { pen_ws = w; break; } }
    if (pen_ws < 0) return;
    struct wm_toplevel *inc = NULL, *t;
    wl_list_for_each(t, &s->toplevels, link) if (t->in_list && t->workspace == pen_ws) { inc = t; break; }
    if (!inc) return;
    struct wm_toplevel *out = s->active_toplevel;
    if (!out || !out->in_list || out->workspace != ws)
        wl_list_for_each(t, &s->toplevels, link) if (t->in_list && t->workspace == ws && !t->is_minimized) { out = t; break; }
    if (out) {
        inc->gx = out->gx; inc->gy = out->gy; inc->gw = out->gw; inc->gh = out->gh;
        out->workspace = pen_ws; out->is_floating = false; out->is_minimized = false; wm_hide_toplevel_internal(out);
    }
    inc->workspace = ws; inc->is_floating = s->workspace_floating[ws]; inc->is_minimized = false;
    if (grid_count_on_ws(s, pen_ws) == 0) s->workspace_group_owner[pen_ws] = 0;
    wm_relayout_internal(s); wm_focus_toplevel_internal(inc);
}

void grid_restore_minimized(struct wm_server *s) {
    if (grid_count_tiled_ws(s, s->active_workspace) >= grid_effective_capacity(s)) return;
    struct wm_toplevel *t;
    wl_list_for_each(t, &s->toplevels, link) if (t->in_list && !t->is_floating && t->workspace == s->active_workspace && t->is_minimized) { t->is_minimized = false; break; }
}

void grid_toggle_workspace_floating(struct wm_server *s) {
    int ws = s->active_workspace; bool nf = !s->workspace_floating[ws]; s->workspace_floating[ws] = nf;
    if (nf) {
        int ux, uy, uw, uh, idx = 0; grid_usable_box(s, &ux, &uy, &uw, &uh);
        struct wm_toplevel *t;
        wl_list_for_each(t, &s->toplevels, link) if (t->in_list && t->workspace == ws) {
            t->is_floating = true; t->is_minimized = false; t->width = uw / 2; t->height = uh / 2;
            t->x = ux + (uw - t->width) / 2 + (idx - 1) * 28; t->y = uy + (uh - t->height) / 2 + (idx - 1) * 24;
            wm_apply_floating_geometry_internal(t); idx++;
        }
    } else {
        int cap = grid_effective_capacity(s), kept = 0; struct wm_toplevel *t;
        wl_list_for_each(t, &s->toplevels, link) if (t->in_list && t->workspace == ws) {
            t->is_floating = false; t->is_minimized = false;
            if (kept < cap) kept++;
            else { int pws = grid_find_penalty_ws(s, ws, cap); if (pws >= 0) t->workspace = pws; else t->is_minimized = true; wm_hide_toplevel_internal(t); }
        }
        grid_apply_bounds(s); return;
    }
    wm_relayout_internal(s);
}

void grid_toggle_window_floating(struct wm_server *s) {
    static uint64_t last_ms = 0; struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now = (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000ULL);
    if (now - last_ms < 250ULL) return;
    last_ms = now;
    struct wm_toplevel *t = s->active_toplevel;
    if (!t || !t->in_list || t->workspace != s->active_workspace) return;
    if (t->is_floating) { t->is_floating = false; grid_place_at_cursor(s, t); }
    else {
        int hx = t->gx, hy = t->gy, hw = t->gw, hh = t->gh;
        int ux, uy, uw, uh; grid_usable_box(s, &ux, &uy, &uw, &uh);
        t->is_floating = true; t->width = uw / 2; t->height = uh / 2; t->x = ux + (uw - t->width) / 2; t->y = uy + (uh - t->height) / 2;
        wm_apply_floating_geometry_internal(t);
        if ((s->grid.preset == 3 && grid_count_tiled_ws(s, s->active_workspace) >= 4) || !grid_try_merge_hole(s, s->active_workspace, hx, hy, hw, hh)) grid_normalize_3x2(s, s->active_workspace);
    }
    wm_relayout_internal(s); wm_focus_toplevel_internal(t);
}

void grid_swap_windows(struct wm_server *s, struct wm_toplevel *a, struct wm_toplevel *b) {
    if (!a || !b || a == b) return;
    if (s && grid_count_tiled_ws(s, s->active_workspace) == 2) {
        int ux, uy, uw, uh; grid_usable_box(s, &ux, &uy, &uw, &uh);
        double dx = (s->cursor->x - ux) / (uw > 0 ? uw : 1) - 0.5;
        double dy = (s->cursor->y - uy) / (uh > 0 ? uh : 1) - 0.5;
        if (dx * dx >= dy * dy) {
            if (dx < 0.0) { a->gx = 0; a->gy = 0; a->gw = 500; a->gh = 1000; b->gx = 500; b->gy = 0; b->gw = 500; b->gh = 1000; }
            else { a->gx = 500; a->gy = 0; a->gw = 500; a->gh = 1000; b->gx = 0; b->gy = 0; b->gw = 500; b->gh = 1000; }
        } else {
            if (dy < 0.0) { a->gx = 0; a->gy = 0; a->gw = 1000; a->gh = 500; b->gx = 0; b->gy = 500; b->gw = 1000; b->gh = 500; }
            else { a->gx = 0; a->gy = 500; a->gw = 1000; a->gh = 500; b->gx = 0; b->gy = 0; b->gw = 1000; b->gh = 500; }
        }
        return;
    }
    if (s && grid_count_tiled_ws(s, s->active_workspace) == 3 && (b->gw > 600 || b->gh > 600) && a->gw <= 600 && a->gh <= 600) {
        int ux, uy, uw, uh; grid_usable_box(s, &ux, &uy, &uw, &uh);
        int cx_n = (int)(((s->cursor->x - ux) * 1000.0) / (uw > 0 ? uw : 1));
        int cy_n = (int)(((s->cursor->y - uy) * 1000.0) / (uh > 0 ? uh : 1));
        struct wm_toplevel *c = NULL, *t;
        wl_list_for_each(t, &s->toplevels, link)
            if (t != a && t != b && t->in_list && !t->is_floating && !t->is_minimized && t->workspace == s->active_workspace) { c = t; break; }
        if (c) {
            if (b->gw > 600) {
                bool first = cx_n < 500;
                a->gx = first ? 0 : 500; a->gy = b->gy; a->gw = 500; a->gh = 500;
                b->gx = first ? 500 : 0; b->gw = 500; b->gh = 500;
                c->gx = 0; c->gy = 500 - b->gy; c->gw = 1000; c->gh = 500;
            } else {
                bool first = cy_n < 500;
                a->gx = b->gx; a->gy = first ? 0 : 500; a->gw = 500; a->gh = 500;
                b->gy = first ? 500 : 0; b->gw = 500; b->gh = 500;
                c->gx = 500 - b->gx; c->gy = 0; c->gw = 500; c->gh = 1000;
            }
            return;
        }
    }
    int tx = a->gx, ty = a->gy, tw = a->gw, th = a->gh;
    a->gx = b->gx; a->gy = b->gy; a->gw = b->gw; a->gh = b->gh;
    b->gx = tx; b->gy = ty; b->gw = tw; b->gh = th;
}

void grid_swap_direction(struct wm_server *s, int dx, int dy) {
    struct wm_toplevel *cur = s->active_toplevel;
    if (!cur || cur->is_floating || cur->is_minimized || cur->workspace != s->active_workspace) return;
    double cx = cur->x + cur->width * 0.5, cy = cur->y + cur->height * 0.5, best_s = -1.0;
    struct wm_toplevel *best = NULL, *t;
    wl_list_for_each(t, &s->toplevels, link) {
        if (t == cur || !t->in_list || t->is_floating || t->is_minimized || t->workspace != s->active_workspace) continue;
        double vx = (t->x + t->width * 0.5) - cx, vy = (t->y + t->height * 0.5) - cy;
        if ((dx > 0 && vx <= 10.0) || (dx < 0 && vx >= -10.0) || (dy > 0 && vy <= 10.0) || (dy < 0 && vy >= -10.0)) continue;
        double d2 = (dx != 0) ? (vx * vx + 6.0 * vy * vy) : (6.0 * vx * vx + vy * vy);
        if (best_s < 0.0 || d2 < best_s) { best_s = d2; best = t; }
    }
    if (best) { grid_swap_windows(s, cur, best); wm_relayout_internal(s); wm_focus_toplevel_internal(cur); }
}

void grid_fit_rect(struct wm_rect *r, const struct wm_rect *b, int m) {
    int mw = b->w - 2 * m, mh = b->h - 2 * m;
    if (mw < 1) mw = 1;
    if (mh < 1) mh = 1;
    if (r->w > mw) r->w = mw;
    if (r->h > mh) r->h = mh;
    if (r->w < 1) r->w = 1;
    if (r->h < 1) r->h = 1;
    r->x = b->x + (b->w - r->w) / 2; r->y = b->y + (b->h - r->h) / 2;
}

void grid_focus_direction(struct wm_server *s, int dx, int dy) {
    struct wm_toplevel *cur = s->active_toplevel;
    if (!cur || cur->workspace != s->active_workspace) return;
    double cx = cur->x + cur->width * 0.5, cy = cur->y + cur->height * 0.5, best_s = -1.0;
    struct wm_toplevel *best = NULL, *t;
    wl_list_for_each(t, &s->toplevels, link) {
        if (t == cur || !t->in_list || t->is_minimized || t->workspace != s->active_workspace) continue;
        double vx = (t->x + t->width * 0.5) - cx, vy = (t->y + t->height * 0.5) - cy;
        if ((dx > 0 && vx <= 10.0) || (dx < 0 && vx >= -10.0) || (dy > 0 && vy <= 10.0) || (dy < 0 && vy >= -10.0)) continue;
        double d2 = (dx != 0) ? (vx * vx + 6.0 * vy * vy) : (6.0 * vx * vx + vy * vy);
        if (best_s < 0.0 || d2 < best_s) { best_s = d2; best = t; }
    }
    if (best) wm_focus_toplevel_internal(best);
}
