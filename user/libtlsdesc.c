#include "types.h"

__thread uint64_t nova_tlsdesc_value __attribute__((tls_model("global-dynamic"))) =
    0x544C534445534349UL;

uint64_t nova_tlsdesc_get(void) {
    return nova_tlsdesc_value;
}

void nova_tlsdesc_set(uint64_t value) {
    nova_tlsdesc_value = value;
}
