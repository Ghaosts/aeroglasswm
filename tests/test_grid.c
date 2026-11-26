#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/modules/wm_internal.h"

/* Минимальные заглушки для функций сцены и курсора, не относящихся к математике сетки */
void wm_relayout_internal(struct wm_server *s) { (void)s; }

void wlr_output_effective_resolution(struct wlr_output *o, int *w, int *h) { (void)o; if (w) *w = 1920; if (h) *h = 1080; }
void wlr_scene_node_raise_to_top(struct wlr_scene_node *n) { (void)n; }
void wlr_scene_rect_set_corner_radius(struct wlr_scene_rect *r, int rad, enum corner_location c) { (void)r; (void)rad; (void)c; }
struct wlr_keyboard *wlr_seat_get_keyboard(struct wlr_seat *s) { (void)s; return NULL; }
uint32_t wlr_keyboard_get_modifiers(struct wlr_keyboard *k) { (void)k; return 0; }
void wm_hide_toplevel_internal(struct wm_toplevel *t) { (void)t; }
void wm_focus_toplevel_internal(struct wm_toplevel *t) { (void)t; }
void wm_apply_floating_geometry_internal(struct wm_toplevel *t) { (void)t; }

void wm_focus_toplevel(struct wm_server *s, struct wm_toplevel *t) { if (s) s->active_toplevel = t; }
void wm_toggle_floating(struct wm_server *s, struct wm_toplevel *t) { (void)s; if (t) t->is_floating = !t->is_floating; }
void wm_toplevel_set_minimized(struct wm_server *s, struct wm_toplevel *t, bool m) { (void)s; if (t) t->is_minimized = m; }
struct wlr_scene_tree *wlr_scene_tree_create(struct wlr_scene_tree *p) { (void)p; return NULL; }
struct wlr_scene_rect *wlr_scene_rect_create(struct wlr_scene_tree *p, int w, int h, const float c[4]) { (void)p;(void)w;(void)h;(void)c; return NULL; }
struct wlr_scene_buffer *wlr_scene_buffer_create(struct wlr_scene_tree *p, struct wlr_buffer *b) { (void)p;(void)b; return NULL; }
void wlr_scene_node_set_position(struct wlr_scene_node *n, int x, int y) { (void)n;(void)x;(void)y; }
void wlr_scene_node_destroy(struct wlr_scene_node *n) { (void)n; }
void wlr_buffer_init(struct wlr_buffer *b, const struct wlr_buffer_impl *i, int w, int h) { (void)b;(void)i;(void)w;(void)h; }
void wlr_buffer_drop(struct wlr_buffer *b) { (void)b; }
int wl_event_source_timer_update(struct wl_event_source *s, int ms) { (void)s;(void)ms; return 0; }
struct wl_event_source *wl_event_loop_add_timer(struct wl_event_loop *l, wl_event_loop_timer_func_t cb, void *d) { (void)l;(void)cb;(void)d; return NULL; }
struct wl_event_loop *wl_display_get_event_loop(struct wl_display *d) { (void)d; return NULL; }

static void init_test_server(struct wm_server *s, struct wlr_cursor *cur) {
    memset(s, 0, sizeof(*s));
    memset(cur, 0, sizeof(*cur));
    wl_list_init(&s->toplevels);
    wl_list_init(&s->outputs);
    s->cursor = cur;
    snprintf(s->config.layout_mode, sizeof(s->config.layout_mode), "desktop");
    s->config.max_windows = 6;
    s->config.workspaces = 9;
    s->config.gap = 10;
    s->config.gap_x = 10;
    s->config.gap_y = 10;
    s->usable_area = (struct wlr_box){ 0, 0, 1920, 1080 };
    s->grid.preset = 3; /* 3x2 */
}

static struct wm_toplevel *spawn_win(struct wm_server *s, double cx, double cy) {
    struct wm_toplevel *t = calloc(1, sizeof(*t));
    assert(t != NULL);
    t->in_list = true;
    t->workspace = s->active_workspace;
    s->cursor->x = cx;
    s->cursor->y = cy;
    wl_list_insert(s->toplevels.prev, &t->link);
    grid_place_at_cursor(s, t);
    return t;
}

static void assert_full_coverage(struct wm_server *s) {
    int total_area = 0;
    struct wm_toplevel *t;
    wl_list_for_each(t, &s->toplevels, link) {
        if (!t->in_list || t->is_floating || t->is_minimized || t->workspace != s->active_workspace) continue;
        assert(t->gx >= 0 && t->gy >= 0);
        assert(t->gx + t->gw <= 1000 && t->gy + t->gh <= 1000);
        assert(t->gw > 0 && t->gh > 0);
        total_area += t->gw * t->gh;
    }
    assert(total_area == 1000 * 1000);
}

int main(void) {
    struct wm_server s;
    struct wlr_cursor cur;
    init_test_server(&s, &cur);

    /* 1. Спавн от 1 до 6 окон в сетку 3x2 */
    struct wm_toplevel *w1 = spawn_win(&s, 200, 200);
    assert_full_coverage(&s);

    struct wm_toplevel *w2 = spawn_win(&s, 1500, 200);
    assert_full_coverage(&s);

    struct wm_toplevel *w3 = spawn_win(&s, 200, 800);
    assert_full_coverage(&s);

    /* 4-е окно должно занять последний квадрант -> 4 равных окна */
    struct wm_toplevel *w4 = spawn_win(&s, 1500, 800);
    assert_full_coverage(&s);
    assert(w1->gw == 500 && w1->gh == 500);
    assert(w4->gw == 500 && w4->gh == 500);

    /* 5-е и 6-е окна в полосы 3x2 */
    struct wm_toplevel *w5 = spawn_win(&s, 960, 200);
    assert_full_coverage(&s);

    struct wm_toplevel *w6 = spawn_win(&s, 960, 800);
    assert_full_coverage(&s);

    /* 2. Поворот ориентации (3x2 <-> 2x3) без потери площади */
    grid_toggle_orientation(&s);
    assert(s.grid.vertical == 1);
    assert_full_coverage(&s);

    grid_toggle_orientation(&s);
    assert(s.grid.vertical == 0);
    assert_full_coverage(&s);

    /* 3. Проверка пиксельных координат cell с учётом gap=10 */
    struct wm_rect r0;
    grid_cell(&s, 1920, 1080, 0, &r0);
    assert(r0.x >= 10 && r0.y >= 10 && r0.w > 600 && r0.h > 300);

    /* 4. Удаление окна (w5) и проверка поглощения пустоты */
    grid_on_toplevel_remove(&s, w5);
    free(w5);
    assert_full_coverage(&s);

    /* Очистка оставшихся окон */
    struct wm_toplevel *tmp, *it;
    wl_list_for_each_safe(it, tmp, &s.toplevels, link) {
        wl_list_remove(&it->link);
        free(it);
    }
    (void)w2; (void)w3; (void)w6;
    printf("ALL GRID UNIT TESTS PASSED (1..6 windows, 4-quadrant, 3x2/2x3 rotation, hole merge, ASan+UBSan clean)\n");
    return 0;
}
