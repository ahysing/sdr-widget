#include "loudness_fast.h"
#include "loudness_highres.h"
#include "loudness_internal.h"
#include "loudness_inferred_gain.h"
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

#define LOUDNESS_TABLE_SECTIONS 2
#define LOUDNESS_FILTERS 1

/*
 * AVR32 runs one biquad per sample at 48 kHz in the USB audio task. Two sections
 * (low + high shelf) exceeded the CPU budget and caused skip/insert glitches;
 * keep LOUDNESS_FILTERS at 1 until the hot path is faster or rates are lower.
 * ROM tables still hold both shelves (LOUDNESS_TABLE_SECTIONS); only the first
 * section is loaded and stepped at runtime.
 */
#define LOUDNESS_Q29_ONE  ((int32_t)1 << 29)
#define TO_Q29(X)         (((int64_t)(X)) << 29)
#define FROM_Q29(X)       (((X) + (1LL << 28)) >> 29)

/*
 * Canonical DF-II accumulates pole gain before zeros attenuate. At 50 Hz the
 * internal delay line can grow ~5000x larger than x[n] (~13 bits), overflowing
 * int32_t w1/w2 even when input and output stay in range.
 *
 * Fix: store w1/w2 right-shifted by M bits (w' = w >> M). Coefficients and
 * x[n]/y[n] remain full Q29. Feedback lifts x_n to Q29; stored history states
 * are lifted by M before FMA products in both pole and zero paths.
 */
#define LOUDNESS_DF2_STATE_HEADROOM_M   13
#define LOUDNESS_DF2_Q29_SHIFT          29
#define LOUDNESS_DF2_Q29_ROUND          (1LL << (LOUDNESS_DF2_Q29_SHIFT - 1))

#if defined(__GNUC__)
#define LOUDNESS_FAST_INLINE static __attribute__((always_inline)) inline
#else
#define LOUDNESS_FAST_INLINE static inline
#endif

static inline void loudness_fast_memory_barrier(void)
{
#if defined(__GNUC__)
    __asm__ __volatile__("" ::: "memory");
#endif
}

static uint8_t loudness_fast_committed_step = 0xFFu;
static uint32_t loudness_fast_frequency_hz;

static const biquad_quotients_fast_t
loudness_quotients_44100hz[LOUDNESS_NUM_EQUALIZER_STEPS][LOUDNESS_TABLE_SECTIONS] = {
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
loudness_quotients_48000hz[LOUDNESS_NUM_EQUALIZER_STEPS][LOUDNESS_TABLE_SECTIONS] = {
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

static biquad_quotients_fast_t loudness_quotients_scaled[LOUDNESS_NUM_EQUALIZER_STEPS][LOUDNESS_TABLE_SECTIONS];
static const biquad_quotients_fast_t (*active_equalizer_step_table)[LOUDNESS_TABLE_SECTIONS] = loudness_quotients_44100hz;
static biquad_quotients_fast_t staging_quotients[LOUDNESS_FILTERS];
static biquad_runtime_fast_t staging_runtime[LOUDNESS_FILTERS];
static biquad_quotients_fast_t loudness_quotients_bank[2][LOUDNESS_FILTERS];
static biquad_runtime_fast_t loudness_runtime_bank[2][LOUDNESS_FILTERS];
static volatile uint8_t loudness_active_quotients_bank;
static biquad_state_fast_t     loudness_states[LOUDNESS_FILTERS];

static biquad_quotients_fast_t loudness_scale_quotients_fast(biquad_quotients_fast_t base, uint32_t n, biquad_type_t filter_type);

static int loudness_fast_resolve_equalizer_step(int equalizer_step)
{
#ifdef LOUDNESS_FORCE_UNITY_STEP
    (void)equalizer_step;
    return LOUDNESS_NEUTRAL_STEP;
#else
    return equalizer_step;
#endif
}

static const biquad_runtime_fast_t *loudness_fast_active_runtime(void)
{
    return loudness_runtime_bank[loudness_active_quotients_bank & 1u];
}

static void loudness_runtime_from_quotients(const biquad_quotients_fast_t *src,
    biquad_runtime_fast_t *dst)
{
    dst->b0 = src->b0;
    dst->b1 = src->b1;
    dst->b2 = src->b2;
    dst->a1 = src->a1;
    dst->a2 = src->a2;
}

static inline int32_t loudness_saturate_s64_to_s32(int64_t value)
{
    if (value > INT32_MAX) {
        return INT32_MAX;
    }
    if (value < INT32_MIN) {
        return INT32_MIN;
    }
    return (int32_t)value;
}

#if defined(__GNUC__) && defined(__AVR32_HAS_DSP__)
LOUDNESS_FAST_INLINE int32_t loudness_biquad1_step_runtime_inline(int32_t x_n,
    biquad_state_fast_t *st, const biquad_runtime_fast_t *rt)
{
    int64_t acc;
    int64_t fb;
    int64_t w_unscaled;
    int32_t w0;
    int32_t y_n;

    fb = FMS_24BIT(FMS_24BIT(0, rt->a1, st->w1), rt->a2, st->w2);
    acc = ((int64_t)x_n << LOUDNESS_DF2_Q29_SHIFT)
        + (fb << LOUDNESS_DF2_STATE_HEADROOM_M);
    w_unscaled = acc >> LOUDNESS_DF2_Q29_SHIFT;

    fb = FMA_24BIT(FMA_24BIT(0, rt->b1, st->w1), rt->b2, st->w2);
    acc = (int64_t)rt->b0 * w_unscaled + (fb << LOUDNESS_DF2_STATE_HEADROOM_M);
    y_n = saturate_24bit_s64_to_s32((acc + LOUDNESS_DF2_Q29_ROUND) >> LOUDNESS_DF2_Q29_SHIFT);

    w0 = loudness_saturate_s64_to_s32(w_unscaled >> LOUDNESS_DF2_STATE_HEADROOM_M);
    st->w2 = st->w1;
    st->w1 = w0;
    return y_n;
}

int32_t loudness_fast_biquad1_step_runtime(int32_t x_n, biquad_state_fast_t *st,
    const biquad_runtime_fast_t *rt)
{
    return loudness_biquad1_step_runtime_inline(x_n, st, rt);
}

#define LOUDNESS_BIQUAD1_STEP loudness_biquad1_step_runtime_inline
#else
int32_t loudness_fast_biquad1_step_runtime(int32_t x_n,
    biquad_state_fast_t *st, const biquad_runtime_fast_t *rt)
{
    int64_t acc;
    int64_t fb;
    int64_t w_unscaled;
    int32_t w0;
    int32_t y_n;

    fb = FMA_24BIT(FMA_24BIT(0, rt->a1, st->w1), rt->a2, st->w2);
    acc = ((int64_t)x_n << LOUDNESS_DF2_Q29_SHIFT) - (fb << LOUDNESS_DF2_STATE_HEADROOM_M);
    w_unscaled = acc >> LOUDNESS_DF2_Q29_SHIFT;

    fb = FMA_24BIT(FMA_24BIT(0, rt->b1, st->w1), rt->b2, st->w2);
    acc = (int64_t)rt->b0 * w_unscaled + (fb << LOUDNESS_DF2_STATE_HEADROOM_M);
    y_n = saturate_24bit_s64_to_s32(FROM_Q29(acc));

    w0 = loudness_saturate_s64_to_s32(w_unscaled >> LOUDNESS_DF2_STATE_HEADROOM_M);
    st->w2 = st->w1;
    st->w1 = w0;
    return y_n;
}

#define LOUDNESS_BIQUAD1_STEP loudness_fast_biquad1_step_runtime
#endif

static inline int32_t loudness_downsample_filter_to_16bit_container(int32_t y_24)
{
    int32_t s;

    s = y_24 >> 8;
    if (s > INT16_MAX) {
        s = INT16_MAX;
    } else if (s < INT16_MIN) {
        s = INT16_MIN;
    }
    return s << 16;
}

/*
 * At unity (step 13) the biquad is identity on the output, but w1/w2 must still
 * follow the same delay-line update as the full kernel so contour transitions
 * off unity do not start from stale state.
 */
static inline void loudness_biquad1_unity_advance_state_24bit(int32_t x_n,
    biquad_state_fast_t *st)
{
    int32_t w0;

    w0 = loudness_saturate_s64_to_s32((int64_t)x_n >> LOUDNESS_DF2_STATE_HEADROOM_M);
    st->w2 = st->w1;
    st->w1 = w0;
}

LOUDNESS_FAST_INLINE int32_t loudness_biquad1_16bit_container_unity_step(
    int32_t x_container, biquad_state_fast_t *st)
{
    int32_t x_24;

    x_24 = (int32_t)(int16_t)(x_container >> 16) << 8;
    loudness_biquad1_unity_advance_state_24bit(x_24, st);
    return x_container;
}

LOUDNESS_FAST_INLINE int32_t loudness_biquad1_16bit_container_step(int32_t x_container,
    biquad_state_fast_t *st, const biquad_runtime_fast_t *rt)
{
    int32_t x_24;
    int32_t y_24;

    x_24 = (int32_t)(int16_t)(x_container >> 16) << 8;
    y_24 = LOUDNESS_BIQUAD1_STEP(x_24, st, rt);
    return loudness_downsample_filter_to_16bit_container(y_24);
}

void loudness_test_filter_16bit_stereo_packet_hires_fullrate(S32 *sample_L,
    S32 *sample_R, U16 num_samples)
{
    loudness_highres_test_filter_16bit_stereo_packet_fullrate(sample_L, sample_R,
        num_samples, &loudness_states[0], loudness_fast_active_runtime());
}

int32_t biquad_step_fast_24bit(int32_t x_n, biquad_state_fast_t* biquad_states,
    const biquad_quotients_fast_t* q)
{
    biquad_runtime_fast_t rt;

    loudness_runtime_from_quotients(q, &rt);
    return LOUDNESS_BIQUAD1_STEP(x_n, biquad_states, &rt);
}

int32_t biquad_step_fast_32bit(int32_t x_n, biquad_state_fast_t* biquad_states,
    const biquad_quotients_fast_t* q)
{
    return biquad_step_fast_24bit(x_n, biquad_states, q);
}

static void loudness_fill_staging_quotients_fast(int equalizer_step)
{
    int i;
    const biquad_quotients_fast_t *hires_halfrate_src;

    hires_halfrate_src = loudness_highres_halfrate_quotients(
        loudness_fast_frequency_hz, equalizer_step,
        loudness_quotients_44100hz, loudness_quotients_48000hz);

    for (i = 0; i < LOUDNESS_FILTERS; i++) {
        const biquad_quotients_fast_t* src = &active_equalizer_step_table[equalizer_step][i];
        int64_t b0 = src->b0;
        int64_t b1 = src->b1;
        int64_t b2 = src->b2;
        staging_quotients[i].b0 = (int32_t)b0;
        staging_quotients[i].b1 = (int32_t)b1;
        staging_quotients[i].b2 = (int32_t)b2;
        staging_quotients[i].a1 = src->a1;
        staging_quotients[i].a2 = src->a2;
        loudness_runtime_from_quotients(&staging_quotients[i], &staging_runtime[i]);
    }
    loudness_highres_staging_fill_halfrate(hires_halfrate_src, staging_runtime);
}

static void loudness_commit_staging_quotients_fast(void)
{
    uint8_t inactive = (uint8_t)(loudness_active_quotients_bank ^ 1u);

    memcpy(loudness_quotients_bank[inactive], staging_quotients,
        sizeof(staging_quotients));
    memcpy(loudness_runtime_bank[inactive], staging_runtime,
        sizeof(staging_runtime));
    loudness_highres_staging_commit();
    loudness_fast_memory_barrier();
    loudness_active_quotients_bank = inactive;
}

#ifdef BUILD_TESTING
static const biquad_quotients_fast_t *loudness_fast_active_quotients(void)
{
    return loudness_quotients_bank[loudness_active_quotients_bank & 1u];
}

void loudness_test_load_active_quotients_fast(int equalizer_step)
{
    equalizer_step = loudness_fast_resolve_equalizer_step(equalizer_step);
    loudness_fill_staging_quotients_fast(equalizer_step);
    taskENTER_CRITICAL();
    loudness_commit_staging_quotients_fast();
    taskEXIT_CRITICAL();
    loudness_fast_committed_step = (uint8_t)equalizer_step;
}

void loudness_test_get_fast_section(int section,
    biquad_state_fast_t *state, biquad_quotients_fast_t *quotients)
{
    taskENTER_CRITICAL();
    *state = loudness_states[section];
    if (quotients != NULL) {
        *quotients = loudness_fast_active_quotients()[section];
    }
    taskEXIT_CRITICAL();
}

void loudness_test_set_fast_section(int section,
    const biquad_state_fast_t *state)
{
    taskENTER_CRITICAL();
    loudness_states[section] = *state;
    taskEXIT_CRITICAL();
}

#endif

void loudness_fast_select_equalizer_step(int32_t db_spl, int equalizer_step) {
    int32_t prev_db_spl = (int32_t)last_db_spl;
    int prev_step = loudness_get_equalizer_step(prev_db_spl);
    int resolved_step = loudness_fast_resolve_equalizer_step(equalizer_step);

    target_gain_dbfs_q8 = (S16)loudness_clamp_gain_dbfs_q8(
        (int32_t)target_gain_dbfs_q8);

    if (resolved_step == (int)loudness_fast_committed_step) {
        loudness_publish_equalizer_step(db_spl);
        return;
    }

    loudness_fill_staging_quotients_fast(resolved_step);
    loudness_commit_staging_quotients_fast();
    loudness_fast_committed_step = (uint8_t)resolved_step;
    loudness_publish_equalizer_step(db_spl);

    loudness_report_equalizer_step_switch(prev_db_spl, db_spl, prev_step,
        resolved_step);
}

void loudness_fast_reset_states(void)
{
    taskENTER_CRITICAL();
    memset(loudness_states, 0, sizeof(loudness_states));
    taskEXIT_CRITICAL();
}

int32_t loudness_fast_24bit(int32_t sample)
{
    const biquad_runtime_fast_t *runtime;

    if (loudness_fast_is_unity_step()) {
        loudness_biquad1_unity_advance_state_24bit(sample, &loudness_states[0]);
        return saturate_24bit_s32_to_s32(sample);
    }

    runtime = loudness_fast_active_runtime();
    sample = LOUDNESS_BIQUAD1_STEP(sample, &loudness_states[0], &runtime[0]);
    return saturate_24bit_s32_to_s32(sample);
}

S32 loudness_filter_16bit_container(S32 sample)
{
    if (loudness_fast_is_unity_step()) {
        return loudness_biquad1_16bit_container_unity_step(sample,
            &loudness_states[0]);
    }

    return loudness_biquad1_16bit_container_step(sample, &loudness_states[0],
        loudness_fast_active_runtime());
}

Bool loudness_fast_is_unity_step(void)
{
    return loudness_fast_committed_step == (uint8_t)LOUDNESS_NEUTRAL_STEP;
}

void loudness_filter_16bit_stereo_packet(S32 *sample_L, S32 *sample_R, U16 num_samples)
{
    const biquad_runtime_fast_t *runtime;
    biquad_state_fast_t *st;
    U16 i;

    st = &loudness_states[0];

    if (loudness_fast_is_unity_step()) {
        for (i = 0; i < num_samples; i++) {
            int32_t xL = (int32_t)(int16_t)(sample_L[i] >> 16) << 8;
            int32_t xR = (int32_t)(int16_t)(sample_R[i] >> 16) << 8;

            loudness_biquad1_unity_advance_state_24bit(xL, st);
            loudness_biquad1_unity_advance_state_24bit(xR, st);
        }
        return;
    }

    runtime = loudness_fast_active_runtime();

    if (loudness_highres_applies(loudness_fast_frequency_hz)) {
        loudness_highres_filter_16bit_stereo_packet(sample_L, sample_R,
            num_samples, st);
        return;
    }

    for (i = 0; i < num_samples; i++) {
        sample_L[i] = loudness_biquad1_16bit_container_step(sample_L[i], st,
            runtime);
        sample_R[i] = loudness_biquad1_16bit_container_step(sample_R[i], st,
            runtime);
    }
}

S32 loudness_filter_24bit_container(S32 sample)
{
    /*
     * USB 24-bit samples occupy bits 31:8 of the S32 container.  The FAST
     * biquad itself uses signed 24-bit sample units, so shift down before
     * filtering and restore the container alignment afterwards.
     */
    S32 x = sample >> 8;
    S32 y = loudness_fast_24bit(x);
    return y << 8;
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
    loudness_fast_frequency_hz = frequency;
    uint32_t n = 1;
    const biquad_quotients_fast_t (*base_table)[LOUDNESS_TABLE_SECTIONS] = loudness_quotients_44100hz;
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
            for (f = 0; f < LOUDNESS_TABLE_SECTIONS; f++) {
                loudness_quotients_scaled[b][f] = loudness_scale_quotients_fast(base_table[b][f], n, (biquad_type_t)f);
            }
        }
        base_table = (const biquad_quotients_fast_t (*)[LOUDNESS_TABLE_SECTIONS])(void*)loudness_quotients_scaled;
    }
    int equalizer_step = loudness_fast_resolve_equalizer_step(
        loudness_get_equalizer_step(loudness_internal_current_db_spl()));

    active_equalizer_step_table = base_table;
    loudness_fill_staging_quotients_fast(equalizer_step);
    loudness_commit_staging_quotients_fast();
    loudness_fast_committed_step = (uint8_t)equalizer_step;
    loudness_inferred_gain_set_rate(frequency);
}

Bool loudness_filter_is_active() {
    int i;
    Bool filter_is_active = FALSE;
    for (i = 0; i < LOUDNESS_FILTERS; i++) {
        filter_is_active = filter_is_active || (loudness_states[i].w1 != 0);
        filter_is_active = filter_is_active || (loudness_states[i].w2 != 0);
    }
    return filter_is_active;
}

