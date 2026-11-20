#include <stdint.h>
uint64_t nx2_fixture_bias = 0x1234;
static volatile uint64_t calls;
__attribute__((visibility("default")))
uint64_t nx2_module_entry(uint64_t input) {
    calls++;
    return input + nx2_fixture_bias + calls;
}
