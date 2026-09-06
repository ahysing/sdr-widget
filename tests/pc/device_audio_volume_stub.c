/* PC test stub for device_audio_task volume apply function pointer. */
#include "compiler.h"
#include "device_audio_volume.h"
#include "usb_specific_request.h"
#include "loudness.h"

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

static inline void hard_clip_single(S32 *sample)
{
	if (*sample > INT24_MAX)
		*sample = INT24_MAX;
	if (*sample < INT24_MIN)
		*sample = INT24_MIN;
}

void hard_clip(S32 *sample_L, S32 *sample_R)
{
	hard_clip_single(sample_L);
	hard_clip_single(sample_R);
}

void adjust_volume_hard_clip(S32 *sample_L, S32 *sample_R)
{
	adjust_volume(sample_L, sample_R);
	hard_clip(sample_L, sample_R);
}

void keep_volume(S32 *sample_L, S32 *sample_R)
{
	(void)sample_L;
	(void)sample_R;
}

void device_audio_set_volume_in_biquad(Bool source_has_volume_control, Bool active_filter_enabled)
{
	if (source_has_volume_control) {
		device_audio_volume_apply_fn = active_filter_enabled ? adjust_volume_hard_clip : adjust_volume;
	} else {
		device_audio_volume_apply_fn = active_filter_enabled ? hard_clip : keep_volume;
	}
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
