#ifndef P_AVX512_H
#define P_AVX512_H

#ifndef P_H
#warning "you should not include per-architecture headers directly, include p.h"
#endif

#include <immintrin.h>

#define VECTORLEN (64)
typedef u8  u8vu __attribute__((vector_size(64), may_alias, aligned(1)));
typedef u8   u8v __attribute__((vector_size(64), may_alias));
typedef u16 u16v __attribute__((vector_size(64), may_alias));
typedef u32 u32v __attribute__((vector_size(64), may_alias));
typedef u64 u64v __attribute__((vector_size(64), may_alias));

static inline u8v readv(const void *p) { return *(const u8vu*)p; }

/* debug helper */
static inline void hexdumpv(const void *p, const char *s)
{
	const u8 *in = p;
	for (int i=0; i<VECTORLEN; i++) {
		fprintf(stderr, "%02x", in[i]);
		if (i==VECTORLEN-1)
			fprintf(stderr, "  %s\n", s);
		else if (i%8 == 7)
			fprintf(stderr, "  ");
		else
			fprintf(stderr, " ");
	}
}

/* widen vector elements from u8 to u16 */
static inline void widen_u8v(u16v *o0, u16v *o1, u8v in)
{
	u16v map0 = {  0,  1,  2,  3,  4,  5,  6,  7,   8,  9, 10, 11, 12, 13, 14, 15,  16, 17, 18, 19, 20, 21, 22, 23,  24, 25, 26, 27, 28, 29, 30, 31 };
	u16v map1 = { 32, 33, 34, 35, 36, 37, 38, 39,  40, 41, 42, 43, 44, 45, 46, 47,  48, 49, 50, 51, 52, 53, 54, 55,  56, 57, 58, 59, 60, 61, 62, 63 };
	*o0 = (u16v)_mm512_permutexvar_epi8((__m512i)map0, (__m512i)in);
	*o1 = (u16v)_mm512_permutexvar_epi8((__m512i)map1, (__m512i)in);
}

/* horizontal sum (sum of all vector elements) */
static inline u64 hsum_u64v(u64v in)
{
	__m512i sum512 = (__m512i)in;
	__m256i sum256 = _mm512_extracti64x4_epi64(sum512, 1) + _mm512_castsi512_si256(sum512);
	__m128i sum128 = _mm256_extracti128_si256(sum256, 1) + _mm256_castsi256_si128(sum256);
	return sum128[0] + sum128[1];
}

static inline u64 hsum_u16v(u16v in)
{
	__m512i lomask = _mm512_set1_epi16(0xff);
	__m512i himask = ~lomask;
	__m512i zero = {};
	u64v lo = (u64v)_mm512_sad_epu8((__m512i)in & lomask, zero);
	u64v hi = (u64v)_mm512_sad_epu8((__m512i)in & himask, zero);
	lo += hi<<8;
	return hsum_u64v(lo);
}

#endif
