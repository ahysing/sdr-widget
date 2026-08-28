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
#define USB_STATS_PACKET_VERSION     2u
#define USB_STATS_PACKET_WIRE_SIZE   39u
#define USB_STATS_PACKET_CHECKSUM_OFFSET 3u

#define USB_STATS_TAG_NONE         0u
#define USB_STATS_TAG_EQUALIZER_STEP_SWITCH  1u
#define USB_STATS_TAG_RAMP_COMPLETE 2u
#define USB_STATS_TAG_FREQ_CHANGE  3u
#define USB_STATS_TAG_SKIP         4u
#define USB_STATS_TAG_INSERT       5u
#define USB_STATS_TAG_FORCED_RESYNC 6u

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
    U16 frequency_100hz;
    S8 gain_dbfs_left;
    S8 gain_dbfs_right;
    S8 db_spl_left;
    S8 db_spl_right;
    U32 event_count;
    U8 last_tag;
    U8 last_arg0;
    U8 last_arg1;
    U8 last_arg2;
    U8 equalizer_step_left;
    U8 equalizer_step_right;
    U8 source_has_volume_control;
});
typedef struct usb_stats_packet usb_stats_packet_t;

#ifndef USBSTATISTICS_DISABLE
extern void statistics_init();
extern void statistics_task(void *pvParameters);
extern void statistics_report_iteration();
volatile usb_stats_t* get_usb_stats();
Bool statistics_runtime_is_active(void);
void statistics_runtime_set_active(Bool active);

#ifdef UNIT_TEST
void statistics_test_reset(void);
void statistics_test_set_collect_index(int index);
volatile usb_stats_t* statistics_test_get_buffer(int index);
int statistics_test_get_collect_index(void);
U8 statistics_test_get_report_seq(void);
U8 statistics_test_build_wire_checksum(const U8 *wire);
void statistics_test_build_wire_packet(U8 *wire, const volatile usb_stats_t *s, U8 report_seq);
#endif
#endif // USBSTATISTICS_DISABLE

#endif //SDR_WIDGET_USB_STATISTICS_H
