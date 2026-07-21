#include "types.h"

__thread uint64_t nova_tls_value __attribute__((tls_model("initial-exec"))) =
    0x544C53494E495421UL;
__thread uint64_t nova_tls_gd_value __attribute__((tls_model("global-dynamic"))) =
    0x4744544C53494E49UL;

uint64_t nova_tls_get(void) {
    return nova_tls_value;
}

void nova_tls_set(uint64_t value) {
    nova_tls_value = value;
}

uintptr_t nova_tls_address(void) {
    return (uintptr_t)&nova_tls_value;
}

uint64_t nova_tls_gd_get(void) {
    return nova_tls_gd_value;
}

void nova_tls_gd_set(uint64_t value) {
    nova_tls_gd_value = value;
}
