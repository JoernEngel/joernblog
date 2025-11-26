#ifndef P_AVX512_H
#define P_AVX512_H

#ifndef P_H
#warning "you should not include per-architecture headers directly, include p.h"
#endif

#include <immintrin.h>

#define VECTORLEN (32)
typedef u8  u8vu __attribute__((vector_size(32), may_alias, aligned(1)));
typedef u8   u8v __attribute__((vector_size(32), may_alias));
typedef u16 u16v __attribute__((vector_size(32), may_alias));
typedef u32 u32v __attribute__((vector_size(32), may_alias));
typedef u64 u64v __attribute__((vector_size(32), may_alias));

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
	*o0 = (u16v)_mm256_cvtepu8_epi16(_mm256_castsi256_si128((__m256i)in));
	*o1 = (u16v)_mm256_cvtepu8_epi16(_mm256_extracti128_si256((__m256i)in, 1));
}

/* horizontal sum (sum of all vector elements) */
static inline u64 hsum_u64v(u64v in)
{
	__m256i sum256 = (__m256i)in;
	__m128i sum128 = _mm256_extracti128_si256(sum256, 1) + _mm256_castsi256_si128(sum256);
	return sum128[0] + sum128[1];
}

static inline u64 hsum_u16v(u16v in)
{
	__m256i lomask = _mm256_set1_epi16(0xff);
	__m256i himask = ~lomask;
	__m256i zero = {};
	u64v lo = (u64v)_mm256_sad_epu8((__m256i)in & lomask, zero);
	u64v hi = (u64v)_mm256_sad_epu8((__m256i)in & himask, zero);
	lo += hi<<8;
	return hsum_u64v(lo);
}

#endif
