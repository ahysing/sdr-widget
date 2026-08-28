/* PC test stub for device_audio_task volume apply function pointer. */
#include "compiler.h"
#include "device_audio_volume.h"
#include "usb_specific_request.h"

S32 spk_vol_mult_L = VOL_MULT_UNITY;
S32 spk_vol_mult_R = VOL_MULT_UNITY;

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
}

void device_audio_volume_update_mult_right(void)
{
}

void device_audio_volume_refresh_mult(void)
{
}
