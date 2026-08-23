"""Apply 14-step LUT firmware logic changes to src/loudness.c."""
from pathlib import Path
import re

LOUDNESS_C = Path(__file__).resolve().parent.parent / "src" / "loudness.c"

NEW_STEP_SECTION = r'''/* Apply the speaker volume to the feed-forward (b*) coefficients in Q61. */
static inline S64 apply_volume_q61(S64 coeff)
{
#if defined(_MSC_VER)
    return (S64)round((double)coeff * (SPEAKER_VOLUME_Q61 / (double)LOUDNESS_Q61_ONE));
#elif defined(HAS_INT128)
    __int128 result = (__int128)coeff * SPEAKER_VOLUME_Q61;
    return (S64)(result >> 61);
#else
    return mul_shift_q61(coeff, SPEAKER_VOLUME_Q61);
#endif
}

static int loudness_get_equalizer_step(int32_t db_spl) {
    if (db_spl >= LOUDNESS_DB_SPL_MAX) {
        return LOUDNESS_NEUTRAL_STEP;
    }
    if (db_spl < LOUDNESS_MIN_PHON) {
        return 0;
    }
    {
        int step = (db_spl - LOUDNESS_MIN_PHON) / LOUDNESS_PHON_STEP_DB;
        if (step >= LOUDNESS_CONTOUR_STEPS) {
            step = LOUDNESS_CONTOUR_STEPS - 1;
        }
        return step;
    }
}

#ifdef BUILD_TESTING
int loudness_test_get_equalizer_step(int32_t db_spl) {
    return loudness_get_equalizer_step(db_spl);
}
#endif

static int loudness_get_current_equalizer_step(void) {
    return loudness_get_equalizer_step((int32_t)last_db_spl);
}

static Bool loudness_should_change_equalizer_step(int32_t db_spl_x10) {
#ifndef BUILD_TESTING
    int current = loudness_get_current_equalizer_step();

    if (current == LOUDNESS_NEUTRAL_STEP) {
        return db_spl_x10 < 795;
    }

    if (current == (LOUDNESS_CONTOUR_STEPS - 1)) {
        if (db_spl_x10 >= 800) {
            return TRUE;
        }
        {
            int band_start_phon = LOUDNESS_MIN_PHON + current * LOUDNESS_PHON_STEP_DB;
            int32_t lower_x10 = band_start_phon * 10 - 5;
            return db_spl_x10 < lower_x10;
        }
    }

    {
        int band_start_phon = LOUDNESS_MIN_PHON + current * LOUDNESS_PHON_STEP_DB;
        int32_t lower_x10 = band_start_phon * 10 - 5;
        int next_phon = band_start_phon + LOUDNESS_PHON_STEP_DB;
        int32_t upper_x10 = next_phon * 10;

        if (db_spl_x10 >= upper_x10) {
            return TRUE;
        }
        if (db_spl_x10 < lower_x10) {
            return TRUE;
        }
        return FALSE;
    }
#else
    (void)db_spl_x10;
    return TRUE;
#endif
}
'''

SELECT_FAST = r'''static void loudness_select_equalizer_step_fast(int32_t db_spl, int equalizer_step) {
    int32_t prev_db_spl = (int32_t)last_db_spl;
    loudness_set_target_from_equalizer_step_fast(equalizer_step);

    taskENTER_CRITICAL();
    last_db_spl = (int16_t)db_spl;
#ifdef FREERTOS_USED
    target_db_spl = (int16_t)db_spl;
#endif
    loudness_commit_coefficients_fast();
    taskEXIT_CRITICAL();
#if !defined(USBSTATISTICS_DISABLE)
    loudness_record_equalizer_step_switch_event(prev_db_spl, db_spl, equalizer_step);
    stats_telemetry_set_equalizer_state(loudness_clamp_s8(db_spl), (U8)equalizer_step);
#endif
}
'''

SELECT_WRAPPER = r'''static void loudness_select_equalizer_step(int32_t db_spl) {
    int equalizer_step = loudness_get_equalizer_step(db_spl);
    loudness_select_equalizer_step_fast(db_spl, equalizer_step);
}
'''

UPDATE_ACTIVE = r'''static int32_t loudness_calculate_db_spl_x10(void) {
    int32_t track_dbfs = loudness_get_track_dbfs();
    int32_t track_normalized = track_dbfs - LOUDNESS_TRACK_DBFS_MIN;
    int32_t host_gain_dbfs = loudness_get_gain_dbfs();
    int32_t host_gain_scaled = host_gain_dbfs * 10;
    int32_t track_scaled = track_normalized * 10;

    int32_t blended_scaled_x100 = (host_gain_scaled * LOUDNESS_GAIN_WEIGHT_PCT) +
                                  (track_scaled * LOUDNESS_TRACK_WEIGHT_PCT);

    int32_t blended_dbfs_x10 = (blended_scaled_x100 - 50) / 100;
    return blended_dbfs_x10 + (LOUDNESS_DB_SPL_MAX * 10);
}

static int32_t loudness_calculate_db_spl(void) {
    int32_t db_spl_x10 = loudness_calculate_db_spl_x10();
    return (db_spl_x10 + 5) / 10;
}

void loudness_update_active_equalizer_step(void) {
    int32_t db_spl_x10 = loudness_calculate_db_spl_x10();
    int32_t db_spl = (db_spl_x10 + 5) / 10;
    Bool db_spl_changed;

#ifdef FREERTOS_USED
    target_db_spl = (int16_t)db_spl;
#endif

#ifdef BUILD_TESTING
    db_spl_changed = (db_spl != (int32_t)last_db_spl);
#else
    db_spl_changed = loudness_should_change_equalizer_step(db_spl_x10);
#endif

    if (db_spl_changed) {
        loudness_select_equalizer_step(db_spl);
    }
#if !defined(USBSTATISTICS_DISABLE)
    {
        int32_t track_rms_dbfs = loudness_get_track_rms_dbfs();
        int32_t track_dbfs = loudness_get_track_dbfs();

        stats_telemetry_set_track_levels(
            loudness_clamp_s8(track_dbfs),
            loudness_clamp_s8(track_rms_dbfs));
        stats_telemetry_set_gain_dbfs(
            loudness_clamp_s8(loudness_get_gain_dbfs()));
    }
#endif
}
'''

FREQ_COMMIT = r'''    taskENTER_CRITICAL();
    loudness_commit_coefficients_precise();
    taskEXIT_CRITICAL();
'''


def main():
    text = LOUDNESS_C.read_text(encoding="utf-8")

    text = text.replace(
        "/* Coefficient de-zip ramp duration (ms). ~15 ms => 662 samples @ 44.1 kHz, 2880 @ 192 kHz. */\n"
        "#define LOUDNESS_COEFF_RAMP_MS 15\n\n",
        "",
    )
    text = text.replace(
        " * Layout per filter row: { b0, b1, b2, a1, a2 } in Q30 for PRECISE,\n"
        " * and Q2.30 for FAST (32-bit signed).\n",
        " * Layout per filter row: { a1, a2, b0, b1, b2 } (a0 normalized to 1, not stored).\n",
    )

    text = re.sub(
        r"#ifdef BUILD_TESTING\n/\* Samples remaining.*?\n#endif\n\n",
        "",
        text,
        count=1,
        flags=re.DOTALL,
    )

    text = text.replace(
        "static biquad_quotients_precise_t coeff_buf_ramp_blend[LOUDNESS_FILTERS];\n\n",
        "",
    )
    text = text.replace(
        "static biquad_quotients_fast_t coeff_buf_ramp_blend[LOUDNESS_FILTERS];\n\n",
        "",
    )

    text = re.sub(
        r"/\* ---------------------------------------------------------------------------\n"
        r" \* Equalizer-step selection \+ coefficient de-zipping \(interpolation\)\n"
        r" \* -------------------------------------------------------------------------*/\n\n"
        r"#define SPEAKER_VOLUME_Q61.*?"
        r"static int loudness_get_equalizer_step\(int32_t db_spl\) \{.*?\n\}\n",
        "/* ---------------------------------------------------------------------------\n"
        " * Equalizer-step selection (immediate coefficient commit)\n"
        " * -------------------------------------------------------------------------*/\n\n"
        "#define SPEAKER_VOLUME_Q61  ((S64)1 << 61)\n\n"
        + NEW_STEP_SECTION
        + "\n",
        text,
        count=1,
        flags=re.DOTALL,
    )

    text = re.sub(
        r"void loudness_coeff_ramp_step\(void\) \{.*?\n\}\n\n",
        "",
        text,
        count=1,
        flags=re.DOTALL,
    )

    text = text.replace("        target_quotients[i].a0 = src->a0;\n", "")
    text = text.replace("        coeff_buf_endpoint_ping[i].a0 = 0;\n", "")
    text = text.replace("        coeff_buf_endpoint_pong[i].a0 = 0;\n", "")
    text = re.sub(
        r"        coeff_buf_ramp_blend\[i\]\.b0 = 0;\n"
        r"        coeff_buf_ramp_blend\[i\]\.b1 = 0;\n"
        r"        coeff_buf_ramp_blend\[i\]\.b2 = 0;\n"
        r"        coeff_buf_ramp_blend\[i\]\.a0 = 0;\n"
        r"        coeff_buf_ramp_blend\[i\]\.a1 = 0;\n"
        r"        coeff_buf_ramp_blend\[i\]\.a2 = 0;\n",
        "",
        text,
    )
    text = text.replace("    result.a0 = (int64_t)LOUDNESS_Q61_ONE;\n", "")
    text = text.replace("    result.a0 = (int32_t)536870912LL;\n", "")

    text = re.sub(
        r"static void loudness_select_equalizer_step_fast\(int32_t db_spl, int equalizer_step, Bool immediate\) \{.*?\n\}\n#endif",
        SELECT_FAST + "#endif",
        text,
        count=1,
        flags=re.DOTALL,
    )
    text = re.sub(
        r"static void loudness_select_equalizer_step\(int32_t db_spl, Bool immediate\) \{.*?\n\}\n",
        SELECT_WRAPPER + "\n",
        text,
        count=1,
        flags=re.DOTALL,
    )

    text = re.sub(
        r"/\*\*\n \* @brief Compute blended listening level in dB SPL \(phon estimate\)\..*?"
        r"void loudness_update_active_equalizer_step\(void\) \{.*?\n\}\n",
        UPDATE_ACTIVE,
        text,
        count=1,
        flags=re.DOTALL,
    )

    text = text.replace(
        "    loudness_select_equalizer_step((int32_t)LOUDNESS_REF_PHON, TRUE);",
        "    loudness_select_equalizer_step((int32_t)LOUDNESS_REF_PHON);",
    )

    text = re.sub(
        r"    taskENTER_CRITICAL\(\);\n    \{\n        uint32_t ramp = loudness_coeff_ramp_length\(\);\n"
        r"        if \(ramp == 0\) \{\n            loudness_commit_coefficients_precise\(\);\n"
        r"            coeff_ramp_remaining = 0;\n        \} else \{\n            coeff_ramp_remaining = ramp;\n        \}\n    \}\n    taskEXIT_CRITICAL\(\);",
        FREQ_COMMIT,
        text,
    )
    text = re.sub(
        r"    taskENTER_CRITICAL\(\);\n    \{\n        uint32_t ramp = loudness_coeff_ramp_length\(\);\n"
        r"        if \(ramp == 0\) \{\n            loudness_commit_coefficients_fast\(\);\n"
        r"            coeff_ramp_remaining = 0;\n        \} else \{\n            coeff_ramp_remaining = ramp;\n        \}\n    \}\n    taskEXIT_CRITICAL\(\);",
        "    taskENTER_CRITICAL();\n    loudness_commit_coefficients_fast();\n    taskEXIT_CRITICAL();",
        text,
    )

    text = text.replace(
        "    // Recalculate target quotients for the current equalizer step and ramp\n",
        "    // Recalculate coefficients for the current equalizer step (immediate commit)\n",
    )

    if "loudness_coeff_ramp_step" in text:
        raise SystemExit("loudness_coeff_ramp_step still present")
    if ".a0" in text:
        raise SystemExit(".a0 references still present")

    biquad_comment = (
        "    /* a0 is normalized to 1 (Q61/Q29 unity); not stored in biquad_quotients_*_t. */\n"
        "    /* y_n += q->a0 * x_n; */"
    )
    text = text.replace(
        "    const biquad_quotients_precise_t* q = &loudness_quotients[filter_idx];\n\n#if defined(_MSC_VER)",
        "    const biquad_quotients_precise_t* q = &loudness_quotients[filter_idx];\n\n"
        + biquad_comment
        + "\n\n#if defined(_MSC_VER)",
    )
    text = text.replace(
        "    const biquad_quotients_fast_t* q = &loudness_quotients[filter_idx];\n\n    int64_t y_n",
        "    const biquad_quotients_fast_t* q = &loudness_quotients[filter_idx];\n\n"
        + biquad_comment
        + "\n\n    int64_t y_n",
    )

    LOUDNESS_C.write_text(text, encoding="utf-8")
    print("Patched loudness.c logic")


if __name__ == "__main__":
    main()
