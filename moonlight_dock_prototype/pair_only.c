/* Minimal front end to Moonlight Embedded's libgamestream for the dock trial. */
#include "client.h"
#include "errors.h"
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: dock-moonlight-pair HOST KEY_DIRECTORY\n");
        return 2;
    }
    uint32_t random_number;
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0 || read(fd, &random_number, sizeof random_number) != sizeof random_number) {
        perror("random PIN");
        return 2;
    }
    close(fd);
    char pin[5];
    snprintf(pin, sizeof pin, "%04u", random_number % 10000);
    umask(077);
    SERVER_DATA server;
    int result = gs_init(&server, argv[1], 0, argv[2], 0, true);
    if (result != GS_OK) {
        fprintf(stderr, "Host connection failed: %s (%d)\n", gs_error ? gs_error : "unknown", result);
        return 1;
    }
    printf("MOONLIGHT_PIN=%s\n", pin);
    fflush(stdout);
    result = gs_pair(&server, pin);
    if (result != GS_OK) {
        fprintf(stderr, "Pairing failed: %s (%d)\n", gs_error ? gs_error : "unknown", result);
        return 1;
    }
    puts("MOONLIGHT_PAIRED=1");
    return 0;
}
