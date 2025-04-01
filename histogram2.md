# My own AVX512 histograms

After Harold's [big improvement](histogram.md), here is a smaller one
from me.  I started bottom-up and implemented a nibble-histogram
first.  Let's look at benchmark results:

```
  cycles  mc/b   vop
    2598    79    10 vhist16
    2603    79    10 vhist16
    2605    79    10 vhist16
    2605    79    10 vhist16
```

Benchmark first tests for correctness, then processes a 32k buffer of
random data.  I should probably benchmark an all-zero buffer as well.
Anyway, on my Tiger Lake it runs in 2600 cycles or 0.08 cycles per
byte.  Third column is an estimate of how many vector operations I run
per 64B and the estimate matches the code.

Core loop is mostly this:
```
	__m512i x0 = _mm512_shldv_epi16(one, one, raw);
	__m512i x1 = _mm512_shldv_epi16(one, one, _mm512_alignr_epi8(raw, raw, 1));
	FA2(&x1, &b0, x0);
	acc += bitpermute_popcnt16(x1);
```
3 operations for the vector shifts, 2 for the full-adder, 5 for the
bitpermute and popcount.


# vhist32

If we want a larger value range than nibbles, we can go two ways.
Either we do binning with vpcompressb.  Or we use a different vector
shift.  Going up from 4-bit values to 5-bit values, binning would be
too expensive.

```
  cycles  mc/b   vop
    4712   143    18 vhist32
    4712   143    18 vhist32
    4715   143    18 vhist32
    4720   144    18 vhist32
```

Cost hasn't quite doubled.  The vop estimate is spot-on again, my code
comments also add up to 18 operations.  Operations are fairly evenly
balanced across the ports for Intel cores, 5 on p0, 6 on p5 and 7
operations can go to either port.  We don't have to worry about either
port becoming a bottleneck.
```
	/* 4*p0, 3*p5 */
	__m512i x0 = _mm512_shldv_epi32(one, one, raw);
	__m512i x1 = _mm512_shldv_epi32(one, one, _mm512_alignr_epi8(raw, raw, 1));
	__m512i x2 = _mm512_shldv_epi32(one, one, _mm512_alignr_epi8(raw, raw, 2));
	__m512i x3 = _mm512_shldv_epi32(one, one, _mm512_alignr_epi8(raw, raw, 3));
	/* 6*p05 */
	FA2(&x1, &b0, x0);
	FA2(&x3, &b0, x2);
	FA2(&x3, &b1, x1);
	/* 1*p0, 3*p5, 1*p05 */
	acc += bitpermute_popcnt32(x3);
```


# vhist64

Next step up is getting interesting.  Again, we can either start
binning or use a different vector shift.  So which one is faster?

```
  cycles  mc/b   vop
    9294   283    36 vhist64
    9306   283    36 vhist64
    9312   284    36 vhist64
    9313   284    36 vhist64

  cycles  mc/b   vop
    8116   247    31 vhist64e
    8135   248    31 vhist64e
    8146   248    31 vhist64e
    8146   248    31 vhist64e
```

The winner is `vhist64e`.  But let's look at `vhist64` first.

```
	/* 8*p0, 7*p5 */
	__m512i x0 = _mm512_shldv_epi64(one, one, raw);
	__m512i x1 = _mm512_shldv_epi64(one, one, _mm512_alignr_epi8(raw, raw, 1));
	__m512i x2 = _mm512_shldv_epi64(one, one, _mm512_alignr_epi8(raw, raw, 2));
	__m512i x3 = _mm512_shldv_epi64(one, one, _mm512_alignr_epi8(raw, raw, 3));
	__m512i x4 = _mm512_shldv_epi64(one, one, _mm512_alignr_epi8(raw, raw, 4));
	__m512i x5 = _mm512_shldv_epi64(one, one, _mm512_alignr_epi8(raw, raw, 5));
	__m512i x6 = _mm512_shldv_epi64(one, one, _mm512_alignr_epi8(raw, raw, 6));
	__m512i x7 = _mm512_shldv_epi64(one, one, _mm512_alignr_epi8(raw, raw, 7));
	/* 14*p05 */
	FA2(&x1, &b0, x0);
	FA2(&x3, &b0, x2);
	FA2(&x5, &b0, x4);
	FA2(&x7, &b0, x6);
	FA2(&x3, &b1, x1);
	FA2(&x7, &b1, x5);
	FA2(&x7, &b2, x3);
	/* 1*p0, 3*p5, 1*p05 */
	acc += bitpermute_popcnt64(x7);
```

The benchmark estimates 36 vector operations, my own count is 34.  No
idea where the extra time is spent, it doesn't seem worth chasing
down.  Even with 32 vector operations, this would get beaten by the
other approach.

```
		__mmask64 k1 = _mm512_movepi8_mask(raw<<2);
		u64 gpr1 = _cvtmask64_u64(k1);
		u64 gpr0 = ~gpr1;
		__mmask64 k0 = _cvtu64_mask64(gpr0);

		p0 = compress_write_kgpr(p0, raw, k0, gpr0);
		p1 = compress_write_kgpr(p1, raw, k1, gpr1);
...
	__vhist32(hgram+ 0, buf0, p0-buf0); p0 = buf0;
	__vhist32(hgram+32, buf1, p1-buf1); p1 = buf1;
```

Main loop consists of two compress-operations to sort inputs into two
temporary buffers.  Then we run `__vhist32` on both buffers.  The only
difference between `__vhist32` and `vhist32` is whether the histogram
gets initialized first.  The underscore-variant adds to an existing
histogram.

Most of the code in the main loop is dealing with the warts of
`vpcompressb`.  Curtis Bezault[^1] pointed them out to me.  The
problem is that we have to deal with three completely different
register sets and at least two moves between them.  Once you realize
that, you can play some tricks.

First move is from a vector register to a kreg.  We want to extract
bit 5, cheapest method is to shift left, then call
`_mm512_movepi8_mask`.  We need two masks, so we could negate in the
kreg.  But all kreg-operations go to the same two ports used by vector
instructions, so the negation would add to our bottleneck.

We also have to advance the output pointers, which requires a
popcount.  Popcount instruction only works on general purpose
registers (GPR) and the pointers are in GPR anyway.  So we also have
to move both masks from kregs to GPRs.

The trick Curtis found is to only move one kreg to GPR.  We negate in
a GPR, then move the negated GPR back to a kreg.  Whether we move from
kreg to GPR or the other way around barely matters, one operation uses
port 0, the other port 5.  We have enough instructions that can use
either port.  But doing the negation in GPR allows us to use 4 ports,
and only two of them are our bottleneck.

Code isn't pretty.  And the compiler could decide to undo our tricky
optimization.  I think gcc currently preserves it and clang generates
bad code.  But at least I tried and made my intentions obvious to the
reader - for a very limited set of readers at least.


# vhist128 and vhist256

Once we have our binning approach, we can repeat things for bits 6 and 7.
Unlike Harold, I do the binning in multiple steps and only select
based on a single bit for each step.

Mathematically it doesn't make a difference whether you do 4-way
binning or 2 rounds of 2-way binning.  Either way you have to cover
all data with 4 compress-operations.  But my approach of 2-way binning
still seems to help a bit.

I think the answer is down to write pressure again.  2-way binning
ends up consuming some of the output bins sooner.  Binning is limited
by the write port and we have to switch to our histogram code before
the accumulated write operations overwhelm the reorder buffer (ROB).
With 2-way binning we do that sooner, or alternatively we can use
larger buffers without overflowing the ROB.

```
  cycles  mc/b   vop
   10987   335    42 vhist128e
   10996   335    42 vhist128e
   11008   335    43 vhist128e
   11011   336    43 vhist128e
```

```
  cycles  mc/b   vop
   15517   473    60 vhist256e
   15586   475    60 vhist256e
   15633   477    61 vhist256e
   15645   477    61 vhist256e
```


# Tail handling

Vector loops are great for multiples of 64.  But input buffers come in
all sizes and we need to deal with the last 1-63 bytes somehow.
Harold took the easy way out and ran a trivial scalar histogram.  I
wanted to do better.  Here is my solution for vhist16:

```
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
tail:
		__m512i x0 = _mm512_shldv_epi16(one, one, raw);
		__m512i x1 = _mm512_shldv_epi16(one, one, _mm512_alignr_epi8(raw, raw, 1));
		FA2(&x1, &b0, x0);
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

```

This is one of those rare examples where `goto` is the opposite of
harmful.  After we finish the main loop, we check whether we still
have a tail to deal with.  If we do, we do a masked load.  AVX512
masked loads are great, they set all the masked-off bytes to zero and
also avoid segmentation faults.

When doing a regular vector read for the tail, you always read beyond
the end of the buffer.  That may be harmless in the sense that you
don't actually use the extra bytes that you have read.  But if the
buffer happens to end on a page boundary and the next page is
unmapped, it will cause a segmentation fault.  These kinds of
segmentation faults are too rare to be caught in testing, but will
eventually hit your customers.

With AVX512 masked reads, only the bytes you actually read can cause
segmentation faults.  So if you mask off all the bytes beyond your
input buffer, you are safe.

Since we calculate a regular histogram including the extra zeroes from
our masked read, we subtract the padding bytes from the histogram in
the last line.  Deliberately make a mistake, then fix it up again.

Overall tail handling is pretty cheap.  Even the branches should be
fairly easy to predict.


# Conclusion

```
  cycles  mc/b   vop
    2598    79    10 vhist16
    4712   143    18 vhist32
    8116   247    31 vhist64e
   10987   335    42 vhist128e
   15517   473    60 vhist256e
```

End result is pretty fast.  You can estimate the cost of the binning
from the delta between two versions.  It is notable that the extra
binning from `vhist128e` to `vhist256e` is more expensive than the
others, so maybe we can still squeeze some blood from this stone.

This could also be a benchmarking artifact.  The benchmark isn't
completely stable and it's easy to confuse CPU frequency changes with
improvements.

Anyway, in the current state it is a clear improvement.  A bit faster
than Harold's version.  Or rather than my copy of Harold's version, I
turned it into C, converted the output from a 32bit histogram to 16bit
and may have messed things up.  More importantly, it's almost 2.4x
faster than my old scalar `hgram_8x4`.

```
  cycles  mc/b   vop
   15517   473    60 vhist256e
   17478   533    68 hgram_harold
   37184  1134   145 hgram_8x4
```

Code is still a bit of a mess and I want to clean things up before
publishing it.  Forgive me.


[^1] There is a surprisingly small Cabal of instruction counters,
people that care deeply about performance and learn all the associated
tricks.  There are 10-20 well-known names that constantly pop up when
searching stack overflow, blog posts, academic papers, etc.  I suspect
there are another 100-200 similarly good people lurking in the shadows.
Curtis is one of those lurkers.
