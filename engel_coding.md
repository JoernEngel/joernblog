Engel coding
------------

What most people call Huffman coding is technically prefix coding.  It
predates Huffman's algorithm and often doesn't employ Huffman's
algorithm.  Catchy names tend to be stickier than implementation
details.

Fast huffman coders tend to use length-limited codes.  For example,
[Huff0](https://github.com/Cyan4973/FiniteStateEntropy) uses a 12-bit
limit.  As a result, it can use a 4096-entry table, which nicely fits
into L1 cache and (on 64bit) can decode 4 symbols before refilling the
shift register.  If it allowed longer symbols, the decode would be
slower.

But once you enforce a length limit, you can no longer use Huffman's
algorithm as-is.  Basically you are left with three options.

1. Use the [Package Merge](citeseerx.ist.psu.edu/viewdoc/summary?doi=10.1.1.84.5911)
   algorithm.  It is optimal, but somewhat hard to understand and
   there is currently no open-source implementation anywhere.
2. Use Huffman's algorithm, then shorten long symbols to not exceed
   your limit (violating the Kraft invariant), then heuristically
   adjust other symbols to compensate and return to K=1.  Huff0 does
   this.
3. Use a heuristic without Huffman's algorithm as a starting point.
   Using Huffman to get an optimal intermediate step, then throwing
   away the optimality with a heuristic is a bit pointless, after all.
   [Polar coding](http://www.ezcodesample.com/prefixer/prefixer_article.html)
   is an example of this.

For my own Huffman coder, I picked option 3.  My encoder is faster
than others, at least in my own benchmarks, so the heuristic I picked
appears to have some merit.  It also appears to be something new.
Charles Bloom hasn't found a prior description and he coined the term
Engel coding for it.


Algorithm
---------

Step 1: Sort symbols by histogram.  
Step 2: Create initial bitlen for each symbol.  
Step 3: Calculate credit/debt.  
Step 4: Use up credit/repay debt.  

Sorting symbols has known solutions.  Yann's version in Huff0 is
pretty good.  Initial bitlen is round(-log2(p(sym))), so a sym with
p=.5 gets bitlen=1, p=.25 gets bitlen=2, etc.  Boundary between 1-bit
and 2-bit symbols has 1.5 = -log2(p) or p = 2^(-1.5), about .353.

Floating point math is slow, logarithms are slow, so let's do
everything with integer math.  Because symbols are already sorted, we
can decide that all symbols receive 1 bit until we hit a symbols with
p <≈ .353.  The next symbols receive 2 bits until we reach p<≈ .176,
etc.  We don't even need to calculate p if we pick a boundary based on
histogram count.

	static const uint64_t sqrt_2_32 = 1518500250;
	int bit_boundary = (slen * sqrt_2_32) >> 32;

1518500250 is 2^(-1.5) << 32 or 2^(30.5).  Large enough for good
precision, small enough to avoid integer overflows.  After
multiplication and shift we have the boundary between 1-bit and 2-bit
based on histogram and can do a simple comparison for each symbol.

You have to be careful with the rare symbols, so make sure nothing
gets a bitlen larger than 12 (or whatever).  Give symbols with p=0 a
special value, I use MAX_BITS+1 in my code.

Calculating credit/debt is the same as calculating the Kraft number.
But I find it nicer use table slots as units, i.e. with MAX_BITS=12
and 4096 table slots you multiply K with 4096.  It nicely avoids
floating point math and rounding errors.


Repaying debt
-------------

This is the heuristic part where you fix one of two problems.  Either
you use more slots than you have in your table (debt) or fewer
(credit).  Credit is simply inefficient, debt breaks the decoder.

To repay debt, you make a symbol longer.  That reduces the slots by
half, up to MAX_BITS, which uses a single slot.  So you only want to
consider symbols with 1..MAX_BITS-1 bits.  And you only want to
consider the cheapest symbols, i.e. the symbols where lengthening
would do the least harm in terms of compression ratio.

Since your symbols are still sorted, the cheapest symbols necessarily
are right at the boundary between N bits and N+1 bits.  So you only
have to consider MAX_BITS-1 symbols and pick the one that costs you
the least per slot.  Cost is calculated as
	sym.cost = sym.hgram << sym.bitlen;

Finally, you don't want to repay too much.  If you need to free 3
slots, lengthening a 1-bit symbols is a bit excessive.  But you might
want to lengthen a MAX_BITS-3 symbol and free 4 slots.  Now you have
repaid too much, but absolute debt (credit is just negative debt) has
been reduced.

Dealing with credit is pretty much the same, except that nearly
everything is off-by-one.  You consider symbols 2..MAX_BITS, consider
the symbol on the other side of the bit-boundary, etc.

Once your debt reaches 0, you are done.


Dealing with rare cases
-----------------------

You can run into cases where the algorithm above cannot terminate.  If
you have debt of 1, you need to lengthen a symbol with MAX_BITS-1.
But you might not have such a symbol.  You could lengthen two symbols
with MAX_BITS, but that would be illegal.  So really you have to go
back and lengthen a symbol with MAX_BITS-2, MAX_BITS-3 or so.

As a result, your absolute debt may have to grow.  If you don't allow
that, you are stuck.  If you do allow it, you will next pick the
least-cost symbol to shorten, which is exactly the symbol you just
lengthened, and have an infinite loop.

My solution is to fall back to a naïve heuristic, described by Charles
[here](http://cbloomrants.blogspot.com/2010/07/07-03-10-length-limitted-huffman-codes.html).


End result should be faster than using Huffman's algorithm and give a
similar result, sometimes a little better, sometimes a little worse.
Definitely beats Charles' heuristic, which afaics is used by Huff0 as
well.  Package-merge would be optimal, but I expect it to be
noticeably slower.
