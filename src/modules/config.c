#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/util/log.h>
#include "toml.h"
#include "wm_internal.h"

#ifndef WM_HOTKEY_MOD_MASK
#define WM_HOTKEY_MOD_MASK (WLR_MODIFIER_SHIFT | WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO)
#endif

static void wm_parse_color(toml_table_t *table, const char *key, float out_color[4], float default_r, float default_g, float default_b, float default_a) {
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

void wm_config_defaults(struct wm_config *config) {
    memset(config, 0, sizeof(*config));
    config->gap = 10;
    config->gap_x = 20;
    config->gap_y = 20;
    config->corner_radius = 10;
    config->border_width = 2;
    config->slider_enabled = true;
    config->anim_duration = 240;
    config->workspaces = 9;
    config->max_windows = 0;
    config->grid_preset = 3;
    config->grid_vertical = 0;
    config->grid_lock = 0;
    config->hud_radius = -1;
    snprintf(config->hud_mode, sizeof(config->hud_mode), "miniature");
    config->hud_radius = -1;
    config->config_path[0] = 0;
    snprintf(config->layout_mode, sizeof(config->layout_mode), "%s", "desktop");
    snprintf(config->theme, sizeof(config->theme), "%s", "dark");
    wm_parse_color(NULL, "", config->bg_color, 0.95f, 0.95f, 0.98f, 1.0f);
    wm_parse_color(NULL, "", config->border_color_active, 1.0f, 1.0f, 1.0f, 1.0f);
    wm_parse_color(NULL, "", config->border_color_inactive, 0.5f, 0.5f, 0.5f, 0.5f);
    config->blur = true;
    config->blur_size = 6;
    config->blur_passes = 3;
    config->blur_layers = true;
    config->shadow = true;
    config->shadow_range = 18;
    wm_parse_color(NULL, "", config->shadow_color, 0.0f, 0.0f, 0.0f, 0.45f);
    config->active_opacity = 1.0f;
    config->inactive_opacity = 0.94f;
}

static void wm_config_int(toml_table_t *table, const char *key, int *out) {
    toml_datum_t value = toml_int_in(table, key);
    if (value.ok) *out = (int)value.u.i;
}

static void wm_config_autostart(struct wm_config *config, toml_table_t *conf) {
    toml_array_t *arr = toml_array_in(conf, "autostart");
    if (arr == NULL) {
        toml_table_t *tbl = toml_table_in(conf, "autostart");
        if (tbl != NULL) {
            arr = toml_array_in(tbl, "exec");
            if (arr == NULL) arr = toml_array_in(tbl, "commands");
        }
    }
    if (arr == NULL) return;
    config->autostart_count = 0;
    int n = toml_array_nelem(arr);
    for (int i = 0; i < n && config->autostart_count < WM_MAX_AUTOSTART; i++) {
        toml_datum_t v = toml_string_at(arr, i);
        if (v.ok == 0) continue;
        char *dst = config->autostart[config->autostart_count];
        snprintf(dst, sizeof(config->autostart[0]), "%s", v.u.s);
        free(v.u.s);
        if (dst[0] != '\0') config->autostart_count++;
    }
}

static void wm_config_window_rules(struct wm_config *config, toml_table_t *conf) {
    toml_array_t *arr = toml_array_in(conf, "windowrule");
    if (arr == NULL) return;
    config->rule_count = 0;
    int n = toml_array_nelem(arr);
    for (int i = 0; i < n && config->rule_count < WM_MAX_WINDOW_RULES; i++) {
        toml_table_t *rt = toml_table_at(arr, i);
        if (rt == NULL) continue;
        struct wm_window_rule *r = &config->rules[config->rule_count];
        memset(r, 0, sizeof(*r));
        r->floating = -1; r->workspace = -1; r->center = -1;
        r->blur = -1; r->pin = -1; r->fullscreen = -1; r->no_anim = -1;
        r->opacity_active = -1.0f; r->opacity_inactive = -1.0f;
        toml_datum_t aid = toml_string_in(rt, "app_id");
        if (aid.ok) { snprintf(r->app_id, sizeof(r->app_id), "%s", aid.u.s); free(aid.u.s); }
        toml_datum_t ttl = toml_string_in(rt, "title");
        if (ttl.ok) { snprintf(r->title, sizeof(r->title), "%s", ttl.u.s); free(ttl.u.s); }
        toml_datum_t fl = toml_bool_in(rt, "float");
        if (fl.ok) r->floating = fl.u.b ? 1 : 0;
        toml_datum_t ws = toml_int_in(rt, "workspace");
        if (ws.ok && ws.u.i >= 1 && ws.u.i <= 16) r->workspace = (int)ws.u.i - 1;
        toml_datum_t cn = toml_bool_in(rt, "center");
        if (cn.ok) r->center = cn.u.b ? 1 : 0;
        toml_datum_t bl = toml_bool_in(rt, "blur");
        if (bl.ok) r->blur = bl.u.b ? 1 : 0;
        toml_datum_t pn = toml_bool_in(rt, "pin");
        if (pn.ok) r->pin = pn.u.b ? 1 : 0;
        toml_datum_t fs = toml_bool_in(rt, "fullscreen");
        if (fs.ok) r->fullscreen = fs.u.b ? 1 : 0;
        toml_datum_t na = toml_bool_in(rt, "no_anim");
        if (na.ok) r->no_anim = na.u.b ? 1 : 0;
        toml_array_t *sz = toml_array_in(rt, "size");
        if (sz && toml_array_nelem(sz) == 2) {
            toml_datum_t sw = toml_int_at(sz, 0), sh = toml_int_at(sz, 1);
            if (sw.ok) r->width = (int)sw.u.i;
            if (sh.ok) r->height = (int)sh.u.i;
        }
        toml_datum_t op1 = toml_double_in(rt, "opacity");
        if (op1.ok) { r->opacity_active = (float)op1.u.d; r->opacity_inactive = (float)op1.u.d; }
        toml_array_t *opa = toml_array_in(rt, "opacity");
        if (opa && toml_array_nelem(opa) >= 1) {
            toml_datum_t v0 = toml_double_at(opa, 0);
            if (v0.ok) { r->opacity_active = (float)v0.u.d; r->opacity_inactive = (float)v0.u.d; }
            if (toml_array_nelem(opa) >= 2) {
                toml_datum_t v1 = toml_double_at(opa, 1);
                if (v1.ok) r->opacity_inactive = (float)v1.u.d;
            }
        }
        toml_array_t *bc = toml_array_in(rt, "border_color");
        if (bc && toml_array_nelem(bc) == 4) {
            r->has_border_color = true;
            wm_parse_color(rt, "border_color", r->border_color, 0.2f, 0.6f, 1.0f, 1.0f);
        }
        if (r->app_id[0] != 0 || r->title[0] != 0) config->rule_count++;
    }
}

void wm_config_load(struct wm_config *config, toml_table_t *conf) {
    if (conf == NULL) return;
    toml_table_t *gen = toml_table_in(conf, "general");
    toml_table_t *grd = toml_table_in(conf, "grid");
    toml_table_t *dec = toml_table_in(conf, "decoration");
    toml_table_t *sld = toml_table_in(conf, "slider");
    toml_table_t *t_gen = gen ? gen : conf;
    toml_table_t *t_grd = grd ? grd : conf;
    toml_table_t *t_dec = dec ? dec : conf;

    int gap_between = 20;
    wm_config_int(t_grd, "gap", &config->gap);
    wm_config_int(t_grd, "gap_between", &gap_between);
    config->gap_x = gap_between;
    config->gap_y = gap_between;
    wm_config_int(t_grd, "gap_x", &config->gap_x);
    wm_config_int(t_grd, "gap_y", &config->gap_y);
    wm_config_int(t_grd, "max_windows", &config->max_windows);
    wm_config_int(t_grd, "preset", &config->grid_preset);
    wm_config_int(t_grd, "grid_preset", &config->grid_preset);
    wm_config_int(t_grd, "vertical", &config->grid_vertical);
    wm_config_int(t_grd, "grid_vertical", &config->grid_vertical);
    wm_config_int(t_grd, "lock", &config->grid_lock);
    wm_config_int(t_grd, "grid_lock", &config->grid_lock);
    toml_datum_t gl = toml_bool_in(t_grd, "lock");
    if (!gl.ok) gl = toml_bool_in(t_grd, "grid_lock");
    if (gl.ok) config->grid_lock = gl.u.b ? 1 : 0;
    wm_config_int(t_grd, "hud_radius", &config->hud_radius);
    toml_datum_t hm = toml_string_in(t_grd, "hud_mode");
    if (hm.ok) { snprintf(config->hud_mode, sizeof(config->hud_mode), "%s", hm.u.s); free(hm.u.s); }

    wm_config_int(t_dec, "corner_radius", &config->corner_radius);
    wm_config_int(t_dec, "border_width", &config->border_width);
    wm_config_int(t_dec, "anim_duration", &config->anim_duration);
    toml_datum_t th = toml_string_in(t_dec, "theme");
    if (!th.ok) th = toml_string_in(t_gen, "theme");
    if (th.ok) { snprintf(config->theme, sizeof(config->theme), "%s", th.u.s); free(th.u.s); }
    toml_datum_t b_en = toml_bool_in(t_dec, "blur");
    if (b_en.ok) config->blur = b_en.u.b;
    wm_config_int(t_dec, "blur_size", &config->blur_size);
    wm_config_int(t_dec, "blur_passes", &config->blur_passes);
    toml_datum_t bl_en = toml_bool_in(t_dec, "blur_layers");
    if (bl_en.ok) config->blur_layers = bl_en.u.b;
    toml_datum_t sh_en = toml_bool_in(t_dec, "shadow");
    if (sh_en.ok) config->shadow = sh_en.u.b;
    wm_config_int(t_dec, "shadow_range", &config->shadow_range);
    toml_datum_t ao = toml_double_in(t_dec, "active_opacity");
    if (ao.ok) config->active_opacity = (float)ao.u.d;
    toml_datum_t io = toml_double_in(t_dec, "inactive_opacity");
    if (io.ok) config->inactive_opacity = (float)io.u.d;
    toml_table_t *colors = dec ? toml_table_in(dec, "colors") : NULL;
    if (!colors) colors = toml_table_in(conf, "colors");
    if (colors) {
        wm_parse_color(colors, "bg", config->bg_color, 0.95f, 0.95f, 0.98f, 1.0f);
        wm_parse_color(colors, "border_active", config->border_color_active, 0.2f, 0.6f, 1.0f, 1.0f);
        wm_parse_color(colors, "border_inactive", config->border_color_inactive, 0.5f, 0.5f, 0.5f, 0.5f);
    }

    toml_datum_t ws = toml_int_in(t_gen, "workspaces");
    if (ws.ok && ws.u.i >= 1 && ws.u.i <= 16) config->workspaces = (int)ws.u.i;
    wm_config_int(t_gen, "refresh_rate", &config->refresh_rate);
    if (config->refresh_rate == 0 && t_gen != conf) wm_config_int(conf, "refresh_rate", &config->refresh_rate);
    toml_datum_t sl = sld ? toml_bool_in(sld, "enabled") : toml_bool_in(conf, "slider");
    if (sl.ok) config->slider_enabled = sl.u.b ? true : false;
    toml_datum_t layout = toml_string_in(t_gen, "layout");
    if (layout.ok) { snprintf(config->layout_mode, sizeof(config->layout_mode), "%s", layout.u.s); free(layout.u.s); }
    char *lm = config->layout_mode;
    while (*lm == 32 || *lm == 9 || *lm == 13) lm++;
    int lm_len = (int)strlen(lm);
    while (lm_len > 0 && (lm[lm_len - 1] == 32 || lm[lm_len - 1] == 9 || lm[lm_len - 1] == 13)) lm[--lm_len] = 0;
    if (lm != config->layout_mode) memmove(config->layout_mode, lm, (size_t)lm_len + 1);
    toml_datum_t xl = toml_string_in(t_gen, "xkb_layout");
    if (xl.ok) { snprintf(config->xkb_layout, sizeof(config->xkb_layout), "%s", xl.u.s); free(xl.u.s); }
    toml_datum_t xo = toml_string_in(t_gen, "xkb_options");
    if (xo.ok) { snprintf(config->xkb_options, sizeof(config->xkb_options), "%s", xo.u.s); free(xo.u.s); }

    wm_config_autostart(config, t_gen);
    if (t_gen != conf && config->autostart_count == 0) wm_config_autostart(config, conf);
    wm_config_window_rules(config, conf);
}

static void wm_register_default_hotkeys(struct wm_server *server);
static bool wm_has_action_bound(const struct wm_server *server, const char *action) {
    const struct wm_hotkey *hk;
    wl_list_for_each(hk, &server->hotkeys, link) {
        if (strcmp(hk->action, action) == 0) return true;
    }
    return false;
}

static void wm_add_hotkey_raw(struct wm_server *server, uint32_t mods, xkb_keysym_t sym, const char *action) {
    struct wm_hotkey *hk = calloc(1, sizeof(*hk));
    if (hk == NULL) return;
    wl_list_init(&hk->link);
    hk->modifiers = mods;
    hk->keysym = sym;
    snprintf(hk->action, sizeof(hk->action), "%s", action);
    wl_list_insert(&server->hotkeys, &hk->link);
}

static void wm_register_default_hotkeys(struct wm_server *server) {
    if (wm_has_action_bound(server, "focus_left") == false) wm_add_hotkey_raw(server, WLR_MODIFIER_LOGO, XKB_KEY_Left, "focus_left");
    if (wm_has_action_bound(server, "focus_right") == false) wm_add_hotkey_raw(server, WLR_MODIFIER_LOGO, XKB_KEY_Right, "focus_right");
    if (wm_has_action_bound(server, "focus_up") == false) wm_add_hotkey_raw(server, WLR_MODIFIER_LOGO, XKB_KEY_Up, "focus_up");
    if (wm_has_action_bound(server, "focus_down") == false) wm_add_hotkey_raw(server, WLR_MODIFIER_LOGO, XKB_KEY_Down, "focus_down");
    if (wm_has_action_bound(server, "grid_unlimited") == false) {
        wm_add_hotkey_raw(server, WLR_MODIFIER_LOGO, XKB_KEY_u, "grid_unlimited");
        wm_add_hotkey_raw(server, WLR_MODIFIER_LOGO, XKB_KEY_U, "grid_unlimited");
        wm_add_hotkey_raw(server, WLR_MODIFIER_LOGO, XKB_KEY_Cyrillic_ghe, "grid_unlimited");
        wm_add_hotkey_raw(server, WLR_MODIFIER_LOGO, XKB_KEY_Cyrillic_GHE, "grid_unlimited");
    }
    if (wm_has_action_bound(server, "grid_flip") == false) {
        wm_add_hotkey_raw(server, WLR_MODIFIER_LOGO, XKB_KEY_r, "grid_flip");
        wm_add_hotkey_raw(server, WLR_MODIFIER_LOGO, XKB_KEY_R, "grid_flip");
        wm_add_hotkey_raw(server, WLR_MODIFIER_LOGO, XKB_KEY_Cyrillic_ka, "grid_flip");
        wm_add_hotkey_raw(server, WLR_MODIFIER_LOGO, XKB_KEY_Cyrillic_KA, "grid_flip");
    }
    if (wm_has_action_bound(server, "stack_next") == false) {
        wm_add_hotkey_raw(server, WLR_MODIFIER_LOGO, XKB_KEY_bracketright, "stack_next");
        wm_add_hotkey_raw(server, WLR_MODIFIER_LOGO, XKB_KEY_braceright, "stack_next");
        wm_add_hotkey_raw(server, WLR_MODIFIER_LOGO, XKB_KEY_Cyrillic_hardsign, "stack_next");
        wm_add_hotkey_raw(server, WLR_MODIFIER_LOGO, XKB_KEY_Cyrillic_HARDSIGN, "stack_next");
    }
    if (wm_has_action_bound(server, "stack_prev") == false) {
        wm_add_hotkey_raw(server, WLR_MODIFIER_LOGO, XKB_KEY_bracketleft, "stack_prev");
        wm_add_hotkey_raw(server, WLR_MODIFIER_LOGO, XKB_KEY_braceleft, "stack_prev");
        wm_add_hotkey_raw(server, WLR_MODIFIER_LOGO, XKB_KEY_Cyrillic_ha, "stack_prev");
        wm_add_hotkey_raw(server, WLR_MODIFIER_LOGO, XKB_KEY_Cyrillic_HA, "stack_prev");
    }
    if (wm_has_action_bound(server, "ws_floating") == false) {
        wm_add_hotkey_raw(server, WLR_MODIFIER_LOGO, XKB_KEY_f, "ws_floating");
        wm_add_hotkey_raw(server, WLR_MODIFIER_LOGO, XKB_KEY_F, "ws_floating");
        wm_add_hotkey_raw(server, WLR_MODIFIER_LOGO, XKB_KEY_Cyrillic_a, "ws_floating");
        wm_add_hotkey_raw(server, WLR_MODIFIER_LOGO, XKB_KEY_Cyrillic_A, "ws_floating");
    }
    if (wm_has_action_bound(server, "win_floating") == false) {
        wm_add_hotkey_raw(server, WLR_MODIFIER_LOGO, XKB_KEY_v, "win_floating");
        wm_add_hotkey_raw(server, WLR_MODIFIER_LOGO, XKB_KEY_V, "win_floating");
        wm_add_hotkey_raw(server, WLR_MODIFIER_LOGO, XKB_KEY_Cyrillic_em, "win_floating");
        wm_add_hotkey_raw(server, WLR_MODIFIER_LOGO, XKB_KEY_Cyrillic_EM, "win_floating");
    }
    if (wm_has_action_bound(server, "win_center") == false) {
        wm_add_hotkey_raw(server, WLR_MODIFIER_LOGO, XKB_KEY_c, "win_center");
        wm_add_hotkey_raw(server, WLR_MODIFIER_LOGO, XKB_KEY_C, "win_center");
        wm_add_hotkey_raw(server, WLR_MODIFIER_LOGO, XKB_KEY_Cyrillic_es, "win_center");
        wm_add_hotkey_raw(server, WLR_MODIFIER_LOGO, XKB_KEY_Cyrillic_ES, "win_center");
    }
    if (wm_has_action_bound(server, "swap_left") == false) wm_add_hotkey_raw(server, WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT, XKB_KEY_Left, "swap_left");
    if (wm_has_action_bound(server, "swap_right") == false) wm_add_hotkey_raw(server, WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT, XKB_KEY_Right, "swap_right");
    if (wm_has_action_bound(server, "swap_up") == false) wm_add_hotkey_raw(server, WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT, XKB_KEY_Up, "swap_up");
    if (wm_has_action_bound(server, "swap_down") == false) wm_add_hotkey_raw(server, WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT, XKB_KEY_Down, "swap_down");
    for (int i = 0; i < 9; i++) {
        char act_ws[16];
        char act_mv[24];
        snprintf(act_ws, sizeof(act_ws), "ws_%d", i + 1);
        snprintf(act_mv, sizeof(act_mv), "move_ws_%d", i + 1);
        if (wm_has_action_bound(server, act_ws) == false) wm_add_hotkey_raw(server, WLR_MODIFIER_ALT, XKB_KEY_1 + i, act_ws);
        if (wm_has_action_bound(server, act_mv) == false) wm_add_hotkey_raw(server, WLR_MODIFIER_ALT | WLR_MODIFIER_SHIFT, XKB_KEY_1 + i, act_mv);
    }
}

void wm_load_hotkeys(struct wm_server *server, toml_table_t *conf) {
    if (conf == NULL) { wm_register_default_hotkeys(server); return; }
toml_table_t *hotkeys_table = toml_table_in(conf, "hotkeys");
    if (hotkeys_table != NULL) {
        int i = 0;
        const char *key;
        while ((key = toml_key_in(hotkeys_table, i++)) != NULL) {
            toml_datum_t val = toml_string_in(hotkeys_table, key);
            if (val.ok) {
                struct wm_hotkey *hk = calloc(1, sizeof(*hk));
                if (hk == NULL) { free(val.u.s); continue; }
                wl_list_init(&hk->link);
                char buf[256];
                snprintf(buf, sizeof(buf), "%s", key);
                hk->modifiers = 0;
                hk->keysym = XKB_KEY_NoSymbol;

                char *saveptr = NULL;
                char *token = strtok_r(buf, "+", &saveptr);
                char *last = NULL;

                while (token != NULL) {
                    while (*token == ' ') token++;
                    char *end = token + strlen(token) - 1;
                    while (end > token && *end == ' ') { *end = '\0'; end--; }

                    last = token;
                    token = strtok_r(NULL, "+", &saveptr);
                    if (token != NULL) {
                        if (strcasecmp(last, "Super") == 0 || strcasecmp(last, "Logo") == 0) hk->modifiers |= WLR_MODIFIER_LOGO;
                        else if (strcasecmp(last, "Alt") == 0) hk->modifiers |= WLR_MODIFIER_ALT;
                        else if (strcasecmp(last, "Ctrl") == 0 || strcasecmp(last, "Control") == 0) hk->modifiers |= WLR_MODIFIER_CTRL;
                        else if (strcasecmp(last, "Shift") == 0) hk->modifiers |= WLR_MODIFIER_SHIFT;
                    }
                }
                if (last != NULL) {
                    hk->keysym = xkb_keysym_from_name(last, XKB_KEYSYM_CASE_INSENSITIVE);
                }
                if (hk->keysym == XKB_KEY_NoSymbol) {
                    wlr_log(WLR_ERROR, "HOTKEY: invalid keysym in '%s'", key);
                    free(val.u.s);
                    free(hk);
                    continue;
                }
                snprintf(hk->action, sizeof(hk->action), "%s", val.u.s);
                wl_list_insert(&server->hotkeys, &hk->link);
                xkb_keysym_t lo = xkb_keysym_to_lower(hk->keysym), ru_sym = XKB_KEY_NoSymbol;
                if (lo == XKB_KEY_q) ru_sym = XKB_KEY_Cyrillic_shorti;
                else if (lo == XKB_KEY_w) ru_sym = XKB_KEY_Cyrillic_tse;
                else if (lo == XKB_KEY_e) ru_sym = XKB_KEY_Cyrillic_u;
                else if (lo == XKB_KEY_r) ru_sym = XKB_KEY_Cyrillic_ka;
                else if (lo == XKB_KEY_t) ru_sym = XKB_KEY_Cyrillic_ie;
                else if (lo == XKB_KEY_y) ru_sym = XKB_KEY_Cyrillic_en;
                else if (lo == XKB_KEY_u) ru_sym = XKB_KEY_Cyrillic_ghe;
                else if (lo == XKB_KEY_i) ru_sym = XKB_KEY_Cyrillic_sha;
                else if (lo == XKB_KEY_o) ru_sym = XKB_KEY_Cyrillic_shcha;
                else if (lo == XKB_KEY_p) ru_sym = XKB_KEY_Cyrillic_ze;
                else if (lo == XKB_KEY_a) ru_sym = XKB_KEY_Cyrillic_ef;
                else if (lo == XKB_KEY_s) ru_sym = XKB_KEY_Cyrillic_yeru;
                else if (lo == XKB_KEY_d) ru_sym = XKB_KEY_Cyrillic_ve;
                else if (lo == XKB_KEY_f) ru_sym = XKB_KEY_Cyrillic_a;
                else if (lo == XKB_KEY_g) ru_sym = XKB_KEY_Cyrillic_pe;
                else if (lo == XKB_KEY_h) ru_sym = XKB_KEY_Cyrillic_er;
                else if (lo == XKB_KEY_j) ru_sym = XKB_KEY_Cyrillic_o;
                else if (lo == XKB_KEY_k) ru_sym = XKB_KEY_Cyrillic_el;
                else if (lo == XKB_KEY_l) ru_sym = XKB_KEY_Cyrillic_de;
                else if (lo == XKB_KEY_z) ru_sym = XKB_KEY_Cyrillic_ya;
                else if (lo == XKB_KEY_x) ru_sym = XKB_KEY_Cyrillic_che;
                else if (lo == XKB_KEY_c) ru_sym = XKB_KEY_Cyrillic_es;
                else if (lo == XKB_KEY_v) ru_sym = XKB_KEY_Cyrillic_em;
                else if (lo == XKB_KEY_b) ru_sym = XKB_KEY_Cyrillic_i;
                else if (lo == XKB_KEY_n) ru_sym = XKB_KEY_Cyrillic_te;
                else if (lo == XKB_KEY_m) ru_sym = XKB_KEY_Cyrillic_softsign;
                else if (lo == XKB_KEY_bracketleft) ru_sym = XKB_KEY_Cyrillic_ha;
                else if (lo == XKB_KEY_bracketright) ru_sym = XKB_KEY_Cyrillic_hardsign;
                else if (lo == XKB_KEY_Print) ru_sym = XKB_KEY_Sys_Req;
                if (ru_sym != XKB_KEY_NoSymbol) {
                    struct wm_hotkey *rhk = calloc(1, sizeof(*rhk));
                    if (rhk != NULL) {
                        wl_list_init(&rhk->link);
                        rhk->modifiers = hk->modifiers;
                        rhk->keysym = ru_sym;
                        snprintf(rhk->action, sizeof(rhk->action), "%s", hk->action);
                        wl_list_insert(&server->hotkeys, &rhk->link);
                    }
                }
                free(val.u.s);
                wlr_log(WLR_INFO, "HOTKEY: Bound '%s' -> '%s'", key, hk->action);
            }
        }
    }
    wm_register_default_hotkeys(server);
}
