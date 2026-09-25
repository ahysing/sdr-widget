/*
 * PC unit-test stand-in for src/usb_statistics_descriptors.h.
 * Avoids the AVR USB stack (hid.h / usb_task.h); wire layout matches firmware.
 */
#ifndef SDR_WIDGET_USB_STATISTICS_DESCRIPTORS_H
#define SDR_WIDGET_USB_STATISTICS_DESCRIPTORS_H

#define USB_STATS_PACKET_HID_ANCHOR              0x53u
#define USB_STATS_PACKET_VERSION                 6u
#define USB_STATS_PACKET_WIRE_SIZE               45u
#define USB_STATS_PACKET_CHECKSUM_OFFSET         3u
#define USB_STATS_WIRE_OFFSET_BASS_BOOST_ENABLED 39u
#define USB_STATS_WIRE_OFFSET_GAIN_INFERRED_LEFT   40u
#define USB_STATS_WIRE_OFFSET_GAIN_INFERRED_RIGHT  41u
#define USB_STATS_WIRE_OFFSET_LOUDNESS_ENABLED     42u
#define USB_STATS_WIRE_OFFSET_SAMPLE_BITS          43u
#define USB_STATS_WIRE_OFFSET_NUM_SAMPLES          44u

#ifndef USBSTATISTICS_DISABLE

#define HID_CLASS                    0x03u
#define TYPE_INTERRUPT               0x03u
#define MSK_EP_DIR                   0x80u

#include "usb_stats_hid_report_descriptor.h"

#define INTERFACE_NB_STATS           3
#define ALTERNATE_NB_STATS           0
#define NB_ENDPOINT_STATS            1
#define INTERFACE_CLASS_STATS        HID_CLASS
#define INTERFACE_SUB_CLASS_STATS    0x00
#define INTERFACE_PROTOCOL_STATS     0x00
#define INTERFACE_INDEX_STATS        0
#define DSC_INTERFACE_STATISTICS     INTERFACE_NB_STATS
#define HID_STATS_VERSION            0x0111
#define HID_STATS_COUNTRY_CODE       0x00
#define HID_STATS_NUM_DESCRIPTORS    0x01
#define EP_STATS_HID_TX              6
#define ENDPOINT_NB_STATS_HID        (EP_STATS_HID_TX | MSK_EP_DIR)
#define EP_ATTRIBUTES_STATS_HID      TYPE_INTERRUPT
#define EP_SIZE_STATS_HID_FS         USB_STATS_HID_TRANSFER_SIZE
#define EP_SIZE_STATS_HID_HS         USB_STATS_HID_TRANSFER_SIZE
#define EP_INTERVAL_STATS_HID_FS     10
#define EP_INTERVAL_STATS_HID_HS     13
#define EP_STATISTICS                EP_STATS_HID_TX

#endif /* USBSTATISTICS_DISABLE */

#endif /* SDR_WIDGET_USB_STATISTICS_DESCRIPTORS_H */
