# New histogram speed record

Three months ago Harold Aptroot entered the compression hall of fame
when he posted some [code](https://gitlab.com/-/snippets/3745720) and
a [blog post](https://bitmath.blogspot.com/2024/11/histogramming-bytes-with-positional.html).

In short, he shattered the previous record and doubled histogram
performance.


# Background

Histograms have two main applications I know.  In compression, entropy
coders like Huffman make common symbols shorter and uncommon symbols
longer.  To do that you first need to know how common each symbol is,
in other words you need a histogram.  Incidentally the
[next step](engel_coding.md) is covered by my first entry to this
blog.

Radix sort also needs a histogram before moving entries to the correct
spot.  Typically you need multiple histograms.  To sort 32bit numbers
would run 4 histogram, one for each byte of the 32bit numbers.
Incidentally again, radix sort can be used as the sorting step in
Huffman.

There is a third histogram hidden in my Huffman coder as well, so let's
just say that I care a little bit about histograms and how much time
it takes to calculate them.

# State of the art

At least four compression people have spent significant time trying to
optimize histogram calculation.  Afaik they all ended up with more or
less the same solution.  You start with a naïve loop.

```
	while (ip<end)
		hgram[*ip++]++;
```

Each iteration loads a byte, loads the corresponding counter in the
histogram, increments the counter and writes it back.  Simple and
surprisingly fast.  Unless you have many repeats of the same byte.

If you have to increment the same counter twice, most CPUs will make
that a dependency.  You first have to wait for the previous iteration
to write the counter back to L1.  Only then can you load the counter
again.  To solve that you can have multiple counters.


```
	while (ip<end) {
		hgram0[*ip++]++;
		hgram1[*ip++]++;
		hgram2[*ip++]++;
		hgram3[*ip++]++;
	}
```

How many counters you need to completely avoid such dependencies, that
depends on your CPU micro-architecture.  I think 9 on Haswell, 11 on
newer Intel cores.  But with out of order execution (OoO), you can
hide some dependencies and only need 11 if every single byte hits the
same counter.  Most people seem to use 8 copies, then merge the 8
histograms later on.

My implementation also does 4x loop unrolling.  Not absolutely
necessary, but it still helps a little bit.  Then you can reduce
memory traffic by loading 8 bytes at once.  On x86 you can read into
`rax`, then use `al` and `ah` to consume two bytes before shifting and
consuming two more, etc.

Finally you can load 8 bytes ahead of time.  Each loop iteration loads
the 8 bytes for the next iteration, then uses the 8 bytes loaded by
the previous iteration.  Normally with OoO that sort of trick
shouldn't be necessary.  But we put a lot of pressure on reads and
writes.

End result is moderately complicated and not significantly faster than
the naïve loop on random inputs.  You should get close to 1 cpb (cycle
per byte).


# More background

There are two dominant flavors of histograms and Harold introduced a
third.  I don't know an established name for the third, so I just made
up a new one for it:

```
Byte - an 8bit value
Morsel - a 6bit value
Nibble - a 4bit value
```

Most of the time you want a byte-histogram.  Sometimes you can limit
the value range to 0..15 and only need a nibble-histogram.  I am not
aware of any use for a morsel-histogram, apart from Harold's
masterpiece.

It is possible to calculate a nibble-histogram faster than 1 cpb, but
the vector code I designed for that purpose was rather awful and could
only reduce cost to .75 cpb.  So I abandoned the idea in 2020.  Harold
then independently came up with the same basic idea, plus many more.

Specifically you can use `_mm256_sllv_epi32` or some other variant of
`sllv`.  `_mm256_sllv_epi32` does 8 independent shifts, using 8
inputs.  For a nibble-histogram you can treat the resulting u32 has 16
independent 2bit counters.  So you can run `_mm256_sllv_epi32` 3 times
before your 2bit counters might overflow.

It is possible to spill a vector of 2bit counters into 2 vectors of
4bit counters, then 8 vectors of 8bit counters, etc.  This can be made
to work, but it won't be nice and you constantly have to deal with
overflows.


# Harold's histogram

So what did Harold do better than me?  Quite a few things.  He uses
AVX512, something that wasn't quite usable back in 2020 yet.  He uses
`_mm512_sllv_epi64`, allowing him to consume 6bit morsels instead of
4bit nibbles.  Ok, fine.  And then he switched from horizontal
arithmetic to vertical arithmetic.  Wait, what?  What on earth is
vertical arithmetic?


# Vertical arithmetic

Horizontal arithmetic is what you are familiar with.  `5` can be
expressed as `00000101`.  Each 1 in the binary representation has a
different power-of-two value.  I hope that sounds totally boring.  You
will rarely think about the details and leave those to your CPU.

Vertical arithmetic works the same way, but you now store the
different power-of-two values in different registers.  Now you would
express `5` as three or more registers:

```
reg0 = 1
reg1 = 0
reg2 = 1
...
```

You have the same bit-pattern, just vertically in different registers
instead of horizontally.  And if you want to add two numbers you now
have to create your own adder from scratch, using only basic logic
gates.  If you don't remember the difference between half-adders and
full-adders, find your old university notes or
[wikipedia](https://en.wikipedia.org/wiki/Adder_(electronics)).

Implementing a full-adder in logic requires quite a few gates.  Unless
you use AVX512 and the cheat code known as `vpternlog`.  You can
implement a full-adder in two such instructions.  Which allowed Harold
to turn 1bit counters into vertical n-bit counters.


# Affine bit-shuffle

Eventually the vertical counters have to be extracted.  Each 512bit
register contains 8 sets of 64 1bit counters.  The problem is that all
counters for the symbol 0x00 are at bit-offsets 0, 64, 128, etc.
Quite far apart from each other.

With a byte-shuffle we can bring them to bit-offsets 0, 8, 16, etc.
And with a bit-shuffle we can get them to bit-offsets 0, 1, 2, etc.
After that you can run a byte-wise popcnt to turn 8 individual 1bit
counters into a single 8bit counter you can add to some accumulator.
A horizontal 8bit counter, we are back in familiar territory.  Huzzah!

```
            w3_0 = _mm512_permutexvar_epi8(tp, w3_0);
            w3_0 = _mm512_gf2p8affine_epi64_epi8(_mm512_set1_epi64(0x8040201008040201), w3_0, 0);
            w3_0 = _mm512_popcnt_epi8(w3_0);
            w = _mm512_add_epi8(w, w3_0);
```

Ok, if we eventually do this anyway, why the long detour with vertical
arithmetic and building our own full-adder?  Well, the full-adder
consumes 2 vector-instructions and the shuffle/popcnt/add (SPA)
consumes 4 vector-instructions.  Harold does 8 full-adder steps per
SPA, amortizing the cost quite a bit.  Nice.

And in case you want yet another deep rabbit-hole to get lost in, you
can look at all the things `_mm512_gf2p8affine_epi64_epi8` does.  It
can be used to reshuffle bits within a 64bit block.  But it can also
do many more things and finding new creative uses for it is one of the
favorite games for hackers that enjoy bit-twiddling.  Have a fun week!


# Morsel-binning

We still have the problem that using a 64bit shift we can only run a
morsel-histogram while most people want a byte-histogram.  To solve
that problem, Harold uses `_mm512_maskz_compress_epi8`.  Compress is
one of the better new instructions introduced by AVX512.  Yet another
deep rabbit hole, but not quite as deep as affine.

Harold calculates four masks matching the top two bits and sorts the
inputs into four temporary buffers, one each for 00, 01, 10 and 11.
Pretty obvious, in principle.  But the code looks a bit funny.

```
            __mmask64 bit7 = _mm512_movepi8_mask(data);
            __mmask64 bit6 = _mm512_movepi8_mask(_mm512_add_epi8(data, data));
```

The obvious approach would be to mask off the bottom 6 bits and
compare the remainder against 00, 01, 10 and 11.  Instead Harold
extracts the top bit into a k-reg.  He then shifts left by adding data
to itself and extracts the second-from-top bit into a k-reg.  Then he
does a bunch of bit operations on the k-regs to get the expected 4
masks.

In case you aren't familiar with k-regs, they were introduced with
AVX512 and are used everywhere for masking.  Travis Downs has a nice
[article](https://travisdowns.github.io/blog/2020/05/26/kreg2.html)
about them.

So, why the bizarre approach to calculate masks?  Because each
compress-operations consumes 2 µops on port 5.  Anything else we do on
port 5 will add to our current bottleneck.  All the k-reg operations
are on port 0 and it is better to do a few more operations on port 0
than adding anything on port 5.

I suspect it might be possible to do one less instruction to calculate
the masks while still staying on port 0.  But the existing code is
already close to optimal.  And so far most of my attempts to make
improvements turned out to either not help or actively hurt.  The code
really is quite clever.


# Masking off the top bits

Since our morsel-histogram requires the two top-bits to be 0, we have
to mask them off somewhere.  Harold does it just before binning, fine.
And he uses affine for that purpose again.  A simple mask would have
worked just as well, but sure.  If you want to show off, so will
affine.

But wait a moment.  He doesn't just mask off two bits, he also
reshuffles the remaining 6 bits?  What the...?!?  Why?

To answer that question, remember that we eventually end up with 64
8bit counters.  If you histogram large buffers you need 16bit counters
or maybe even 32bit counters.  So you split one vector of 64 8bit
counters into two vectors 32 16bit counters, using a mask and a shift.

```
        h0 = _mm512_add_epi16(h0, _mm512_and_epi64(w, _mm512_set1_epi16(0xFF)));
        h1 = _mm512_add_epi16(h1, _mm512_srli_epi16(w, 8));
```

The problem here is that you now have one vector with all the even
counters (0, 2, 4, etc) and another vector with all the odd counters
(1, 3, 5, etc).  So you need yet another shuffle to restore the
correct order (0, 1, 2, etc).  But there is no such shuffle.  Or is
there?

When the individual bits get reshuffled with the affine instruction
that does the masking, it removes the need to reshuffle the counters
later on.  The bit-shuffle is carefully chosen to obsolete one or two
more expensive shuffles later on.  Quite clever.


# Possible improvements

Almost everything I tried ended up in failure, with one exception so
far.  Shrinking the temporary buffers for the binning to 1k or 2k
results in a speedup on Icelake.  It doesn't appear to help or to hurt
much on Emerald Rapids.

The problem on Icelake appears to be the unaligned writes during
binning.  They eventually fill the write buffers and become an even
worse bottleneck than port 5 congestion.  But if you keep the
temporary buffer small enough, OoO allows you to overlap those writes
with the consumption of our temporary buffers.

Too much binning before we start consuming eventually fills the entire
OoO window of about 500 instructions and we are hard limited by the
write performance.  Keep things small enough for OoO to do its magic
and the write bottleneck no longer matters, now you are dominated by
vector instructions.


# Performance

End result is roughly 0.5 cpb, or half the cost of the previous
histogram kings that reached 1cpb.  Very impressive.

If you only need a morsel-histogram, it is even faster.  The
morsel-histogram costs roughly 0.25 cpb, with the remaining 0.25
coming from the binning.  Nice.  But wait!

If you only need a nibble-histogram, you can do much better.  Replace
`_mm512_sllv_epi64` with `_mm512_sllv_epi16` and you can consume 32
nibbles instead of 8 morsels.  Your nibble-histogram should now reach
0.06 cpb.  At least theoretically.  It is now so cheap that all the
random fixed overhead we typically ignore starts to dominate cost.

I haven't written the code yet, but you can probably imagine what one
of my next projects is going to be...


# Epilogue

I am deeply impressed by this masterpiece.  And so is everyone else I
talked to.  Anyone that can write code like this should be drowning in
job offers.  In the extremely unlikely event that you are not, Harold,
please contact me.  And if we ever meet somewhere, drinks are on me.
