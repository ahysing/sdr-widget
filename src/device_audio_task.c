//* -*- mode: c++; tab-width: 4; c-basic-offset: 4 -*- */
#include "compiler.h"
#include "device_audio_task.h"
#include "usb_specific_request.h"

//!
//! Public : (bit) mute
//! mute is set to TRUE when ACTIVE
//! mute is set to FALSE otherwise
//!/
volatile Bool mute, spk_mute;	// These variables are written to extensively but not heeded in playback
volatile uint8_t usb_spk_mute = 0; // This variable is written to by usb subsystem and heeded in playback

volatile S32 FB_rate, FB_rate_initial, FB_rate_nominal; // BSB 20131031 FB_rate_initial and FB_rate_nominal added and changed to S32
// With working volume flash:
// S16 spk_vol_usb_L = VOL_INVALID;			// BSB 20160320 Added stereo volume control
// S16 spk_vol_usb_R = VOL_INVALID;			// Not yet initialized from flash

// Without working volume flash;
S16 spk_vol_usb_L = VOL_DEFAULT;			// BSB 20160320 Added stereo volume control
S16 spk_vol_usb_R = VOL_DEFAULT;			// Forced to default value


S32 spk_vol_mult_L = 0;						// Full mute for now, re-formated in uac?_device_audio_task_init
S32 spk_vol_mult_R = 0;

volatile uint8_t input_select;							// BSB 20150501 global variable for input selector

#ifdef FEATURE_VOLUME_CTRL
static S16 spk_vol_formatted_L = VOL_INVALID;
static S16 spk_vol_formatted_R = VOL_INVALID;

device_audio_volume_apply_fn_t device_audio_volume_apply_fn = adjust_volume;

void adjust_volume(S32 *sample_L, S32 *sample_R)
{
	if (spk_vol_mult_L != VOL_MULT_UNITY) {
		*sample_L = (S32)((int64_t)(*sample_L) * (int64_t)spk_vol_mult_L
			>> VOL_MULT_SHIFT);
	}
	if (spk_vol_mult_R != VOL_MULT_UNITY) {
		*sample_R = (S32)((int64_t)(*sample_R) * (int64_t)spk_vol_mult_R
			>> VOL_MULT_SHIFT);
	}
}

void keep_volume(S32 *sample_L, S32 *sample_R)
{
	(void)sample_L;
	(void)sample_R;
}

void device_audio_set_volume_in_biquad(Bool volume_in_biquad)
{
	device_audio_volume_apply_fn = volume_in_biquad
		? keep_volume : adjust_volume;
}

void device_audio_volume_update_mult_left(void)
{
	if (spk_vol_usb_L != spk_vol_formatted_L) {
		spk_vol_mult_L = usb_volume_format(spk_vol_usb_L);
		spk_vol_formatted_L = spk_vol_usb_L;
	}
}

void device_audio_volume_update_mult_right(void)
{
	if (spk_vol_usb_R != spk_vol_formatted_R) {
		spk_vol_mult_R = usb_volume_format(spk_vol_usb_R);
		spk_vol_formatted_R = spk_vol_usb_R;
	}
}

void device_audio_volume_refresh_mult(void)
{
	device_audio_volume_update_mult_left();
	device_audio_volume_update_mult_right();
}
#endif

#ifdef HW_GEN_DIN20
volatile uint8_t usb_ch;					// Front or rear USB channel
volatile uint8_t usb_ch_swap;				// USB channel is about to swap!
#endif

#if (defined HW_GEN_DIN10) || (defined HW_GEN_DIN20)
volatile xSemaphoreHandle input_select_semphr = NULL; // BSB 20150626 audio channel selection semaphore
#endif

