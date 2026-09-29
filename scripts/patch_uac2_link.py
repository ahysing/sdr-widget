#!/usr/bin/env python3
"""Add LOUDNESS_PROCESS_UAC2_STEREO_PACKET to src/loudness.h if missing (link fix)."""
from pathlib import Path

HDR = Path(__file__).resolve().parents[1] / "src" / "loudness.h"
text = HDR.read_text(encoding="utf-8")
if "LOUDNESS_PROCESS_UAC2_STEREO_PACKET" in text:
    raise SystemExit(0)

enabled_needle = (
    "#define LOUDNESS_FILTER_24BIT_STEREO_PACKET(L, R, N) \\\n"
    "    loudness_filter_24bit_stereo_packet((L), (R), (N))\n"
)
enabled_insert = enabled_needle + (
    "#define LOUDNESS_PROCESS_UAC2_STEREO_PACKET(L, R, N, IS_16BIT) \\\n"
    "    do { \\\n"
    "        if (IS_16BIT) { \\\n"
    "            LOUDNESS_FILTER_16BIT_STEREO_PACKET((L), (R), (N)); \\\n"
    "        } else { \\\n"
    "            LOUDNESS_FILTER_24BIT_STEREO_PACKET((L), (R), (N)); \\\n"
    "        } \\\n"
    "    } while (0)\n\n"
)
if enabled_needle in text:
    text = text.replace(enabled_needle, enabled_insert, 1)
else:
    legacy_needle = (
        "#define LOUDNESS_FILTER_24BIT_CONTAINER(ch, sample_32) \\\n"
        "    loudness_filter_24bit_container((ch), (sample_32))\n"
    )
    legacy_insert = legacy_needle + (
        "#define LOUDNESS_FILTER_24BIT_STEREO_PACKET(L, R, N) \\\n"
        "    do { \\\n"
        "        U16 _i, _n = (U16)(N); \\\n"
        "        for (_i = 0; _i < _n; ++_i) { \\\n"
        "            (L)[_i] = loudness_filter_24bit_container(0, (L)[_i]); \\\n"
        "            (R)[_i] = loudness_filter_24bit_container(1, (R)[_i]); \\\n"
        "        } \\\n"
        "    } while (0)\n"
        "#define LOUDNESS_PROCESS_UAC2_STEREO_PACKET(L, R, N, IS_16BIT) \\\n"
        "    do { \\\n"
        "        if (IS_16BIT) { \\\n"
        "            LOUDNESS_FILTER_16BIT_STEREO_PACKET((L), (R), (N)); \\\n"
        "        } else { \\\n"
        "            LOUDNESS_FILTER_24BIT_STEREO_PACKET((L), (R), (N)); \\\n"
        "        } \\\n"
        "    } while (0)\n\n"
        "Bool loudness_uac2_packet_filter_enabled(Bool not_muted, uint32_t freq_hz);\n\n"
    )
    if legacy_needle not in text:
        raise SystemExit("patch_uac2_link: expected loudness.h layout not found")
    text = text.replace(legacy_needle, legacy_insert, 1)

disabled_stub = (
    "#define LOUDNESS_FILTER_24BIT_STEREO_PACKET(L, R, N) \\\n"
    "    do { (void)(L); (void)(R); (void)(N); } while (0)\n"
    "#endif /* LOUDNESS_DISABLE */"
)
disabled_new = (
    "#define LOUDNESS_FILTER_24BIT_STEREO_PACKET(L, R, N) \\\n"
    "    do { (void)(L); (void)(R); (void)(N); } while (0)\n"
    "#define LOUDNESS_PROCESS_UAC2_STEREO_PACKET(L, R, N, IS_16BIT) \\\n"
    "    do { (void)(L); (void)(R); (void)(N); (void)(IS_16BIT); } while (0)\n"
    "#endif /* LOUDNESS_DISABLE */"
)
if disabled_stub in text:
    text = text.replace(disabled_stub, disabled_new, 1)
else:
    legacy_stub = (
        "#define LOUDNESS_FILTER_24BIT_CONTAINER(ch, sample_32) \\\n"
        "    ((void)(ch), (sample_32))\n"
        "#endif /* LOUDNESS_DISABLE */"
    )
    legacy_stub_new = (
        "#define LOUDNESS_FILTER_24BIT_CONTAINER(ch, sample_32) \\\n"
        "    ((void)(ch), (sample_32))\n"
        "#define LOUDNESS_FILTER_24BIT_STEREO_PACKET(L, R, N) \\\n"
        "    do { (void)(L); (void)(R); (void)(N); } while (0)\n"
        "#define LOUDNESS_PROCESS_UAC2_STEREO_PACKET(L, R, N, IS_16BIT) \\\n"
        "    do { (void)(L); (void)(R); (void)(N); (void)(IS_16BIT); } while (0)\n"
        "#endif /* LOUDNESS_DISABLE */"
    )
    if legacy_stub in text:
        text = text.replace(legacy_stub, legacy_stub_new, 1)

HDR.write_text(text, encoding="utf-8", newline="\n")
