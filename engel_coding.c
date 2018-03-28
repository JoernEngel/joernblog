#include <limits.h>
#include <stdint.h>
#include <string.h>

#define MAX_BITS	(12)
#define MAX_SLOTS	(1 << MAX_BITS)
#define WEIGHT_ABSENT	(MAX_BITS + 1)

struct bitlen_temp {
	uint16_t hgram;
	uint8_t bitlen;
	uint8_t sym;
	uint32_t cost;	/* hgram << bitlen */
};

struct extent {
	int ofs;
	int len;
};

struct rank {
	uint16_t end;
	uint16_t cur;
};

static inline int fls(int x)
{
	int r = 0;

	if (x)
		r = 32 - __builtin_clz(x);
	return r;
}

static int get_rank(uint16_t count)
{
	if (count < 8)
		return count;
	int highbit = fls(count) - 3;
	int subbin = (count >> (highbit - 1)) - 8;
	return highbit * 8 + subbin;
}

static void sort_syms(const uint16_t hgram[256], struct bitlen_temp temp[256])
{
	/*
	 * We first sort into "ranks", based on the position of the
	 * high bit and the next 3 msb.  Then we sort entries within
	 * each rank.
	 */
#define MAX_RANK 112 /* 14*8, 16 for uint16_t, minus (3-1), time 2^3 */
	struct rank ranks[MAX_RANK] = { { .end = 0, }, };
	uint8_t rcache[256];

	/* Calculate ranks */
	for (int sym = 0; sym < 256; sym++) {
		int rank = get_rank(hgram[sym]);
		rcache[sym] = rank;
		ranks[rank].end++;
	}
	/* Find rank offsets */
	ranks[0].cur = 0;
	for (int i = 1; i < MAX_RANK; i++) {
		ranks[i].end += ranks[i - 1].end;
		ranks[i].cur  = ranks[i - 1].end;
		if (ranks[i].end == 256) {
			break;
		}
	}
	/* sort symbols by count */
	for (int sym = 0; sym < 256; sym++) {
		int rank = rcache[sym];
		int count = hgram[sym];
		int pos = ranks[rank].cur++;
		int base = rank ? ranks[rank - 1].end : 0;
		while (pos > base && count < temp[pos - 1].hgram)
			temp[pos] = temp[pos - 1], pos--;
		temp[pos].hgram = count;
		temp[pos].sym = sym;
	}
}

static void create_initial_bitlen(struct bitlen_temp temp[256], int slen)
{
	/*
	 * 1518500250 is the 2^-1.5 << 32, i.e. the boundary between
	 * 1-bit and 2-bit symbols shifted to avoid floating point
	 * math.  (slen * sqrt_2_32) >> 32 is the boundary between
	 * 1-bit and 2-bit symbols for any slen.  Other boundaries are
	 * creating by further shifting of the result.
	 */
	static const uint64_t sqrt_2_32 = 1518500250;
	int bit_boundary = (slen * sqrt_2_32) >> 32;
	int bitlen = 1;

	for (int i = 255; i >= 0; i--) {
retry:
		if (temp[i].hgram >= bit_boundary) {
			temp[i].bitlen = bitlen;
			temp[i].cost = temp[i].hgram << bitlen;
		} else if (bitlen < MAX_BITS - 1 && bit_boundary > 1) {
			bitlen++;
			bit_boundary >>= 1;
			goto retry;
		} else if (bit_boundary > 1) {
			bitlen = MAX_BITS;
			bit_boundary = 1;
			goto retry;
		} else {
			bitlen = WEIGHT_ABSENT;
			bit_boundary = 0;
			goto retry;
		}
	}
}

static int calc_debt(struct bitlen_temp temp[256])
{
	int debt = -MAX_SLOTS;
	for (int i = 0; i < 256; i++) {
		debt += MAX_SLOTS >> temp[i].bitlen;
	}
	return debt;
}

static int extent_remove_first(struct extent *e)
{
	e->len--;
	return e->ofs++;
}

static int extent_remove_last(struct extent *e)
{
	return e->ofs + --e->len;
}

static void extent_add_last(struct extent *e, int ofs)
{
	if (e->len) {
		e->len++;
	} else {
		e->ofs = ofs;
		e->len = 1;
	}
}

static void extent_add_first(struct extent *e, int ofs)
{
	if (e->len) {
		e->ofs--;
		e->len++;
	} else {
		e->ofs = ofs;
		e->len = 1;
	}
}

static void adjust_bitlen_naive(struct bitlen_temp temp[256], int debt, struct extent extents[WEIGHT_ABSENT + 1])
{
	while (debt > 0) {
		/* repay debt, possibly overshooting */
		for (int bitlen = MAX_BITS - 1; bitlen; bitlen--) {
			int change = MAX_SLOTS >> (bitlen + 1);
			while (extents[bitlen].len) {
				int ofs = extent_remove_first(&extents[bitlen]);
				extent_add_last(&extents[bitlen + 1], ofs);

				temp[ofs].bitlen++;
				temp[ofs].cost = temp[ofs].hgram << temp[ofs].bitlen;
				debt -= change;
				if (debt <= 0)
					goto done_repay;
			}
		}
	}
done_repay:
	while (debt < 0) {
		/* retake debt in case of overshoot */
		for (int bitlen = 1; bitlen <= MAX_BITS; bitlen++) {
			int change = MAX_SLOTS >> bitlen;
			if (change > -debt)
				continue;
			while (extents[bitlen].len) {
				int ofs = extent_remove_last(&extents[bitlen]);
				extent_add_first(&extents[bitlen - 1], ofs);

				temp[ofs].bitlen--;
				temp[ofs].cost = temp[ofs].hgram << temp[ofs].bitlen;
				debt += change;
				if (debt >= 0)
					return;
			}
		}
	}
}

static void adjust_bitlen(struct bitlen_temp temp[256], int debt)
{
	if (!debt)
		return;

	struct extent extents[WEIGHT_ABSENT + 1];
	{
		/* TODO: creating extents could be done via create_initial_bitlen() */
		int bitlen = WEIGHT_ABSENT;
		memset(extents, 0, sizeof(extents));
		for (int i = 0; i < 256; i++) {
retry:
			if (temp[i].bitlen == bitlen) {
				extents[bitlen].len++;
			} else {
				bitlen--;
				extents[bitlen].ofs = i;
				goto retry;
			}
		}
	}
	while (debt) {
		if (debt > 0) {
			/*
			 * We have to find a short symbol to make
			 * longer.  Pick the lowest-cost symbol.
			 * Overshoot by up to debt-1.
			 */
			int best_len = -1, best_cost = INT_MAX;
			for (int bitlen = 1; bitlen < MAX_BITS; bitlen++) {
				if (!extents[bitlen].len)
					continue;
				if ((MAX_SLOTS >> (bitlen + 1)) >= debt * 2)
					continue;
				int ofs = extents[bitlen].ofs;
				int cost = temp[ofs].cost;
				if (cost > best_cost)
					continue;
				best_len = bitlen;
				best_cost = cost;
			}
			if (best_len == -1 && extents[MAX_BITS - 1].len == 0) {
				/* simpler heuristic is generally worse, but always works */
				adjust_bitlen_naive(temp, debt, extents);
				return;
			}
			int ofs = extent_remove_first(&extents[best_len]);
			extent_add_last(&extents[best_len + 1], ofs);

			temp[ofs].bitlen++;
			temp[ofs].cost = temp[ofs].hgram << temp[ofs].bitlen;

			int change = MAX_SLOTS >> (best_len + 1);
			debt -= change;
		}
		if (debt < 0) {
			/* We have to find a long symbol to make
			 * shorter.  Pick the highest-cost symbol.
			 * Overshoot by up to credit-1.
			 */
			int credit = -debt;
			int best_len = -1, best_cost = 0;
			for (int bitlen = 2; bitlen <= MAX_BITS; bitlen++) {
				if (!extents[bitlen].len)
					continue;
				if ((MAX_SLOTS >> bitlen) >= credit * 2)
					continue;
				int ofs = extents[bitlen].ofs + extents[bitlen].len - 1;
				int cost = temp[ofs].cost;
				if (cost < best_cost)
					continue;
				best_len = bitlen;
				best_cost = cost;
			}
			int ofs = extent_remove_last(&extents[best_len]);
			extent_add_first(&extents[best_len - 1], ofs);

			temp[ofs].bitlen--;
			temp[ofs].cost = temp[ofs].hgram << temp[ofs].bitlen;

			int change = MAX_SLOTS >> best_len;
			debt += change;
		}
	}
}

static int huffe_bitlength_v2(const uint16_t hgram[256], int slen, uint8_t bitlen[256], void *mem, unsigned mlen)
{
	struct bitlen_temp *temp = mem;

	memset(temp, 0, 256 * sizeof(struct bitlen_temp));
	/* Step 1: sort symbols by hgram */
	sort_syms(hgram, temp);

	if (temp[255].hgram * 108 < slen)
		return -1;
	/* Step 2a: create ideal bitlen (within a factor of sqrt(2) of ideal) */
	/* Step 2b: create cost */
	create_initial_bitlen(temp, slen);

	/* Step 3: calculate debt/credit */
	int debt = calc_debt(temp);

	/* Step 4: repay debt, use credit */
	adjust_bitlen(temp, debt);

	/* Step 5: generate output */
	for (int i = 0; i < 256; i++) {
		int sym = temp[i].sym;
		bitlen[sym] = temp[i].bitlen;
	}
	return 0;
}
