
cat << 'EOF' > setup_igloo.py
import os
import subprocess

os.makedirs("dizi-root", exist_ok=True)
print("Created dizi-root directory.")

if os.path.exists("dizi-root/igloo.c"):
    res = subprocess.run(["clang", "-O2", "-Wall", "dizi-root/igloo.c", "-o", "dizi-root/igloo"])
    if res.returncode == 0:
        subprocess.run(["strip", "dizi-root/igloo"])
        subprocess.run(["cp", "dizi-root/igloo", "dizi-root/su"])
        print("Built dizi-root/igloo and dizi-root/su cleanly!")
EOF
python3 setup_igloo.py

mkdir -p dizi-root
cat << 'EOF' > dizi-root/igloo.c
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pty.h>
#include <termios.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/ioctl.h>

#define SOCK_NAME "\0igloo_ipc"
#define OP_SHELL  0x01
#define OP_CMD    0x02
#define OP_PING   0x03
#define OP_MELT   0x04

static volatile sig_atomic_t daemon_running = 1;

static void handle_sigterm(int sig) {
    (void)sig;
    daemon_running = 0;
}

static uid_t resolve_target_uid(const char *arg) {
    if (!arg || !*arg) return (uid_t)-1;
    if (*arg >= '0' && *arg <= '9') {
        return (uid_t)atoi(arg);
    }
    char *pkg_path = calloc(1, 256);
    if (!pkg_path) return (uid_t)-1;

    struct stat st;
    snprintf(pkg_path, 256, "/data/data/%s", arg);
    if (stat(pkg_path, &st) == 0) {
        free(pkg_path);
        return st.st_uid;
    }
    snprintf(pkg_path, 256, "/data/user/0/%s", arg);
    if (stat(pkg_path, &st) == 0) {
        free(pkg_path);
        return st.st_uid;
    }
    free(pkg_path);
    return (uid_t)-1;
}

static int connect_igloo(void) {
    int sfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sfd < 0) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, SOCK_NAME, sizeof(SOCK_NAME) - 1);

    socklen_t addrlen = sizeof(sa_family_t) + sizeof(SOCK_NAME) - 1;
    if (connect(sfd, (struct sockaddr *)&addr, addrlen) < 0) {
        close(sfd);
        return -1;
    }
    return sfd;
}

static int run_daemon(uid_t extra_uid) {
    if (fork() > 0) _exit(0);
    setsid();

    signal(SIGCHLD, SIG_IGN);
    signal(SIGTERM, handle_sigterm);

    int sfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sfd < 0) return 1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, SOCK_NAME, sizeof(SOCK_NAME) - 1);

    socklen_t addrlen = sizeof(sa_family_t) + sizeof(SOCK_NAME) - 1;
    if (bind(sfd, (struct sockaddr *)&addr, addrlen) < 0) {
        close(sfd);
        return 1;
    }
    listen(sfd, 8);

    while (daemon_running) {
        int cfd = accept(sfd, NULL, NULL);
        if (cfd < 0) continue;

        struct ucred cred;
        socklen_t len = sizeof(cred);
        if (getsockopt(cfd, SOL_SOCKET, SO_PEERCRED, &cred, &len) < 0) {
            close(cfd);
            continue;
        }

        int allowed = (cred.uid == 0 || cred.uid == 2000);
        if (!allowed && extra_uid != (uid_t)-1 && cred.uid == extra_uid) {
            allowed = 1;
        }
        if (!allowed) {
            close(cfd);
            continue;
        }

        unsigned char op = 0;
        if (read(cfd, &op, 1) != 1) {
            close(cfd);
            continue;
        }

        if (op == OP_PING) {
            if (write(cfd, "OK", 2) < 0) {}
            close(cfd);
            continue;
        }

        if (op == OP_MELT) {
            if (write(cfd, "MELTED", 6) < 0) {}
            close(cfd);
            break;
        }

        char *cmd_buf = calloc(1, 4096);
        if (!cmd_buf) {
            close(cfd);
            continue;
        }
        if (op == OP_CMD) {
            uint16_t clen = 0;
            if (read(cfd, &clen, sizeof(clen)) != sizeof(clen) || clen >= 4096) {
                free(cmd_buf);
                close(cfd);
                continue;
            }
            if (read(cfd, cmd_buf, clen) != clen) {
                free(cmd_buf);
                close(cfd);
                continue;
            }
        }

        int mfd, sfd_pty;
        if (openpty(&mfd, &sfd_pty, NULL, NULL, NULL) < 0) {
            free(cmd_buf);
            close(cfd);
            continue;
        }

        pid_t pid = fork();
        if (pid == 0) {
            close(sfd);
            close(cfd);
            close(mfd);

            setsid();
            ioctl(sfd_pty, TIOCSCTTY, 0);
            dup2(sfd_pty, 0);
            dup2(sfd_pty, 1);
            dup2(sfd_pty, 2);
            if (sfd_pty > 2) close(sfd_pty);

            setresgid(0, 0, 0);
            setresuid(0, 0, 0);
            setenv("PATH", "/data/local/tmp:/sbin:/system/bin:/vendor/bin", 1);
            setenv("HOME", "/data/local/tmp", 1);
            setenv("TERM", "xterm-256color", 1);

            if (op == OP_CMD) {
                execl("/system/bin/sh", "sh", "-c", cmd_buf, NULL);
            } else {
                execl("/system/bin/sh", "sh", "-", NULL);
            }
            _exit(127);
        }

        free(cmd_buf);
        close(sfd_pty);
        fd_set fds;
        char *io_buf = malloc(4096);
        while (io_buf) {
            FD_ZERO(&fds);
            FD_SET(cfd, &fds);
            FD_SET(mfd, &fds);
            int maxfd = (cfd > mfd ? cfd : mfd) + 1;
            if (select(maxfd, &fds, NULL, NULL, NULL) <= 0) break;

            if (FD_ISSET(cfd, &fds)) {
                ssize_t n = read(cfd, io_buf, 4096);
                if (n <= 0) break;
                if (write(mfd, io_buf, n) <= 0) break;
            }
            if (FD_ISSET(mfd, &fds)) {
                ssize_t n = read(mfd, io_buf, 4096);
                if (n <= 0) break;
                if (write(cfd, io_buf, n) <= 0) break;
            }
        }
        free(io_buf);
        close(mfd);
        close(cfd);
    }

    close(sfd);
    return 0;
}

static int run_client(int argc, char **argv) {
    if (argc > 1 && strcmp(*(argv + 1), "--status") == 0) {
        int sfd = connect_igloo();
        if (sfd < 0) {
            printf("\033[1;34m[igloo]\033[0m Status: \033[1;30mMELTED (inactive)\033[0m\n");
            return 1;
        }
        unsigned char op = OP_PING;
        char reply = 0;
        if (write(sfd, &op, 1) < 0) {}
        if (read(sfd, &reply, 1) < 0) {}
        close(sfd);
        printf("\033[1;36m[igloo]\033[0m Status: \033[1;32mFROZEN (active in RAM, hookless)\033[0m\n");
        return 0;
    }

    if (argc > 1 && strcmp(*(argv + 1), "--melt") == 0) {
        int sfd = connect_igloo();
        if (sfd < 0) {
            fprintf(stderr, "igloo: already melted\n");
            return 1;
        }
        unsigned char op = OP_MELT;
        char reply = 0;
        if (write(sfd, &op, 1) < 0) {}
        if (read(sfd, &reply, 1) < 0) {}
        close(sfd);
        printf("\033[1;36m[igloo]\033[0m Daemon melted cleanly.\n");
        return 0;
    }

    int sfd = connect_igloo();
    if (sfd < 0) {
        fprintf(stderr, "igloo: daemon cold\n");
        return 1;
    }

    if (argc > 2 && strcmp(*(argv + 1), "-c") == 0) {
        unsigned char op = OP_CMD;
        uint16_t clen = (uint16_t)strlen(*(argv + 2));
        if (write(sfd, &op, 1) < 0) {}
        if (write(sfd, &clen, sizeof(clen)) < 0) {}
        if (write(sfd, *(argv + 2), clen) < 0) {}
    } else {
        unsigned char op = OP_SHELL;
        if (write(sfd, &op, 1) < 0) {}
    }

    struct termios orig_tio, raw_tio;
    int is_tty = isatty(0);
    if (is_tty) {
        tcgetattr(0, &orig_tio);
        raw_tio = orig_tio;
        cfmakeraw(&raw_tio);
        tcsetattr(0, TCSANOW, &raw_tio);
    }

    fd_set fds;
    char *buf = malloc(4096);
    while (buf) {
        FD_ZERO(&fds);
        FD_SET(0, &fds);
        FD_SET(sfd, &fds);
        if (select(sfd + 1, &fds, NULL, NULL, NULL) <= 0) break;

        if (FD_ISSET(0, &fds)) {
            ssize_t n = read(0, buf, 4096);
            if (n <= 0) break;
            if (write(sfd, buf, n) <= 0) break;
        }
        if (FD_ISSET(sfd, &fds)) {
            ssize_t n = read(sfd, buf, 4096);
            if (n <= 0) break;
            if (write(1, buf, n) <= 0) break;
        }
    }
    free(buf);

    if (is_tty) tcsetattr(0, TCSANOW, &orig_tio);
    close(sfd);
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(*(argv + 1), "--daemon") == 0) {
        uid_t extra_uid = (argc > 2) ? resolve_target_uid(*(argv + 2)) : (uid_t)-1;
        return run_daemon(extra_uid);
    }
    return run_client(argc, argv);
}
EOF

clang -O2 -Wall dizi-root/igloo.c -o dizi-root/igloo && strip dizi-root/igloo && cp dizi-root/igloo dizi-root/su && ls -lh dizi-root/igloo
