#ifndef P_AVX512_H
#define P_AVX512_H

#ifndef P_H
#warning "you should not include per-architecture headers directly, include p.h"
#endif

#include <immintrin.h>

#define VECTORLEN (16)
typedef u8  u8vu __attribute__((vector_size(16), may_alias, aligned(1)));
typedef u8   u8v __attribute__((vector_size(16), may_alias));
typedef u16 u16v __attribute__((vector_size(16), may_alias));
typedef u32 u32v __attribute__((vector_size(16), may_alias));
typedef u64 u64v __attribute__((vector_size(16), may_alias));

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
	__m128i zero = {};

	*o0 = (u16v)_mm_unpacklo_epi8((__m128i)in, zero);
	*o1 = (u16v)_mm_unpackhi_epi8((__m128i)in, zero);
}

/* horizontal sum (sum of all vector elements) */
static inline u64 hsum_u64v(u64v in)
{
	return in[0] + in[1];
}

static inline u64 hsum_u16v(u16v in)
{
	__m128i lomask = _mm_set1_epi16(0xff);
	__m128i himask = ~lomask;
	__m128i zero = {};
	u64v lo = (u64v)_mm_sad_epu8((__m128i)in & lomask, zero);
	u64v hi = (u64v)_mm_sad_epu8((__m128i)in & himask, zero);
	lo += hi<<8;
	return hsum_u64v(lo);
}

#endif
