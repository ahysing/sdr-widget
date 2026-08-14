#ifndef USB_SPECIFIC_REQUEST_H_TEST
#define USB_SPECIFIC_REQUEST_H_TEST

#include "compiler.h"

#define VOL_MIN       (S16)0xC400
#define VOL_INVALID   (S16)(VOL_MIN - 0x0100)
#define VOL_MAX       (S16)0x0000
#define VOL_MULT_UNITY  0x10000000
#define VOL_MULT_SHIFT  28

typedef union {
    U32 frequency;
    U8 freq_bytes[4];
} S_freq;

extern S_freq current_freq;
extern volatile Bool freq_changed;
extern volatile U8 usb_alternate_setting_out;

S32 usb_volume_format(S16 spk_vol_usb);

#endif
