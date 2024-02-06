Improving ECC-DRAM repair - part 2
----------------------------------

In the [previous installment](ldpc_ecc.md) I ignored
[Chipkill](https://en.wikipedia.org/wiki/Chipkill).  This installment is
plugging that gap.


What is Chipkill?
-----------------

In a legal sense Chipkill is a trademark owned by IBM.  But among engineers it
has become a generic term similar to Thermos bottles.  You can find the term in
the Linux kernel source code in contexts that could be considered trademark
infringement.  Anyway, I would rather not get sued and will use the term
"Chiprepair" from now on.  As a reader, feel free to replace "Chiprepair" with
"Chipkill".


What is Chiprepair?
-------------------

Put simply, chiprepair can repair any garbage produced by a single chip.  Which
means that it depends on the memory geometry.  In a 64+8 geometry where each
chip provides 8 bits or 1 byte toward a cacheline read, being able to repair any
byte would be sufficient.  In a 32+4 geometry where each chip provides 16 bits
or 2 bytes, we need the ability to repair a 16bit word.

Chiprepair is significantly easier than repairing any 8 or 16 errors, since we
have the constraint that all errors must be part of the same group, within a
byte or within a 16bit word.

Existing implementations seem to either use SECDED with each of the 64+8 bits
going to a different chip (64+8 geometry) or some variant of Reed Solomon
allowing a 32+4 geometry.  I haven't found useful documentation on the details
of the Reed Solomon code.  Many papers appear to blindly copy each other,
indicating that the paper authors don't understand the details either.


Can LDPC be used for Chiprepair?
--------------------------------

Short answer is yes.  You can exhaustively try every possible 1-byte or 1-word
(meaning 1 16-bit word in this context) corruption and find that they all map to
a unique error syndrome.  Which means that mapping back from those syndromes to
the underlying error would allow repair.

But there are 18360 possible 1-byte corruptions and 2359260 possible 1-word
corruptions, so a naive implementation via a large hashtable would be quite
expensive.  We don't just want to repair, we also want the repair to be cheap.
So let's look for a better approach.

The key insight is that our LDPC code uses a carryless multiplication (clmul) to
calculate the ECC information and/or the syndrome.  It has 8 different
multipliers, one for each 64bit chunk of the data.  If we only look at the first
chunk, we calculate the ECC as follows:
```
	u128 product = clmul(data, 0b10000100010010111);
	ecc ^= product >> 64;
	ecc ^= product;
```

We calculate an intermediate 128bit value, the product of the data chunk and our
multiplication constant.  Then we fold both the high and low halves into the
ECC.  The high half is a bit of a problem.  But that problem can be avoided.

If the error is in the low 16 bits of the chunk, we only need the product of the
error and our multiplication constant.  Since the multiplication constant is
smaller than 48 bits, the product will be smaller than 64 bits and we can ignore
the high half.

If the error is in a different part of the chunk, we can use rotational symmetry
to our advantage.  We rotate the error until it sits within the low 16 bits.  We
then rotate the syndrome by the same amount.  Using this approach we can handle
any 1-word error within this chunk.  And most other chunks, with one exception.

```
	u128 product = clmul(data, 0b10001000000000000100000010000001000000001000000001);
	ecc ^= product >> 64;
	ecc ^= product;
```

The last multiplier is too large, it exceeds 48 bits.  We could try to handle
that exceptional case, but it is easier to find new multiplication constants.

Old:
```
static u64 mask[8] = {
	0b10000100010010111,
	0b1000000000011000010000000100011,
	0b10100000010000000000001100000011,
	0b10010000000000010000000001000001000101,
	0b10000000000000010010000100100000000000000011,
	0b1000010000100000000000000010100000000010000001,
	0b10100000000001001000000000100000000001000000001,
	0b10001000000000000100000010000001000000001000000001,
};
```

New:
```
static u64 mask[8] = {
	0b10000100010010111,
	0b1000000000011000010000000100011,
	0b10100000010000000000001100000011,
	0b1000000100000001000000000001000001000101,
	0b10001000000000010000000000001010000100001,
	0b100001000001000000000100000000001001000001,
	0b10000000000000010010000100100000000000000011,
	0b10100000000000000101000000000000010000000001001,
};
```

Now all constants are small enough that we our product fits in a 64bit number
and we can ignore the high half.  And if we can invert the multiplication, we
can map back from the syndrome to the underlying error.  We need a carryless
division, like one used for [crc](crc.md) calculation.

But divisions are generally slower than multiplications.  So we use the old
trick of dividing by a [multiplicative
inverse](https://en.wikipedia.org/wiki/Multiplicative_inverse).  In carryless
math, we can use these inverse multipliers:
```
static u64 im[8] = {
	0b1000010001101010101110110011101011011001101010000,
	0b10000000000110000100001011000110010,
	0b1010101011101110110001011110011011,
	0b10000001000000110000010000,
	0b1000100010001001100010011,
	0b100001000011000100001010,
	0b1000000000000001001000,
	0b1010101010101010111,
};
```

Now we can do fairly simple math using the multiplier `m` and inverse multiplier
`im`:
```
	error = clmul(syndrome, im) >> 64;
	modulus = syndrome ^ clmul(error, m);
```

If the modulus is zero, we have found the correct error.  Otherwise we move on
and check the next word.  Repeat for all 32 data words and you are guaranteed to
repair one 1-word corruption in data words.

Repairing a corruption in the ECC words is even easier.  Mask off the
corresponding 16 bits of the syndrome.  If the masked syndrome is zero, you had
a corruption in an ECC word.


Does it work?
-------------

It does.  I have exhaustively tested all possible 1-word corruptions and was
able repair all of them.  Chiprepair appears even faster than repairing 1-bit
errors using the old code, but that is a measurement artifact.

| Error count	| 8 x SECDED	| LDPC		| LDPC chiprepair	|
| -----------	| -----------	| -----------	| --------------	|
| 1		| 170ns		|     140ns	| 105ns			|
| 2		| 190ns		|     320ns	| 105ns	(1-word)	|
| 3		| 210ns		|    1µs	| 105ns	(1-word)	|
| 4		| 230ns		|    6µs	| 105ns	(1-word)	|
| 5		| 250ns		|  130µs	| 105ns	(1-word)	|
| 6		| 270ns		| 8ms		| 105ns	(1-word)	|
|16		|		|		| 105ns	(1-word)	|

The entire table is highly unfair.  First off, LDPC chiprepair can only repair
multiple bit-errors if all those errors are confined to the same word.  It fails
to repair even 2 random errors.  But chiprepair is still useful in practice.

Performance of chiprepair is about the same as performance for repairing 1-bit
errors when I generate random errors.  So the improvement appears to be
partially from random generator overhead and partially from CPU branch
predictors being able to correctly predict branches.


Can we still correct 2 random errors?
-------------------------------------

Mostly yes, but not quite.  There are a few exceptions where this doesn't work.
To understand those, let's look at our first multiplier again.
```
	0b10000100010010111,
```

The multiplier is a 17bit number.  A 16bit number would be catastrophic, we
could not distinguish between 1 bit-error in the data bit and 1 word-error in
the ECC.  17bit still works, as long as we only get one error.  But if the
second error effectively clears the top bit in our multiplier, we are in
trouble.
```
	0b00000100010010111,
```

Now all syndrome errors are within a stretch of 12 adjacent bits.  In many cases
those will fall within the range of a 16bit word and chiprepair will interpret
the syndrome as an ECC error.  In those instances we lose the ability of doing
both chiprepair and repairing 2 or more random errors.  We can still repair
99.9% of 2-bit error, but no longer all.

Looks like we need a new set of multipliers.  Again.

New new:
```
static u64 mask[8] = {
	0b100010000000010010111,
	0b1000000010000000000011000100011,
	0b1010000000000000000000001100100000011,
	0b1010001000001000000100000000000100000001,
	0b10000010000000001000000000010000001000101,
	0b10000000000000010000100000100001000000000101,
	0b10000000000000010000001000000100100000000000011,
	0b101000010000000010000000000001000000000000001001,
};
```

The first mask/multiplier is larger than before.  Now we should be able to
correct any 2 errors and do chiprepair.  But we would run into the same issue
when trying to correct any 3 errors.  Mask off the first two bits and the
multiplier becomes 0b10010111, too small.

New new new:
```
static u64 mask[8] = {
	0b100010010000000000010111,
	0b1010000010000000000000000001100011,
	0b1000010000000000000000000100000000110000011,
	0b10001000000000001000000000000100100000000011,
	0b1000000000001000000001000010000000101000000001,
	0b10000000000100010000100000000001000000100000001,
	0b10000010000000001000010000001000000100000000001,
	0b10010000010000000000000100000000000010000001001,
};
```

Finally!  This set of multipliers has all the required attributes:
- 7 bit set per multiplier,
- max overlap of 2 bit for any two rotated multipliers,
- no multiplier exceeds 47bit, allowing fast 16bit chiprepair,
- multipliers with any 2 bit removed span at least 17 bits,
- hamming distance of 8, allowing repair of any 3 bit-errors,
- only 512 undetected 8-error patterns (mostly hamming distance of 10),

And just to make sure, I have verified correct repair of:
- every possible 1-word error (using 16bit words),
- every possible 1-bit error,
- every possible 2-bit error and
- every possible 3-bit error

In other words, this set of multipliers is just as good as the old set for
random bit errors, while also allowing chiprepair using a 32+4 geometry.


Conclusion
----------

I think LDPC makes for a much better error correction code and should replace
SECDED in memory controllers.  It can also provide what IBM calls Chipkill and I
call Chiprepair to avoid IBM's trademark.

My [code](ldpc.c) is available.
