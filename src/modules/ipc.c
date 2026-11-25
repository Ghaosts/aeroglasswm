#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <wlr/util/log.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/xwayland/xwayland.h>
#include "wm_internal.h"

static void wm_ipc_json_escape(const char *src, char *dst, size_t cap) {
    size_t j = 0;
    if (!src) { dst[0] = 0; return; }
    for (size_t k = 0; src[k] && j + 3 < cap; k++) {
        unsigned char c = (unsigned char)src[k];
        if (c == 34 || c == 92) { dst[j++] = 92; dst[j++] = (char)c; }
        else if (c >= 32) dst[j++] = (char)c;
    }
    dst[j] = 0;
}

static int wm_ipc_readable(int fd, uint32_t mask, void *data) {
    (void)mask;
    struct wm_server *srv = data;
    int cfd = accept(fd, NULL, NULL);
    if (cfd < 0) return 0;
    char cmd[512] = {0};
    ssize_t n = read(cfd, cmd, sizeof(cmd) - 1);
    if (n > 0) {
        while (n > 0 && (cmd[n - 1] == 10 || cmd[n - 1] == 13 || cmd[n - 1] == 32)) cmd[--n] = 0;
        char out[8192]; out[0] = 0;
        if (strncmp(cmd, "dispatch ", 9) == 0) {
            wm_execute_action(srv, srv->active_toplevel, cmd + 9);
            snprintf(out, sizeof(out), "{\"ok\":true}\n");
        } else if (strcmp(cmd, "clients") == 0) {
            size_t off = (size_t)snprintf(out, sizeof(out), "[");
            struct wm_toplevel *t; bool first = true;
            wl_list_for_each(t, &srv->toplevels, link) {
                if (!t->in_list) continue;
                const char *aid = t->is_x11 ? (t->xwayland_surface ? t->xwayland_surface->class : "") : (t->xdg_toplevel ? t->xdg_toplevel->app_id : "");
                const char *ttl = t->is_x11 ? (t->xwayland_surface ? t->xwayland_surface->title : "") : (t->xdg_toplevel ? t->xdg_toplevel->title : "");
                char ea[256], et[512];
                wm_ipc_json_escape(aid, ea, sizeof(ea));
                wm_ipc_json_escape(ttl, et, sizeof(et));
                off += (size_t)snprintf(out + off, sizeof(out) - off,
                    "%s{\"app_id\":\"%s\",\"title\":\"%s\",\"ws\":%d,\"floating\":%s,\"pinned\":%s,\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d}",
                    first ? "" : ",", ea, et, t->workspace + 1, t->is_floating ? "true" : "false", t->is_pinned ? "true" : "false", t->x, t->y, t->width, t->height);
                first = false;
                if (off + 512 >= sizeof(out)) break;
            }
            snprintf(out + off, sizeof(out) - off, "]\n");
        } else {
            const char *ttl = "";
            if (srv->active_toplevel) {
                ttl = srv->active_toplevel->is_x11 ? (srv->active_toplevel->xwayland_surface ? srv->active_toplevel->xwayland_surface->title : "")
                                                   : (srv->active_toplevel->xdg_toplevel ? srv->active_toplevel->xdg_toplevel->title : "");
            }
            char et[512]; wm_ipc_json_escape(ttl, et, sizeof(et));
            size_t off = (size_t)snprintf(out, sizeof(out),
                "{\"active_ws\":%d,\"grid\":\"%dx%d\",\"floating_ws\":%s,\"title\":\"%s\",\"workspaces\":[",
                srv->active_workspace + 1, grid_cols(srv), grid_rows(srv),
                srv->workspace_floating[srv->active_workspace] ? "true" : "false", et);
            for (int w_i = 0; w_i < srv->config.workspaces && w_i < 16; w_i++) {
                int cnt = 0; struct wm_toplevel *t;
                wl_list_for_each(t, &srv->toplevels, link) if (t->in_list && t->workspace == w_i) cnt++;
                off += (size_t)snprintf(out + off, sizeof(out) - off,
                    "%s{\"id\":%d,\"windows\":%d,\"penalty_for\":%d}",
                    w_i == 0 ? "" : ",", w_i + 1, cnt, srv->workspace_group_owner[w_i]);
            }
            snprintf(out + off, sizeof(out) - off, "]}\n");
        }
        (void)write(cfd, out, strlen(out));
    }
    close(cfd);
    return 0;
}

void wm_ipc_start(struct wm_server *srv, const char *sock_name) {
    srv->ipc_fd = -1;
    const char *rt = getenv("XDG_RUNTIME_DIR");
    if (!rt) rt = "/tmp";
    snprintf(srv->ipc_path, sizeof(srv->ipc_path), "%s/wm-ipc-%s.sock", rt, sock_name);
    unlink(srv->ipc_path);
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) return;
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", srv->ipc_path);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(fd, 8) < 0) { close(fd); return; }
    srv->ipc_fd = fd;
    setenv("WM_IPC_SOCK", srv->ipc_path, true);
    srv->ipc_source = wl_event_loop_add_fd(wl_display_get_event_loop(srv->wl_display), fd, WL_EVENT_READABLE, wm_ipc_readable, srv);
}
