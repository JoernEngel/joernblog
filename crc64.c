#include <string.h>
#include <immintrin.h>

typedef unsigned long long u64;
typedef unsigned char u8;

typedef u64 u64u __attribute__((may_alias, aligned(1)));
static inline u64 read64(const void *buf) { return *(const u64u *)buf; }
static inline void write64(void *buf, u64 val) { *(u64u *)buf = val; }

static inline __m128i fold1(__m128i acc, __m128i mu)
{
	return _mm_clmulepi64_si128(acc, mu, 0x00) ^ _mm_clmulepi64_si128(acc, mu, 0x11);
}

static inline __m128i fold2(__m128i acc, __m128i mu, __m128i src)
{
	return fold1(acc, mu) ^ src;
}

static inline __m128i fold3(__m128i acc, __m128i mu, const void *src)
{
	return fold2(acc, mu, _mm_loadu_si128(src));
}

/* crc64-ecma, polynomial 42f0e1eba9ea3693 */
u64 crc64_clmul(u64 crc, const void *data, size_t n)
{
	__m128i mu8 = _mm_set_epi64x(0xd7d86b2af73de740ull, 0x8757d71d4fcc1000ull); /* 1<<960 % poly, 1<<1024 % poly */
	__m128i mu4 = _mm_set_epi64x(0x081f6054a7842df4ull, 0x6ae3efbb9dd441f3ull); /* 1<<448 % poly, 1<< 512 % poly */
	__m128i mu2 = _mm_set_epi64x(0x3be653a30fe1af51ull, 0x60095b008a9efa44ull); /* 1<<192 % poly, 1<< 256 % poly */
	__m128i mu1 = _mm_set_epi64x(0xdabe95afc7875f40ull, 0xe05dd497ca393ae4ull); /* 1<< 64 % poly, 1<< 128 % poly */
	__m128i mub = _mm_set_epi64x(0x92d8af2baf0e1e85ull, 0x9c3e466c172963d5ull); /* bitreverse(p)<<1|1, bitreverse(mi)<<1|1 */
	__m128i a, b, c, d, e, f, g, h;
	u8 buf[48] = {};
	void *start;

	if (n >= 128) {
		/*
		 * Search for Intel's paper "Fast CRC Computation for Generic
		 * Polynomials Using PCLMULQDQ Instruction"  It has disappeared
		 * from Intel's website, but archived copies exist.
		 *
		 * When possible, we start using 8 fold chains, consuming 128
		 * bytes or 1024 bits at a time.
		 */
		a = _mm_loadu_si128(data) ^ _mm_set_epi64x(0, ~crc);
		b = _mm_loadu_si128(data+ 16);
		c = _mm_loadu_si128(data+ 32);
		d = _mm_loadu_si128(data+ 48);
		e = _mm_loadu_si128(data+ 64);
		f = _mm_loadu_si128(data+ 80);
		g = _mm_loadu_si128(data+ 96);
		h = _mm_loadu_si128(data+112);
		n -= 128;
		data += 128;
		while (n>=128) {
			a = fold3(a, mu8, data+  0);
			b = fold3(b, mu8, data+ 16);
			c = fold3(c, mu8, data+ 32);
			d = fold3(d, mu8, data+ 48);
			e = fold3(e, mu8, data+ 64);
			f = fold3(f, mu8, data+ 80);
			g = fold3(g, mu8, data+ 96);
			h = fold3(h, mu8, data+112);
			n -= 128;
			data += 128;
		}
		/*
		 * When we approach the end, we reduce 8 chains down to 4 and
		 * continue consuming the remaining data.
		 */
		a = fold2(a, mu4, e);
		b = fold2(b, mu4, f);
		c = fold2(c, mu4, g);
		d = fold2(d, mu4, h);
chain4:
		while (n>=64) {
			a = fold3(a, mu4, data+ 0);
			b = fold3(b, mu4, data+16);
			c = fold3(c, mu4, data+32);
			d = fold3(d, mu4, data+48);
			n -= 64;
			data += 64;
		}
		/* We reduce 4 chains down to 2. */
		a = fold2(a, mu2, c);
		b = fold2(b, mu2, d);
chain2:
		while (n>=32) {
			a = fold3(a, mu2, data+ 0);
			b = fold3(b, mu2, data+16);
			n -= 32;
			data += 32;
		}
		/* We reduce 2 chains down to 1. */
		a = fold2(a, mu1, b);
		if (n>24) {
			a = fold3(a, mu1, data+ 0);
			n -= 16;
			data += 16;
		}

		/*
		 * And finally we copy our accumulator a and the remaining data
		 * to a 48B buffer.  The buffer includes the implicitly appended
		 * 8 bytes for the crc:
		 * | 0-padding  | a        | data      | 0-padding |
		 * | 0-24 bytes | 16 bytes | 0-24 bytes| 8 bytes   |
		 */
		start = buf+sizeof(buf)-8-n;
		_mm_storeu_si128(start-16, a);
		memcpy(start, data, n);
	} else if (n >= 64) {
		/* Set up 4 accumulators before jumping to common code */
		a = _mm_loadu_si128(data) ^ _mm_set_epi64x(0, ~crc);
		b = _mm_loadu_si128(data+16);
		c = _mm_loadu_si128(data+32);
		d = _mm_loadu_si128(data+48);
		n -= 64;
		data += 64;
		goto chain4;
	} else if (n >= 32) {
		/* Set up 2 accumulators before jumping to common code */
		a = _mm_loadu_si128(data) ^ _mm_set_epi64x(0, ~crc);
		b = _mm_loadu_si128(data+16);
		n -= 32;
		data += 32;
		goto chain2;
	} else {
		/* All data fits in our 48B buffer, copy it directly */
		start = buf+sizeof(buf)-8-n;
		memcpy(start, data, n);
		write64(start, ~crc ^ read64(start));
	}

	/* Reduce 48B buffer down to one 128b accumulator */
	start = buf;
	a = _mm_loadu_si128(start);
	b = _mm_loadu_si128(start+16);
	c = _mm_loadu_si128(start+32);
	a = fold1(a, mu2) ^ fold1(b, mu1) ^ c;

	/* Reduce 128b down to 64b via barret reduction. */
	b = _mm_clmulepi64_si128(a, mub, 0x00);	/* b = a*mi */
	c = _mm_clmulepi64_si128(b, mub, 0x10);	/* c = b*p (64 of 65 bits) */
	c ^= _mm_slli_si128(b, 8);		/* c = b*p (1 of 65 bits) */
	d = c ^ a;				/* xor with low bits of a */
	return ~_mm_extract_epi64(d, 1);
}
