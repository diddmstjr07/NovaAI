#include "types.h"

/* The kernel performs eager ELF dependency loading.  This compatibility DSO
   exposes the glibc libdl ABI and treats dlopen(NULL) as the global namespace.
   Loading a new DSO after process start is reported as unsupported for now. */
static int main_program_handle;
static int error_pending;
static char unsupported_message[] = "NovaOS: late shared-object loading is not available";

void *dlopen(const char *name, int flags) {
    (void)flags;
    if (!name) return &main_program_handle;
    error_pending = 1;
    return NULL;
}

void *dlsym(void *handle, const char *name) {
    (void)handle;
    (void)name;
    error_pending = 1;
    return NULL;
}

void *dlvsym(void *handle, const char *name, const char *version) {
    (void)version;
    return dlsym(handle, name);
}

int dlclose(void *handle) {
    if (handle == &main_program_handle) return 0;
    error_pending = 1;
    return -1;
}

char *dlerror(void) {
    if (!error_pending) return NULL;
    error_pending = 0;
    return unsupported_message;
}

int dladdr(const void *address, void *information) {
    (void)address;
    (void)information;
    return 0;
}

int dlinfo(void *handle, int request, void *information) {
    (void)handle;
    (void)request;
    (void)information;
    error_pending = 1;
    return -1;
}
