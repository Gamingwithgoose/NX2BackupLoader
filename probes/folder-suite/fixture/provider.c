#include <stdint.h>
uint64_t nx2_fixture_bias=0x1234;
__thread uint64_t nx2_tls_counter=7;
uint64_t nx2_provider(uint64_t input){nx2_tls_counter+=input;return nx2_tls_counter+nx2_fixture_bias;}
uint64_t nx2_provider_tls(void){return nx2_tls_counter;}
double nx2_simd_tls(double input){return input+(double)nx2_tls_counter;}
