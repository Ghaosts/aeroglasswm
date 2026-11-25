#ifndef AEROGLASS_SLIDER_H
#define AEROGLASS_SLIDER_H

#include <stdbool.h>
#include "toml.h"
#include "wm.h"

struct slider;

struct slider_config {
    bool overlay_mode;
    int bar_h;
    int slider_w;
    int slider_h;
    int btn_w;
    int btn_h;
    int slider_radius;
    int btn_radius;
    int bar_radius;
    int morph_radius;
    float bar_color[4];
    float slider_color[4];
    float slider_pull_color[4];
    float btn_fs_color[4];
    float btn_close_color[4];
    char action_left[512];
    char action_right[512];
    char swipe_left_action[512];
    char swipe_right_action[512];
    char swipe_up_action[512];
    char image_bar[512];
    char image_slider[512];
    char image_slider_pull[512];
    char image_btn_left[512];
    char image_btn_right[512];
};

void slider_config_defaults(struct slider_config *config);
void slider_config_load(struct slider_config *config, toml_table_t *conf);

struct slider *slider_attach(struct wm_server *server, const struct slider_config *config);
void slider_detach(struct slider *slider);

#endif
