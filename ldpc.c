#include <assert.h>
#include <immintrin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef unsigned __int128 u128;

struct state {
	u64 a;
	u64 b;
};

#define M0	(0x62aa751d16399637ull)

static inline void whisper(u64 *a, u64 *b, u64 m)
{
#ifdef __BMI2__
	u64 tmp;
	asm(	"mulx %[a],%[a],%[t];"
		"xor  %[t],%[b];"
		: [a]"+r"(*a), [b]"+r"(*b), [t]"=r"(tmp)
		: [d]"d"(m)
		: "cc" );
#else /* gcc generates poor code for this */
	u128 p = *a;
	p *= m;
	*a = p;
	*b ^= p >> 64;
#endif
}

static __thread struct state thread_state;

#define noinline inline __attribute__((noinline))

/* noinline shrinks whisper_random object size by 32 bytes */
static void init_state(struct state *state)
{
	state->b = 1;
	return;
	u64 ret;
	do {
		ret = getrandom(state, sizeof(*state), 0);
	} while (ret != sizeof(*state));
}

void whisper_init(u64 b)
{
	struct state *s = &thread_state;
	s->a = 0;
	s->b = b;
}

u64 whisper_random(void)
{
	struct state *s = &thread_state;
	if (!s->b)
		init_state(s);
	u64 a = s->a;
	u64 b = s->b;
	s->a += M0; /* slightly better mixing than adding 1 */
	whisper(&a, &b, M0);
	whisper(&b, &a, M0);
	whisper(&a, &b, M0);
	return b * M0; /* skip the last XOR */
}

typedef u64 u64u __attribute__((may_alias, aligned(1)));
static inline void write64(void *buf, u64 val) { *(u64u *)buf = val; }

static void randset(void *buf, size_t n)
{
	while (n >= 8) {
		write64(buf, whisper_random());
		buf += 8;
		n -= 8;
	}
	if (n) {
		u64 last = whisper_random();
		memcpy(buf, &last, n);
	}
}

static u64 rand_n(u64 n)
{
	u64 r;
	u64 limit = -n % n;
	for (;;) {
		r = whisper_random();
		if (limit && r > -limit)
			continue;
		return r % n;
	}
}

static inline u64 rand_range(u64 fro, u64 to)
{
	return fro + rand_n(to-fro);
}

static inline u64 rotl64(u64 val, u8 rot)
{
	u8 l = rot&63;
	u8 r = (64-l) & 63;
	return val<<l | val>>r;
}

static inline u64 rotr64(u64 val, u8 rot)
{
	u8 r = rot&63;
	u8 l = (64-r) & 63;
	return val<<l | val>>r;
}

static int popcnt64(u64 arg)
{
	return __builtin_popcountll(arg);
}


static u64 alpha_mask[8] = {
	0b100010010000000000010111,
	0b1010000010000000000000000001100011,
	0b1000010000000000000000000100000000110000011,
	0b10001000000000001000000000000100100000000011,
	0b1000000000001000000001000010000000101000000001,
	0b10000000000100010000100000000001000000100000001,
	0b10000010000000001000010000001000000100000000001,
	0b10010000010000000000000100000000000010000001001,
};

static u64 alpha_im[8] = {
	0b100010011000101110111010100101000111110000,
	0b10101010001000101000001000011011,
	0b10000100001000010000100,
	0b1000100010001000000010,
	0b10000000000010000000,
	0b1000000000010001000,
	0b1000001000001000101,
	0b1001001000001000000,
};

static u8 alpha_bit[8][6] = { /* bit 0 is implicit */
	{ 1,  2,  4, 16, 19, 23 },
	{ 1,  5,  6, 25, 31, 33 },
	{ 1,  7,  8, 17, 37, 42 },
	{ 1, 11, 14, 27, 39, 43 },
	{ 9, 11, 19, 24, 33, 45 },
	{ 8, 15, 26, 31, 35, 46 },
	{11, 18, 25, 30, 40, 46 },
	{ 3, 10, 23, 37, 43, 46 },
};

static void alpha_syndrome(u64 data[9], u64 syndrome[1])
{
	u64 ecc=0;
	for (int i=0; i<8; i++) {
#if 0 /* naive implementation */
		u64 val = data[i];
		u64 m = alpha_mask[i];
		for (int b=0; b<64; b++) {
			if (val&1)
				ecc ^= rotl64(m, b);
			val >>= 1;
		}
#else /* roughly 10x faster */
		u64 val = data[i];
		ecc ^= val; /* ecc ^= rotl64(val, 0); */
		for (int k=0; k<6; k++)
			ecc ^= rotl64(val, alpha_bit[i][k]);
#endif
	}
	syndrome[0] = ecc;
}

static void alpha_gen(u64 data[9])
{
	u64 syndrome=0;
	alpha_syndrome(data, &syndrome);
	data[8] = syndrome;
}

static u64 pattern(int n)
{
	return rotl64(alpha_mask[n/64], n%64);
}

static int map_one(u64 syndrome)
{
	static int initialized;
	static u32 map[1024];
	u64 m = 0x1dedf3c9ull;
	if (!initialized) {
		initialized=1;
		for (int i=0; i<512; i++) {
			u64 h = m*pattern(i);
			h >>= 54;
			assert(h<1024);
			assert(map[h] < 0xffff);
			map[h] <<= 16;
			map[h] |= i;
		}
	}
	int slot = (m*syndrome) >> 54;
	u16 a = map[slot];
	u16 b = map[slot]>>16;
	if (pattern(a) == syndrome)
		return a;
	if (pattern(b) == syndrome)
		return b;
	return -1;
}

/* returns 1 on success, -1 on failure */
static int alpha_repair_one(u64 data[9], u64 syndrome)
{
	int pc = popcnt64(syndrome);
	if (pc==1) {
		data[8] ^= syndrome;
		return 1;
	}
#if 0 /* naive implementation */
	if (pc!=7)
		return -1;

	for (int i=0; i<8; i++) {
		u64 m = alpha_mask[i];
		for (int b=0; b<64; b++) {
			if (syndrome != rotl64(m, b))
				continue;
			data[i] ^= 1ull<<b;
			return 1;
		}
	}
#else /* hash table lookup */
	int bit = map_one(syndrome);
	if (bit<0)
		return -1;

	data[bit/64] ^= 1ull << (bit%64);
#endif
	return 1;
}


static int alpha_repair_two(u64 data[9], u64 syndrome)
{
	int pc = popcnt64(syndrome);
	if (pc==2) {
		data[8] ^= syndrome;
		return 2;
	}
	if (pc&1 || pc<6 || pc>14)
		return -1;

	for (int i=0; i<8; i++) {
		u64 m = alpha_mask[i];
		for (int b=0; b<64; b++) {
			int count = popcnt64(rotl64(m, b) & syndrome);
			if (count<5)
				continue;
			data[i] ^= 1ull<<b;
			syndrome ^= rotl64(m, b);
			int ret = alpha_repair_one(data, syndrome);
			if (ret!=1) {
				data[i] ^= 1ull<<b;
				return -1;
			}
			return 2;
		}
	}
	return -1;
}

static int alpha_repair_recursive(u64 data[9], int budget, int threshold, int min_threshold)
{
	if (min_threshold > 7-budget/2)
		min_threshold = 7-budget/2;
	assert(budget);
	if (budget==1) {
		u64 syndrome=0;
		alpha_syndrome(data, &syndrome);
		syndrome ^= data[8];
		int ret = alpha_repair_one(data, syndrome);
		if (ret<0)
			return -1;
		return 0;
	}
	if (budget==2) {
		u64 syndrome=0;
		alpha_syndrome(data, &syndrome);
		syndrome ^= data[8];
		int ret = alpha_repair_two(data, syndrome);
		if (ret<0)
			return -1;
		return 0;
	}
	while (threshold>=min_threshold) {
		u64 syndrome=0;
		alpha_syndrome(data, &syndrome);
		syndrome ^= data[8];
		if (popcnt64(syndrome)>7*budget)
			return 1;
		if (!syndrome)
			return 0;
		if (popcnt64(syndrome) <= budget) {
			data[8] ^= syndrome;
			return 0;
		}

		for (int i=0; i<8; i++) {
			u64 m = alpha_mask[i];
			for (int b=0; b<64; b++) {
				int count = popcnt64(rotl64(m, b) & syndrome);
				if (count==threshold) {
					data[i] ^= 1ull<<b;

					int err = alpha_repair_recursive(data, budget-1, threshold<5? threshold+2 : 7, min_threshold+1);
					if (!err)
						return 0;
					data[i] ^= 1ull<<b; /* undo failed repair */
				}
			}
		}
		threshold = threshold==0 ? 0 : threshold-1;
	}
	return -1;
}

static u128 clmul(u64 a, u64 b)
{
	__m128i va={}, vb={};
	va = _mm_insert_epi64(va, a, 0);
	vb = _mm_insert_epi64(vb, b, 0);
	va = _mm_clmulepi64_si128(va, vb, 0);
	u128 ret = _mm_extract_epi64(va, 1);
	ret <<= 64;
	ret ^= _mm_extract_epi64(va, 0);
	return ret;
}
static u64 clmulhi(u64 a, u64 b) { return clmul(a, b) >> 64; }
static u64 clmullo(u64 a, u64 b) { return clmul(a, b); }

static int chip_repair(u64 data[9])
{
	u64 syndrome=0;
	alpha_syndrome(data, &syndrome);
	syndrome ^= data[8];
	if (!syndrome)
		return 0;
	for (int rot=0; rot<64; rot+=16) {
		u64 rs = rotl64(syndrome, rot);
		u64 mask = ~0xffffull;
		if (!(rs & mask)) { /* ecc corruption */
			data[8] ^= syndrome;
			return popcnt64(syndrome);
		}
		for (int i=0; i<8; i++) {
			u64 error = clmulhi(rs, alpha_im[i]);
			u64 mod = rs ^ clmullo(error, alpha_mask[i]);
			if (!mod && (error == (u16)error)) {
				data[i] ^= rotr64(error, rot);
				return popcnt64(error);
			}
		}
	}
	return -1;
}

static int alpha_repair(u64 data[9])
{
	int ret = chip_repair(data);
	if (ret>0)
		return ret;
	u64 syndrome=0;
	alpha_syndrome(data, &syndrome);
	syndrome ^= data[8];
	if (!syndrome)
		return 0;
	int pc = popcnt64(syndrome);
	if (pc<=4) { /* only parity bit errors */
		data[8] ^= syndrome;
		return pc;
	}
	if (~pc&1) { /* even number of errors */
		if (pc<=14 && pc!=4 && !alpha_repair_recursive(data, 2, 7, 5)) /* two errors */
			return 2;
		if (pc<=28 && !alpha_repair_recursive(data, 4, 7, 3)) /* four errors */
			return 4;
		if (pc<=28 && !alpha_repair_recursive(data, 4, 7, 2)) /* four errors, hard cases */
			return 4;
		if (pc<=42 && !alpha_repair_recursive(data, 6, 7, 4)) /* six errors */
			return 6;
		if (pc<=42 && !alpha_repair_recursive(data, 6, 7, 3)) /* six errors, hard cases */
			return 6;
		if (pc<=42 && !alpha_repair_recursive(data, 6, 7, 2)) /* six errors, harder cases */
			return 6;
	} else { /* odd number of errors */
		if (pc==7 && !alpha_repair_recursive(data, 1, 7, 7)) /* one error */
			return 1;
		if (pc<=21 && !alpha_repair_recursive(data, 3, 7, 3)) /* three errors */
			return 3;
		if (pc<=35 && !alpha_repair_recursive(data, 5, 7, 5)) /* five errors */
			return 5;
		if (pc<=35 && !alpha_repair_recursive(data, 5, 7, 3)) /* five errors, hard cases */
			return 5;
		if (pc<=35 && !alpha_repair_recursive(data, 5, 7, 2)) /* five errors, harder cases */
			return 5;
	}
	return -1;
}

static int chip_corrupt(u64 data[9], int i, int verbose)
{
#if 1 /* to systematically try all 2359296 possibilities */
	int word = (i>>18) % 9;
	int subword = (i>>16) & 3;
	u64 error = i & 0xffff;
#else
	int word = rand_n(9);
	int subword = rand_n(4) * 16;
	u64 error = rand_range(1, 65536) << subword;
#endif
	data[word] ^= error;
	if (verbose) printf("corrupt %2d %64llb\n", word, error);
	return popcnt64(error);
}

static void alpha_corrupt(u64 data[9], int count, int verbose)
{
	u64 err[9]={};
	for (int i=0; i<count; i++) {
		int word, bit;
		do {
			word = rand_n(9);
			bit = rand_n(64);
		} while (err[word] & 0x1ull<<bit);
		data[word] ^= 0x1ull<<bit;
		err[word] ^= 0x1ull<<bit;
		if (verbose) printf("corrupt %2d:%2d %16llx\n", word, bit, 1ull<<bit);
	}
}

u8 secded_encode(u64 data)
{
	u64 lo = _pdep_u64(data, 0b1111111111111111111111111111111011111111111111101111111011101000ull);
	u64 hi = _pdep_u64(data>>57, 0b11111110ull);
	data = hi^lo;
	u8 ecc=0;
	ecc |= __builtin_parityll(data & 0b1010101010101010101010101010101010101010101010101010101010101010ull) << 0;
	ecc |= __builtin_parityll(data & 0b1100110011001100110011001100110011001100110011001100110011001100ull) << 1;
	ecc |= __builtin_parityll(data & 0b1111000011110000111100001111000011110000111100001111000011110000ull) << 2;
	ecc |= __builtin_parityll(data & 0b1111111100000000111111110000000011111111000000001111111100000000ull) << 3;
	ecc |= __builtin_parityll(data & 0b1111111111111111000000000000000011111111111111110000000000000000ull) << 4;
	ecc |= __builtin_parityll(data & 0b1111111111111111111111111111111100000000000000000000000000000000ull) << 5;
	ecc |= __builtin_parityll(hi) << 6;
	ecc |= __builtin_parityll(ecc^data) << 7;
	return ecc;
}

/* returns number of detected errors, does repair when returning 1 */
int secded_decode(u64 *data, u8 *ecc)
{
	u8 syndrome = secded_encode(*data) ^ *ecc;
	if (!syndrome)
		return 0;
	if (!__builtin_parity(syndrome))
		return 2;
	u8 bit_table[72] = {
		71,64,65, 0, 66, 1, 2, 3,  67, 4, 5, 6,  7, 8, 9,10,
		68,11,12,13, 14,15,16,17,  18,19,20,21, 22,23,24,25,
		69,26,27,28, 29,30,31,32,  33,34,35,36, 37,38,39,40,
		41,42,43,44, 45,46,47,48,  49,50,51,52, 53,54,55,56,
		70,57,58,59, 60,61,62,63,
	};
	if ((syndrome&0x7f) >= 72)
		return 3;
	u8 bit = bit_table[syndrome&0x7f];
	if (bit<64)
		*data ^= 1ull<<bit;
	else
		*ecc ^= 1ull<<(bit-64);
	return 1;
}

static void secded_gen(u64 data[9])
{
	u8 *ecc = (void*)&data[8];
	for (int i=0; i<8; i++)
		ecc[i] = secded_encode(data[i]);
}

static int secded_repair(u64 data[9])
{
	int ret=0;
	int err=0;
	u8 *ecc = (void*)&data[8];
	for (int i=0; i<8; i++) {
		int r = secded_decode(data+i, ecc+i);
		if (r>1)
			err = -1;
		ret += r;
	}
	if (err)
		return err;
	return ret;
}

int main(void)
{
	int errors=2; /* number of errors to generate */
	int verbose=1;
	int use_secded=0;
	int use_chipcorrupt=0;
	int iterations=1000000;

	int success_count=0;
	int fail_count=0;
	int silent_count=0;
	int misrepair_count=0;
	int detected_count=0;
	for (int i=1; i<=iterations; i++) {
		if (verbose) printf("round %7d\n", i);
		u64 orig[9];
		randset(orig, sizeof(orig)); /* mostly unnecessary */

		if (use_secded) secded_gen(orig); else alpha_gen(orig);
		u64 copy[9]={};
		memcpy(copy, orig, sizeof(orig));

		if (use_chipcorrupt) errors = chip_corrupt(copy, i, verbose); else alpha_corrupt(copy, errors, verbose);
		u64 copy2[9]={};
		memcpy(copy2, copy, sizeof(copy));
		int ret = use_secded ? secded_repair(copy) : alpha_repair(copy);

		u64 delta=0;
		for (int i=0; i<9; i++)
			delta |= orig[i]^copy[i];
		if (!delta) {
			if (verbose) printf("%d %d\n", ret, errors);
			assert(ret>=errors);
			if (verbose) printf("all repaired %4d\n", i);
			success_count++;
			continue;
		}
		if (!ret) {
			silent_count++;
			if (1) printf("silent %d/%d\n", silent_count, i);
		} else if (ret<0) {
			detected_count++;
		}
		delta=0;
		for (int i=0; i<9; i++)
			delta |= copy2[i]^copy[i];
		if (delta && ret>0) {
			misrepair_count++;
			if (verbose) printf("misrepair %d!=%d %d\n", ret, errors, misrepair_count);
		}
		fail_count++;
		if (verbose) printf("FAILURE %d %4d/%4d\n", ret, fail_count, i);
		if (verbose) for (int i=0; i<9; i++) printf("delta %2d %16llx\n", i, orig[i]^copy[i]);
		if (verbose) for (int i=0; i<9; i++) printf("delta2 %2d %16llx\n", i, copy2[i]^copy[i]);
		if (verbose) for (int i=0; i<9; i++) printf("delta3 %2d %16llx\n", i, copy2[i]^orig[i]);
	}
	printf("Created %d sets of %d errors\n", iterations, errors);
	printf("%12d successful repairs\n", success_count);
	printf("%12d detected errors\n", detected_count);
	printf("%12d failed repairs\n", misrepair_count);
	printf("%12d silent errors\n", silent_count);
	printf("%12d total failures\n", fail_count);
	return 0;
}
