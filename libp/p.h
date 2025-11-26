#ifndef P_H
#define P_H

/*
 * libp or just P is my C-style abstraction for vector instructions.  Goal
 * is to behave roughly like C does compared to assembler instructions.
 * Since I am not above a little hubris, P is the next letter in BCPL after
 * Ken Thompson's B and Denis Ritchie's C.  Maybe this is even half as good
 * as either of those two were.
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef unsigned __int128 u128;

/* TODO: support and test on ARM (neon/sve) */

#if defined __AVX512F__ && defined __AVX512BW__ && __AVX512VBMI2__
#include <p_avx512.h>
#elif defined __AVX2__
#include <p_avx2.h>
#elif defined __SSE4_2__
#include <p_sse42.h>
#else
#error "compiling for unsupported CPU, check compiler flags"
#endif

#endif
