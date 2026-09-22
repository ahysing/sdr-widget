#include "usb_stats_hid_report_descriptor.h"

#ifndef USBSTATISTICS_DISABLE

const U8 usb_stats_hid_report_descriptor[USB_STATS_HID_REPORT_DESC] =
{
	0x06, 0x00, 0xFF,	/* Usage Page (Vendor Defined 0xFF00) */
	0x09, 0x01,		/* Usage 0x01 */
	0xA1, 0x01,		/* Collection (Application) */
	0x85, USB_STATS_HID_REPORT_ID, /* Report ID */
	0x09, 0x01,		/* Usage 0x01 */
	0x15, 0x00,		/* Logical Minimum (0) */
	0x26, 0xFF, 0x00,	/* Logical Maximum (255) */
	0x75, 0x08,		/* Report Size (8 bits) */
	0x95, USB_STATS_HID_REPORT_SIZE, /* Report Count */
	0x81, 0x02,		/* Input (Data, Variable, Absolute) */
	0xC0			/* End Collection */
};

#endif
