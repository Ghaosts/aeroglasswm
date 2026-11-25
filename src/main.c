#define _POSIX_C_SOURCE 200809L
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wlr/util/log.h>
#include "modules/wm.h"
#ifdef AEROGLASS_WITH_SLIDER
#include "modules/slider.h"
#endif
#include "toml.h"

static void default_config_path(char *buf, size_t cap) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    const char *base = (xdg && xdg[0] != '\0') ? xdg : home;
    if (base == NULL) base = ".";
    snprintf(buf, cap, "%s/aeroglasswm/config.toml", base);
    if (access(buf, R_OK) == 0) return;

    char legacy[1024];
    snprintf(legacy, sizeof(legacy), "%s/aeroglass/config.toml", base);
    if (access(legacy, R_OK) == 0) {
        snprintf(buf, cap, "%s", legacy);
    }
}

static toml_table_t *open_config(const char *path) {
    FILE *fp = fopen(path, "r");
    if (fp == NULL) return NULL;
    char errbuf[256];
    toml_table_t *conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
    fclose(fp);
    if (conf == NULL) {
        wlr_log(WLR_ERROR, "failed to parse %s: %s", path, errbuf);
    }
    return conf;
}

static void print_usage(const char *prog) {
    printf("Usage: %s [-c config.toml] [-s startup_cmd] [-n] [-d]\n", prog);
    printf("  -c  path to config.toml\n");
    printf("  -s  command to spawn on startup\n");
    printf("  -n  run without the slider module\n");
    printf("  -d  enable debug logging\n");
}

int main(int argc, char *argv[]) {
    char cfg_path[512];
    default_config_path(cfg_path, sizeof(cfg_path));
    const char *startup_cmd = NULL;
    bool force_no_slider = false;
    enum wlr_log_importance log_level = WLR_INFO;

    int opt;
    while ((opt = getopt(argc, argv, "c:s:ndh")) != -1) {
        switch (opt) {
        case 'c': snprintf(cfg_path, sizeof(cfg_path), "%s", optarg); break;
        case 's': startup_cmd = optarg; break;
        case 'n': force_no_slider = true; break;
        case 'd': log_level = WLR_DEBUG; break;
        default: print_usage(argv[0]); return 0;
        }
    }

    wlr_log_init(log_level, NULL);

    toml_table_t *conf = open_config(cfg_path);
    struct wm_config wm_cfg;
    wm_config_defaults(&wm_cfg);
    wm_config_load(&wm_cfg, conf);
    bool use_slider = wm_cfg.slider_enabled && !force_no_slider;

    struct wm_server *server = wm_create(&wm_cfg);
    if (server == NULL) {
        if (conf) toml_free(conf);
        return 1;
    }

#ifdef AEROGLASS_WITH_SLIDER
    struct slider *slider = NULL;
    if (use_slider) {
        struct slider_config slider_cfg;
        slider_config_defaults(&slider_cfg);
        slider_config_load(&slider_cfg, conf);
        slider = slider_attach(server, &slider_cfg);
        if (slider == NULL) wlr_log(WLR_ERROR, "slider module failed to attach, continuing without it");
    }
#else
    if (use_slider) wlr_log(WLR_INFO, "built without slider module");
#endif

    if (conf) toml_free(conf);

    wm_run(server);

#ifdef AEROGLASS_WITH_SLIDER
    slider_detach(slider);
#endif
    wm_destroy(server);
    return 0;
}
