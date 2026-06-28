//
// Created by AHysing on 6/27/2026.
//
#ifndef SDR_WIDGET_USB_STATISTICS_H
#define SDR_WIDGET_USB_STATISTICS_H

#include "compiler.h"

typedef volatile struct {
    // what generation of the struct
    U32 generation;

    // USB packet arrives, but the receive buffer is already full.
    U32 overruns;

    // DAC requires, but the receive buffer is empty.
    U32 underruns;

    // USB asynchronous audio devices continuously tell the computer: speed up sending, speed down sending
    // U32 feedback_changes;

    // This is the number of samples that have been received but not yet played.
    U16 fifo_level;

    // Largest number of samples that have been received but not yet played on the FIFO queue
    U16 max_fifo;

    // Smallest number of samples that have been received but not yet played on the FIFO queue
    U16 min_fifo;

    //audio-processing loop fails to complete before the next packet is due to arrive
    U32 deadline_misses;
} usb_stats_t;

typedef struct __attribute__((packed)) {
    U32 version;

    U32 overruns;
    U32 underruns;

    // U32 feedback_changes;

    U16 fifo_level;
    U16 max_fifo;
    U16 min_fifo;

    U32 deadline_misses;
} usb_stats_packet_t;

typedef volatile struct {
    // packets received
    U32 packet_count;

    // packets received containing n frames
    U32 packet_histogram[64];
} usb_histogram_t;

typedef struct __attribute__((packed)) {
    // packets received
    U32 packet_count;

    // packets received containing n frames
    U32 packet_histogram[64];
} usb_histogram_packet_t;

extern void statistics_init();
extern void statistics_task(void *pvParameters);
usb_stats_t* get_usb_stats();
usb_stats_t snapshot();
#endif //SDR_WIDGET_USB_STATISTICS_H
