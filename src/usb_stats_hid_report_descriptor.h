#ifndef SDR_WIDGET_USB_STATS_HID_REPORT_DESCRIPTOR_H
#define SDR_WIDGET_USB_STATS_HID_REPORT_DESCRIPTOR_H

#include "compiler.h"
#include "usb_statistics.h"

#define USB_STATS_HID_REPORT_ID     1u
#define USB_STATS_HID_REPORT_DESC   23u
/* FS interrupt endpoints allow 64 bytes max per transfer (Report ID + payload). */
#define USB_STATS_HID_REPORT_SIZE   63u
#define USB_STATS_HID_TRANSFER_SIZE 64u

#if (USB_STATS_HID_TRANSFER_SIZE != 64)
#error USB_STATS_HID_TRANSFER_SIZE must be 64
#endif
#if (1u + USB_STATS_HID_REPORT_SIZE != USB_STATS_HID_TRANSFER_SIZE)
#error HID report size and transfer size mismatch
#endif
#if (1u + USB_STATS_PACKET_WIRE_SIZE > USB_STATS_HID_REPORT_SIZE)
#error stats wire payload exceeds HID report count
#endif

extern const U8 usb_stats_hid_report_descriptor[USB_STATS_HID_REPORT_DESC];

#endif
