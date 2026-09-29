#!/usr/bin/env python3
"""Add loudness_uac2_packet_filter_enabled() to src/loudness.c when UAC2 hook exists (78644772-era)."""
from pathlib import Path

SRC = Path(__file__).resolve().parents[1] / "src" / "loudness.c"
text = SRC.read_text(encoding="utf-8")
if "loudness_uac2_packet_filter_enabled" in text:
    raise SystemExit(0)

needle = "#endif /* LOUDNESS_DISABLE */\n\nint32_t loudness_apply_noise_shaper_to_output"
insert = """Bool loudness_uac2_packet_filter_enabled(Bool not_muted, uint32_t freq_hz)
{
    if (!not_muted) {
        return FALSE;
    }
    if (freq_hz != (uint32_t)FREQ_44 && freq_hz != (uint32_t)FREQ_48) {
        return FALSE;
    }
    if (!loudness_loudness_is_enabled() && !loudness_bass_boost_allows_contour()) {
        return FALSE;
    }
    return TRUE;
}

#endif /* LOUDNESS_DISABLE */

int32_t loudness_apply_noise_shaper_to_output"""

if needle not in text:
    raise SystemExit("patch_uac2_loudness_c: expected loudness.c layout not found")
SRC.write_text(text.replace(needle, insert, 1), encoding="utf-8", newline="\n")
