#ifndef COMPILER_H
#define COMPILER_H

#include <stdint.h>
#include <stdbool.h>

typedef uint8_t  U8;
typedef uint16_t U16;
typedef uint32_t U32;
typedef uint64_t U64;
typedef int8_t   S8;
typedef int16_t  S16;
typedef int32_t  S32;
typedef int64_t  S64;

typedef bool Bool;

#ifdef BUILD_TESTING
typedef struct avr32_ssc_t avr32_ssc_t;
typedef struct avr32_pdca_channel_t avr32_pdca_channel_t;
typedef void *xSemaphoreHandle;
typedef void *xTaskHandle;
typedef void *xQueueHandle;
#endif

#ifndef TRUE
#define TRUE true
#endif

#ifndef FALSE
#define FALSE false
#endif

#ifdef _MSC_VER
#define PACK( __Declaration__ ) __pragma( pack(push, 1) ) __Declaration__ __pragma( pack(pop))
#define __builtin_expect(expr, val) (expr)
#else
#define PACK( __Declaration__ ) __Declaration__ __attribute__((__packed__))
#endif

#endif
