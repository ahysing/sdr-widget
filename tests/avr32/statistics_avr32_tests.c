#include "conf_usb.h"
#include "usb_statistics.h"
#include "audio_stats_logic.h"
#include <assert.h>

// Simple test to verify the statistics logic on AVR32 target
// This is mainly to verify compilation and basic logic integration
void test_avr32_stats_logic() {
    usb_stats_t stats;
    // Initialize stats
    stats.overruns = 0;
    stats.underruns = 0;
    stats.fifo_level = 0;
    stats.max_fifo = 0;
    stats.min_fifo = 0xFFFF;
    stats.deadline_misses = 0;
    
    // Test update
    audio_stats_update(&stats, 100, 1536);
    
    // We can't use assert() easily in embedded if there's no console,
    // but for a test build we assume it's linked to something that handles it.
    if (stats.fifo_level != 100) while(1); 
    if (stats.max_fifo != 100) while(1);
    if (stats.min_fifo != 100) while(1);
}

int main() {
    test_avr32_stats_logic();
    return 0;
}
