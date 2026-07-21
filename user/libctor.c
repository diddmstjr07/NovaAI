#include "types.h"

static volatile uint64_t constructor_state;

static void __attribute__((constructor)) nova_constructor(void) {
    constructor_state = 0x434F4E5354525543UL;
}

uint64_t nova_constructor_state(void) {
    return constructor_state;
}
