#include <stdio.h>
#include <assert.h>
#include "compiler.h"
#include "audio_stats_logic.h"

#define TEST_BUFFER_SIZE 100

void test_calculate_gap_same_buffer() {
    printf("Running test_calculate_gap_same_buffer...\n");
    U16 gap = audio_stats_calculate_gap(20, 30, 0, 0, TEST_BUFFER_SIZE);
    assert(gap == 150);
    printf("test_calculate_gap_same_buffer passed\n");
}

void test_calculate_gap_different_buffer() {
    printf("Running test_calculate_gap_different_buffer...\n");
    // spk_index = 20, num_remaining = 30, different buffer
    // spk_index (20) < (100 - 30) (70)
    // gap = 100 - 30 - 20 = 50
    U16 gap = audio_stats_calculate_gap(20, 30, 0, 1, TEST_BUFFER_SIZE);
    assert(gap == 50);
    
    // spk_index = 80, num_remaining = 30, different buffer
    // spk_index (80) >= (100 - 30) (70)
    // gap = 100 - 80 + 100 - 30 + 100 = 20 + 70 + 100 = 190
    gap = audio_stats_calculate_gap(80, 30, 0, 1, TEST_BUFFER_SIZE);
    assert(gap == 190);
    printf("test_calculate_gap_different_buffer passed\n");
}

void test_calculate_gap_clipping() {
    printf("Running test_calculate_gap_clipping...\n");
    // Max value is 2 * buffer_size when spk_index = 0, num_remaining = buffer_size in different buffer branch
    U16 gap = audio_stats_calculate_gap(0, TEST_BUFFER_SIZE, 0, 1, TEST_BUFFER_SIZE);
    assert(gap == 200); 
    
    // Also max value when spk_index = buffer_size, num_remaining = 0 in different buffer branch
    gap = audio_stats_calculate_gap(TEST_BUFFER_SIZE, 0, 0, 1, TEST_BUFFER_SIZE);
    assert(gap == 200);

    printf("test_calculate_gap_clipping passed\n");
}

void test_audio_stats_update() {
    printf("Running test_audio_stats_update...\n");
    usb_stats_t stats = {0, 0, 0, 0, 0, 0xFFFF, 0, 0, 0, 0, 0, 0};
    
    audio_stats_update(&stats, 50, TEST_BUFFER_SIZE);
    assert(stats.fifo_level == 50);
    assert(stats.max_fifo == 50);
    assert(stats.min_fifo == 50);
    assert(stats.generation == 2);
    
    audio_stats_update(&stats, 80, TEST_BUFFER_SIZE);
    assert(stats.fifo_level == 80);
    assert(stats.max_fifo == 80);
    assert(stats.min_fifo == 50);
    
    audio_stats_update(&stats, 0, TEST_BUFFER_SIZE);
    assert(stats.underruns == 1);
    assert(stats.min_fifo == 0);
    
    audio_stats_update(&stats, 200, TEST_BUFFER_SIZE);
    assert(stats.overruns == 1);
    assert(stats.max_fifo == 200);
    
    printf("test_audio_stats_update passed\n");
}

void test_audio_stats_record_event() {
    printf("Running test_audio_stats_record_event...\n");
    usb_stats_t stats = {0};
    stats.min_fifo = 0xFFFF;

    audio_stats_record_event(&stats, USB_STATS_TAG_EQUALIZER_STEP_SWITCH, 80, 71, 1);

    assert(stats.event_count == 1);
    assert(stats.last_tag == USB_STATS_TAG_EQUALIZER_STEP_SWITCH);
    assert(stats.last_arg0 == 80);
    assert(stats.last_arg1 == 71);
    assert(stats.last_arg2 == 1);
    printf("test_audio_stats_record_event passed\n");
}

int main() {
    test_calculate_gap_same_buffer();
    test_calculate_gap_different_buffer();
    test_calculate_gap_clipping();
    test_audio_stats_update();
    test_audio_stats_record_event();
    printf("\nAll audio stats logic tests completed!\n");
    return 0;
}
