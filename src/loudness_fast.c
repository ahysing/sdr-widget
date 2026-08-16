#ifdef FAST
#include "loudness_fast.h"
#include "loudness_internal.h"
#include "loudness_inferred_gain.h"
#include "track_dbfs.h"
#include "compiler.h"
#include <stdint.h>
#include <limits.h>
#include <string.h>
#include <math.h>
#ifdef FREERTOS_USED
#include "FreeRTOS.h"
#include "task.h"
#else
#define taskENTER_CRITICAL()
#define taskEXIT_CRITICAL()
#endif

#if defined(__GNUC__) && defined(__AVR32_HAS_DSP__)
S64 macs_d(S64 d, S32 a, S32 b) {
    __asm__ ("macs.d %0, %1, %2" : "+r"(d) : "r"(a), "r"(b));
    return d;
}
#define FMA_24BIT(A, B, C) macs_d(A, B, C)
#define FMS_24BIT(A, B, C) macs_d(A, B, -C)
#else
#define FMA_24BIT(A, B, C) \
    ((S64)(A) + ((S64)(S32)(B) * (S64)(S32)(C)))
#define FMS_24BIT(A, B, C) \
    ((S64)(A) - ((S64)(S32)(B) * (S64)(S32)(C)))
#endif

#define LOUDNESS_FILTERS 2
#define LOUDNESS_Q29_ONE  ((int32_t)1 << 29)
#define TO_Q29(X)         (((int64_t)(X)) << 29)
#define FROM_Q29(X)       (((X) + (1LL << 28)) >> 29)
#define LOUDNESS_SCALE_Q15_SHIFT       15
#define LOUDNESS_SCALE_Q15_UNITY       (1 << LOUDNESS_SCALE_Q15_SHIFT)

static const biquad_quotients_fast_t
loudness_quotients_44100hz[LOUDNESS_NUM_EQUALIZER_STEPS][LOUDNESS_FILTERS] = {
    /* 55 phon */
    {
        { -1058120971,   521318017,   546321909, -1057989460,   511998531 },
        {   252053234,   153963618,   799542430,   -60155282,   203500616 },
    },
    /* 57 phon */
    {
        { -1057515682,   520717957,   545579974, -1057395344,   512129233 },
        {   239261185,   150864612,   774923158,   -42818076,   194891628 },
    },
    /* 59 phon */
    {
        { -1056896596,   520104439,   544833647, -1056787263,   512251037 },
        {   226582503,   147972775,   750957779,   -26382895,   186851306 },
    },
    /* 61 phon */
    {
        { -1056263714,   519477480,   544083830, -1056165235,   512363040 },
        {   214013914,   145284806,   727644098,   -10821770,   179347305 },
    },
    /* 63 phon */
    {
        { -1055616488,   518836556,   543331163, -1055528717,   512464076 },
        {   201538297,   142794897,   704976111,     3881224,   172346771 },
    },
    /* 65 phon */
    {
        { -1054955125,   518181884,   542576221, -1054877935,   512553764 },
        {   189146447,   140502104,   682945113,    17751753,   165822597 },
    },
    /* 67 phon */
    {
        { -1054279065,   517512934,   541819427, -1054212345,   512631139 },
        {   176828237,   138404296,   661541323,    30815328,   159746794 },
    },
    /* 69 phon */
    {
        { -1053587775,   516829199,   541061120, -1053531429,   512695338 },
        {   164574378,   136499642,   640753771,    43098815,   154092344 },
    },
    /* 71 phon */
    {
        { -1052881207,   516130651,   540301561, -1052835160,   512746048 },
        {   152384444,   134786017,   620571001,    54638272,   148832101 },
    },
    /* 73 phon */
    {
        { -1052159569,   515417511,   539540903, -1052123773,   512783316 },
        {   140236716,   133263286,   600979287,    65448040,   143943588 },
    },
    /* 75 phon */
    {
        { -1051421688,   514688655,   538779235, -1051396110,   512805910 },
        {   128121632,   131931326,   581965494,    75554568,   139403808 },
    },
    /* 77 phon */
    {
        { -1050667734,   513944271,   538016623, -1050652370,   512813925 },
        {   116052714,   130785351,   563516742,    85007166,   135185069 },
    },
    /* 79 phon */
    {
        { -1049896783,   513183476,   537253063, -1049891651,   512806458 },
        {   104012966,   129826638,   545618272,    93823625,   131268618 },
    },
    /* 80 phon */
    {
        { 0, 0, LOUDNESS_Q29_ONE, 0, 0 },
        { 0, 0, LOUDNESS_Q29_ONE, 0, 0 },
    },
};

static const biquad_quotients_fast_t
loudness_quotients_48000hz[LOUDNESS_NUM_EQUALIZER_STEPS][LOUDNESS_FILTERS] = {
    /* 55 phon */
    {
        { -1059375934,   522562441,   545561913, -1059264798,   513982577 },
        {   166644095,   151876494,   835796175,  -213042321,   232637647 },
    },
    /* 57 phon */
    {
        { -1058818558,   522009490,   544879958, -1058716855,   514102148 },
        {   152991121,   149266953,   806964151,  -189105382,   221270217 },
    },
    /* 59 phon */
    {
        { -1058248516,   521444160,   544193957, -1058156108,   514213524 },
        {   139524531,   146884419,   779081484,  -166457595,   210655973 },
    },
    /* 61 phon */
    {
        { -1057665981,   520866634,   543504645, -1057582743,   514316138 },
        {   126230669,   144723212,   752121773,  -145045631,   200748651 },
    },
    /* 63 phon */
    {
        { -1057070078,   520276062,   542812689, -1056995886,   514408476 },
        {   113090453,   142778618,   726056926,  -124822526,   191505583 },
    },
    /* 65 phon */
    {
        { -1056460973,   519672620,   542118584, -1056395721,   514490200 },
        {   100092502,   141046466,   700860133,  -105735895,   182885642 },
    },
    /* 67 phon */
    {
        { -1055838265,   519055931,   541422709, -1055781858,   514560540 },
        {    87212483,   139522363,   676503162,   -87747524,   174850121 },
    },
    /* 69 phon */
    {
        { -1055201833,   518425888,   540725397, -1055154195,   514619041 },
        {    74440352,   138203719,   652959773,   -70807560,   167362770 },
    },
    /* 71 phon */
    {
        { -1054551130,   517781971,   540026861, -1054512198,   514664954 },
        {    61763979,   137087705,   630203865,   -54870011,   160388742 },
    },
    /* 73 phon */
    {
        { -1053886438,   517124473,   539327221, -1053856171,   514698432 },
        {    49173728,   136172434,   608209780,   -39888336,   153895629 },
    },
    /* 75 phon */
    {
        { -1053206852,   516452524,   538626609, -1053185224,   514718455 },
        {    36656356,   135454654,   586952339,   -25821522,   147851104 },
    },
    /* 77 phon */
    {
        { -1052512706,   515766466,   537925054, -1052499714,   514725316 },
        {    24194054,   134935555,   566406548,   -12634990,   142228963 },
    },
    /* 79 phon */
    {
        { -1051801467,   515063847,   537222542, -1051797127,   514716557 },
        {    11811062,   134603921,   546549269,     -254239,   136990865 },
    },
    /* 80 phon */
    {
        { 0, 0, LOUDNESS_Q29_ONE, 0, 0 },
        { 0, 0, LOUDNESS_Q29_ONE, 0, 0 },
    },
};

static biquad_quotients_fast_t loudness_quotients_scaled[LOUDNESS_NUM_EQUALIZER_STEPS][LOUDNESS_FILTERS];
static const biquad_quotients_fast_t (*active_equalizer_step_table)[LOUDNESS_FILTERS] = loudness_quotients_44100hz;
static biquad_quotients_fast_t active_quotients[LOUDNESS_FILTERS];
static biquad_quotients_fast_t staging_quotients[LOUDNESS_FILTERS];
static biquad_state_fast_t     loudness_states[LOUDNESS_FILTERS];

static biquad_quotients_fast_t loudness_scale_quotients_fast(biquad_quotients_fast_t base, uint32_t n, biquad_type_t filter_type);

static int32_t loudness_saturate_s64_to_s32(int64_t value)
{
    if (value > INT32_MAX) {
        return INT32_MAX;
    }
    if (value < INT32_MIN) {
        return INT32_MIN;
    }
    return (int32_t)value;
}

static int32_t loudness_scale_w_q15_s32(int32_t w, int32_t factor_q15)
{
    int64_t scaled = (int64_t)w * (int64_t)factor_q15;
    return loudness_saturate_s64_to_s32(
        scaled >> LOUDNESS_SCALE_Q15_SHIFT);
}

static void loudness_apply_df2_state_scale_fast(int32_t factor_q15)
{
    int i;
    if (factor_q15 == LOUDNESS_SCALE_Q15_UNITY) {
        return;
    }
    for (i = 0; i < LOUDNESS_FILTERS; i++) {
        loudness_states[i].w1 = loudness_scale_w_q15_s32(loudness_states[i].w1, factor_q15);
        loudness_states[i].w2 = loudness_scale_w_q15_s32(loudness_states[i].w2, factor_q15);
    }
}

int32_t biquad_step_fast_24bit(int32_t x_n, biquad_state_fast_t* biquad_states, const biquad_quotients_fast_t* q)
{
    int64_t w_n;
    int64_t y_n_scaled;
    S32 y_n_safe;
    int64_t acc = TO_Q29((int64_t)x_n);
    acc = FMS_24BIT(acc, q->a1, biquad_states->w1);
    acc = FMS_24BIT(acc, q->a2, biquad_states->w2);
    w_n = FROM_Q29(acc);
    acc = (int64_t)q->b0 * w_n;
    acc = FMA_24BIT(acc, q->b1, biquad_states->w1);
    acc = FMA_24BIT(acc, q->b2, biquad_states->w2);
    y_n_scaled = FROM_Q29(acc);
    y_n_safe = saturate_24bit_s64_to_s32(y_n_scaled);
    biquad_states->w2 = biquad_states->w1;
    biquad_states->w1 = loudness_saturate_s64_to_s32(w_n);
    return y_n_safe;
}

int32_t biquad_step_fast_32bit(int32_t x_n, biquad_state_fast_t* biquad_states,
    const biquad_quotients_fast_t* q)
{
    return biquad_step_fast_24bit(x_n, biquad_states, q);
}

/* Unity gain on b* taps; host volume is applied per-sample in the audio task. */
static inline int64_t apply_volume_q29(int64_t coeff)
{
    return coeff;
}

static void loudness_fill_staging_quotients_fast(int equalizer_step)
{
    int i;
    for (i = 0; i < LOUDNESS_FILTERS; i++) {
        const biquad_quotients_fast_t* src = &active_equalizer_step_table[equalizer_step][i];
        int64_t b0 = apply_volume_q29(src->b0);
        int64_t b1 = apply_volume_q29(src->b1);
        int64_t b2 = apply_volume_q29(src->b2);
        staging_quotients[i].b0 = (int32_t)b0;
        staging_quotients[i].b1 = (int32_t)b1;
        staging_quotients[i].b2 = (int32_t)b2;
        staging_quotients[i].a1 = src->a1;
        staging_quotients[i].a2 = src->a2;
    }
}

static void loudness_commit_staging_quotients_fast(void)
{
    memcpy(active_quotients, staging_quotients, sizeof(staging_quotients));
}

static void loudness_load_active_quotients_fast(int equalizer_step)
{
    loudness_fill_staging_quotients_fast(equalizer_step);
    loudness_commit_staging_quotients_fast();
}

#ifdef BUILD_TESTING
void loudness_test_load_active_quotients_fast(int equalizer_step)
{
    loudness_load_active_quotients_fast(equalizer_step);
}
#endif

void loudness_fast_select_equalizer_step(int32_t db_spl, int equalizer_step) {
    int32_t prev_db_spl = (int32_t)last_db_spl;
    int prev_step = loudness_get_equalizer_step(prev_db_spl);
    int32_t factor_q15;
    target_gain_dbfs_q8 = (S16)loudness_clamp_gain_dbfs_q8((int32_t)target_gain_dbfs_q8);
    loudness_fill_staging_quotients_fast(equalizer_step);
    factor_q15 = loudness_combined_step_scale_q15(prev_step, equalizer_step);
    taskENTER_CRITICAL();
    loudness_apply_df2_state_scale_fast(factor_q15);
    loudness_commit_staging_quotients_fast();
    loudness_publish_equalizer_step(db_spl);
    taskEXIT_CRITICAL();
    loudness_report_equalizer_step_switch(prev_db_spl, db_spl, prev_step, equalizer_step);
}

void loudness_fast_reset_states(void)
{
    int i;
    for (i = 0; i < LOUDNESS_FILTERS; i++) {
        loudness_states[i].w1 = 0;
        loudness_states[i].w2 = 0;
    }
}

int64_t loudness_fast_24bit(int32_t sample)
{
    int i;
    for (i = 0; i < LOUDNESS_FILTERS; i++) {
        sample = biquad_step_fast_24bit(sample, &loudness_states[i], &active_quotients[i]);
    }
    return (S64)saturate_24bit_s32_to_s32(sample);
}

S32 loudness_filter_16bit_container(S32 sample)
{
    S32 x = UPSAMPLE_16BIT_TO_FILTER_32(sample);
    S32 y = (S32)loudness_fast_24bit(x);
    return DOWNSAMPLE_FILTER_TO_16BIT_CONTAINER(y);
}

S32 loudness_filter_24bit_container(S32 sample)
{
    /*
     * USB 24-bit samples occupy bits 31:8 of the S32 container.  The FAST
     * biquad itself uses signed 24-bit sample units, so shift down before
     * filtering and restore the container alignment afterwards.
     */
    S32 x = sample >> 8;
    S32 y = (S32)loudness_fast_24bit(x);
    // TODO: condider using
    //  return (S32)(y << 8); // Fjernet (U32) bit-casting for å bevare aritmetisk fortegn!
    return (S32)((U32)y << 8);
}

static biquad_quotients_fast_t loudness_scale_quotients_fast(biquad_quotients_fast_t base, uint32_t n, biquad_type_t filter_type) {
    (void)filter_type;
    if (n <= 1 || n > 4) {
        return base;
    }
    double b0 = (double)base.b0 / (double)LOUDNESS_Q29_ONE;
    double b1 = (double)base.b1 / (double)LOUDNESS_Q29_ONE;
    double b2 = (double)base.b2 / (double)LOUDNESS_Q29_ONE;
    double a1 = (double)base.a1 / (double)LOUDNESS_Q29_ONE;
    double a2 = (double)base.a2 / (double)LOUDNESS_Q29_ONE;
    double cosw = -a1 / (1.0 + a2);
    if (cosw > 1.0) {
        cosw = 1.0;
    } else if (cosw < -1.0) {
        cosw = -1.0;
    }
    double w0 = acos(cosw);
    double k = tan(w0 / (2.0 * n)) / tan(w0 / 2.0);
    double den_s = 1.0 - a1 + a2;
    if (fabs(den_s) < 1e-12) {
        return base;
    }
    double N0 = (b0 + b1 + b2) / den_s;
    double N1 = 2.0 * (b0 - b2) / den_s;
    double N2 = (b0 - b1 + b2) / den_s;
    double D1 = 2.0 * (1.0 - a2) / den_s;
    double D2 = (1.0 + a1 + a2) / den_s;
    double k2 = k * k;
    double N1_new = N1 / k;
    double N2_new = N2 / k2;
    double D1_new = D1 / k;
    double D2_new = D2 / k2;
    double a0_n = D2_new + D1_new + 1.0;
    if (fabs(a0_n) < 1e-12) {
        return base;
    }
    double b0_n = (N2_new + N1_new + N0) / a0_n;
    double b1_n = 2.0 * (N0 - N2_new) / a0_n;
    double b2_n = (N2_new - N1_new + N0) / a0_n;
    double a1_n = 2.0 * (1.0 - D2_new) / a0_n;
    double a2_n = (D2_new - D1_new + 1.0) / a0_n;
    biquad_quotients_fast_t result;
    result.b0 = (int32_t)round(b0_n * (double)LOUDNESS_Q29_ONE);
    result.b1 = (int32_t)round(b1_n * (double)LOUDNESS_Q29_ONE);
    result.b2 = (int32_t)round(b2_n * (double)LOUDNESS_Q29_ONE);
    result.a1 = (int32_t)round(a1_n * (double)LOUDNESS_Q29_ONE);
    result.a2 = (int32_t)round(a2_n * (double)LOUDNESS_Q29_ONE);
    return result;
}

void loudness_change_frequency_fast(uint32_t frequency) {
    if (frequency == 0) {
        return;
    }
    uint32_t n = 1;
    const biquad_quotients_fast_t (*base_table)[LOUDNESS_FILTERS] = loudness_quotients_44100hz;
    if (frequency % 48000 == 0) {
        base_table = loudness_quotients_48000hz;
        n = frequency / 48000;
    } else if (frequency % 44100 == 0) {
        base_table = loudness_quotients_44100hz;
        n = frequency / 44100;
    }
    if (n > 4) {
        n = 4;
    }
    if (n > 1) {
        int b, f;
        for (b = 0; b < LOUDNESS_NUM_EQUALIZER_STEPS; b++) {
            for (f = 0; f < LOUDNESS_FILTERS; f++) {
                loudness_quotients_scaled[b][f] = loudness_scale_quotients_fast(base_table[b][f], n, (biquad_type_t)f);
            }
        }
        base_table = (const biquad_quotients_fast_t (*)[LOUDNESS_FILTERS])(void*)loudness_quotients_scaled;
    }
    int equalizer_step = loudness_get_equalizer_step(loudness_internal_current_db_spl());
    active_equalizer_step_table = base_table;
    loudness_fill_staging_quotients_fast(equalizer_step);
    taskENTER_CRITICAL();
    loudness_commit_staging_quotients_fast();
    taskEXIT_CRITICAL();
    loudness_inferred_gain_set_rate(frequency);
}

#endif /* FAST */
