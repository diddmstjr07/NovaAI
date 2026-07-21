#ifndef NOVA_DEB_H
#define NOVA_DEB_H

#include "types.h"

typedef struct {
    uint32_t files;
    uint32_t directories;
    uint32_t symlinks;
} DebInstallResult;

bool deb_install(const void *package, size_t size, DebInstallResult *result);
bool deb_install_file(const char *name, DebInstallResult *result);
bool deb_self_test(void);

#endif
