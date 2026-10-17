#include "client.h"
#include "errors.h"
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    SERVER_DATA server;
    int result = gs_init(&server, argv[1], 0, argv[2], 2, true);
    if (result != GS_OK) {
        fprintf(stderr, "Host status failed: %s (%d)\n", gs_error, result);
        return 1;
    }
    printf("PAIRED=%d\n", server.paired);
    PAPP_LIST apps = NULL;
    result = gs_applist(&server, &apps);
    if (result != GS_OK) {
        fprintf(stderr, "Secure app list failed: %s (%d)\n", gs_error, result);
        return 1;
    }
    for (PAPP_LIST app = apps; app; app = app->next)
        printf("APP=%d:%s\n", app->id, app->name);
    return 0;
}
