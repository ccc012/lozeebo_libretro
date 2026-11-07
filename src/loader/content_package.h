#ifndef ZCONTENT_PACKAGE_H
#define ZCONTENT_PACKAGE_H
#include <stdbool.h>
#include <stddef.h>
/* member is an optional exact archive member, never a MIF. */
bool zcontent_prepare(const char *source, const char *member, char *out,
                      size_t out_size, char *error, size_t error_size);
bool zcontent_is_package(const char *path);
#endif
