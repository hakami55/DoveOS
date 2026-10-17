#include "discover.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* mDNS can be absent on a direct, link-local Ethernet connection. Only probe
 * neighbors the kernel has already learned; do not scan an address range. */
static int sunshine_at(const char *address) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    struct sockaddr_in peer = {.sin_family = AF_INET, .sin_port = htons(47989)};
    if (inet_pton(AF_INET, address, &peer.sin_addr) != 1 ||
        fcntl(fd, F_SETFL, O_NONBLOCK) < 0) { close(fd); return 0; }
    int rc = connect(fd, (struct sockaddr *)&peer, sizeof(peer));
    if (rc < 0) {
        struct pollfd pfd = {.fd = fd, .events = POLLOUT};
        int error = 0;
        socklen_t length = sizeof(error);
        if (poll(&pfd, 1, 350) <= 0 ||
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) < 0 || error) {
            close(fd); return 0;
        }
    }
    const char request[] = "GET /serverinfo HTTP/1.0\r\nHost: localhost\r\n\r\n";
    if (send(fd, request, sizeof(request) - 1, 0) < 0) { close(fd); return 0; }
    struct pollfd pfd = {.fd = fd, .events = POLLIN};
    char response[2048];
    if (poll(&pfd, 1, 350) <= 0) { close(fd); return 0; }
    ssize_t n = recv(fd, response, sizeof(response) - 1, 0);
    close(fd);
    if (n <= 0) return 0;
    response[n] = 0;
    return strstr(response, "<hostname>") != NULL ||
           strstr(response, "<root status_code=\"200\"") != NULL;
}

static int neighbor_sunshine(char *address, size_t capacity) {
    FILE *arp = fopen("/proc/net/arp", "r");
    if (!arp) return 0;
    char line[256];
    if (!fgets(line, sizeof(line), arp)) { fclose(arp); return 0; }
    while (fgets(line, sizeof(line), arp)) {
        char candidate[64], mac[64], mask[64], device[64];
        unsigned type, flags;
        if (sscanf(line, "%63s 0x%x 0x%x %63s %63s %63s",
                   candidate, &type, &flags, mac, mask, device) != 6 ||
            !(flags & 2) || !sunshine_at(candidate)) continue;
        snprintf(address, capacity, "%s", candidate);
        fclose(arp);
        return 1;
    }
    fclose(arp);
    return 0;
}

int main(void) {
    char address[64] = {0};
    unsigned short port = 0;
    setvbuf(stdout, NULL, _IONBF, 0);
    gs_discover_server(address, &port);
    struct in_addr ip;
    if (inet_pton(AF_INET, address, &ip) != 1 &&
        !neighbor_sunshine(address, sizeof(address))) {
        puts("PC_NOT_FOUND");
        return 1;
    }
    printf("FOUND_PC=%s\n", address);
    return 0;
}
