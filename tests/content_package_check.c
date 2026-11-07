/* Standalone probe used by check-content-packages.py; exit 2 is rejection. */
#include "loader/content_package.h"
#include <stdio.h>
int main(int argc, char **argv) {
    char out[1024], error[256];
    if (argc < 2) return 1;
    if (!zcontent_prepare(argv[1], argc > 2 ? argv[2] : NULL,
                          out, sizeof(out), error, sizeof(error))) {
        fprintf(stderr, "%s\n", error); return 2;
    }
    puts(out); return 0;
}
