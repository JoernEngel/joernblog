# Vectorized Huffman

This is a patent prevention post.  Less thought out than usual, but I
want to get a priority date before some patent troll can register a
patent on this.

The idea is based on my limited understanding of Marcin Zukowski's
[pivco-huffman](https://marcinzukowski.github.io/pivco-huffman/paper-1.0/ph.html)
paper.  It is possible that I'm just repeating his work in my own
words.  If not, my lack of understanding forced me to come up with my
own design roughly along the lines of what he described.


# Transposed Huffman

What Marcin calls a "pivot", I typically call a "transpose".  You
transpose a matrix by switching rows and columns.  Same thing here.
Using Marcin's example:

```
h 101
u 01
f 00
f 00
m 110
a 100
n 111
```

Regular Huffman would iterate this row-by-row, so `101`, `01`, `00`,
...

Transposed Huffman iterates column-by-column, so `1000111` followed
by... not `0100101`.  From the second column on, things get a little
tricky.  We first take the bits with a `0` in the first column, then
the bits with a `1` in the first column.  Third column gets split in
four parts, based on the 2-bit prefix.  etc.

`1000111`, `100`, `0101`, ...

Transposing things this way is a bit weird, but we generate the exact
same bits and only muck with the order a bit.  That mucking is what
allows us to vectorize Huffman decode.  We can also vectorize encode,
but decode is what typically matters and what I'll focus on.


# Decode step 1, count the spans

I will use the term `span` for all the bits in a column with the same
prefix.  `1000111` (no prefix), `100` (prefix 0), `0101` (prefix 1),
`10` (prefix 10), etc.

Finding the first span is trivial.  We need the number of bytes to
decode in a header somewhere.  For `huffman` that is 7 bytes, so the
first span is the first 7 bits.

For the following spans we use the popcnt instruction.  Grab all the
bits in the previous span and count the 1-bits.  3 0-bits and 4 1-bits
means the next 3 bits are our prefix-0 span, the next 4 bits our
prefix-1 span.  Repeat until you get all the spans.


# Decode step 2, merge with vpexpandb

Now you go bottom-up and merge spans with a shared prefix.  Taking the
last step as our example, you have already decoded the prefix-0 span
to `uff` and the prefix-1 span to `hman`.  Now you have to merge those
two using the no-prefix span.

`1000111` + `uff` + `hman` = `huffman`

In scalar code you can iterate the bits in the span and for every
0-bit you grab the next byte from the prefix-0 decoded output,
otherwise you grab the next byte from the prefix-1 decoded output.

Similarly you get the prefix-1 output by merging the prefix-10 and
prefix-11 output.  That's pretty much it.

To vectorize this, you run vpexpandb twice to generate 64 bytes of
output.  If you have written vector code before, this should be fairly
straight-forward.  If you haven't, consider learning a new skill. ;)

Performance is a bit unusual.  A good scalar Huffman decoder runs at
roughly 1.5 cycles/byte (decoded byte), on modern-ish x86 hardware.
Ignoring the popcnt, this vectorized decoder runs at 4 cycles per 64
bit.  Those are encoded bits, which means that performance depends on
compression ratio.  But if we are ultra-conservative and assume no
compression, we have 8 bits per byte and run at roughly 0.5
cycles/byte or 3x faster.

That is of course assuming I didn't make a horrible mistake.


# Optimizing leaves

When merging two leaves, you have fairly boring inputs.  Something
like merging `aaaaaaaaaaaaaa` with `bbbbbbbbbb`, according to some
bitmap.  Using vpexpandb still works, but I think you can easily spot
some optimization potential here.

We can use pshufb to merge up to 16 leaves at once, with some
restrictions.  I'll leave the details to the reader.


# Drawback

Problem with this approach is that it depends on avx512 for vpexpandb.
Without that instruction, performance will suffer and decode will be
slower than for scalar Huffman.

And the encoder has to decide the format.  Transposed Huffman is
faster on machines with avx512, but slower everywhere else.  Regular
Huffman is missing the 3x speed boost.


# Conclusion

If everything I have described here matches what Marcin already does,
forgive me for not understanding his paper.  It is pretty common that
what appears natural and obvious to the author is quite alien to the
reader, presumably some of my readers have a similar response.

In case I have come up with something new, I could only do it because
Marcin's paper has pushed my brain in the right direction.  This is a
new idea and the first significant improvement for Huffman since 2013.
The usual suspects are likely to pay some attention soon, if they
haven't already.
