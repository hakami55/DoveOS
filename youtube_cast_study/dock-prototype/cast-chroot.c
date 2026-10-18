#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <grp.h>

/* Launch an optional GUI app from a separately mounted read-only root. */
int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: optional_chroot ROOT PROGRAM [ARG...]\n");
        return 2;
    }
    if (chroot(argv[1]) || chdir("/tmp")) {
        perror("chroot/chdir");
        return 3;
    }
    /* The dock's ALSA playback nodes belong to group 29 (audio). */
    gid_t castGroups[] = {29, 3003};
    /* Android's paranoid-network kernel requires AID_INET for app sockets. */
    if (setgroups(2, castGroups) || setgid(29) || setuid(65534)) {
        perror("setgid/setuid");
        return 4;
    }
    setenv("HOME", "/tmp/doveos-optional-home", 1);
    setenv("XDG_RUNTIME_DIR", "/tmp/doveos-optional-runtime", 1);
    setenv("DISPLAY", ":7", 1);
    setenv("LANG", "C.UTF-8", 1);
    setenv("QT_X11_NO_MITSHM", "1", 1);
    setenv("QT_XCB_GL_INTEGRATION", "none", 1);
    /* A diagnostic launcher may block signals; GUI apps need normal delivery. */
    sigset_t unblocked;
    sigemptyset(&unblocked);
    sigprocmask(SIG_SETMASK, &unblocked, NULL);
    signal(SIGCHLD, SIG_DFL);
    execv(argv[2], argv + 2);
    perror("execv");
    return errno ? errno : 5;
}
