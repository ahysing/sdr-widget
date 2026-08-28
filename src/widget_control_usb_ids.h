#ifndef WIDGET_CONTROL_USB_IDS_H
#define WIDGET_CONTROL_USB_IDS_H

#include <stdint.h>

/*
 * USB VID/PID pairs accepted by widget-control.
 * Keep in sync with src/usb_descriptors.h (FEATURE_PRODUCT_* sections).
 */
typedef struct {
	uint16_t vendor_id;
	uint16_t product_id;
} widget_usb_id_t;

static const widget_usb_id_t widget_usb_ids[] = {
	{ 0x16c0, 0x05dc }, /* DG8SAQ */
	{ 0x16c0, 0x03e8 },
	{ 0x16c0, 0x03e9 }, /* UAC1 lab */
	{ 0x16d0, 0x0761 }, /* SDR-Widget UAC1 */
	{ 0x16d0, 0x0762 }, /* SDR-Widget UAC2 */
	{ 0x16d0, 0x0763 }, /* USB9023 UAC1 */
	{ 0x16d0, 0x0764 }, /* USB9023 UAC2 */
	{ 0x16d0, 0x0765 }, /* USB5102 UAC1 */
	{ 0x16d0, 0x0766 }, /* USB5102 UAC2 */
	{ 0x16d0, 0x0767 }, /* USB8741 UAC1 */
	{ 0x16d0, 0x0768 }, /* USB8741 UAC2 */
	{ 0x16d0, 0x075c }, /* AB-1.x / Henry Audio legacy UAC1 */
	{ 0x16d0, 0x075d }, /* AB-1.x / Henry Audio legacy UAC2 */
	{ 0x16d0, 0x075e }, /* AB-1.x UAC1 (usb_descriptors.h AUDIO_PRODUCT_ID_9) */
	{ 0x16d0, 0x075f }, /* AB-1.x UAC2 / Henry Audio 128 Mk3 (AUDIO_PRODUCT_ID_10) */
	{ 0x16d0, 0x098b }, /* AMB UAC1 */
	{ 0x16d0, 0x098c }, /* AMB UAC2 */
	{ 0xfffe, 0x0007 }, /* HPSDR Ozy */
};

static inline int widget_usb_id_matches(uint16_t vendor_id, uint16_t product_id)
{
	size_t i;

	for (i = 0; i < sizeof(widget_usb_ids) / sizeof(widget_usb_ids[0]); i += 1) {
		if (widget_usb_ids[i].vendor_id == vendor_id
			&& widget_usb_ids[i].product_id == product_id) {
			return 1;
		}
	}
	return 0;
}

#endif
