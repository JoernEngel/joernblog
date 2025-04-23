#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <immintrin.h>

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef unsigned long long u64;
typedef unsigned __int128 u128;

static void hgram_scalar(u16 hgram[256], u8 *buf, u16 n)
{
	for (int i=0; i<256; i++)
		hgram[i] = 0;
	for (u32 i=0; i<n; i++)
		hgram[buf[i]]++;
}

/* compute 16 32bit counts from 32*16 1bit counts */
static inline __m512i bitpermute_popcnt16(__m512i v)
{
	__m512i byte_permute1 = {
		/* even bytes */
		0x0e0c0a0806040200ull,
		0x1e1c1a1816141210ull,
		0x2e2c2a2826242220ull,
		0x3e3c3a3836343230ull,
		/* odd bytes */
		0x0f0d0b0907050301ull,
		0x1f1d1b1917151311ull,
		0x2f2d2b2927252321ull,
		0x3f3d3b3937353331ull,
	};
	__m512i bit_permute = _mm512_set1_epi64(0x8040201008040201);
	__m512i byte_permute2 = {
		0x1911090118100800ull, /* 1+0 */
		0x1b130b031a120a02ull, /* 3+2 */
		0x1d150d051c140c04ull, /* 5+4 */
		0x1f170f071e160e06ull, /* 7+6 */
		0x3931292138302820ull, /* 9+8 */
		0x3b332b233a322a22ull, /* b+a */
		0x3d352d253c342c24ull, /* d+c */
		0x3f372f273e362e26ull, /* f+e */
	};

	// Change v from 512x 1-bit counters to 16x 32-bit counters.
	v = _mm512_permutexvar_epi8(byte_permute1, v);
	v = _mm512_gf2p8affine_epi64_epi8(bit_permute, v, 0);
	v = _mm512_permutexvar_epi8(byte_permute2, v);
	v = _mm512_popcnt_epi32(v);
	return v;
}

/* compute 32 16bit counts from 16*32 1bit counts */
static inline __m512i bitpermute_popcnt32(__m512i v)
{
	__m512i byte_permute1 = {
		0x3830282018100800ull,
		0x3931292119110901ull,
		0x3a322a221a120a02ull,
		0x3b332b231b130b03ull,
		0x3c342c241c140c04ull,
		0x3d352d251d150d05ull,
		0x3e362e261e160e06ull,
		0x3f372f271f170f07ull,
	};
	__m512i bit_permute = _mm512_set1_epi64(0x8040201008040201);
	__m512i byte_permute2 = {
		0x2303220221012000ull,
		0x2707260625052404ull,
		0x2b0b2a0a29092808ull,
		0x2f0f2e0e2d0d2c0cull,
		0x3313321231113010ull,
		0x3717361635153414ull,
		0x3b1b3a1a39193818ull,
		0x3f1f3e1e3d1d3c1cull,
	};

	// Change v from 512x 1-bit counters to 16x 32-bit counters.
	v = _mm512_permutexvar_epi8(byte_permute1, v);
	v = _mm512_gf2p8affine_epi64_epi8(bit_permute, v, 0);
	v = _mm512_permutexvar_epi8(byte_permute2, v);
	v = _mm512_popcnt_epi16(v);
	return v;
}

/* Adds 3 1bit inputs (*h, *l, x) to a 2bit result (*h, *l) */
static inline void FA(__m512i *h, __m512i *l, __m512i x)
{
	/* h 01010101
	 * l 00110011
	 * x 00001111
	 *   01101001 0x96 (xor3)
	 *   00010111 0xe8 (carry)
	 *
	 * h 01010101
	 * l 00110011
	 * x 00001111
	 *   01001101 0xb2 (carry) */
	*l = _mm512_ternarylogic_epi32(*l, *h, x, 0x96);
	*h = _mm512_ternarylogic_epi32(*h, *l, x, 0xb2);
}

void vhist16(u16 hgram[16], const void *src, int slen)
{
	int padding = 0;
	__m512i acc = {};
	__m512i b0 = {};
	__m512i one = _mm512_set1_epi16(1);
	__m512i raw;
	/* vector ops on icelake per iteration: 3*p0, 4*p5, 3*p05 */
	while (slen>=64) {
		raw = _mm512_loadu_epi8(src);
tail:;
		__m512i x0 = _mm512_shldv_epi16(one, one, raw);
		__m512i x1 = _mm512_shldv_epi16(one, one, _mm512_alignr_epi8(raw, raw, 1));
		FA(&x1, &b0, x0);
		acc += bitpermute_popcnt16(x1);
		src += 64;
		slen -= 64;
	}
	if (slen) {
		padding = 64-slen;
		__mmask64 mask = -1ull >> padding;
		slen = 64;
		raw = _mm512_maskz_loadu_epi8(mask, src);
		goto tail;
	}
	acc += acc;
	acc += bitpermute_popcnt16(b0);

	__m512i narrow_permute = {
		0x0d0c090805040100ull,
		0x1d1c191815141110ull,
		0x2d2c292825242120ull,
		0x3d3c393835343130ull,
		-1ull, -1ull, -1ull, -1ull,
	};
	acc = _mm512_permutexvar_epi8(narrow_permute, acc);
	_mm256_storeu_epi8(hgram, _mm512_castsi512_si256(acc));

	hgram[0] -= padding;
}

void __vhist32(u16 hgram[32], const void *src, int slen)
{
	int padding = 0;
	__m512i acc = {};
	__m512i b0 = {};
	__m512i b1 = {};
	__m512i one = _mm512_set1_epi32(1);
	__m512i raw;
	/* vector ops on icelake per iteration: 5*p0, 6*p5, 7*p05 */
	while (slen>=64) {
		raw = _mm512_loadu_epi8(src);
tail:;
		/* 4*p0, 3*p5 */
		__m512i x0 = _mm512_shldv_epi32(one, one, raw);
		__m512i x1 = _mm512_shldv_epi32(one, one, _mm512_alignr_epi8(raw, raw, 1));
		__m512i x2 = _mm512_shldv_epi32(one, one, _mm512_alignr_epi8(raw, raw, 2));
		__m512i x3 = _mm512_shldv_epi32(one, one, _mm512_alignr_epi8(raw, raw, 3));
		/* 6*p05 */
		FA(&x1, &b0, x0);
		FA(&x3, &b0, x2);
		FA(&x3, &b1, x1);
		/* 1*p0, 3*p5, 1*p05 */
		acc += bitpermute_popcnt32(x3);
		src += 64;
		slen -= 64;
	}
	if (slen) {
		padding = 64-slen;
		__mmask64 mask = -1ull >> padding;
		slen = 64;
		raw = _mm512_maskz_loadu_epi8(mask, src);
		goto tail;
	}
	acc += acc;
	acc += bitpermute_popcnt32(b1);
	acc += acc;
	acc += bitpermute_popcnt32(b0);
	_mm512_storeu_epi16(hgram+ 0, acc + _mm512_loadu_epi16(hgram+ 0));

	hgram[0] -= padding;
}

void vhist32(u16 hgram[32], const void *src, int slen)
{
	int padding = 0;
	__m512i acc = {};
	__m512i b0 = {};
	__m512i b1 = {};
	__m512i one = _mm512_set1_epi32(1);
	__m512i raw;
	/* vector ops on icelake per iteration: 5*p0, 6*p5, 7*p05 */
	while (slen>=64) {
		raw = _mm512_loadu_epi8(src);
tail:;
		/* 4*p0, 3*p5 */
		__m512i x0 = _mm512_shldv_epi32(one, one, raw);
		__m512i x1 = _mm512_shldv_epi32(one, one, _mm512_alignr_epi8(raw, raw, 1));
		__m512i x2 = _mm512_shldv_epi32(one, one, _mm512_alignr_epi8(raw, raw, 2));
		__m512i x3 = _mm512_shldv_epi32(one, one, _mm512_alignr_epi8(raw, raw, 3));
		/* 6*p05 */
		FA(&x1, &b0, x0);
		FA(&x3, &b0, x2);
		FA(&x3, &b1, x1);
		/* 1*p0, 3*p5, 1*p05 */
		acc += bitpermute_popcnt32(x3);
		src += 64;
		slen -= 64;
	}
	if (slen) {
		padding = 64-slen;
		__mmask64 mask = -1ull >> padding;
		slen = 64;
		raw = _mm512_maskz_loadu_epi8(mask, src);
		goto tail;
	}
	acc += acc;
	acc += bitpermute_popcnt32(b1);
	acc += acc;
	acc += bitpermute_popcnt32(b0);
	_mm512_storeu_epi8(hgram, acc);

	hgram[0] -= padding;
}

static inline void *compress_write_kgpr(void *dst, const __m512i v, __mmask64 kmask, u64 gpr)
{
	__m512i tmp = _mm512_maskz_compress_epi8(kmask, v);
	_mm512_storeu_si512(dst, tmp);
	return dst + _mm_popcnt_u64(gpr);
}

static void __vhist64(u16 hgram[64], const void *src, int slen)
{
	int padding = 0;
	__m512i raw;
	const int chunk = 12*1024;
	u8 buf0[chunk] __attribute__((aligned(64))), *p0 = buf0;
	u8 buf1[chunk] __attribute__((aligned(64))), *p1 = buf1;
	const void *end = src + (slen&-64);
	const void *fake_end = end;
	while (src<end) {
		fake_end = src+chunk;
		if (fake_end>end)
			fake_end = end;
		while (src<fake_end) {
			raw = _mm512_loadu_si512(src);
tail:;
			__mmask64 k1 = _mm512_movepi8_mask(raw<<2);
			u64 gpr1 = _cvtmask64_u64(k1);
			u64 gpr0 = ~gpr1;
			__mmask64 k0 = _cvtu64_mask64(gpr0);

			p0 = compress_write_kgpr(p0, raw, k0, gpr0);
			p1 = compress_write_kgpr(p1, raw, k1, gpr1);
			src += 64;
		}
		__vhist32(hgram+ 0, buf0, p0-buf0); p0 = buf0;
		__vhist32(hgram+32, buf1, p1-buf1); p1 = buf1;
	}
	if (slen&63) {
		padding = 64-(slen&63);
		__mmask64 mask = -1ull >> padding;
		slen = 64;
		raw = _mm512_maskz_loadu_epi8(mask, src);
		goto tail;
	}
	hgram[0] -= padding;
}

void vhist64(u16 hgram[64], const void *src, int slen)
{
	__m512i zero = {};
	_mm512_storeu_epi16(hgram+  0, zero);
	_mm512_storeu_epi16(hgram+ 32, zero);
	return __vhist64(hgram, src, slen);
}

static void __vhist64t(u16 hgram[64], const void *src, int slen)
{
	int padding = 0;
	__m512i raw;
	__m512i mask20 = _mm512_set1_epi8(0x20);
	const int chunk = 12*1024;
	u8 buf0[chunk] __attribute__((aligned(64))), *p0 = buf0;
	u8 buf1[chunk] __attribute__((aligned(64))), *p1 = buf1;
	const void *end = src + (slen&-64);
	const void *fake_end = end;
	while (src<end) {
		fake_end = src+chunk;
		if (fake_end>end)
			fake_end = end;
		while (src<fake_end) {
			raw = _mm512_loadu_si512(src);
tail:;
			__mmask64 k0 = _mm512_testn_epi8_mask(raw, mask20);
			u64 gpr0 = _cvtmask64_u64(k0);
			u64 gpr1 = ~gpr0;
			__mmask64 k1 = _cvtu64_mask64(gpr1);

			p0 = compress_write_kgpr(p0, raw, k0, gpr0);
			p1 = compress_write_kgpr(p1, raw, k1, gpr1);
			src += 64;
		}
		__vhist32(hgram+ 0, buf0, p0-buf0); p0 = buf0;
		__vhist32(hgram+32, buf1, p1-buf1); p1 = buf1;
	}
	if (slen&63) {
		padding = 64-(slen&63);
		__mmask64 mask = -1ull >> padding;
		slen = 64;
		raw = _mm512_maskz_loadu_epi8(mask, src);
		goto tail;
	}
	hgram[0] -= padding;
}

void vhist64t(u16 hgram[64], const void *src, int slen)
{
	__m512i zero = {};
	_mm512_storeu_epi16(hgram+  0, zero);
	_mm512_storeu_epi16(hgram+ 32, zero);
	return __vhist64t(hgram, src, slen);
}

static void __vhist128(u16 hgram[128], const void *src, int slen)
{
	int padding = 0;
	__m512i raw;
	const int chunk = 12*1024;
	u8 buf0[chunk] __attribute__((aligned(64))), *p0 = buf0;
	u8 buf1[chunk] __attribute__((aligned(64))), *p1 = buf1;
	const void *end = src + (slen&-64);
	const void *fake_end = end;
	while (src<end) {
		fake_end = src+chunk;
		if (fake_end>end)
			fake_end = end;
		while (src<fake_end) {
			raw = _mm512_loadu_si512(src);
tail:;
			__mmask64 k1 = _mm512_movepi8_mask(raw+raw);
			u64 gpr1 = _cvtmask64_u64(k1);
			u64 gpr0 = ~gpr1;
			__mmask64 k0 = _cvtu64_mask64(gpr0);

			p0 = compress_write_kgpr(p0, raw, k0, gpr0);
			p1 = compress_write_kgpr(p1, raw, k1, gpr1);
			src += 64;
		}
		__vhist64(hgram+ 0, buf0, p0-buf0); p0 = buf0;
		__vhist64(hgram+64, buf1, p1-buf1); p1 = buf1;
	}
	if (slen&63) {
		padding = 64-(slen&63);
		__mmask64 mask = -1ull >> padding;
		slen = 64;
		raw = _mm512_maskz_loadu_epi8(mask, src);
		goto tail;
	}
	hgram[0] -= padding;
}

void vhist128(u16 hgram[128], const void *src, int slen)
{
	__m512i zero = {};
	_mm512_storeu_epi16(hgram+ 0, zero);
	_mm512_storeu_epi16(hgram+32, zero);
	_mm512_storeu_epi16(hgram+64, zero);
	_mm512_storeu_epi16(hgram+96, zero);
	__vhist128(hgram, src, slen);
}

static void __vhist128t(u16 hgram[128], const void *src, int slen)
{
	int padding = 0;
	__m512i raw;
	__m512i mask40 = _mm512_set1_epi8(0x40);
	const int chunk = 12*1024;
	u8 buf0[chunk] __attribute__((aligned(64))), *p0 = buf0;
	u8 buf1[chunk] __attribute__((aligned(64))), *p1 = buf1;
	const void *end = src + (slen&-64);
	const void *fake_end = end;
	while (src<end) {
		fake_end = src+chunk;
		if (fake_end>end)
			fake_end = end;
		while (src<fake_end) {
			raw = _mm512_loadu_si512(src);
tail:;
			__mmask64 k0 = _mm512_testn_epi8_mask(raw, mask40);
			u64 gpr0 = _cvtmask64_u64(k0);
			u64 gpr1 = ~gpr0;
			__mmask64 k1 = _cvtu64_mask64(gpr1);

			p0 = compress_write_kgpr(p0, raw, k0, gpr0);
			p1 = compress_write_kgpr(p1, raw, k1, gpr1);
			src += 64;
		}
		__vhist64(hgram+ 0, buf0, p0-buf0); p0 = buf0;
		__vhist64(hgram+64, buf1, p1-buf1); p1 = buf1;
	}
	if (slen&63) {
		padding = 64-(slen&63);
		__mmask64 mask = -1ull >> padding;
		slen = 64;
		raw = _mm512_maskz_loadu_epi8(mask, src);
		goto tail;
	}
	hgram[0] -= padding;
}

void vhist128t(u16 hgram[128], const void *src, int slen)
{
	__m512i zero = {};
	_mm512_storeu_epi16(hgram+ 0, zero);
	_mm512_storeu_epi16(hgram+32, zero);
	_mm512_storeu_epi16(hgram+64, zero);
	_mm512_storeu_epi16(hgram+96, zero);
	__vhist128t(hgram, src, slen);
}

static void __vhist256(u16 hgram[256], const void *src, int slen)
{
	int padding = 0;
	__m512i raw;
	const int chunk = 12*1024;
	u8 buf0[chunk] __attribute__((aligned(64))), *p0 = buf0;
	u8 buf1[chunk] __attribute__((aligned(64))), *p1 = buf1;
	const void *end = src + (slen&-64);
	const void *fake_end = end;
	while (src<end) {
		fake_end = src+chunk;
		if (fake_end>end)
			fake_end = end;
		while (src<fake_end) {
			raw = _mm512_loadu_si512(src);
tail:;
			__mmask64 k1 = _mm512_movepi8_mask(raw);
			u64 gpr1 = _cvtmask64_u64(k1);
			u64 gpr0 = ~gpr1;
			__mmask64 k0 = _cvtu64_mask64(gpr0);

			p0 = compress_write_kgpr(p0, raw, k0, gpr0);
			p1 = compress_write_kgpr(p1, raw, k1, gpr1);
			src += 64;
		}
		__vhist128(hgram+ 0, buf0, p0-buf0); p0 = buf0;
		__vhist128(hgram+128, buf1, p1-buf1); p1 = buf1;
	}
	if (slen&63) {
		padding = 64-(slen&63);
		__mmask64 mask = -1ull >> padding;
		slen = 64;
		raw = _mm512_maskz_loadu_epi8(mask, src);
		goto tail;
	}
	hgram[0] -= padding;
}

void vhist256(u16 hgram[256], const void *src, int slen)
{
	__m512i zero = {};
	_mm512_storeu_epi16(hgram+  0, zero);
	_mm512_storeu_epi16(hgram+ 32, zero);
	_mm512_storeu_epi16(hgram+ 64, zero);
	_mm512_storeu_epi16(hgram+ 96, zero);
	_mm512_storeu_epi16(hgram+128, zero);
	_mm512_storeu_epi16(hgram+160, zero);
	_mm512_storeu_epi16(hgram+192, zero);
	_mm512_storeu_epi16(hgram+224, zero);
	__vhist256(hgram, src, slen);
}

static inline u64 rdtsc(void)
{
	unsigned int low, high;

	asm volatile ("rdtsc":"=a" (low), "=d"(high));
	return low | ((u64) high) << 32;
}

static inline u64 loop16(void)
{
	u64 t = rdtsc();
	u64 rcx = 1ull<<16;
	asm volatile ("1: sub $1, %%rcx; jg 1b" : "+c" (rcx));
	t = rdtsc() - t;
	return t;
}

static u64 rdcore(u64 start_tsc)
{
	static u64 last;
	static u64 div;
	u64 now = rdtsc();
	if (now-last > 1<<22) {
		div = loop16();
		last = now;
	}
	u128 c = now - start_tsc;
	c <<= 16;
	return c/div;
}

static void bench(u8 *src, int slen, char *str,
		void (*f)(u16 *hgram, const void *src, int slen))
{
	u16 hgramv[256];
	printf("  cycles  mc/b   vop\n");
	for (int i=0; i<8; i++) {
		u64 t = rdtsc();
		f(hgramv, src, slen);
		t = rdcore(t);
		printf("%8lld %5lld %5lld %s\n", t, 1000*t/slen, 128*t/slen, str);
	}
	printf("\n");
	static volatile u16 compiler_hack;
	compiler_hack += hgramv[0]; /* force compiler to actually generate code */
}

static void test(int mask, char *str,
		void (*f)(u16 *hgram, const void *src, int slen))
{
	u16 hgrams[256];
	u16 hgramv[256];
	u8 buf[32768] __attribute__((aligned(64)));
	for (int i=0; i<sizeof(buf); i++)
		buf[i] = (random()&mask);
	for (int skip=0; skip<4; skip++) {
		hgram_scalar(hgrams, buf+skip, sizeof(buf)-2*skip);
		f	    (hgramv, buf+skip, sizeof(buf)-2*skip);
		for (int i=0; i<=mask; i++) {
			if (hgrams[i] != hgramv[i])
				printf("mismatch %2x: %5d %5d\n", i, hgrams[i], hgramv[i]);
		}
	}
	bench(buf, sizeof(buf), str, f);
}

int main(void)
{
	test(0x0f, "vhist16",  vhist16);
	test(0x1f, "vhist32",  vhist32);
	test(0x3f, "vhist64",  vhist64);
	test(0x7f, "vhist128", vhist128);
	test(0xff, "vhist256", vhist256);

	/* Exercise left to the reader:
	 * The t-variants use _mm512_testn_epi8_mask() and should result in
	 * one less vector-operation, yielding a slight performance advantage.
	 * However, in my testing they are slightly slower, not faster.
	 *
	 * Answer appears to be compiler decisions.  Both gcc and clang
	 * generate imperfect code.  So if you really care about the last
	 * 10% of performance gains, you have to write your own assembly.
	 */
	test(0x3f, "vhist64t", vhist64t);
	test(0x7f, "vhist128t",vhist128t);

	return 0;
}
