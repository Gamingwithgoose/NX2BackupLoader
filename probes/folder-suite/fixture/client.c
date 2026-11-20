#include <stdint.h>
extern uint64_t nx2_provider(uint64_t input);
extern __thread uint64_t nx2_tls_counter;
extern uint64_t nx2_missing_weak(void) __attribute__((weak));
static uint64_t ctor_value;
static uint64_t big_bss[2048];
static uint64_t *relative_pointers[3]={&big_bss[0],&big_bss[10],&big_bss[2047]};
__attribute__((constructor)) static void start(void){ctor_value=0xcafe;}
__attribute__((destructor)) static void stop(void){ctor_value=0;}
uint64_t nx2_suite_entry(uint64_t input){return nx2_provider(input)+nx2_tls_counter;}
uint64_t nx2_weak_check(void){return nx2_missing_weak?1:0;}
uint64_t nx2_ctor_check(void){return ctor_value;}
uint64_t nx2_bss_check(void){
    for(unsigned i=0;i<2048;i++)if(big_bss[i])return 0;
    *relative_pointers[0]=11;*relative_pointers[1]=22;*relative_pointers[2]=33;
    uint64_t ok=big_bss[0]==11&&big_bss[10]==22&&big_bss[2047]==33;
    big_bss[0]=big_bss[10]=big_bss[2047]=0;return ok;
}
