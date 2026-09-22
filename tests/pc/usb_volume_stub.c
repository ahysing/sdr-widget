/* PC test stub: digital volume conversion from src/usb_specific_request.c */
#include "compiler.h"

#define VOL_MIN       (S16)0xC400
#define VOL_MAX       (S16)0x0000
#define VOL_MULT_UNITY 0x10000000

S32 usb_volume_format(S16 spk_vol_usb) {
	S32 V = VOL_MULT_UNITY;

	if ((spk_vol_usb < VOL_MIN) || (spk_vol_usb > VOL_MAX))
		return 0;

	spk_vol_usb -= (S16)(0.5 * 256);
	while (spk_vol_usb < (-6 * 256)) {
		V >>= 1;
		spk_vol_usb += (6 * 256);
	}
	while (spk_vol_usb < (S16)(-0.5 * 256)) {
		V -= V >> 4;
		spk_vol_usb += (S16)(0.5 * 256);
	}

	return V;
}
