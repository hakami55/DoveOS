/* Start Wi-Fi services without HP launcher's signals or device descriptors. */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 2) return 2;
    /* Only this new process is changed; the parent and desktop stay intact. */
    DIR *dir = opendir("/proc/self/fd");
    if (!dir) { perror("open descriptors"); return 3; }
    int own = dirfd(dir);
    struct dirent *ent;
    while ((ent = readdir(dir))) {
        char *end;
        long fd = strtol(ent->d_name, &end, 10);
        if (*ent->d_name && !*end && fd > 2 && fd != own) close((int)fd);
    }
    closedir(dir);
    struct sigaction action = {0};
    action.sa_handler = SIG_DFL;
    sigemptyset(&action.sa_mask);
    for (int sig = 1; sig < NSIG; sig++) {
        if (sig != SIGKILL && sig != SIGSTOP) sigaction(sig, &action, NULL);
    }
    sigset_t empty;
    sigemptyset(&empty);
    if (sigprocmask(SIG_SETMASK, &empty, NULL)) { perror("signals"); return 4; }
    if (setenv("DOVE_WIFI_CLEAN_EXEC", "1", 1)) return 5;
    execvp(argv[1], argv + 1);
    perror("Wi-Fi service exec");
    return errno == ENOENT ? 127 : 126;
}
