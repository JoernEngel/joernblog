Whisperrandom
-------------

Hidden in my LDPC work is a random number generator that may be of interest to
some.  It is reasonably good in all respects, though not the best in any one
category.  Comparing against some popular alternatives:

|		| Whisper		| libc/LCG	| Mersenne Twister	| rdtsc		| rdrand		| xorshift		|
| ---		| ---			| ---		| ---			| ---		| ---			| ---			|
| quality	| passes BigCrush	| fail		| passes most tests	| complete fail	| should pass BigCrush	| passes most tests	|
| speed		| 4 cycles		| see below	| 5 cycles		| 24 cycles	| 100-3500 cycles	| 6 cycles		|
| state size	| 16 bytes		| 4 bytes	| 2496 bytes		| 0		| 0			| 8 bytes		|
| cycle	length	| 2^64			| 2^31		| 2^19937 (I think)	| 2^64		| infinite?		| 1 (see below)		|


libc/LCG
--------

Most libc implementations use an
[LCG](https://en.wikipedia.org/wiki/Linear_congruential_generator).
Not because that is a good random number generator, but because some software
depends on the order or generated random numbers not changing.  So whatever
random number was chosen decades ago continues to be used today.  With
surprising attributes like returning a 32-bit number, but only within a 31-bit
range.  Surely there are countless examples of bugs caused by programmers
expecting the full 32-bit range.

LCG quality is poor as well.  The least significant bit of random numbers is a
strictly alternating sequence of 0 and 1.  The lowest N bit always follow the
same sequence of 2^N numbers.

And finally the random number generator was designed before multithreading was a
thing.  Which means that today it uses a lock and the locking overhead dominates
performance.  That is not a problem with LCG, but with an ossified library that
has to maintain compatibility with existing code.  Otherwise LCG should clock at
4 cycles per generated number, quite fast.


Mersenne Twister
----------------

The Mersenne Twister was fairly popular in the late 90s and is still used in a
lot of places today.  It has better quality than LCG, but still fails some
statistical tests in
[BigCrush](http://simul.iro.umontreal.ca/testu01/tu01.html).

Speed seems to be OK, although I couldn't find any measurements online.  Reading
the code, it should generate a 128bit random number in 3-4 cycles.  Or rather it
generates 156 128bit numbers, then spends most of the time returning those
pre-generated numbers.  So maybe 5 cycles overall, nothing to worry about.

The problem is the state size.  For multithreading you typically want each
thread to use a different RNG state.  Otherwise you need locks and performance
is completely dominated by locking overhead.  Some applications want even more
dedicated states for various reasons.  At 2496 bytes, each state is expensive
enough to become a consideration.


rdtsc
-----

Somewhat surprisingly, I have seen people use the x86 cycle counter as a random
number source.  I shouldn't have to explain what a horrible idea that is, but I
will do so anyway.

The thinking seems to be that a high-precision clock is a reasonably good
entropy source.  I agree, in principle it is.  The problem with rdtsc is that
the clock precision is a multiple of 1.  On my notebook, all numbers returned
are multiples of 3.  On other machines, all numbers returned are multiples of 4.
If you only use the bottom 2 bits of rdtsc, your RNG will always return 0.

Performance is poor as well.  There is a single cycle counter in each chip, not
one per core.  So each core has communication overhead to access the cycle
counter.  Overall cost seems to be in the 20-30 cycle range, depending on the
specific CPU.


rdrand
------

X86 CPUs have a dedicated random number generator first introduced in 2012.  It
is surrounded by a lot of controversy.  If you don't care about cryptographic
strength, it is mostly too slow.  Like rdtsc, it lives outside the core and
comes with communication overhead.  On top of that it appears to collect entropy
from some hardware and measure the collected entropy.  The time to read it out
then depends on how much entropy has been collected.  If you read it too often,
it has to wait for new entropy to accumulate.

Actual performance depends on the specific CPU design implementing rdrand,
possibly on the specific CPU in question and how noisy the circuit is after
manufacturing defects and possibly on recent history.

The fastest documented time, 104 cycles for Ivy Bridge, is already too slow for
many applications.


xorshift
--------

Xorshift was another popular RNG for a while.  It has a small state, just one
64bit number, and is relatively fast.  Code is simple enough to show it all.

```
u64 xorshift64(u64 *state)
{
	u64 x = *state;
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	return *state = x;
}
```

The problem is the cycle length.  And the fact that there are multiple cycles.
Presumably one cycle spans all 64bit numbers except one.  I don't know if that
is actually true.  It would take too long to test and I don't know of a
mathematical proof.  But it doesn't matter because the other cycle is just a
single number: zero.

You can easily check it yourself.  If the state is zero, it will remain zero and
every "random" number returned will be zero.  That is a critical flaw in my
book.  Users have to be careful to initialize the state and the most obvious
initialization value that most people would choose leads to catastrophic
failure.

Having a cycle that doesn't span the full range of 64bit number would lead to
other problems as well.  To simulate a fair 6-sided die, you might use code like
this:

```
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

```

In principle it returns `random() % n`, with `n` being 6 for a die.  We just
have to be careful if the random number is very large.  2^64 doesn't evenly
divide by 6, so the number 1-4 would occur slightly more often than 5-6.  If
your RNG returned a 4bit number instead of a 64bit, the effect would be a lot
more obvious.

If your RNG doesn't cycle over all possible 64bit numbers, you will get the same
effect.  Whatever numbers are skipped will give you a slight bias.  Whether that
bias is large enough to be a serious problem is hard to say.  It depends on the
specific problem you are trying to solve.  But I prefer not having to consider
that question in the first place.

So avoid RNGs that don't cover the entire number space and specifically avoid
xorshift.  I would also avoid xoroshiro and similar derivatives of xorshift.


PCG
---
[PCG](https://www.pcg-random.org/), developed by Melissa O'Neill, looks really
good to me and I wanted to use it.  But the default implementation
`pcg32_random_r` returns 32bit numbers and I often need 64bit numbers.  There is
a more full-featured [library](https://github.com/imneme/pcg-c) as well.  That
one contains dozens of functions with names like
`pcg_setseq_64_xsl_rr_rr_64_random_r`.

I have no idea what setseq, xsl, rr or the second rr mean or why I should choose
`pcg_setseq_64_xsl_rr_rr_64_random_r` over
`pcg_unique_64_xsl_rr_rr_64_random_r`.  Or why we have both
`pcg_setseq_64_xsh_rs_32_random_r` and `pcg_setseq_64_xsh_rr_32_random_r`, but
no `pcg_setseq_64_xsl_rs_64_random_r`.

Really, what I want is a single function that generates a random 64bit number.
So until someone create a simple PCG-library, I have given up.

The underlying idea is a good one.  You take a moderately good RNG (LCG) as a
basis, then apply a hash function to the output to improve the quality.  Now you
just have to find a hash function that is cheap enough and high-quality enough.


Whisperrandom
-------------

Whisperrandom can be viewed as a more extreme version of PCG.  We use a counter
as a starting point, then hash it to generate a random number.  Counters are not
very random.  But it is trivial to calculate the cycle length of a counter.

As long as your hash function exclusively uses reversible operations, it is
[bijective](https://en.wikipedia.org/wiki/Bijective).  So if the input spans all
possible 64bit numbers, the output also spans all possible 64bit number, only in
a different order.  The PCG hash function also has this attribute.

So, what sort of hash function is cheap enough and good enough?  Murmur is a
good starting point.  Multiplications with any odd number are reversible.  And
multiplications provide a lot of mixing.  All high output bits are influenced by
all low input bits.  The main problem is that low output bits are not influenced
by high input bits.  So output bit 0 is always identical to input bit 0.  Output
bit 1 is only influenced by input bits 0+1, etc.  High output bits have good
mixing, low output bits have poor mixing.

Murmur combines multiplication with rotate.  Rotate exchanges the high and the
low bits.  Which means that after another multiplication, all output bits depend
on all input bits.  The name is just a shorthand for MUltiply, Rotate, MUltiply,
Rotate.

Whisper follows the same idea, but uses two 64bit numbers.  X86 multiplications
generate a 128bit result.  Most languages just make it somewhat cumbersome to
access the high 64 bits of the result.  We multiply one number, then XOR the
high u64 with the other number.  Effectively this is using the
[Feistel](https://en.wikipedia.org/wiki/Feistel_cipher) construction.  But it
boils down to just two instructions Multiply and XOR.  We then alternate and
multiply first one number, then the other.  Again, very similar to Feistel
construction.

The name Whisper is mostly a pun on murmur.  Muxmux just doesn't have the same
ring to it.

For either murmur or whisper you need a minimum of three multiplications to pass
all statistical tests in BigCrush.  But I like a little extra margin, so
whisperrandom uses 4 or 5.  The 5th multiplication is a little sneaky.  Instead
of incrementing the counter by 1, we increment it by our multiplication
constant.  Which is the same as incrementing it by 1 and immediately doing a
multiplication, just much cheaper.

Having two 64bit numbers when we only use one of them as our counter might look
surprising.  The point is that different states (threads, etc) can initialize
the second number to a different value.  Since both numbers are fed into our
hash-function, we will now generate different sequences for each state.


Conclusion
----------

Overall whisperrandom follows the solid engineering practice of not being
perfect, but being good enough in many dimensions.  The quality is good enough
for all non-cryptographic purposes.  It is not the fastest, but fast enough that
I never worry about performance.  And all faster RNGs that I know have quality
issues.  The state is reasonably small.  The cycle length is 2^64, longer than
any computer could iterate within your lifetime.  It is very predictable in the
sense that you can jump ahead by N steps if you increment the counter by `N*M`.

It is good enough in all dimensions I care about that I no longer worry about
it.  So if you encounter any trouble with your random number generator, feel
free to replace it with mine.  Or create your own, that can be a fun exercise as
well.

If you want to create your own, an interesting approach might be to combine
whisper with the main operation of PCG, rotation be a somewhat-random number.
Making each round more expensive can be worthwhile if you can reduce the number
of rounds.

Code can be found in [ldpc.c](ldpc.c).
