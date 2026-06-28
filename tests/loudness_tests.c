#include "fff.h"
#include "loudness.h"
#include <stdio.h>
#include <assert.h>
#include <stdint.h>

DEFINE_FFF_GLOBALS;

// Function prototypes for internal functions in loudness.c (not in loudness.h)
U32 calculate_dB_24bit(U32 rms);
uint32_t sqrt_i(uint64_t tall);
U32 update_rms(U32 sample);

void test_sqrt_i() {
    printf("Running test_sqrt_i...\n");
    assert(sqrt_i(0) == 0);
    assert(sqrt_i(1) == 1);
    assert(sqrt_i(4) == 2);
    assert(sqrt_i(100) == 10);
    assert(sqrt_i(10000) == 100);
    assert(sqrt_i(100000000ULL) == 10000);
    printf("test_sqrt_i passed\n");
}

void test_calculate_dB_24bit() {
    printf("Running test_calculate_dB_24bit...\n");
    // 0xFFFFFF is 24-bit max. leading zeros = 8.
    // If bit_position = 32 - 8 = 24.
    // db_estimate = (24 - 24) * 6 = 0.
    U32 db = calculate_dB_24bit(0xFFFFFF);
    printf("dB for 0xFFFFFF: %d\n", (int)db);
    assert((int)db == 0);
    
    // 0x7FFFFF is -6dB
    db = calculate_dB_24bit(0x7FFFFF);
    printf("dB for 0x7FFFFF: %d\n", (int)db);
    assert((int)db <= -5 && (int)db >= -7);
    
    printf("test_calculate_dB_24bit passed\n");
}

void test_update_rms() {
    printf("Running test_update_rms...\n");
    dsp_init();
    // Test with a constant value
    U32 rms = 0;
    for(int i = 0; i < 10000; i++) {
        rms = update_rms(10000);
    }
    printf("RMS for constant 10000: %u\n", rms);
    assert(rms >= 9990 && rms <= 10010);
    printf("test_update_rms passed\n");
}

void test_loudness_init() {
    printf("Running test_loudness_init...\n");
    dsp_init();
    printf("test_loudness_init passed\n");
}

void test_loudness_processing() {
    printf("Running test_loudness_processing...\n");
    U64 sample = 1000;
    U64 result = loudness(sample);
    assert(result == sample);
    printf("test_loudness_processing passed\n");
}

int main() {
    test_sqrt_i();
    test_calculate_dB_24bit();
    test_update_rms();
    test_loudness_init();
    test_loudness_processing();
    printf("\nAll tests completed!\n");
    return 0;
}
