#define _POSIX_C_SOURCE 200809L
/* Reversible, process-memory-only keyboard/mouse sender compatibility patches.
 * The executable on flash is never opened for writing.
 */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

struct site { unsigned long addr, stock; };
static const struct site keyboard_sites[] = {
    {0x000f3c14UL, 0xe2033001UL},
    {0x000f3c7cUL, 0xe2033001UL},
};
static const struct site mouse_sites[] = {
    {0x000f3b08UL, 0xe2033001UL},
    {0x000f3b54UL, 0xe2033001UL},
    {0x000f3960UL, 0xe3530007UL},
    {0x000f3968UL, 0xe3530009UL},
    {0x000f3970UL, 0xe353000aUL},
};
static const unsigned long mouse_values[]={0xe3a03001UL,0xe3a03001UL,0xe3530000UL,0xe3530002UL,0xe3530003UL};
static const unsigned long forced_hid = 0xe3a03001UL;

static int peek(pid_t pid, unsigned long addr, unsigned long *out) {
    errno = 0;
    long v = ptrace(PTRACE_PEEKTEXT, pid, (void *)addr, 0);
    if (v == -1 && errno) return -1;
    *out = (unsigned long)v & 0xffffffffUL;
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    const int mouse = !strcmp(argv[1],"verify-mouse") || !strcmp(argv[1],"apply-mouse") || !strcmp(argv[1],"restore-mouse");
    char operation[16];snprintf(operation,sizeof operation,"%s",argv[1]);
    char *suffix=strstr(operation,"-mouse");if(suffix)*suffix=0;
    if (strcmp(operation,"verify") && strcmp(operation,"apply") && strcmp(operation,"restore")) {
        fprintf(stderr, "usage: dex-keyboard-route verify|apply|restore[-mouse] PID\n");
        return 2;
    }
    const struct site *sites=mouse?mouse_sites:keyboard_sites;
    const unsigned count_sites=mouse?5:2;
    char *end = NULL;
    long number = strtol(argv[2], &end, 10);
    if (!end || *end || number <= 1 || number > INT_MAX) return 2;
    pid_t pid = (pid_t)number;
    char link[64], exe[PATH_MAX];
    snprintf(link, sizeof link, "/proc/%ld/exe", number);
    ssize_t count = readlink(link, exe, sizeof exe - 1);
    if (count < 0) { perror("readlink"); return 1; }
    exe[count] = 0;
    if (strcmp(exe, "/usr/local/bin/IMS_Modules/wfd_dfb")) {
        fprintf(stderr, "Unexpected process executable: %s\n", exe);
        return 1;
    }
    if (ptrace(PTRACE_ATTACH, pid, 0, 0) == -1) { perror("PTRACE_ATTACH"); return 1; }
    int status = 0, result = 1;
    if (waitpid(pid, &status, 0) != pid || !WIFSTOPPED(status)) {
        fprintf(stderr, "Process did not stop safely\n");
        goto detach;
    }
    unsigned long current[5];
    for (unsigned i = 0; i < count_sites; ++i) {
        if (peek(pid, sites[i].addr, &current[i]) == -1) { perror("PTRACE_PEEKTEXT"); goto detach; }
        printf("%08lx: %08lx\n", sites[i].addr, current[i]);
        if (current[i] != sites[i].stock && current[i] != (mouse?mouse_values[i]:forced_hid)) {
            fprintf(stderr, "Unexpected instruction at %08lx\n", sites[i].addr);
            goto detach;
        }
    }
    if (!strcmp(operation, "verify")) { result = 0; goto detach; }
    if (!strcmp(operation, "apply")) {
        for(unsigned i=0;i<count_sites;i++)if(current[i]!=sites[i].stock){
            fprintf(stderr,"Apply requires stock instructions\n");goto detach;
        }
        for(unsigned i=0;i<count_sites;i++){
            unsigned long wanted=mouse?mouse_values[i]:forced_hid;
            if(ptrace(PTRACE_POKETEXT,pid,(void *)sites[i].addr,(void *)wanted)==-1){
                perror("PTRACE_POKETEXT");
                for(unsigned j=0;j<i;j++)ptrace(PTRACE_POKETEXT,pid,(void *)sites[j].addr,(void *)sites[j].stock);
                goto detach;
            }
        }
    } else {
        for (unsigned i = 0; i < count_sites; ++i) {
            if (current[i] != sites[i].stock && ptrace(PTRACE_POKETEXT, pid, (void *)sites[i].addr,
                                                   (void *)sites[i].stock) == -1) {
                perror("PTRACE_POKETEXT restore"); goto detach;
            }
        }
    }
    for (unsigned i = 0; i < count_sites; ++i) {
        unsigned long value;
        unsigned long expected = !strcmp(operation, "apply") ? (mouse?mouse_values[i]:forced_hid) : sites[i].stock;
        if (peek(pid, sites[i].addr, &value) == -1 || value != expected) {
            fprintf(stderr, "Post-write verification failed at %08lx\n", sites[i].addr);
            if(!strcmp(operation,"apply"))for(unsigned j=0;j<count_sites;j++)
                ptrace(PTRACE_POKETEXT,pid,(void *)sites[j].addr,(void *)sites[j].stock);
            goto detach;
        }
    }
    printf("%s_%s_ROUTE\n",mouse?"MOUSE":"KEYBOARD",!strcmp(operation,"apply")?"HIDC_APPLIED":"STOCK_RESTORED");
    result = 0;
detach:
    if (ptrace(PTRACE_DETACH, pid, 0, 0) == -1) { perror("PTRACE_DETACH"); return 1; }
    return result;
}
