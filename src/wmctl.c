#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static int find_socket(char *buf, size_t cap) {
    const char *env = getenv("WM_IPC_SOCK");
    if (env && env[0]) { snprintf(buf, cap, "%s", env); return 0; }
    const char *rt = getenv("XDG_RUNTIME_DIR");
    if (!rt) rt = "/tmp";
    DIR *d = opendir(rt);
    if (!d) return -1;
    struct dirent *de;
    int found = -1;
    while ((de = readdir(d)) != NULL) {
        if (strncmp(de->d_name, "wm-ipc-", 7) == 0 && strstr(de->d_name, ".sock")) {
            snprintf(buf, cap, "%s/%s", rt, de->d_name);
            found = 0;
        }
    }
    closedir(d);
    return found;
}

int main(int argc, char **argv) {
    char path[108];
    if (find_socket(path, sizeof(path)) < 0) { fprintf(stderr, "wmctl: WM_IPC_SOCK not found\n"); return 1; }
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("connect"); close(fd); return 1; }
    char msg[512] = {0};
    if (argc <= 1) snprintf(msg, sizeof(msg), "status\n");
    else {
        size_t off = 0;
        for (int i = 1; i < argc && off + 2 < sizeof(msg); i++)
            off += (size_t)snprintf(msg + off, sizeof(msg) - off, "%s%s", i == 1 ? "" : " ", argv[i]);
        snprintf(msg + off, sizeof(msg) - off, "\n");
    }
    if (write(fd, msg, strlen(msg)) < 0) { perror("write"); close(fd); return 1; }
    char resp[4096];
    ssize_t n;
    while ((n = read(fd, resp, sizeof(resp))) > 0) (void)write(STDOUT_FILENO, resp, (size_t)n);
    close(fd);
    return 0;
}
