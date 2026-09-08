#include "loudness_fast.h"
#include "loudness_first_order.h"
#include "loudness_internal.h"
#include "loudness_inferred_gain.h"
#include "taskAK5394A.h"
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

#define LOUDNESS_EQUALIZER_STEP_UNSET   UINT8_MAX
#define REFERENCE_LEVEL_80PHON_IDX      80
#define LOUDNESS_EQUALIZER_STEP_INITIAL REFERENCE_LEVEL_80PHON_IDX
static inline void loudness_fast_memory_barrier(void)
{
#if defined(__GNUC__) || defined(__clang__)
    __asm__ __volatile__("" ::: "memory");
#elif defined(_MSC_VER)
    _ReadWriteBarrier();
#else
#warning "Unsupported compiler! loudness_fast_memory_barrier()"
#endif
}

#define LOUDNESS_CHANNELS     2
/* The equilizers contains the same curve with different gain.
   By changing the index in Look Up Table(s) we change the step and increase the gain.
   Because large instant gain changes are not desired we split them up over the ierations
   in the FreeRTOS task "LOUDNESS". every 20 ms we move the equilizer steps towards the
   desired end step. During transitions say the steps are commited, but not yet desired.
   Active biquad coefficients are swapped in on every step.
*/
static uint8_t committed_equalizer_step[LOUDNESS_CHANNELS] = {
    LOUDNESS_EQUALIZER_STEP_INITIAL, LOUDNESS_EQUALIZER_STEP_INITIAL
};
static uint32_t loudness_filter_frequency_hz;

/* Deferred dominant-channel step-switch report while committed catches up. */
static int loudness_fast_switch_report_from_step = -1;
static int32_t loudness_fast_switch_report_from_db_spl_x10;
static int32_t loudness_fast_switch_report_to_db_spl_x10;
static int loudness_fast_switch_report_to_step;

typedef uint8_t loudness_idle_mask_t;
enum {
    LOUDNESS_LOWSHELF_LEFT = 1u << 0,
    LOUDNESS_LOWSHELF_RIGHT = 1u << 1,
    LOUDNESS_HIGHSHELF_LEFT = 1u << 2,
    LOUDNESS_HIGHSHELF_RIGHT = 1u << 3,
    LOUDNESS_FILTER_ALL = (LOUDNESS_LOWSHELF_LEFT | LOUDNESS_LOWSHELF_RIGHT |
                        LOUDNESS_HIGHSHELF_LEFT | LOUDNESS_HIGHSHELF_RIGHT)
};
loudness_idle_mask_t filter_idle_cached = LOUDNESS_FILTER_ALL;

static const biquad_quotients_fast_t
lowshelf_and_volume_44100hz[LOUDNESS_NUM_EQUALIZER_STEPS] = {
    {  -267671632,      279028,     -257079 },  /* phon=25.0 volume=-60.0 dB fs=44100 Hz biquad * volume */
    {  -267671632,      295428,     -272445 },  /* phon=25.5 volume=-59.5 dB fs=44100 Hz biquad * volume */
    {  -267671632,      312791,     -288732 },  /* phon=26.0 volume=-59.0 dB fs=44100 Hz biquad * volume */
    {  -267671632,      331171,     -305993 },  /* phon=26.5 volume=-58.5 dB fs=44100 Hz biquad * volume */
    {  -267671632,      350630,     -324289 },  /* phon=27.0 volume=-58.0 dB fs=44100 Hz biquad * volume */
    {  -267671632,      371230,     -343680 },  /* phon=27.5 volume=-57.5 dB fs=44100 Hz biquad * volume */
    {  -267671632,      393039,     -364232 },  /* phon=28.0 volume=-57.0 dB fs=44100 Hz biquad * volume */
    {  -267671632,      416127,     -386015 },  /* phon=28.5 volume=-56.5 dB fs=44100 Hz biquad * volume */
    {  -267671632,      440570,     -409102 },  /* phon=29.0 volume=-56.0 dB fs=44100 Hz biquad * volume */
    {  -267671632,      466447,     -433571 },  /* phon=29.5 volume=-55.5 dB fs=44100 Hz biquad * volume */
    {  -267671632,      493843,     -459505 },  /* phon=30.0 volume=-55.0 dB fs=44100 Hz biquad * volume */
    {  -267671632,      522846,     -486992 },  /* phon=30.5 volume=-54.5 dB fs=44100 Hz biquad * volume */
    {  -267671632,      553551,     -516123 },  /* phon=31.0 volume=-54.0 dB fs=44100 Hz biquad * volume */
    {  -267671632,      586059,     -546997 },  /* phon=31.5 volume=-53.5 dB fs=44100 Hz biquad * volume */
    {  -267654950,      620512,     -579645 },  /* phon=32.0 volume=-53.0 dB fs=44100 Hz biquad * volume */
    {  -267618465,      657038,     -614146 },  /* phon=32.5 volume=-52.5 dB fs=44100 Hz biquad * volume */
    {  -267582151,      695714,     -650702 },  /* phon=33.0 volume=-52.0 dB fs=44100 Hz biquad * volume */
    {  -267545942,      736666,     -689433 },  /* phon=33.5 volume=-51.5 dB fs=44100 Hz biquad * volume */
    {  -267509784,      780028,     -730471 },  /* phon=34.0 volume=-51.0 dB fs=44100 Hz biquad * volume */
    {  -267473629,      825943,     -773951 },  /* phon=34.5 volume=-50.5 dB fs=44100 Hz biquad * volume */
    {  -267437435,      874560,     -820019 },  /* phon=35.0 volume=-50.0 dB fs=44100 Hz biquad * volume */
    {  -267401164,      926039,     -868828 },  /* phon=35.5 volume=-49.5 dB fs=44100 Hz biquad * volume */
    {  -267364783,      980549,     -920542 },  /* phon=36.0 volume=-49.0 dB fs=44100 Hz biquad * volume */
    {  -267328264,     1038267,     -975333 },  /* phon=36.5 volume=-48.5 dB fs=44100 Hz biquad * volume */
    {  -267291579,     1099383,    -1033384 },  /* phon=37.0 volume=-48.0 dB fs=44100 Hz biquad * volume */
    {  -267254704,     1164098,    -1094889 },  /* phon=37.5 volume=-47.5 dB fs=44100 Hz biquad * volume */
    {  -267217619,     1232622,    -1160053 },  /* phon=38.0 volume=-47.0 dB fs=44100 Hz biquad * volume */
    {  -267180302,     1305180,    -1229092 },  /* phon=38.5 volume=-46.5 dB fs=44100 Hz biquad * volume */
    {  -267142735,     1382011,    -1302239 },  /* phon=39.0 volume=-46.0 dB fs=44100 Hz biquad * volume */
    {  -267104904,     1463365,    -1379735 },  /* phon=39.5 volume=-45.5 dB fs=44100 Hz biquad * volume */
    {  -267066788,     1549509,    -1461841 },  /* phon=40.0 volume=-45.0 dB fs=44100 Hz biquad * volume */
    {  -267028377,     1640726,    -1548829 },  /* phon=40.5 volume=-44.5 dB fs=44100 Hz biquad * volume */
    {  -266989657,     1737314,    -1640990 },  /* phon=41.0 volume=-44.0 dB fs=44100 Hz biquad * volume */
    {  -266950617,     1839590,    -1738630 },  /* phon=41.5 volume=-43.5 dB fs=44100 Hz biquad * volume */
    {  -266911241,     1947888,    -1842076 },  /* phon=42.0 volume=-43.0 dB fs=44100 Hz biquad * volume */
    {  -266871519,     2062564,    -1951672 },  /* phon=42.5 volume=-42.5 dB fs=44100 Hz biquad * volume */
    {  -266831441,     2183993,    -2067783 },  /* phon=43.0 volume=-42.0 dB fs=44100 Hz biquad * volume */
    {  -266790998,     2312574,    -2190795 },  /* phon=43.5 volume=-41.5 dB fs=44100 Hz biquad * volume */
    {  -266750175,     2448727,    -2321120 },  /* phon=44.0 volume=-41.0 dB fs=44100 Hz biquad * volume */
    {  -266708982,     2592899,    -2459190 },  /* phon=44.5 volume=-40.5 dB fs=44100 Hz biquad * volume */
    {  -266667395,     2745563,    -2605466 },  /* phon=45.0 volume=-40.0 dB fs=44100 Hz biquad * volume */
    {  -266625390,     2907218,    -2760434 },  /* phon=45.5 volume=-39.5 dB fs=44100 Hz biquad * volume */
    {  -266582988,     3078395,    -2924611 },  /* phon=46.0 volume=-39.0 dB fs=44100 Hz biquad * volume */
    {  -266540163,     3259655,    -3098543 },  /* phon=46.5 volume=-38.5 dB fs=44100 Hz biquad * volume */
    {  -266496931,     3451591,    -3282809 },  /* phon=47.0 volume=-38.0 dB fs=44100 Hz biquad * volume */
    {  -266453262,     3654833,    -3478023 },  /* phon=47.5 volume=-37.5 dB fs=44100 Hz biquad * volume */
    {  -266409159,     3870047,    -3684834 },  /* phon=48.0 volume=-37.0 dB fs=44100 Hz biquad * volume */
    {  -266364610,     4097940,    -3903930 },  /* phon=48.5 volume=-36.5 dB fs=44100 Hz biquad * volume */
    {  -266319616,     4339257,    -4136040 },  /* phon=49.0 volume=-36.0 dB fs=44100 Hz biquad * volume */
    {  -266274169,     4594790,    -4381937 },  /* phon=49.5 volume=-35.5 dB fs=44100 Hz biquad * volume */
    {  -266228259,     4865377,    -4642438 },  /* phon=50.0 volume=-35.0 dB fs=44100 Hz biquad * volume */
    {  -266181892,     5151905,    -4918410 },  /* phon=50.5 volume=-34.5 dB fs=44100 Hz biquad * volume */
    {  -266135041,     5455313,    -5210770 },  /* phon=51.0 volume=-34.0 dB fs=44100 Hz biquad * volume */
    {  -266087721,     5776597,    -5520492 },  /* phon=51.5 volume=-33.5 dB fs=44100 Hz biquad * volume */
    {  -266039920,     6116809,    -5848604 },  /* phon=52.0 volume=-33.0 dB fs=44100 Hz biquad * volume */
    {  -265991634,     6477066,    -6196198 },  /* phon=52.5 volume=-32.5 dB fs=44100 Hz biquad * volume */
    {  -265942858,     6858548,    -6564428 },  /* phon=53.0 volume=-32.0 dB fs=44100 Hz biquad * volume */
    {  -265893585,     7262507,    -6954519 },  /* phon=53.5 volume=-31.5 dB fs=44100 Hz biquad * volume */
    {  -265843810,     7690268,    -7367768 },  /* phon=54.0 volume=-31.0 dB fs=44100 Hz biquad * volume */
    {  -265793529,     8143232,    -7805547 },  /* phon=54.5 volume=-30.5 dB fs=44100 Hz biquad * volume */
    {  -265742742,     8622886,    -8269312 },  /* phon=55.0 volume=-30.0 dB fs=44100 Hz biquad * volume */
    {  -265691441,     9130802,    -8760603 },  /* phon=55.5 volume=-29.5 dB fs=44100 Hz biquad * volume */
    {  -265639619,     9668647,    -9281052 },  /* phon=56.0 volume=-29.0 dB fs=44100 Hz biquad * volume */
    {  -265587273,    10238184,    -9832387 },  /* phon=56.5 volume=-28.5 dB fs=44100 Hz biquad * volume */
    {  -265534408,    10841281,   -10416442 },  /* phon=57.0 volume=-28.0 dB fs=44100 Hz biquad * volume */
    {  -265480995,    11479917,   -11035153 },  /* phon=57.5 volume=-27.5 dB fs=44100 Hz biquad * volume */
    {  -265427055,    12156184,   -11690578 },  /* phon=58.0 volume=-27.0 dB fs=44100 Hz biquad * volume */
    {  -265372571,    12872303,   -12384891 },  /* phon=58.5 volume=-26.5 dB fs=44100 Hz biquad * volume */
    {  -265317542,    13630621,   -13120398 },  /* phon=59.0 volume=-26.0 dB fs=44100 Hz biquad * volume */
    {  -265261964,    14433625,   -13899540 },  /* phon=59.5 volume=-25.5 dB fs=44100 Hz biquad * volume */
    {  -265205831,    15283950,   -14724905 },  /* phon=60.0 volume=-25.0 dB fs=44100 Hz biquad * volume */
    {  -265149142,    16184384,   -15599230 },  /* phon=60.5 volume=-24.5 dB fs=44100 Hz biquad * volume */
    {  -265091886,    17137881,   -16525418 },  /* phon=61.0 volume=-24.0 dB fs=44100 Hz biquad * volume */
    {  -265034066,    18147568,   -17506543 },  /* phon=61.5 volume=-23.5 dB fs=44100 Hz biquad * volume */
    {  -264975672,    19216758,   -18545858 },  /* phon=62.0 volume=-23.0 dB fs=44100 Hz biquad * volume */
    {  -264916703,    20348955,   -19646814 },  /* phon=62.5 volume=-22.5 dB fs=44100 Hz biquad * volume */
    {  -264857153,    21547876,   -20813062 },  /* phon=63.0 volume=-22.0 dB fs=44100 Hz biquad * volume */
    {  -264797008,    22817452,   -22048469 },  /* phon=63.5 volume=-21.5 dB fs=44100 Hz biquad * volume */
    {  -264736279,    24161846,   -23357134 },  /* phon=64.0 volume=-21.0 dB fs=44100 Hz biquad * volume */
    {  -264674951,    25585470,   -24743397 },  /* phon=64.5 volume=-20.5 dB fs=44100 Hz biquad * volume */
    {  -264613027,    27092992,   -26211856 },  /* phon=65.0 volume=-20.0 dB fs=44100 Hz biquad * volume */
    {  -264550494,    28689358,   -27767378 },  /* phon=65.5 volume=-19.5 dB fs=44100 Hz biquad * volume */
    {  -264487351,    30379802,   -29415120 },  /* phon=66.0 volume=-19.0 dB fs=44100 Hz biquad * volume */
    {  -264423590,    32169871,   -31160545 },  /* phon=66.5 volume=-18.5 dB fs=44100 Hz biquad * volume */
    {  -264359144,    34065438,   -33009429 },  /* phon=67.0 volume=-18.0 dB fs=44100 Hz biquad * volume */
    {  -264294147,    36072714,   -34967921 },  /* phon=67.5 volume=-17.5 dB fs=44100 Hz biquad * volume */
    {  -264228559,    38198285,   -37042507 },  /* phon=68.0 volume=-17.0 dB fs=44100 Hz biquad * volume */
    {  -264162225,    40449129,   -39240036 },  /* phon=68.5 volume=-16.5 dB fs=44100 Hz biquad * volume */
    {  -264095285,    42832621,   -41567813 },  /* phon=69.0 volume=-16.0 dB fs=44100 Hz biquad * volume */
    {  -264027725,    45356581,   -44033552 },  /* phon=69.5 volume=-15.5 dB fs=44100 Hz biquad * volume */
    {  -263959484,    48029288,   -46645408 },  /* phon=70.0 volume=-15.0 dB fs=44100 Hz biquad * volume */
    {  -263890604,    50859507,   -49412043 },  /* phon=70.5 volume=-14.5 dB fs=44100 Hz biquad * volume */
    {  -263821045,    53856520,   -52342614 },  /* phon=71.0 volume=-14.0 dB fs=44100 Hz biquad * volume */
    {  -263750798,    57030158,   -55446824 },  /* phon=71.5 volume=-13.5 dB fs=44100 Hz biquad * volume */
    {  -263679898,    60390827,   -58734963 },  /* phon=72.0 volume=-13.0 dB fs=44100 Hz biquad * volume */
    {  -263608280,    63949550,   -62217902 },  /* phon=72.5 volume=-12.5 dB fs=44100 Hz biquad * volume */
    {  -263535999,    67717997,   -65907191 },  /* phon=73.0 volume=-12.0 dB fs=44100 Hz biquad * volume */
    {  -263463001,    71708528,   -69815028 },  /* phon=73.5 volume=-11.5 dB fs=44100 Hz biquad * volume */
    {  -263389295,    75934228,   -73954352 },  /* phon=74.0 volume=-11.0 dB fs=44100 Hz biquad * volume */
    {  -263314844,    80408955,   -78338855 },  /* phon=74.5 volume=-10.5 dB fs=44100 Hz biquad * volume */
    {  -263239784,    85147380,   -82983094 },  /* phon=75.0 volume=-10.0 dB fs=44100 Hz biquad * volume */
    {  -263163842,    90165048,   -87902345 },  /* phon=75.5 volume=-9.5 dB fs=44100 Hz biquad * volume */
    {  -263087222,    95478405,   -93112957 },  /* phon=76.0 volume=-9.0 dB fs=44100 Hz biquad * volume */
    {  -263009855,   101104877,   -98632149 },  /* phon=76.5 volume=-8.5 dB fs=44100 Hz biquad * volume */
    {  -262931732,   107062912,  -104478176 },  /* phon=77.0 volume=-8.0 dB fs=44100 Hz biquad * volume */
    {  -262852805,   113372045,  -110670357 },  /* phon=77.5 volume=-7.5 dB fs=44100 Hz biquad * volume */
    {  -262773187,   120052960,  -117229225 },  /* phon=78.0 volume=-7.0 dB fs=44100 Hz biquad * volume */
    {  -262692780,   127127563,  -124176430 },  /* phon=78.5 volume=-6.5 dB fs=44100 Hz biquad * volume */
    {  -262611538,   134619049,  -131534925 },  /* phon=79.0 volume=-6.0 dB fs=44100 Hz biquad * volume */
    {  -262529407,   142551976,  -139329010 },  /* phon=79.5 volume=-5.5 dB fs=44100 Hz biquad * volume */
    {  -262797643,   150952350,  -147781975 },  /* phon=80.0 volume=-5.0 dB fs=44100 Hz biquad * volume */
    {  -262384140,   159847840,  -156341298 },  /* phon=80.5 volume=-4.5 dB fs=44100 Hz biquad * volume */
    {  -262299908,   169267490,  -165603886 },  /* phon=81.0 volume=-4.0 dB fs=44100 Hz biquad * volume */
    {  -262214823,   179242177,  -175414710 },  /* phon=81.5 volume=-3.5 dB fs=44100 Hz biquad * volume */
    {  -262128948,   189804598,  -185806236 },  /* phon=82.0 volume=-3.0 dB fs=44100 Hz biquad * volume */
    {  -262042210,   200989373,  -196812757 },  /* phon=82.5 volume=-2.5 dB fs=44100 Hz biquad * volume */
    {  -261954600,   212833161,  -208470637 },  /* phon=83.0 volume=-2.0 dB fs=44100 Hz biquad * volume */
    {  -261866109,   225374778,  -220818382 },  /* phon=83.5 volume=-1.5 dB fs=44100 Hz biquad * volume */
    {  -261776723,   238655327,  -233896774 },  /* phon=84.0 volume=-1.0 dB fs=44100 Hz biquad * volume */
    {  -261686430,   252718330,  -247749003 },  /* phon=84.5 volume=-0.5 dB fs=44100 Hz biquad * volume */
    {  -261595220,   267609870,  -262420806 },  /* phon=85.0 volume=0.0 dB fs=44100 Hz biquad * volume */
};

static const biquad_quotients_fast_t
lowshelf_no_volume_44100hz[LOUDNESS_NUM_EQUALIZER_STEPS] = {
    {  -267671632,   279027685,  -257079403 },  /* phon=25.0 volume=-60.0 dB fs=44100 Hz biquad */
    {  -267671632,   278902190,  -257204899 },  /* phon=25.5 volume=-59.5 dB fs=44100 Hz biquad */
    {  -267671632,   278774866,  -257332223 },  /* phon=26.0 volume=-59.0 dB fs=44100 Hz biquad */
    {  -267671632,   278645885,  -257461203 },  /* phon=26.5 volume=-58.5 dB fs=44100 Hz biquad */
    {  -267671632,   278515396,  -257591693 },  /* phon=27.0 volume=-58.0 dB fs=44100 Hz biquad */
    {  -267671632,   278383530,  -257723559 },  /* phon=27.5 volume=-57.5 dB fs=44100 Hz biquad */
    {  -267671632,   278250427,  -257856662 },  /* phon=28.0 volume=-57.0 dB fs=44100 Hz biquad */
    {  -267671632,   278116227,  -257990861 },  /* phon=28.5 volume=-56.5 dB fs=44100 Hz biquad */
    {  -267671632,   277981053,  -258126036 },  /* phon=29.0 volume=-56.0 dB fs=44100 Hz biquad */
    {  -267671632,   277845024,  -258262065 },  /* phon=29.5 volume=-55.5 dB fs=44100 Hz biquad */
    {  -267671632,   277708257,  -258398831 },  /* phon=30.0 volume=-55.0 dB fs=44100 Hz biquad */
    {  -267671632,   277570861,  -258536228 },  /* phon=30.5 volume=-54.5 dB fs=44100 Hz biquad */
    {  -267671632,   277432940,  -258674148 },  /* phon=31.0 volume=-54.0 dB fs=44100 Hz biquad */
    {  -267671632,   277294600,  -258812488 },  /* phon=31.5 volume=-53.5 dB fs=44100 Hz biquad */
    {  -267654950,   277172683,  -258917722 },  /* phon=32.0 volume=-53.0 dB fs=44100 Hz biquad */
    {  -267618465,   277070678,  -258983242 },  /* phon=32.5 volume=-52.5 dB fs=44100 Hz biquad */
    {  -267582151,   276968615,  -259048992 },  /* phon=33.0 volume=-52.0 dB fs=44100 Hz biquad */
    {  -267545942,   276866511,  -259114887 },  /* phon=33.5 volume=-51.5 dB fs=44100 Hz biquad */
    {  -267509784,   276764388,  -259180852 },  /* phon=34.0 volume=-51.0 dB fs=44100 Hz biquad */
    {  -267473629,   276662272,  -259246813 },  /* phon=34.5 volume=-50.5 dB fs=44100 Hz biquad */
    {  -267437435,   276560188,  -259312703 },  /* phon=35.0 volume=-50.0 dB fs=44100 Hz biquad */
    {  -267401164,   276458158,  -259378463 },  /* phon=35.5 volume=-49.5 dB fs=44100 Hz biquad */
    {  -267364783,   276356209,  -259444030 },  /* phon=36.0 volume=-49.0 dB fs=44100 Hz biquad */
    {  -267328264,   276254362,  -259509358 },  /* phon=36.5 volume=-48.5 dB fs=44100 Hz biquad */
    {  -267291579,   276152642,  -259574393 },  /* phon=37.0 volume=-48.0 dB fs=44100 Hz biquad */
    {  -267254704,   276051068,  -259639092 },  /* phon=37.5 volume=-47.5 dB fs=44100 Hz biquad */
    {  -267217619,   275949662,  -259703414 },  /* phon=38.0 volume=-47.0 dB fs=44100 Hz biquad */
    {  -267180302,   275848443,  -259767315 },  /* phon=38.5 volume=-46.5 dB fs=44100 Hz biquad */
    {  -267142735,   275747429,  -259830761 },  /* phon=39.0 volume=-46.0 dB fs=44100 Hz biquad */
    {  -267104904,   275646635,  -259893725 },  /* phon=39.5 volume=-45.5 dB fs=44100 Hz biquad */
    {  -267066788,   275546082,  -259956162 },  /* phon=40.0 volume=-45.0 dB fs=44100 Hz biquad */
    {  -267028377,   275445779,  -260018055 },  /* phon=40.5 volume=-44.5 dB fs=44100 Hz biquad */
    {  -266989657,   275345742,  -260079371 },  /* phon=41.0 volume=-44.0 dB fs=44100 Hz biquad */
    {  -266950617,   275245980,  -260140093 },  /* phon=41.5 volume=-43.5 dB fs=44100 Hz biquad */
    {  -266911241,   275146511,  -260200186 },  /* phon=42.0 volume=-43.0 dB fs=44100 Hz biquad */
    {  -266871519,   275047343,  -260259632 },  /* phon=42.5 volume=-42.5 dB fs=44100 Hz biquad */
    {  -266831441,   274948484,  -260318414 },  /* phon=43.0 volume=-42.0 dB fs=44100 Hz biquad */
    {  -266790998,   274849943,  -260376511 },  /* phon=43.5 volume=-41.5 dB fs=44100 Hz biquad */
    {  -266750175,   274751734,  -260433896 },  /* phon=44.0 volume=-41.0 dB fs=44100 Hz biquad */
    {  -266708982,   274653843,  -260490594 },  /* phon=44.5 volume=-40.5 dB fs=44100 Hz biquad */
    {  -266667395,   274556293,  -260546558 },  /* phon=45.0 volume=-40.0 dB fs=44100 Hz biquad */
    {  -266625390,   274459103,  -260601743 },  /* phon=45.5 volume=-39.5 dB fs=44100 Hz biquad */
    {  -266582988,   274362251,  -260656193 },  /* phon=46.0 volume=-39.0 dB fs=44100 Hz biquad */
    {  -266540163,   274265758,  -260709861 },  /* phon=46.5 volume=-38.5 dB fs=44100 Hz biquad */
    {  -266496931,   274169604,  -260762784 },  /* phon=47.0 volume=-38.0 dB fs=44100 Hz biquad */
    {  -266453262,   274073814,  -260814904 },  /* phon=47.5 volume=-37.5 dB fs=44100 Hz biquad */
    {  -266409159,   273978381,  -260866234 },  /* phon=48.0 volume=-37.0 dB fs=44100 Hz biquad */
    {  -266364610,   273883311,  -260916755 },  /* phon=48.5 volume=-36.5 dB fs=44100 Hz biquad */
    {  -266319616,   273788598,  -260966474 },  /* phon=49.0 volume=-36.0 dB fs=44100 Hz biquad */
    {  -266274169,   273694243,  -261015382 },  /* phon=49.5 volume=-35.5 dB fs=44100 Hz biquad */
    {  -266228259,   273600249,  -261063466 },  /* phon=50.0 volume=-35.0 dB fs=44100 Hz biquad */
    {  -266181892,   273506607,  -261110741 },  /* phon=50.5 volume=-34.5 dB fs=44100 Hz biquad */
    {  -266135041,   273413332,  -261157164 },  /* phon=51.0 volume=-34.0 dB fs=44100 Hz biquad */
    {  -266087721,   273320406,  -261202771 },  /* phon=51.5 volume=-33.5 dB fs=44100 Hz biquad */
    {  -266039920,   273227832,  -261247544 },  /* phon=52.0 volume=-33.0 dB fs=44100 Hz biquad */
    {  -265991634,   273135606,  -261291484 },  /* phon=52.5 volume=-32.5 dB fs=44100 Hz biquad */
    {  -265942858,   273043725,  -261334589 },  /* phon=53.0 volume=-32.0 dB fs=44100 Hz biquad */
    {  -265893585,   272952188,  -261376853 },  /* phon=53.5 volume=-31.5 dB fs=44100 Hz biquad */
    {  -265843810,   272860992,  -261418274 },  /* phon=54.0 volume=-31.0 dB fs=44100 Hz biquad */
    {  -265793529,   272770130,  -261458855 },  /* phon=54.5 volume=-30.5 dB fs=44100 Hz biquad */
    {  -265742742,   272679596,  -261498602 },  /* phon=55.0 volume=-30.0 dB fs=44100 Hz biquad */
    {  -265691441,   272589389,  -261537508 },  /* phon=55.5 volume=-29.5 dB fs=44100 Hz biquad */
    {  -265639619,   272499503,  -261575572 },  /* phon=56.0 volume=-29.0 dB fs=44100 Hz biquad */
    {  -265587273,   272409935,  -261612794 },  /* phon=56.5 volume=-28.5 dB fs=44100 Hz biquad */
    {  -265534408,   272320672,  -261649193 },  /* phon=57.0 volume=-28.0 dB fs=44100 Hz biquad */
    {  -265480995,   272231724,  -261684728 },  /* phon=57.5 volume=-27.5 dB fs=44100 Hz biquad */
    {  -265427055,   272143070,  -261719440 },  /* phon=58.0 volume=-27.0 dB fs=44100 Hz biquad */
    {  -265372571,   272054712,  -261753315 },  /* phon=58.5 volume=-26.5 dB fs=44100 Hz biquad */
    {  -265317542,   271966642,  -261786356 },  /* phon=59.0 volume=-26.0 dB fs=44100 Hz biquad */
    {  -265261964,   271878853,  -261818567 },  /* phon=59.5 volume=-25.5 dB fs=44100 Hz biquad */
    {  -265205831,   271791340,  -261849946 },  /* phon=60.0 volume=-25.0 dB fs=44100 Hz biquad */
    {  -265149142,   271704095,  -261880503 },  /* phon=60.5 volume=-24.5 dB fs=44100 Hz biquad */
    {  -265091886,   271617116,  -261910226 },  /* phon=61.0 volume=-24.0 dB fs=44100 Hz biquad */
    {  -265034066,   271530390,  -261939132 },  /* phon=61.5 volume=-23.5 dB fs=44100 Hz biquad */
    {  -264975672,   271443915,  -261967213 },  /* phon=62.0 volume=-23.0 dB fs=44100 Hz biquad */
    {  -264916703,   271357682,  -261994476 },  /* phon=62.5 volume=-22.5 dB fs=44100 Hz biquad */
    {  -264857153,   271271685,  -262020923 },  /* phon=63.0 volume=-22.0 dB fs=44100 Hz biquad */
    {  -264797008,   271185921,  -262046543 },  /* phon=63.5 volume=-21.5 dB fs=44100 Hz biquad */
    {  -264736279,   271100376,  -262071359 },  /* phon=64.0 volume=-21.0 dB fs=44100 Hz biquad */
    {  -264674951,   271015047,  -262095360 },  /* phon=64.5 volume=-20.5 dB fs=44100 Hz biquad */
    {  -264613027,   270929924,  -262118559 },  /* phon=65.0 volume=-20.0 dB fs=44100 Hz biquad */
    {  -264550494,   270845002,  -262140948 },  /* phon=65.5 volume=-19.5 dB fs=44100 Hz biquad */
    {  -264487351,   270760274,  -262162533 },  /* phon=66.0 volume=-19.0 dB fs=44100 Hz biquad */
    {  -264423590,   270675732,  -262183314 },  /* phon=66.5 volume=-18.5 dB fs=44100 Hz biquad */
    {  -264359144,   270591389,  -262203211 },  /* phon=67.0 volume=-18.0 dB fs=44100 Hz biquad */
    {  -264294147,   270507192,  -262222411 },  /* phon=67.5 volume=-17.5 dB fs=44100 Hz biquad */
    {  -264228559,   270423148,  -262240867 },  /* phon=68.0 volume=-17.0 dB fs=44100 Hz biquad */
    {  -264162225,   270339290,  -262258391 },  /* phon=68.5 volume=-16.5 dB fs=44100 Hz biquad */
    {  -264095285,   270255570,  -262275172 },  /* phon=69.0 volume=-16.0 dB fs=44100 Hz biquad */
    {  -264027725,   270171983,  -262291198 },  /* phon=69.5 volume=-15.5 dB fs=44100 Hz biquad */
    {  -263959484,   270088536,  -262306404 },  /* phon=70.0 volume=-15.0 dB fs=44100 Hz biquad */
    {  -263890604,   270005209,  -262320851 },  /* phon=70.5 volume=-14.5 dB fs=44100 Hz biquad */
    {  -263821045,   269922003,  -262334498 },  /* phon=71.0 volume=-14.0 dB fs=44100 Hz biquad */
    {  -263750798,   269838909,  -262347345 },  /* phon=71.5 volume=-13.5 dB fs=44100 Hz biquad */
    {  -263679898,   269755913,  -262359441 },  /* phon=72.0 volume=-13.0 dB fs=44100 Hz biquad */
    {  -263608280,   269673017,  -262370719 },  /* phon=72.5 volume=-12.5 dB fs=44100 Hz biquad */
    {  -263535999,   269590202,  -262381253 },  /* phon=73.0 volume=-12.0 dB fs=44100 Hz biquad */
    {  -263463001,   269507469,  -262390988 },  /* phon=73.5 volume=-11.5 dB fs=44100 Hz biquad */
    {  -263389295,   269424806,  -262399944 },  /* phon=74.0 volume=-11.0 dB fs=44100 Hz biquad */
    {  -263314844,   269342210,  -262408090 },  /* phon=74.5 volume=-10.5 dB fs=44100 Hz biquad */
    {  -263239784,   269259657,  -262415584 },  /* phon=75.0 volume=-10.0 dB fs=44100 Hz biquad */
    {  -263163842,   269177166,  -262422132 },  /* phon=75.5 volume=-9.5 dB fs=44100 Hz biquad */
    {  -263087222,   269094708,  -262427970 },  /* phon=76.0 volume=-9.0 dB fs=44100 Hz biquad */
    {  -263009855,   269012280,  -262433031 },  /* phon=76.5 volume=-8.5 dB fs=44100 Hz biquad */
    {  -262931732,   268929875,  -262437313 },  /* phon=77.0 volume=-8.0 dB fs=44100 Hz biquad */
    {  -262852805,   268847486,  -262440774 },  /* phon=77.5 volume=-7.5 dB fs=44100 Hz biquad */
    {  -262773187,   268765100,  -262443544 },  /* phon=78.0 volume=-7.0 dB fs=44100 Hz biquad */
    {  -262692780,   268682712,  -262445524 },  /* phon=78.5 volume=-6.5 dB fs=44100 Hz biquad */
    {  -262611538,   268600315,  -262446679 },  /* phon=79.0 volume=-6.0 dB fs=44100 Hz biquad */
    {  -262529407,   268517900,  -262446962 },  /* phon=79.5 volume=-5.5 dB fs=44100 Hz biquad */
    {  -262797643,   268435456,  -262797643 },  /* phon=80.0 volume=-5.0 dB fs=44100 Hz biquad */
    {  -262384140,   268353196,  -262466400 },  /* phon=80.5 volume=-4.5 dB fs=44100 Hz biquad */
    {  -262299908,   268270892,  -262464472 },  /* phon=81.0 volume=-4.0 dB fs=44100 Hz biquad */
    {  -262214823,   268188536,  -262461743 },  /* phon=81.5 volume=-3.5 dB fs=44100 Hz biquad */
    {  -262128948,   268106120,  -262458284 },  /* phon=82.0 volume=-3.0 dB fs=44100 Hz biquad */
    {  -262042210,   268023636,  -262454030 },  /* phon=82.5 volume=-2.5 dB fs=44100 Hz biquad */
    {  -261954600,   267941074,  -262448982 },  /* phon=83.0 volume=-2.0 dB fs=44100 Hz biquad */
    {  -261866109,   267858426,  -262443139 },  /* phon=83.5 volume=-1.5 dB fs=44100 Hz biquad */
    {  -261776723,   267775682,  -262436497 },  /* phon=84.0 volume=-1.0 dB fs=44100 Hz biquad */
    {  -261686430,   267692833,  -262429054 },  /* phon=84.5 volume=-0.5 dB fs=44100 Hz biquad */
    {  -261595220,   267609870,  -262420806 },  /* phon=85.0 volume=0.0 dB fs=44100 Hz biquad */
};

static const biquad_quotients_fast_t
lowshelf_and_volume_48000hz[LOUDNESS_NUM_EQUALIZER_STEPS] = {
    {  -267733612,      278168,     -258001 },  /* phon=25.0 volume=-60.0 dB fs=48000 Hz biquad * volume */
    {  -267733612,      294528,     -273411 },  /* phon=25.5 volume=-59.5 dB fs=48000 Hz biquad * volume */
    {  -267733612,      311849,     -289743 },  /* phon=26.0 volume=-59.0 dB fs=48000 Hz biquad * volume */
    {  -267733612,      330186,     -307052 },  /* phon=26.5 volume=-58.5 dB fs=48000 Hz biquad * volume */
    {  -267733612,      349600,     -325397 },  /* phon=27.0 volume=-58.0 dB fs=48000 Hz biquad * volume */
    {  -267733612,      370154,     -344839 },  /* phon=27.5 volume=-57.5 dB fs=48000 Hz biquad * volume */
    {  -267733612,      391914,     -365445 },  /* phon=28.0 volume=-57.0 dB fs=48000 Hz biquad * volume */
    {  -267733612,      414952,     -387283 },  /* phon=28.5 volume=-56.5 dB fs=48000 Hz biquad * volume */
    {  -267733612,      439343,     -410428 },  /* phon=29.0 volume=-56.0 dB fs=48000 Hz biquad * volume */
    {  -267733612,      465165,     -434957 },  /* phon=29.5 volume=-55.5 dB fs=48000 Hz biquad * volume */
    {  -267733612,      492505,     -460954 },  /* phon=30.0 volume=-55.0 dB fs=48000 Hz biquad * volume */
    {  -267733612,      521450,     -488505 },  /* phon=30.5 volume=-54.5 dB fs=48000 Hz biquad * volume */
    {  -267733612,      552095,     -517703 },  /* phon=31.0 volume=-54.0 dB fs=48000 Hz biquad * volume */
    {  -267733612,      584540,     -548648 },  /* phon=31.5 volume=-53.5 dB fs=48000 Hz biquad * volume */
    {  -267718317,      618925,     -581374 },  /* phon=32.0 volume=-53.0 dB fs=48000 Hz biquad * volume */
    {  -267684787,      655376,     -615965 },  /* phon=32.5 volume=-52.5 dB fs=48000 Hz biquad * volume */
    {  -267651414,      693974,     -652615 },  /* phon=33.0 volume=-52.0 dB fs=48000 Hz biquad * volume */
    {  -267618139,      734846,     -691446 },  /* phon=33.5 volume=-51.5 dB fs=48000 Hz biquad * volume */
    {  -267584909,      778123,     -732587 },  /* phon=34.0 volume=-51.0 dB fs=48000 Hz biquad * volume */
    {  -267551682,      823950,     -776176 },  /* phon=34.5 volume=-50.5 dB fs=48000 Hz biquad * volume */
    {  -267518418,      872476,     -822359 },  /* phon=35.0 volume=-50.0 dB fs=48000 Hz biquad * volume */
    {  -267485084,      923859,     -871289 },  /* phon=35.5 volume=-49.5 dB fs=48000 Hz biquad * volume */
    {  -267451648,      978269,     -923130 },  /* phon=36.0 volume=-49.0 dB fs=48000 Hz biquad * volume */
    {  -267418084,     1035884,     -978054 },  /* phon=36.5 volume=-48.5 dB fs=48000 Hz biquad * volume */
    {  -267384369,     1096892,    -1036245 },  /* phon=37.0 volume=-48.0 dB fs=48000 Hz biquad * volume */
    {  -267350479,     1161493,    -1097897 },  /* phon=37.5 volume=-47.5 dB fs=48000 Hz biquad * volume */
    {  -267316393,     1229900,    -1163216 },  /* phon=38.0 volume=-47.0 dB fs=48000 Hz biquad * volume */
    {  -267282094,     1302336,    -1232418 },  /* phon=38.5 volume=-46.5 dB fs=48000 Hz biquad * volume */
    {  -267247567,     1379040,    -1305735 },  /* phon=39.0 volume=-46.0 dB fs=48000 Hz biquad * volume */
    {  -267212796,     1460261,    -1383412 },  /* phon=39.5 volume=-45.5 dB fs=48000 Hz biquad * volume */
    {  -267177765,     1546268,    -1465707 },  /* phon=40.0 volume=-45.0 dB fs=48000 Hz biquad * volume */
    {  -267142460,     1637341,    -1552894 },  /* phon=40.5 volume=-44.5 dB fs=48000 Hz biquad * volume */
    {  -267106870,     1733780,    -1645264 },  /* phon=41.0 volume=-44.0 dB fs=48000 Hz biquad * volume */
    {  -267070984,     1835900,    -1743124 },  /* phon=41.5 volume=-43.5 dB fs=48000 Hz biquad * volume */
    {  -267034791,     1944037,    -1846802 },  /* phon=42.0 volume=-43.0 dB fs=48000 Hz biquad * volume */
    {  -266998279,     2058546,    -1956641 },  /* phon=42.5 volume=-42.5 dB fs=48000 Hz biquad * volume */
    {  -266961440,     2179801,    -2073008 },  /* phon=43.0 volume=-42.0 dB fs=48000 Hz biquad * volume */
    {  -266924264,     2308201,    -2196290 },  /* phon=43.5 volume=-41.5 dB fs=48000 Hz biquad * volume */
    {  -266886744,     2444166,    -2326898 },  /* phon=44.0 volume=-41.0 dB fs=48000 Hz biquad * volume */
    {  -266848871,     2588143,    -2465267 },  /* phon=44.5 volume=-40.5 dB fs=48000 Hz biquad * volume */
    {  -266810637,     2740604,    -2611857 },  /* phon=45.0 volume=-40.0 dB fs=48000 Hz biquad * volume */
    {  -266772034,     2902050,    -2767156 },  /* phon=45.5 volume=-39.5 dB fs=48000 Hz biquad * volume */
    {  -266733054,     3073009,    -2931681 },  /* phon=46.0 volume=-39.0 dB fs=48000 Hz biquad * volume */
    {  -266693693,     3254042,    -3105980 },  /* phon=46.5 volume=-38.5 dB fs=48000 Hz biquad * volume */
    {  -266653945,     3445744,    -3290632 },  /* phon=47.0 volume=-38.0 dB fs=48000 Hz biquad * volume */
    {  -266613799,     3648744,    -3486252 },  /* phon=47.5 volume=-37.5 dB fs=48000 Hz biquad * volume */
    {  -266573254,     3863707,    -3693492 },  /* phon=48.0 volume=-37.0 dB fs=48000 Hz biquad * volume */
    {  -266532304,     4091340,    -3913039 },  /* phon=48.5 volume=-36.5 dB fs=48000 Hz biquad * volume */
    {  -266490936,     4332388,    -4145624 },  /* phon=49.0 volume=-36.0 dB fs=48000 Hz biquad * volume */
    {  -266449155,     4587643,    -4392022 },  /* phon=49.5 volume=-35.5 dB fs=48000 Hz biquad * volume */
    {  -266406947,     4857942,    -4653050 },  /* phon=50.0 volume=-35.0 dB fs=48000 Hz biquad * volume */
    {  -266364313,     5144173,    -4929578 },  /* phon=50.5 volume=-34.5 dB fs=48000 Hz biquad * volume */
    {  -266321247,     5447274,    -5222525 },  /* phon=51.0 volume=-34.0 dB fs=48000 Hz biquad * volume */
    {  -266277740,     5768241,    -5532864 },  /* phon=51.5 volume=-33.5 dB fs=48000 Hz biquad * volume */
    {  -266233790,     6108127,    -5861626 },  /* phon=52.0 volume=-33.0 dB fs=48000 Hz biquad * volume */
    {  -266189396,     6468047,    -6209906 },  /* phon=52.5 volume=-32.5 dB fs=48000 Hz biquad * volume */
    {  -266144547,     6849182,    -6578860 },  /* phon=53.0 volume=-32.0 dB fs=48000 Hz biquad * volume */
    {  -266099244,     7252784,    -6969714 },  /* phon=53.5 volume=-31.5 dB fs=48000 Hz biquad * volume */
    {  -266053483,     7680177,    -7383768 },  /* phon=54.0 volume=-31.0 dB fs=48000 Hz biquad * volume */
    {  -266007248,     8132764,    -7822395 },  /* phon=54.5 volume=-30.5 dB fs=48000 Hz biquad * volume */
    {  -265960547,     8612031,    -8287055 },  /* phon=55.0 volume=-30.0 dB fs=48000 Hz biquad * volume */
    {  -265913376,     9119549,    -8779290 },  /* phon=55.5 volume=-29.5 dB fs=48000 Hz biquad * volume */
    {  -265865725,     9656986,    -9300735 },  /* phon=56.0 volume=-29.0 dB fs=48000 Hz biquad * volume */
    {  -265817593,    10226106,    -9853122 },  /* phon=56.5 volume=-28.5 dB fs=48000 Hz biquad * volume */
    {  -265768971,    10828775,   -10438286 },  /* phon=57.0 volume=-28.0 dB fs=48000 Hz biquad * volume */
    {  -265719863,    11466974,   -11058169 },  /* phon=57.5 volume=-27.5 dB fs=48000 Hz biquad * volume */
    {  -265670266,    12142796,   -11714830 },  /* phon=58.0 volume=-27.0 dB fs=48000 Hz biquad * volume */
    {  -265620159,    12858461,   -12410448 },  /* phon=58.5 volume=-26.5 dB fs=48000 Hz biquad * volume */
    {  -265569556,    13616317,   -13147333 },  /* phon=59.0 volume=-26.0 dB fs=48000 Hz biquad * volume */
    {  -265518445,    14418852,   -13927930 },  /* phon=59.5 volume=-25.5 dB fs=48000 Hz biquad * volume */
    {  -265466823,    15268700,   -14754831 },  /* phon=60.0 volume=-25.0 dB fs=48000 Hz biquad * volume */
    {  -265414688,    16168652,   -15630780 },  /* phon=60.5 volume=-24.5 dB fs=48000 Hz biquad * volume */
    {  -265362036,    17121662,   -16558683 },  /* phon=61.0 volume=-24.0 dB fs=48000 Hz biquad * volume */
    {  -265308853,    18130858,   -17541618 },  /* phon=61.5 volume=-23.5 dB fs=48000 Hz biquad * volume */
    {  -265255152,    19199554,   -18582848 },  /* phon=62.0 volume=-23.0 dB fs=48000 Hz biquad * volume */
    {  -265200913,    20331256,   -19685826 },  /* phon=62.5 volume=-22.5 dB fs=48000 Hz biquad * volume */
    {  -265146140,    21529682,   -20854211 },  /* phon=63.0 volume=-22.0 dB fs=48000 Hz biquad * volume */
    {  -265090829,    22798764,   -22091878 },  /* phon=63.5 volume=-21.5 dB fs=48000 Hz biquad * volume */
    {  -265034977,    24142669,   -23402933 },  /* phon=64.0 volume=-21.0 dB fs=48000 Hz biquad * volume */
    {  -264978561,    25565809,   -24791721 },  /* phon=64.5 volume=-20.5 dB fs=48000 Hz biquad * volume */
    {  -264921602,    27072856,   -26262850 },  /* phon=65.0 volume=-20.0 dB fs=48000 Hz biquad * volume */
    {  -264864084,    28668756,   -27821196 },  /* phon=65.5 volume=-19.5 dB fs=48000 Hz biquad * volume */
    {  -264806001,    30358750,   -29471926 },  /* phon=66.0 volume=-19.0 dB fs=48000 Hz biquad * volume */
    {  -264747358,    32148384,   -31220512 },  /* phon=66.5 volume=-18.5 dB fs=48000 Hz biquad * volume */
    {  -264688130,    34043535,   -33072748 },  /* phon=67.0 volume=-18.0 dB fs=48000 Hz biquad * volume */
    {  -264628329,    36050422,   -35034776 },  /* phon=67.5 volume=-17.5 dB fs=48000 Hz biquad * volume */
    {  -264567895,    38175637,   -37113088 },  /* phon=68.0 volume=-17.0 dB fs=48000 Hz biquad * volume */
    {  -264506975,    40426149,   -39314598 },  /* phon=68.5 volume=-16.5 dB fs=48000 Hz biquad * volume */
    {  -264445355,    42809355,   -41646562 },  /* phon=69.0 volume=-16.0 dB fs=48000 Hz biquad * volume */
    {  -264383189,    45333071,   -44116737 },  /* phon=69.5 volume=-15.5 dB fs=48000 Hz biquad * volume */
    {  -264320420,    48005584,   -46733296 },  /* phon=70.0 volume=-15.0 dB fs=48000 Hz biquad * volume */
    {  -264257026,    50835668,   -49504903 },  /* phon=70.5 volume=-14.5 dB fs=48000 Hz biquad * volume */
    {  -264193045,    53832609,   -52440748 },  /* phon=71.0 volume=-14.0 dB fs=48000 Hz biquad * volume */
    {  -264128427,    57006248,   -55550545 },  /* phon=71.5 volume=-13.5 dB fs=48000 Hz biquad * volume */
    {  -264063182,    60367001,   -58844594 },  /* phon=72.0 volume=-13.0 dB fs=48000 Hz biquad * volume */
    {  -263997302,    63925900,   -62333804 },  /* phon=72.5 volume=-12.5 dB fs=48000 Hz biquad * volume */
    {  -263930781,    67694626,   -66029727 },  /* phon=73.0 volume=-12.0 dB fs=48000 Hz biquad * volume */
    {  -263863613,    71685548,   -69944599 },  /* phon=73.5 volume=-11.5 dB fs=48000 Hz biquad * volume */
    {  -263795772,    75911767,   -74091374 },  /* phon=74.0 volume=-11.0 dB fs=48000 Hz biquad * volume */
    {  -263727313,    80387151,   -78483797 },  /* phon=74.5 volume=-10.5 dB fs=48000 Hz biquad * volume */
    {  -263658161,    85126392,   -83136384 },  /* phon=75.0 volume=-10.0 dB fs=48000 Hz biquad * volume */
    {  -263588338,    90145042,   -88064542 },  /* phon=75.5 volume=-9.5 dB fs=48000 Hz biquad * volume */
    {  -263517831,    95459572,   -93284576 },  /* phon=76.0 volume=-9.0 dB fs=48000 Hz biquad * volume */
    {  -263446634,   101087425,   -98813759 },  /* phon=76.5 volume=-8.5 dB fs=48000 Hz biquad * volume */
    {  -263374707,   107047069,  -104670370 },  /* phon=77.0 volume=-8.0 dB fs=48000 Hz biquad * volume */
    {  -263302142,   113358061,  -110873825 },  /* phon=77.5 volume=-7.5 dB fs=48000 Hz biquad * volume */
    {  -263228794,   120041112,  -117444585 },  /* phon=78.0 volume=-7.0 dB fs=48000 Hz biquad * volume */
    {  -263154799,   127118151,  -124404447 },  /* phon=78.5 volume=-6.5 dB fs=48000 Hz biquad * volume */
    {  -263080060,   134612402,  -131776389 },  /* phon=79.0 volume=-6.0 dB fs=48000 Hz biquad * volume */
    {  -263004481,   142548456,  -139584740 },  /* phon=79.5 volume=-5.5 dB fs=48000 Hz biquad * volume */
    {  -263028613,   150952350,  -147911859 },  /* phon=80.0 volume=-5.0 dB fs=48000 Hz biquad * volume */
    {  -262871797,   159851786,  -156627830 },  /* phon=80.5 volume=-4.5 dB fs=48000 Hz biquad * volume */
    {  -262794288,   169275852,  -165907457 },  /* phon=81.0 volume=-4.0 dB fs=48000 Hz biquad * volume */
    {  -262715981,   179255465,  -175736367 },  /* phon=81.5 volume=-3.5 dB fs=48000 Hz biquad * volume */
    {  -262636943,   189823369,  -186147097 },  /* phon=82.0 volume=-3.0 dB fs=48000 Hz biquad * volume */
    {  -262557109,   201014232,  -197174018 },  /* phon=82.5 volume=-2.5 dB fs=48000 Hz biquad * volume */
    {  -262476472,   212864767,  -208853568 },  /* phon=83.0 volume=-2.0 dB fs=48000 Hz biquad * volume */
    {  -262395022,   225413849,  -221224336 },  /* phon=83.5 volume=-1.5 dB fs=48000 Hz biquad * volume */
    {  -262312745,   238702640,  -234327192 },  /* phon=84.0 volume=-1.0 dB fs=48000 Hz biquad * volume */
    {  -262229632,   252774731,  -248205417 },  /* phon=84.5 volume=-0.5 dB fs=48000 Hz biquad * volume */
    {  -262145672,   267676276,  -262904852 },  /* phon=85.0 volume=0.0 dB fs=48000 Hz biquad * volume */
};

static const biquad_quotients_fast_t
lowshelf_no_volume_48000hz[LOUDNESS_NUM_EQUALIZER_STEPS] = {
    {  -267733612,   278168040,  -258001028 },  /* phon=25.0 volume=-60.0 dB fs=48000 Hz biquad */
    {  -267733612,   278052728,  -258116340 },  /* phon=25.5 volume=-59.5 dB fs=48000 Hz biquad */
    {  -267733612,   277935757,  -258233311 },  /* phon=26.0 volume=-59.0 dB fs=48000 Hz biquad */
    {  -267733612,   277817248,  -258351819 },  /* phon=26.5 volume=-58.5 dB fs=48000 Hz biquad */
    {  -267733612,   277697342,  -258471726 },  /* phon=27.0 volume=-58.0 dB fs=48000 Hz biquad */
    {  -267733612,   277576183,  -258592885 },  /* phon=27.5 volume=-57.5 dB fs=48000 Hz biquad */
    {  -267733612,   277453891,  -258715177 },  /* phon=28.0 volume=-57.0 dB fs=48000 Hz biquad */
    {  -267733612,   277330589,  -258838479 },  /* phon=28.5 volume=-56.5 dB fs=48000 Hz biquad */
    {  -267733612,   277206386,  -258962682 },  /* phon=29.0 volume=-56.0 dB fs=48000 Hz biquad */
    {  -267733612,   277081400,  -259087668 },  /* phon=29.5 volume=-55.5 dB fs=48000 Hz biquad */
    {  -267733612,   276955735,  -259213333 },  /* phon=30.0 volume=-55.0 dB fs=48000 Hz biquad */
    {  -267733612,   276829492,  -259339576 },  /* phon=30.5 volume=-54.5 dB fs=48000 Hz biquad */
    {  -267733612,   276702769,  -259466299 },  /* phon=31.0 volume=-54.0 dB fs=48000 Hz biquad */
    {  -267733612,   276575659,  -259593409 },  /* phon=31.5 volume=-53.5 dB fs=48000 Hz biquad */
    {  -267718317,   276463622,  -259690151 },  /* phon=32.0 volume=-53.0 dB fs=48000 Hz biquad */
    {  -267684787,   276369940,  -259750303 },  /* phon=32.5 volume=-52.5 dB fs=48000 Hz biquad */
    {  -267651414,   276276207,  -259810663 },  /* phon=33.0 volume=-52.0 dB fs=48000 Hz biquad */
    {  -267618139,   276182430,  -259871164 },  /* phon=33.5 volume=-51.5 dB fs=48000 Hz biquad */
    {  -267584909,   276088636,  -259931730 },  /* phon=34.0 volume=-51.0 dB fs=48000 Hz biquad */
    {  -267551682,   275994847,  -259992291 },  /* phon=34.5 volume=-50.5 dB fs=48000 Hz biquad */
    {  -267518418,   275901086,  -260052789 },  /* phon=35.0 volume=-50.0 dB fs=48000 Hz biquad */
    {  -267485084,   275807375,  -260113164 },  /* phon=35.5 volume=-49.5 dB fs=48000 Hz biquad */
    {  -267451648,   275713737,  -260173368 },  /* phon=36.0 volume=-49.0 dB fs=48000 Hz biquad */
    {  -267418084,   275620193,  -260233347 },  /* phon=36.5 volume=-48.5 dB fs=48000 Hz biquad */
    {  -267384369,   275526763,  -260293062 },  /* phon=37.0 volume=-48.0 dB fs=48000 Hz biquad */
    {  -267350479,   275433467,  -260352468 },  /* phon=37.5 volume=-47.5 dB fs=48000 Hz biquad */
    {  -267316393,   275340325,  -260411525 },  /* phon=38.0 volume=-47.0 dB fs=48000 Hz biquad */
    {  -267282094,   275247354,  -260470196 },  /* phon=38.5 volume=-46.5 dB fs=48000 Hz biquad */
    {  -267247567,   275154569,  -260528454 },  /* phon=39.0 volume=-46.0 dB fs=48000 Hz biquad */
    {  -267212796,   275061986,  -260586266 },  /* phon=39.5 volume=-45.5 dB fs=48000 Hz biquad */
    {  -267177765,   274969621,  -260643600 },  /* phon=40.0 volume=-45.0 dB fs=48000 Hz biquad */
    {  -267142460,   274877488,  -260700427 },  /* phon=40.5 volume=-44.5 dB fs=48000 Hz biquad */
    {  -267106870,   274785598,  -260756728 },  /* phon=41.0 volume=-44.0 dB fs=48000 Hz biquad */
    {  -267070984,   274693964,  -260812476 },  /* phon=41.5 volume=-43.5 dB fs=48000 Hz biquad */
    {  -267034791,   274602594,  -260867653 },  /* phon=42.0 volume=-43.0 dB fs=48000 Hz biquad */
    {  -266998279,   274511500,  -260922235 },  /* phon=42.5 volume=-42.5 dB fs=48000 Hz biquad */
    {  -266961440,   274420691,  -260976205 },  /* phon=43.0 volume=-42.0 dB fs=48000 Hz biquad */
    {  -266924264,   274330172,  -261029548 },  /* phon=43.5 volume=-41.5 dB fs=48000 Hz biquad */
    {  -266886744,   274239953,  -261082247 },  /* phon=44.0 volume=-41.0 dB fs=48000 Hz biquad */
    {  -266848871,   274150037,  -261134290 },  /* phon=44.5 volume=-40.5 dB fs=48000 Hz biquad */
    {  -266810637,   274060432,  -261185661 },  /* phon=45.0 volume=-40.0 dB fs=48000 Hz biquad */
    {  -266772034,   273971142,  -261236348 },  /* phon=45.5 volume=-39.5 dB fs=48000 Hz biquad */
    {  -266733054,   273882172,  -261286338 },  /* phon=46.0 volume=-39.0 dB fs=48000 Hz biquad */
    {  -266693693,   273793524,  -261335626 },  /* phon=46.5 volume=-38.5 dB fs=48000 Hz biquad */
    {  -266653945,   273705198,  -261384203 },  /* phon=47.0 volume=-38.0 dB fs=48000 Hz biquad */
    {  -266613799,   273617202,  -261432053 },  /* phon=47.5 volume=-37.5 dB fs=48000 Hz biquad */
    {  -266573254,   273529533,  -261479177 },  /* phon=48.0 volume=-37.0 dB fs=48000 Hz biquad */
    {  -266532304,   273442191,  -261525570 },  /* phon=48.5 volume=-36.5 dB fs=48000 Hz biquad */
    {  -266490936,   273355184,  -261571208 },  /* phon=49.0 volume=-36.0 dB fs=48000 Hz biquad */
    {  -266449155,   273268501,  -261616110 },  /* phon=49.5 volume=-35.5 dB fs=48000 Hz biquad */
    {  -266406947,   273182151,  -261660252 },  /* phon=50.0 volume=-35.0 dB fs=48000 Hz biquad */
    {  -266364313,   273096127,  -261703642 },  /* phon=50.5 volume=-34.5 dB fs=48000 Hz biquad */
    {  -266321247,   273010429,  -261746273 },  /* phon=51.0 volume=-34.0 dB fs=48000 Hz biquad */
    {  -266277740,   272925057,  -261788139 },  /* phon=51.5 volume=-33.5 dB fs=48000 Hz biquad */
    {  -266233790,   272840009,  -261829238 },  /* phon=52.0 volume=-33.0 dB fs=48000 Hz biquad */
    {  -266189396,   272755277,  -261869574 },  /* phon=52.5 volume=-32.5 dB fs=48000 Hz biquad */
    {  -266144547,   272670865,  -261909138 },  /* phon=53.0 volume=-32.0 dB fs=48000 Hz biquad */
    {  -266099244,   272586765,  -261947936 },  /* phon=53.5 volume=-31.5 dB fs=48000 Hz biquad */
    {  -266053483,   272502973,  -261985966 },  /* phon=54.0 volume=-31.0 dB fs=48000 Hz biquad */
    {  -266007248,   272419495,  -262023208 },  /* phon=54.5 volume=-30.5 dB fs=48000 Hz biquad */
    {  -265960547,   272336317,  -262059686 },  /* phon=55.0 volume=-30.0 dB fs=48000 Hz biquad */
    {  -265913376,   272253435,  -262095396 },  /* phon=55.5 volume=-29.5 dB fs=48000 Hz biquad */
    {  -265865725,   272170849,  -262130332 },  /* phon=56.0 volume=-29.0 dB fs=48000 Hz biquad */
    {  -265817593,   272088552,  -262164497 },  /* phon=56.5 volume=-28.5 dB fs=48000 Hz biquad */
    {  -265768971,   272006542,  -262197886 },  /* phon=57.0 volume=-28.0 dB fs=48000 Hz biquad */
    {  -265719863,   271924809,  -262230510 },  /* phon=57.5 volume=-27.5 dB fs=48000 Hz biquad */
    {  -265670266,   271843347,  -262262374 },  /* phon=58.0 volume=-27.0 dB fs=48000 Hz biquad */
    {  -265620159,   271762162,  -262293453 },  /* phon=58.5 volume=-26.5 dB fs=48000 Hz biquad */
    {  -265569556,   271681236,  -262323776 },  /* phon=59.0 volume=-26.0 dB fs=48000 Hz biquad */
    {  -265518445,   271600570,  -262353331 },  /* phon=59.5 volume=-25.5 dB fs=48000 Hz biquad */
    {  -265466823,   271520156,  -262382123 },  /* phon=60.0 volume=-25.0 dB fs=48000 Hz biquad */
    {  -265414688,   271439988,  -262410157 },  /* phon=60.5 volume=-24.5 dB fs=48000 Hz biquad */
    {  -265362036,   271360059,  -262437434 },  /* phon=61.0 volume=-24.0 dB fs=48000 Hz biquad */
    {  -265308853,   271280369,  -262463941 },  /* phon=61.5 volume=-23.5 dB fs=48000 Hz biquad */
    {  -265255152,   271200903,  -262489705 },  /* phon=62.0 volume=-23.0 dB fs=48000 Hz biquad */
    {  -265200913,   271121661,  -262514709 },  /* phon=62.5 volume=-22.5 dB fs=48000 Hz biquad */
    {  -265146140,   271042635,  -262538962 },  /* phon=63.0 volume=-22.0 dB fs=48000 Hz biquad */
    {  -265090829,   270963817,  -262562468 },  /* phon=63.5 volume=-21.5 dB fs=48000 Hz biquad */
    {  -265034977,   270885200,  -262585233 },  /* phon=64.0 volume=-21.0 dB fs=48000 Hz biquad */
    {  -264978561,   270806786,  -262607232 },  /* phon=64.5 volume=-20.5 dB fs=48000 Hz biquad */
    {  -264921602,   270728557,  -262628501 },  /* phon=65.0 volume=-20.0 dB fs=48000 Hz biquad */
    {  -264864084,   270650512,  -262649028 },  /* phon=65.5 volume=-19.5 dB fs=48000 Hz biquad */
    {  -264806001,   270572644,  -262668813 },  /* phon=66.0 volume=-19.0 dB fs=48000 Hz biquad */
    {  -264747358,   270494942,  -262687872 },  /* phon=66.5 volume=-18.5 dB fs=48000 Hz biquad */
    {  -264688130,   270417408,  -262706178 },  /* phon=67.0 volume=-18.0 dB fs=48000 Hz biquad */
    {  -264628329,   270340028,  -262723757 },  /* phon=67.5 volume=-17.5 dB fs=48000 Hz biquad */
    {  -264567895,   270262811,  -262740540 },  /* phon=68.0 volume=-17.0 dB fs=48000 Hz biquad */
    {  -264506975,   270185708,  -262756723 },  /* phon=68.5 volume=-16.5 dB fs=48000 Hz biquad */
    {  -264445355,   270108769,  -262772043 },  /* phon=69.0 volume=-16.0 dB fs=48000 Hz biquad */
    {  -264383189,   270031943,  -262786702 },  /* phon=69.5 volume=-15.5 dB fs=48000 Hz biquad */
    {  -264320420,   269955239,  -262800637 },  /* phon=70.0 volume=-15.0 dB fs=48000 Hz biquad */
    {  -264257026,   269878651,  -262813831 },  /* phon=70.5 volume=-14.5 dB fs=48000 Hz biquad */
    {  -264193045,   269802165,  -262826337 },  /* phon=71.0 volume=-14.0 dB fs=48000 Hz biquad */
    {  -264128427,   269725782,  -262838101 },  /* phon=71.5 volume=-13.5 dB fs=48000 Hz biquad */
    {  -264063182,   269649491,  -262849147 },  /* phon=72.0 volume=-13.0 dB fs=48000 Hz biquad */
    {  -263997302,   269573285,  -262859473 },  /* phon=72.5 volume=-12.5 dB fs=48000 Hz biquad */
    {  -263930781,   269497158,  -262869079 },  /* phon=73.0 volume=-12.0 dB fs=48000 Hz biquad */
    {  -263863613,   269421102,  -262877967 },  /* phon=73.5 volume=-11.5 dB fs=48000 Hz biquad */
    {  -263795772,   269345112,  -262886116 },  /* phon=74.0 volume=-11.0 dB fs=48000 Hz biquad */
    {  -263727313,   269269174,  -262893595 },  /* phon=74.5 volume=-10.5 dB fs=48000 Hz biquad */
    {  -263658161,   269193287,  -262900331 },  /* phon=75.0 volume=-10.0 dB fs=48000 Hz biquad */
    {  -263588338,   269117441,  -262906353 },  /* phon=75.5 volume=-9.5 dB fs=48000 Hz biquad */
    {  -263517831,   269041630,  -262911658 },  /* phon=76.0 volume=-9.0 dB fs=48000 Hz biquad */
    {  -263446634,   268965845,  -262916245 },  /* phon=76.5 volume=-8.5 dB fs=48000 Hz biquad */
    {  -263374707,   268890081,  -262920082 },  /* phon=77.0 volume=-8.0 dB fs=48000 Hz biquad */
    {  -263302142,   268814324,  -262923273 },  /* phon=77.5 volume=-7.5 dB fs=48000 Hz biquad */
    {  -263228794,   268738575,  -262925675 },  /* phon=78.0 volume=-7.0 dB fs=48000 Hz biquad */
    {  -263154799,   268662819,  -262927435 },  /* phon=78.5 volume=-6.5 dB fs=48000 Hz biquad */
    {  -263080060,   268587053,  -262928463 },  /* phon=79.0 volume=-6.0 dB fs=48000 Hz biquad */
    {  -263004481,   268511269,  -262928668 },  /* phon=79.5 volume=-5.5 dB fs=48000 Hz biquad */
    {  -263028613,   268435456,  -263028613 },  /* phon=80.0 volume=-5.0 dB fs=48000 Hz biquad */
    {  -262871797,   268359821,  -262947431 },  /* phon=80.5 volume=-4.5 dB fs=48000 Hz biquad */
    {  -262794288,   268284145,  -262945599 },  /* phon=81.0 volume=-4.0 dB fs=48000 Hz biquad */
    {  -262715981,   268208418,  -262943018 },  /* phon=81.5 volume=-3.5 dB fs=48000 Hz biquad */
    {  -262636943,   268132635,  -262939763 },  /* phon=82.0 volume=-3.0 dB fs=48000 Hz biquad */
    {  -262557109,   268056787,  -262935779 },  /* phon=82.5 volume=-2.5 dB fs=48000 Hz biquad */
    {  -262476472,   267980865,  -262931064 },  /* phon=83.0 volume=-2.0 dB fs=48000 Hz biquad */
    {  -262395022,   267904861,  -262925617 },  /* phon=83.5 volume=-1.5 dB fs=48000 Hz biquad */
    {  -262312745,   267828768,  -262919434 },  /* phon=84.0 volume=-1.0 dB fs=48000 Hz biquad */
    {  -262229632,   267752575,  -262912513 },  /* phon=84.5 volume=-0.5 dB fs=48000 Hz biquad */
    {  -262145672,   267676276,  -262904852 },  /* phon=85.0 volume=0.0 dB fs=48000 Hz biquad */
};


static const biquad_first_order_quotients_t
highshelf_identity[LOUDNESS_CHANNELS] = {
    { 0, LOUDNESS_Q28_ONE, 0 },
    { 0, LOUDNESS_Q28_ONE, 0 }
};

static biquad_quotients_fast_t lowshelf_runtime_slot[2][LOUDNESS_CHANNELS];
static biquad_first_order_quotients_t highshelf_runtime_slot[2][LOUDNESS_CHANNELS];
static volatile uint8_t active_quotient_slot;
static biquad_state_fast_t     lowshelf_states[LOUDNESS_CHANNELS];
static const biquad_quotients_fast_t *active_lowshelf_table =
    lowshelf_and_volume_44100hz;
static const biquad_first_order_quotients_t *active_highshelf_table =
    highshelf_no_volume_44100hz;

static void loudness_filter_refresh_idle_cache(void);
static void swap_quotient_buffers(void);


static const biquad_quotients_fast_t *active_lowshelf_LUT_44100hz(void)
{
    return loudness_inferred_gain_has_source_volume_control() ? lowshelf_and_volume_44100hz : lowshelf_no_volume_44100hz;
}

static const biquad_quotients_fast_t *active_lowshelf_LUT_48000hz(void)
{
    return loudness_inferred_gain_has_source_volume_control() ? lowshelf_and_volume_48000hz : lowshelf_no_volume_48000hz;
}

static const biquad_quotients_fast_t *active_lowshelf_LUT(
    uint32_t frequency)
{
    return (frequency == (uint32_t)FREQ_48) ? active_lowshelf_LUT_48000hz() : active_lowshelf_LUT_44100hz();
}

static const biquad_quotients_fast_t *active_lowshelf_LUT_for_frequency(
    uint32_t frequency)
{
    if (loudness_active_filter() == BASS_BOOST_MODE) {
        return (frequency == (uint32_t)FREQ_48)
            ? lowshelf_no_volume_48000hz
            : lowshelf_no_volume_44100hz;
    }
    return active_lowshelf_LUT(frequency);
}

typedef struct {
    const biquad_quotients_fast_t *lowshelf;
    const biquad_first_order_quotients_t *highshelf;
} loudness_active_quotients_pair_t;

static loudness_active_quotients_pair_t loudness_snapshot_active_quotients(void)
{
    uint8_t slot = active_quotient_slot & 1u;
    loudness_active_quotients_pair_t q;
    q.lowshelf = lowshelf_runtime_slot[slot];
    q.highshelf = highshelf_runtime_slot[slot];
    return q;
}

const biquad_quotients_fast_t *loudness_lowshelf_active_quotients(void)
{
    return lowshelf_runtime_slot[active_quotient_slot & 1u];
}

const biquad_first_order_quotients_t *loudness_highshelf_active_quotients(void)
{
    return highshelf_runtime_slot[active_quotient_slot & 1u];
}

LOUDNESS_STATIC_INLINE int32_t loudness_lowshelf_inline(
    const int32_t x_n,
    biquad_state_fast_t *const restrict st,
    const biquad_quotients_fast_t *const restrict q)
{
    const int64_t fb1 = -((int64_t)q->a1 * (int64_t)st->w1);

    const int64_t acc1 = ((int64_t)x_n << LOUDNESS_DF2_Q28_SHIFT) + (fb1 << LOUDNESS_DF2_STATE_HEADROOM_M);
    const int64_t w_unscaled = acc1 >> LOUDNESS_DF2_Q28_SHIFT;

    const int64_t fb2 = FMA_24BIT(0, q->b1, st->w1);

    const int64_t acc2 = (int64_t)q->b0 * w_unscaled + (fb2 << LOUDNESS_DF2_STATE_HEADROOM_M);

    const int32_t y_n = saturate_24bit_s64_to_s32((acc2 + LOUDNESS_DF2_Q28_ROUND) >> LOUDNESS_DF2_Q28_SHIFT);
    const int32_t w0 = loudness_saturate_s64_to_s32(w_unscaled >> LOUDNESS_DF2_STATE_HEADROOM_M);
    st->w1 = w0;
    return y_n;
}

#ifdef BUILD_TESTING
int32_t loudness_lowshelf(int32_t x_n, biquad_state_fast_t *st, const biquad_quotients_fast_t *q)
{
    return loudness_lowshelf_inline(x_n, st, q);
}
#endif

LOUDNESS_STATIC_INLINE int32_t loudness_highshelf_inline(
    const int32_t x_n,
    biquad_first_order_state_t *const restrict st,
    const biquad_first_order_quotients_t *const restrict rt)
{
    const int64_t fb1 = -((int64_t)rt->a1 * (int64_t)st->w1);
    
    const int64_t acc1 = ((int64_t)x_n << LOUDNESS_DF2_Q28_SHIFT) + (fb1 << LOUDNESS_DF2_STATE_HEADROOM_M);
    const int64_t w_unscaled = acc1 >> LOUDNESS_DF2_Q28_SHIFT;

    const int64_t fb2 = FMA_24BIT(0, rt->b1, st->w1);
    
    const int64_t acc2 = (int64_t)rt->b0 * w_unscaled + (fb2 << LOUDNESS_DF2_STATE_HEADROOM_M);
    
    const int32_t y_n = saturate_24bit_s64_to_s32((acc2 + LOUDNESS_DF2_Q28_ROUND) >> LOUDNESS_DF2_Q28_SHIFT);
    const int32_t w0 = loudness_saturate_s64_to_s32(w_unscaled >> LOUDNESS_DF2_STATE_HEADROOM_M);
    st->w1 = w0;
    return y_n;
}

#ifdef BUILD_TESTING
int32_t loudness_highshelf(int32_t x_n, biquad_first_order_state_t *st, const biquad_first_order_quotients_t *rt)
{
    return loudness_highshelf_inline(x_n, st, rt);
}
#endif

LOUDNESS_STATIC_INLINE int32_t loudness_downsample_filter_to_16bit_container(const int32_t y_24)
{
    int32_t s = y_24 >> 8;
    return s << 16;
}

LOUDNESS_STATIC_INLINE int32_t loudness_cascade_biquad_step(
    const int32_t x_n,
    biquad_state_fast_t *const restrict st_low,
    const biquad_quotients_fast_t *const restrict q_low,
    biquad_first_order_state_t *const restrict st_high,
    const biquad_first_order_quotients_t *const restrict q_high)
{
    const int32_t y_low = loudness_lowshelf_inline(x_n, st_low, q_low);
    return loudness_highshelf_inline(y_low, st_high, q_high);
}

LOUDNESS_STATIC_INLINE int32_t loudness_cascade_16bit_container_step(
    const int32_t x_container,
    biquad_state_fast_t *const restrict st_low,
    const biquad_quotients_fast_t *const restrict q_low,
    biquad_first_order_state_t *const restrict st_high,
    const biquad_first_order_quotients_t *const restrict q_high)
{
    const int32_t x_24 = (int32_t)(int16_t)(x_container >> 16) << 8;
    const int32_t y_24 = loudness_cascade_biquad_step(x_24, st_low, q_low, st_high, q_high);
    return loudness_downsample_filter_to_16bit_container(y_24);
}

LOUDNESS_STATIC_INLINE int32_t loudness_cascade_24bit_container_step(
    const int32_t x_container,
    biquad_state_fast_t *const restrict st_low,
    const biquad_quotients_fast_t *const restrict q_low,
    biquad_first_order_state_t *const restrict st_high,
    const biquad_first_order_quotients_t *const restrict q_high)
{
    const int32_t x_24 = x_container >> 8;
    const int32_t y_24 = loudness_cascade_biquad_step(x_24, st_low, q_low, st_high, q_high);
    return y_24 << 8;
}

/*
 * We never advance filters more than 1 step and 0.5 dB up per iteration.
 * See graph the biquad tables for intuitive explanation.
 * 
 * We do this  guard analog amplifiers from instant amplification spikes which can sound equipment.
 * 
 * Decreasing the filters happends instant. Decreasing volumes does not damanage equipment.
 */
static int loudness_advance_toward_equilizer_step(uint8_t committed, int desired)
{
    if (desired == LOUDNESS_EQUALIZER_STEP_UNSET) {
        return LOUDNESS_EQUALIZER_STEP_UNSET;
    }
    int effective = (committed == LOUDNESS_EQUALIZER_STEP_UNSET ? 0 : committed);
    return (desired > effective) ? effective + 1 : desired;
}

static Bool loudness_filter_has_reached_equlizer_step(int desired_left, int desired_right)
{
    return (int)committed_equalizer_step[0] == desired_left && (int)committed_equalizer_step[1] == desired_right;
}

static Bool loudness_fast_advance_toward_steps(int desired_left, int desired_right)
{
    if (loudness_filter_has_reached_equlizer_step(desired_left, desired_right)) {
        return TRUE;
    }

    int next_left = loudness_advance_toward_equilizer_step(
        committed_equalizer_step[0], desired_left);
    int next_right = loudness_advance_toward_equilizer_step(
        committed_equalizer_step[1], desired_right);

    loudness_fast_prepare_inactive_quotients(
        active_lowshelf_table, next_left, next_right);
    swap_quotient_buffers();
    committed_equalizer_step[0] = (uint8_t)next_left;
    committed_equalizer_step[1] = (uint8_t)next_right;
    return loudness_filter_has_reached_equlizer_step(desired_left, desired_right);
}

static void loudness_fast_run_toward_steps(int desired_left, int desired_right)
{
    if (!loudness_rtos_is_ready()) {
        while (!loudness_fast_advance_toward_steps(desired_left, desired_right)) {
        }
    } else {
        (void)loudness_fast_advance_toward_steps(desired_left, desired_right);
    }
}

static void loudness_fast_begin_switch_report_if_needed(
    int32_t prev_db_spl_left_x10, int32_t prev_db_spl_right_x10,
    int32_t db_spl_left_x10, int32_t db_spl_right_x10,
    int desired_left, int desired_right)
{
    Bool left_is_dominant = (db_spl_left_x10 >= db_spl_right_x10);
    Bool prev_left_is_dominant = (prev_db_spl_left_x10 >= prev_db_spl_right_x10);
    int prev_dominant_step = prev_left_is_dominant
        ? loudness_get_equalizer_step(prev_db_spl_left_x10)
        : loudness_get_equalizer_step(prev_db_spl_right_x10);
    int target_dominant_step = left_is_dominant ? desired_left : desired_right;

    if (prev_dominant_step == target_dominant_step) {
        return;
    }

    if (loudness_fast_switch_report_from_step < 0) {
        loudness_fast_switch_report_from_step = prev_dominant_step;
        loudness_fast_switch_report_from_db_spl_x10 = prev_left_is_dominant
            ? prev_db_spl_left_x10 : prev_db_spl_right_x10;
    }

    loudness_fast_switch_report_to_step = target_dominant_step;
    loudness_fast_switch_report_to_db_spl_x10 = left_is_dominant
        ? db_spl_left_x10 : db_spl_right_x10;
}

static void loudness_fast_finish_switch_report_if_needed(
    int desired_left, int desired_right)
{
    if (loudness_fast_switch_report_from_step < 0) {
        return;
    }
    if (!loudness_filter_has_reached_equlizer_step(desired_left, desired_right)) {
        return;
    }

    if (loudness_fast_switch_report_from_step
        != loudness_fast_switch_report_to_step) {
        loudness_report_equalizer_step_switch(
            loudness_fast_switch_report_from_db_spl_x10,
            loudness_fast_switch_report_to_db_spl_x10,
            loudness_fast_switch_report_from_step,
            loudness_fast_switch_report_to_step);
    }
    loudness_fast_switch_report_from_step = -1;
}

static void swap_quotient_buffers(void)
{
    loudness_fast_memory_barrier();
    active_quotient_slot ^= 1u;
}

const biquad_quotients_fast_t *loudness_fast_baked_quotient_table_48000hz(void)
{
    return active_lowshelf_LUT_48000hz();
}

const biquad_quotients_fast_t *loudness_fast_baked_quotient_table_44100hz(void)
{
    return active_lowshelf_LUT_44100hz();
}

const biquad_quotients_fast_t *loudness_fast_no_volume_quotient_table_48000hz(void)
{
    return lowshelf_no_volume_48000hz;
}

void loudness_fast_refresh_quotient_table_pointers(void)
{
    switch (loudness_filter_frequency_hz)
    {
        case FREQ_48:
            active_lowshelf_table = active_lowshelf_LUT_for_frequency(FREQ_48);
            active_highshelf_table = active_highshelf_LUT_for_frequency(FREQ_48);
            break;
        case FREQ_44:
        case 0:
            active_lowshelf_table = active_lowshelf_LUT_for_frequency(FREQ_44);
            active_highshelf_table = active_highshelf_LUT_for_frequency(FREQ_44);
            break;
        default:
            break;
    }
}

void loudness_fast_prepare_inactive_quotients(const biquad_quotients_fast_t *table, int equalizer_step_left, int equalizer_step_right)
{
    uint8_t inactive = (uint8_t)(active_quotient_slot ^ 1u);
    int equalizer_steps[] = { equalizer_step_left, equalizer_step_right };
    int channel;
    for (channel = 0; channel < LOUDNESS_CHANNELS; channel++) {
        int step = equalizer_steps[channel];
        lowshelf_runtime_slot[inactive][channel] = table[step];
        if (loudness_active_filter() == BASS_BOOST_MODE) {
            highshelf_runtime_slot[inactive][channel] = highshelf_identity[channel];
        } else {
            highshelf_runtime_slot[inactive][channel] = active_highshelf_table[step];
        }
    }
}


void loudness_publish_quotients(void)
{
    swap_quotient_buffers();
}

biquad_state_fast_t *loudness_fast_biquad_state(int channel)
{
    if (channel < 0 || channel >= LOUDNESS_CHANNELS) {
        return &lowshelf_states[0];
    }
    return &lowshelf_states[channel];
}

const biquad_quotients_fast_t *loudness_fast_channel_quotients(int channel)
{
    if (channel < 0 || channel >= LOUDNESS_CHANNELS) {
        channel = 0;
    }
    return &loudness_lowshelf_active_quotients()[channel];
}

void loudness_refresh_quotient_table_selection(void)
{
    int32_t db_spl_left_x10 = loudness_get_db_spl_left_x10();
    int32_t db_spl_right_x10 = loudness_get_db_spl_right_x10();
    int equalizer_step_left;
    int equalizer_step_right;

    if (loudness_filter_frequency_hz == 0) {
        loudness_fast_refresh_quotient_table_pointers();
        return;
    }

    loudness_fast_refresh_quotient_table_pointers();
    loudness_equalizer_steps_for_mode(
        db_spl_left_x10, db_spl_right_x10,
        &equalizer_step_left, &equalizer_step_right);

    loudness_fast_select_equalizer_steps(
        db_spl_left_x10, db_spl_right_x10,
        equalizer_step_left, equalizer_step_right);
    loudness_filter_refresh_idle_cache();
}

#ifdef BUILD_TESTING
void loudness_test_load_active_quotients_fast(int equalizer_step)
{
    loudness_fast_prepare_inactive_quotients(
        active_lowshelf_table, equalizer_step, equalizer_step);
    taskENTER_CRITICAL();
    loudness_publish_quotients();
    taskEXIT_CRITICAL();
    committed_equalizer_step[0] = (uint8_t)equalizer_step;
    committed_equalizer_step[1] = (uint8_t)equalizer_step;
    loudness_fast_switch_report_from_step = -1;
}

void loudness_test_load_quotient_table_fast(
    const biquad_quotients_fast_t *table, int equalizer_step)
{
    loudness_fast_prepare_inactive_quotients(
        table, equalizer_step, equalizer_step);
    taskENTER_CRITICAL();
    loudness_publish_quotients();
    taskEXIT_CRITICAL();
    committed_equalizer_step[0] = (uint8_t)equalizer_step;
    committed_equalizer_step[1] = (uint8_t)equalizer_step;
    loudness_fast_switch_report_from_step = -1;
}

void loudness_test_get_fast_channel(int channel,
    biquad_state_fast_t *state, biquad_quotients_fast_t *quotients)
{
    if (channel < 0 || channel >= LOUDNESS_CHANNELS) {
        return;
    }
    taskENTER_CRITICAL();
    *state = lowshelf_states[channel];
    if (quotients != NULL) {
        *quotients = loudness_lowshelf_active_quotients()[channel];
    }
    taskEXIT_CRITICAL();
}

void loudness_test_set_fast_channel(int channel,
    const biquad_state_fast_t *state)
{
    if (channel < 0 || channel >= LOUDNESS_CHANNELS) {
        return;
    }
    taskENTER_CRITICAL();
    lowshelf_states[channel] = *state;
    taskEXIT_CRITICAL();
}

#endif

void loudness_fast_select_equalizer_steps(int32_t db_spl_left_x10, int32_t db_spl_right_x10, int equalizer_step_left, int equalizer_step_right)
{
    int32_t prev_db_spl_left_x10 = (int32_t)last_db_spl_left_x10;
    int32_t prev_db_spl_right_x10 = (int32_t)last_db_spl_right_x10;

    loudness_publish_equalizer_step(db_spl_left_x10, db_spl_right_x10);

    if (loudness_filter_has_reached_equlizer_step(equalizer_step_left, equalizer_step_right)) {
        loudness_fast_finish_switch_report_if_needed(
            equalizer_step_left, equalizer_step_right);
        return;
    }

    loudness_fast_begin_switch_report_if_needed(
        prev_db_spl_left_x10, prev_db_spl_right_x10,
        db_spl_left_x10, db_spl_right_x10,
        equalizer_step_left, equalizer_step_right);
    loudness_fast_run_toward_steps(equalizer_step_left, equalizer_step_right);
    loudness_fast_finish_switch_report_if_needed(
        equalizer_step_left, equalizer_step_right);
}

void loudness_fast_select_unity_passthrough(void)
{
    biquad_quotients_fast_t unity;
    biquad_first_order_quotients_t unity_highshelf;
    uint8_t channel;
    uint8_t inactive = (uint8_t)(active_quotient_slot ^ 1u);

    unity.b0 = LOUDNESS_Q28_ONE;
    unity.b1 = 0;
    unity.a1 = 0;

    unity_highshelf.a1 = 0;
    unity_highshelf.b0 = LOUDNESS_Q28_ONE;
    unity_highshelf.b1 = 0;

    for (channel = 0; channel < LOUDNESS_CHANNELS; channel++) {
        lowshelf_runtime_slot[inactive][channel] = unity;
        highshelf_runtime_slot[inactive][channel] = unity_highshelf;
    }
    swap_quotient_buffers();
    committed_equalizer_step[0] = LOUDNESS_EQUALIZER_STEP_UNSET;
    committed_equalizer_step[1] = LOUDNESS_EQUALIZER_STEP_UNSET;
    loudness_fast_switch_report_from_step = -1;
}

LOUDNESS_STATIC_INLINE Bool loudness_lowshelf_biquad_is_idle(int channel)
{
    return lowshelf_states[channel].w1 == 0;
}

static void loudness_filter_refresh_idle_cache(void)
{
    loudness_idle_mask_t mask = 0;
    if (loudness_lowshelf_biquad_is_idle(0)) mask |= LOUDNESS_LOWSHELF_LEFT;
    if (loudness_lowshelf_biquad_is_idle(1)) mask |= LOUDNESS_LOWSHELF_RIGHT;
    if (loudness_highshelf_biquad_is_idle(0)) mask |= LOUDNESS_HIGHSHELF_LEFT;
    if (loudness_highshelf_biquad_is_idle(1)) mask |= LOUDNESS_HIGHSHELF_RIGHT;
    filter_idle_cached = mask;
}

void loudness_lowshelf_reset_states(void)
{
    memset(lowshelf_states, 0, sizeof(lowshelf_states));
}

void loudness_fast_reset_states(void)
{
    taskENTER_CRITICAL();
    loudness_lowshelf_reset_states();
    loudness_highshelf_reset_states();
    taskEXIT_CRITICAL();
    loudness_filter_refresh_idle_cache();
}

static const uint32_t ZERO_BUFFER[UAC2_USB_OUT_MAX_STEREO_SAMPLES] = {0}; // max UAC2 stereo frames per channel (EP_OUT_LENGTH_2_HS / 8)
Bool loudness_stereo_packet_all_zero(S32 *restrict sample_L, S32 *restrict sample_R, U16 num_samples)
{
    return (memcmp(sample_L, ZERO_BUFFER, num_samples * sizeof(S32)) == 0) && (memcmp(sample_R, ZERO_BUFFER, num_samples * sizeof(S32)) == 0);
}

Bool filter_is_idle_and_packet_is_silent(S32 *restrict sample_L, S32 *restrict sample_R, U16 num_samples)
{
    return filter_idle_cached == LOUDNESS_FILTER_ALL && loudness_stereo_packet_all_zero(sample_L, sample_R, num_samples);
}

#define PROCESS_SAMPLE(step_func, i) \
    do { \
        sample_L[i] = step_func(sample_L[i], lowshelf_state_L, q_L, highshelf_state_L, q_high_L); \
        sample_R[i] = step_func(sample_R[i], lowshelf_state_R, q_R, highshelf_state_R, q_high_R); \
    } while (0)

#define UNROLL_STEP_0(step_func)  PROCESS_SAMPLE(step_func, 0);
#define UNROLL_STEP_1(step_func)  UNROLL_STEP_0(step_func)  PROCESS_SAMPLE(step_func, 1);
#define UNROLL_STEP_2(step_func)  UNROLL_STEP_1(step_func)  PROCESS_SAMPLE(step_func, 2);
#define UNROLL_STEP_3(step_func)  UNROLL_STEP_2(step_func)  PROCESS_SAMPLE(step_func, 3);
#define UNROLL_STEP_4(step_func)  UNROLL_STEP_3(step_func)  PROCESS_SAMPLE(step_func, 4);
#define UNROLL_STEP_5(step_func)  UNROLL_STEP_4(step_func)  PROCESS_SAMPLE(step_func, 5);
#define UNROLL_STEP_6(step_func)  UNROLL_STEP_5(step_func)  PROCESS_SAMPLE(step_func, 6);
#define UNROLL_STEP_7(step_func)  UNROLL_STEP_6(step_func)  PROCESS_SAMPLE(step_func, 7);
#define UNROLL_STEP_8(step_func)  UNROLL_STEP_7(step_func)  PROCESS_SAMPLE(step_func, 8);
#define UNROLL_STEP_9(step_func)  UNROLL_STEP_8(step_func)  PROCESS_SAMPLE(step_func, 9);
#define UNROLL_STEP_10(step_func) UNROLL_STEP_9(step_func)  PROCESS_SAMPLE(step_func, 10);
#define UNROLL_11(step_func) UNROLL_STEP_10(step_func)

#define PROCESS_STEREO_PACKET(step_func) \
    { \
        if (__builtin_expect(num_samples == 11 || num_samples == 12, TRUE)) { \
            UNROLL_11(step_func) \
            if (num_samples == 12) \
                PROCESS_SAMPLE(step_func, 11); \
        } else { \
            for (int i = 0; i < num_samples; i++) { \
                PROCESS_SAMPLE(step_func, i); \
            } \
        } \
    }

#define UNROLL_STEP_11(step_func) UNROLL_STEP_10(step_func)  PROCESS_SAMPLE(step_func, 11);
#define UNROLL_STEP_12(step_func) UNROLL_STEP_11(step_func)  PROCESS_SAMPLE(step_func, 12);
#define UNROLL_STEP_13(step_func) UNROLL_STEP_12(step_func)  PROCESS_SAMPLE(step_func, 13);
#define UNROLL_STEP_14(step_func) UNROLL_STEP_13(step_func)  PROCESS_SAMPLE(step_func, 14);
#define UNROLL_STEP_15(step_func) UNROLL_STEP_14(step_func)  PROCESS_SAMPLE(step_func, 15);
#define UNROLL_STEP_16(step_func) UNROLL_STEP_15(step_func)  PROCESS_SAMPLE(step_func, 16);
#define UNROLL_STEP_17(step_func) UNROLL_STEP_16(step_func)  PROCESS_SAMPLE(step_func, 17);
#define UNROLL_STEP_18(step_func) UNROLL_STEP_17(step_func)  PROCESS_SAMPLE(step_func, 18);
#define UNROLL_STEP_19(step_func) UNROLL_STEP_18(step_func)  PROCESS_SAMPLE(step_func, 19);
#define UNROLL_STEP_20(step_func) UNROLL_STEP_19(step_func)  PROCESS_SAMPLE(step_func, 20);
#define UNROLL_STEP_21(step_func) UNROLL_STEP_20(step_func)  PROCESS_SAMPLE(step_func, 21);
#define UNROLL_STEP_22(step_func) UNROLL_STEP_21(step_func)  PROCESS_SAMPLE(step_func, 22);
#define UNROLL_23(step_func) UNROLL_STEP_22(step_func)

#define PROCESS_STEREO_PACKET_2X_HZ(step_func) \
    { \
        if (__builtin_expect(num_samples == 23 || num_samples == 24, TRUE)) { \
            UNROLL_23(step_func) \
            if (num_samples == 24) \
                PROCESS_SAMPLE(step_func, 24); \
        } else { \
            for (int i = 0; i < num_samples; i++) { \
                PROCESS_SAMPLE(step_func, i); \
            } \
        } \
    }

typedef int32_t (*loudness_cascade_step_fn)(
    int32_t x_container,
    biquad_state_fast_t *restrict st_low,
    const biquad_quotients_fast_t *restrict q_low,
    biquad_first_order_state_t *restrict st_high,
    const biquad_first_order_quotients_t *restrict q_high);

typedef enum {
    LOUDNESS_PACKET_UNROLL_1X = 0,
    LOUDNESS_PACKET_UNROLL_2X = 1
} loudness_packet_unroll_t;

static void loudness_filter_stereo_packet_impl(
    S32 *restrict sample_L,
    S32 *restrict sample_R,
    U16 num_samples,
    loudness_cascade_step_fn step_fn,
    loudness_packet_unroll_t unroll)
{
    if (filter_is_idle_and_packet_is_silent(sample_L, sample_R, num_samples)) {
        return;
    }

    biquad_state_fast_t* const restrict lowshelf_state_L = &lowshelf_states[0];
    biquad_state_fast_t* const restrict lowshelf_state_R = &lowshelf_states[1];

    const loudness_active_quotients_pair_t q = loudness_snapshot_active_quotients();
    const biquad_quotients_fast_t* const restrict q_L = &q.lowshelf[0];
    const biquad_quotients_fast_t* const restrict q_R = &q.lowshelf[1];

    biquad_first_order_state_t* const restrict highshelf_state_L = &highshelf_states[0];
    biquad_first_order_state_t* const restrict highshelf_state_R = &highshelf_states[1];
    const biquad_first_order_quotients_t* const restrict q_high_L = &q.highshelf[0];
    const biquad_first_order_quotients_t* const restrict q_high_R = &q.highshelf[1];

    if (unroll == LOUDNESS_PACKET_UNROLL_2X) {
        PROCESS_STEREO_PACKET_2X_HZ(step_fn);
    } else {
        PROCESS_STEREO_PACKET(step_fn);
    }

    loudness_filter_refresh_idle_cache();
}

void loudness_filter_16bit_stereo_packet(S32 *restrict sample_L, S32 *restrict sample_R, U16 num_samples)
{
    loudness_filter_stereo_packet_impl(
        sample_L, sample_R, num_samples,
        loudness_cascade_16bit_container_step, LOUDNESS_PACKET_UNROLL_1X);
}

void loudness_filter_24bit_stereo_packet(S32 *restrict sample_L, S32 *restrict sample_R, U16 num_samples)
{
    loudness_filter_stereo_packet_impl(
        sample_L, sample_R, num_samples,
        loudness_cascade_24bit_container_step, LOUDNESS_PACKET_UNROLL_1X);
}

void loudness_filter_16bit_stereo_packet_2x_hz(S32 *restrict sample_L, S32 *restrict sample_R, U16 num_samples)
{
    loudness_filter_stereo_packet_impl(
        sample_L, sample_R, num_samples,
        loudness_cascade_16bit_container_step, LOUDNESS_PACKET_UNROLL_2X);
}

void loudness_filter_24bit_stereo_packet_2x_hz(S32 *restrict sample_L, S32 *restrict sample_R, U16 num_samples)
{
    loudness_filter_stereo_packet_impl(
        sample_L, sample_R, num_samples,
        loudness_cascade_24bit_container_step, LOUDNESS_PACKET_UNROLL_2X);
}

void loudness_change_frequency_fast(uint32_t frequency) {
    int equalizer_step_left;
    int equalizer_step_right;

    if (frequency != (uint32_t)FREQ_44 && frequency != (uint32_t)FREQ_48) {
        return;
    }

    loudness_filter_frequency_hz = frequency;
    loudness_fast_refresh_quotient_table_pointers();
    loudness_fast_reset_states();

    loudness_equalizer_steps_for_mode(
        loudness_get_db_spl_left_x10(),
        loudness_get_db_spl_right_x10(),
        &equalizer_step_left, &equalizer_step_right);

    loudness_fast_prepare_inactive_quotients(
        active_lowshelf_table, equalizer_step_left, equalizer_step_right);
    swap_quotient_buffers();
    committed_equalizer_step[0] = (uint8_t)equalizer_step_left;
    committed_equalizer_step[1] = (uint8_t)equalizer_step_right;
    loudness_fast_switch_report_from_step = -1;
    loudness_filter_refresh_idle_cache();
}

#ifdef BUILD_TESTING
uint8_t loudness_test_get_filter_idle_mask(void)
{
    return filter_idle_cached;
}
#endif
