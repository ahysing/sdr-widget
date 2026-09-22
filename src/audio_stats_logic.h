#ifndef AUDIO_STATS_LOGIC_H
#define AUDIO_STATS_LOGIC_H

#include "compiler.h"
#include "usb_statistics.h"

// We pass SPK_BUFFER_SIZE as an argument to avoid including taskAK5394A.h here
static inline U16 audio_stats_calculate_gap(U16 spk_index, U16 num_remaining, U8 spk_buffer_in, U8 spk_buffer_out, U16 buffer_size) {
    U16 gap;
    if (spk_buffer_in != spk_buffer_out) {
        if (spk_index < (buffer_size - num_remaining)) {
            gap = buffer_size - num_remaining - spk_index;
        } else {
            gap = buffer_size - spk_index + buffer_size - num_remaining + buffer_size;
        }
    } else {
        gap = (buffer_size - spk_index) + (buffer_size - num_remaining);
    }
    
    if (gap > 2 * buffer_size) {
        gap = 2 * buffer_size;
    }

    return gap;
}

static inline void audio_stats_record_event(volatile usb_stats_t* stats, U8 tag, U8 arg0, U8 arg1, U8 arg2) {
    if (stats == NULL) {
        return;
    }
    stats->generation++;
    stats->event_count++;
    stats->last_tag = tag;
    stats->last_arg0 = arg0;
    stats->last_arg1 = arg1;
    stats->last_arg2 = arg2;
    stats->generation++;
}

static inline void audio_stats_update(volatile usb_stats_t* stats, U16 gap, U16 buffer_size) {
    stats->generation++;
    stats->fifo_level = gap;
    if (gap > stats->max_fifo) stats->max_fifo = gap;
    if (gap < stats->min_fifo) stats->min_fifo = gap;
    if (gap == 0) stats->underruns++;
    if (gap >= 2 * buffer_size) stats->overruns++;
    stats->generation++;
}

#endif
