#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wlr/util/log.h>
#include "toml.h"
#include "modules/wm.h"
#ifdef AEROGLASS_WITH_SLIDER
#include "modules/slider.h"
#endif

static void default_config_path(char *out, size_t cap) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    char base[512];
    if (xdg && xdg[0]) snprintf(base, sizeof(base), "%s", xdg);
    else if (home && home[0]) snprintf(base, sizeof(base), "%s/.config", home);
    else { snprintf(out, cap, "config.toml"); return; }
    snprintf(out, cap, "%s/aeroglasswm/config.toml", base);
    FILE *f = fopen(out, "r");
    if (f) { fclose(f); return; }
    char legacy[1024];
    snprintf(legacy, sizeof(legacy), "%s/aeroglass/config.toml", base);
    f = fopen(legacy, "r");
    if (f) { fclose(f); snprintf(out, cap, "%s", legacy); }
}

static toml_table_t *read_config(const char *path) {
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        wlr_log(WLR_INFO, "CONFIG: %s not found, using defaults", path);
        return NULL;
    }
    char errbuf[256];
    toml_table_t *conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
    fclose(fp);
    if (conf == NULL) {
        wlr_log(WLR_ERROR, "CONFIG: %s parse error: %s", path, errbuf);
        return NULL;
    }
    wlr_log(WLR_INFO, "CONFIG: loaded %s", path);
    return conf;
}

static void print_usage(const char *program) {
    printf("Usage: %s [-s startup command] [-c config.toml] [-n]\n", program);
    printf("  -s  command started inside the compositor\n");
    printf("  -c  path to config.toml\n");
    printf("  -n  run without the slider module\n");
}

int main(int argc, char *argv[]) {
    wlr_log_init(WLR_INFO, NULL);

    const char *startup_cmd = NULL;
    const char *config_override = NULL;
    bool force_no_slider = false;
    int opt;
    while ((opt = getopt(argc, argv, "s:c:n")) != -1) {
        switch (opt) {
        case 's': startup_cmd = optarg; break;
        case 'c': config_override = optarg; break;
        case 'n': force_no_slider = true; break;
        default: print_usage(argv[0]); return 0;
        }
    }
    if (optind < argc) { print_usage(argv[0]); return 0; }

    char config_path[1024];
    if (config_override) snprintf(config_path, sizeof(config_path), "%s", config_override);
    else default_config_path(config_path, sizeof(config_path));
    toml_table_t *conf = read_config(config_path);

    struct wm_config wm_cfg;
    wm_config_defaults(&wm_cfg);
    wm_config_load(&wm_cfg, conf);
    bool use_slider = wm_cfg.slider_enabled && !force_no_slider;

    struct wm_server *server = wm_create(&wm_cfg);
    if (server == NULL) {
        if (conf) toml_free(conf);
        return 1;
    }
    wm_load_hotkeys(server, conf);

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

    int status = 0;
    if (wm_start(server, startup_cmd)) wm_run(server);
    else status = 1;

#ifdef AEROGLASS_WITH_SLIDER
    slider_detach(slider);
#endif
    wm_destroy(server);
    if (conf) toml_free(conf);
    return status;
}
