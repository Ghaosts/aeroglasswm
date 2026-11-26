#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static void print_help(void) {
    printf(
        "Usage: wmctl <command> [args]\n\n"
        "Query commands:\n"
        "  status                       Show JSON workspace & grid status (default)\n"
        "  clients                      Show JSON list of all managed windows\n\n"
        "Control commands:\n"
        "  workspace <1..9>             Switch active workspace (alias: ws)\n"
        "  move <1..9>                  Move active window to workspace\n"
        "  focus <left|right|up|down|next|prev>\n"
        "                               Move focus to window\n"
        "  swap <left|right|up|down>    Swap active window with neighbor\n"
        "  rotate                       Flip grid orientation 3x2 <-> 2x3 (alias: flip)\n"
        "  grid <next|prev|unlimited>   Cycle grid preset or toggle unlimited mode\n"
        "  floating                     Toggle floating mode for active window\n"
        "  ws-floating                  Toggle floating mode for active workspace\n"
        "  fullscreen                   Toggle fullscreen for active window\n"
        "  center                       Center active floating window\n"
        "  detach                       Detach active window to 1/4 floating\n"
        "  close                        Close active window\n"
        "  exec <command...>            Spawn command inside compositor session\n"
        "  dispatch <raw_action>        Send raw action to wm_execute_action\n"
        "  -h, --help                   Show this help message\n"
    );
}

static int find_socket(char *buf, size_t cap) {
    const char *env = getenv("WM_IPC_SOCK");
    if (env && env[0]) {
        snprintf(buf, cap, "%s", env);
        return 0;
    }
    const char *rt = getenv("XDG_RUNTIME_DIR");
    if (!rt) rt = "/tmp";
    DIR *d = opendir(rt);
    if (!d) return -1;
    struct dirent *de;
    time_t best_mtime = 0;
    int found = -1;
    while ((de = readdir(d)) != NULL) {
        if (strncmp(de->d_name, "wm-ipc-", 7) == 0 && strstr(de->d_name, ".sock")) {
            char cand[108];
            if (snprintf(cand, sizeof(cand), "%s/%s", rt, de->d_name) >= (int)sizeof(cand)) continue;
            struct stat st;
            if (stat(cand, &st) == 0 && (found < 0 || st.st_mtime >= best_mtime)) {
                best_mtime = st.st_mtime;
                memcpy(buf, cand, strlen(cand) + 1);
                found = 0;
            }
        }
    }
    closedir(d);
    return found;
}

static int build_ipc_message(int argc, char **argv, char *msg, size_t cap) {
    if (argc <= 1 || strcmp(argv[1], "status") == 0) {
        snprintf(msg, cap, "status\n");
        return 0;
    }
    const char *cmd = argv[1];
    if (strcmp(cmd, "clients") == 0) {
        snprintf(msg, cap, "clients\n");
        return 0;
    }
    if (strcmp(cmd, "dispatch") == 0) {
        if (argc < 3) return -1;
        size_t off = (size_t)snprintf(msg, cap, "dispatch");
        for (int i = 2; i < argc && off + 2 < cap; i++)
            off += (size_t)snprintf(msg + off, cap - off, " %s", argv[i]);
        snprintf(msg + off, cap - off, "\n");
        return 0;
    }
    if ((strcmp(cmd, "workspace") == 0 || strcmp(cmd, "ws") == 0) && argc >= 3) {
        int n = atoi(argv[2]);
        if (n < 1 || n > 16) return -1;
        snprintf(msg, cap, "dispatch ws_%d\n", n);
        return 0;
    }
    if (strcmp(cmd, "move") == 0 && argc >= 3) {
        int n = atoi(argv[2]);
        if (n < 1 || n > 16) return -1;
        snprintf(msg, cap, "dispatch move_ws_%d\n", n);
        return 0;
    }
    if (strcmp(cmd, "focus") == 0 && argc >= 3) {
        if (strcmp(argv[2], "left") == 0 || strcmp(argv[2], "right") == 0 ||
            strcmp(argv[2], "up") == 0 || strcmp(argv[2], "down") == 0 ||
            strcmp(argv[2], "next") == 0 || strcmp(argv[2], "prev") == 0) {
            snprintf(msg, cap, "dispatch focus_%s\n", argv[2]);
            return 0;
        }
        return -1;
    }
    if (strcmp(cmd, "swap") == 0 && argc >= 3) {
        if (strcmp(argv[2], "left") == 0 || strcmp(argv[2], "right") == 0 ||
            strcmp(argv[2], "up") == 0 || strcmp(argv[2], "down") == 0) {
            snprintf(msg, cap, "dispatch swap_%s\n", argv[2]);
            return 0;
        }
        return -1;
    }
    if (strcmp(cmd, "rotate") == 0 || strcmp(cmd, "flip") == 0) {
        snprintf(msg, cap, "dispatch grid_flip\n");
        return 0;
    }
    if (strcmp(cmd, "grid") == 0 && argc >= 3) {
        if (strcmp(argv[2], "next") == 0 || strcmp(argv[2], "prev") == 0 || strcmp(argv[2], "unlimited") == 0) {
            snprintf(msg, cap, "dispatch grid_%s\n", argv[2]);
            return 0;
        }
        return -1;
    }
    if (strcmp(cmd, "floating") == 0) {
        snprintf(msg, cap, "dispatch win_floating\n");
        return 0;
    }
    if (strcmp(cmd, "ws-floating") == 0) {
        snprintf(msg, cap, "dispatch ws_floating\n");
        return 0;
    }
    if (strcmp(cmd, "fullscreen") == 0) {
        snprintf(msg, cap, "dispatch fullscreen\n");
        return 0;
    }
    if (strcmp(cmd, "center") == 0) {
        snprintf(msg, cap, "dispatch win_center\n");
        return 0;
    }
    if (strcmp(cmd, "detach") == 0) {
        snprintf(msg, cap, "dispatch detach\n");
        return 0;
    }
    if (strcmp(cmd, "close") == 0) {
        snprintf(msg, cap, "dispatch close\n");
        return 0;
    }
    if (strcmp(cmd, "exec") == 0 && argc >= 3) {
        size_t off = (size_t)snprintf(msg, cap, "dispatch exec:");
        for (int i = 2; i < argc && off + 2 < cap; i++)
            off += (size_t)snprintf(msg + off, cap - off, "%s%s", i == 2 ? "" : " ", argv[i]);
        snprintf(msg + off, cap - off, "\n");
        return 0;
    }
    return -1;
}

int main(int argc, char **argv) {
    if (argc >= 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "help") == 0)) {
        print_help();
        return 0;
    }
    char msg[512] = {0};
    if (build_ipc_message(argc, argv, msg, sizeof(msg)) < 0) {
        fprintf(stderr, "wmctl: invalid command '%s'. Run 'wmctl --help'.\n", argc >= 2 ? argv[1] : "");
        return 2;
    }
    char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    if (find_socket(path, sizeof(path)) < 0) {
        fprintf(stderr, "wmctl: WM_IPC_SOCK not found\n");
        return 1;
    }
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(fd);
        return 1;
    }
    if (write(fd, msg, strlen(msg)) < 0) {
        perror("write");
        close(fd);
        return 1;
    }
    char resp[4096];
    ssize_t n;
    while ((n = read(fd, resp, sizeof(resp))) > 0)
        (void)write(STDOUT_FILENO, resp, (size_t)n);
    close(fd);
    return 0;
}
