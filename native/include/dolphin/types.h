#ifndef _DOLPHIN_TYPES_H_
#define _DOLPHIN_TYPES_H_
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <math.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
typedef int8_t s8;
typedef uint8_t u8;
typedef int16_t s16;
typedef uint16_t u16;
typedef int32_t s32;
typedef uint32_t u32;
typedef int64_t s64;
typedef uint64_t u64;
typedef float f32;
typedef double f64;
typedef volatile f32 vf32;
typedef volatile f64 vf64;
typedef char* Ptr;
typedef int BOOL;
#define FALSE 0
#define TRUE 1
#define ATTRIBUTE_ALIGN(num) __attribute__((aligned(num)))
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif
