//
// Created by AHysing on 6/27/2026.
//
#ifndef SDR_WIDGET_USB_STATISTICS_H
#define SDR_WIDGET_USB_STATISTICS_H

#include "compiler.h"

typedef struct {
    U8 generation;
    U32 overruns;
    U32 underruns;
    U16 fifo_level;
    U16 max_fifo;
    U16 min_fifo;
    U32 deadline_misses;
    U32 event_count;
    U8 last_tag;
    U8 last_arg0;
    U8 last_arg1;
    U8 last_arg2;
} usb_stats_t;

#define USB_STATS_PACKET_HID_ANCHOR  0x53u
#define USB_STATS_PACKET_VERSION     1u
#define USB_STATS_PACKET_WIRE_SIZE   37u
#define USB_STATS_PACKET_CHECKSUM_OFFSET 3u

#define USB_STATS_TAG_NONE         0u
#define USB_STATS_TAG_EQUALIZER_STEP_SWITCH  1u
#define USB_STATS_TAG_RAMP_COMPLETE 2u
#define USB_STATS_TAG_FREQ_CHANGE  3u

PACK(struct usb_stats_packet {
    U8 hid_anchor;
    U8 version;
    U8 report_seq;
    U8 checksum;
    U32 overruns;
    U32 underruns;
    U16 fifo_level;
    U16 max_fifo;
    U16 min_fifo;
    U32 deadline_misses;
    U16 frequency_hz;
    S8 track_dbfs;
    S8 track_rms_dbfs;
    S8 gain_dbfs;
    S8 db_spl;
    U32 event_count;
    U8 last_tag;
    U8 last_arg0;
    U8 last_arg1;
    U8 last_arg2;
    U8 equalizer_step;
});
typedef struct usb_stats_packet usb_stats_packet_t;

#ifndef USBSTATISTICS_DISABLE
extern void statistics_init();
extern void statistics_task(void *pvParameters);
extern void statistics_report_iteration();
volatile usb_stats_t* get_usb_stats();

#ifdef UNIT_TEST
void statistics_test_reset(void);
void statistics_test_set_collect_index(int index);
volatile usb_stats_t* statistics_test_get_buffer(int index);
int statistics_test_get_collect_index(void);
U8 statistics_test_build_wire_checksum(const U8 *wire);
void statistics_test_build_wire_packet(U8 *wire, const volatile usb_stats_t *s, U8 report_seq);
#endif
#endif // USBSTATISTICS_DISABLE

#endif //SDR_WIDGET_USB_STATISTICS_H
