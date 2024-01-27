Improving ECC-DRAM repair
-------------------------

ECC-DRAM typically uses [SECDED](https://en.wikipedia.org/wiki/Hamming_code),
with a few exceptions using [Chipkill](https://en.wikipedia.org/wiki/Chipkill).
Chipkill is mostly a marketing term.  It is unclear how it is implemented in
detail and there may be multiple different implementations sharing a common
name.  I will therefore ignore it.

SECDED can guarantee repair of single errors and detection of double errors.  It
turns out that an [LDPC](https://en.wikipedia.org/wiki/Ldpc)-code using the same
amount of parity information as SECDED can guarantee repair of triple errors,
detection of quadruple errors and typically still repair six errors.

| Error count	| SECDED	| LDPC		|
| -----------	| -----------	| -----------	|
| 1		| 100% repair	| 100% repair	|
| 2		|   0% repair	| 100% repair	|
| 3		|   0% repair	| 100% repair	|
| 4		|   0% repair	| 99.999% repair|
| 5		|   0% repair	| 99.99% repair	|
| 6		|   0% repair	| 99.9% repair	|

Error detection can be defined in multiple ways.  The common definition appears
to be such that triple errors cannot be detected if they are treated as single
errors and repaired.  I would argue that those errors are still detected, there
is a clear observable difference between repair and no repair.  Therefore, I
will only consider such errors as undetected that result in a zero syndrome and
don't trigger a repair attempt.  Using that definition, SECDED will guarantee
detection of up to 3 errors and mostly still detect 4 errors.

| Error count	| SECDED		| LDPC			|
| -----------	| -----------		| -----------		|
| 1		| 100% detection	| 100% detection	|
| 2		| 100% detection	| 100% detection	|
| 3		| 100% detection	| 100% detection	|
| 4		| 0.8% undetected	| 100% detection	|
| 5		| 100% detection	| 100% detection	|
| 6		| 100% detection	| 100% detection	|
| 7		| 100% detection	| 100% detection	|
| 8		| 10^-4 undetected	| 10^-19 undetected	|

A more interesting problem is misrepair.  With SECDED, roughly 75% of triple
errors get falsely interpreted as single errors and the repair actually
introduces new errors.

| Error count	| SECDED		| LDPC			|
| -----------	| -----------		| -----------		|
| 1		| no misrepair		| no misrepair		|
| 2		|			| no misrepair		|
| 3		| 76% misrepair		| no misrepair		|
| 4		|			| 10^-5 misrepaired	|

Ok, great, so what's the catch?


Bigger blocksize
----------------

SECDED is actually optimal, when encoding 64 data bits with 8 additional parity
bits.  Well, almost.  We could lower the misrepair rate a bit, but that hardly
makes a significant difference.

But CPUs practically never read 64 bits.  They read cachelines and most CPUs
today use cachelines of at least 64 Bytes or 512 bits.  Given 8x more data and
parity, we can do significantly better than just running SECDED 8 times over
smaller blocks.  The LDPC-code described above only works with 512+64
(data+parity) blocks, not with smaller 64+8 blocks.

Which means that all my tables made unfair comparisons.  If we randomly
distribute errors among the 512+64 bits, some of the time SECDED will get lucky
and only encounter a single error in each of the smaller blocks.

| Error count	| 8 x SECDED	| LDPC		|
| -----------	| -----------	| -----------	|
| 1		| 100% repair	| 100% repair	|
| 2		|  87% repair	| 100% repair	|
| 3		|  66% repair	| 100% repair	|
| 4		|  41% repair	| 99.999% repair|
| 5		|  21% repair	| 99.99% repair	|
| 6		|   7% repair	| 99.9% repair	|

| Error count	| 8 x SECDED		| LDPC			|
| -----------	| -----------		| -----------		|
| 1		| 100% detection	| 100% detection	|
| 2		| 100% detection	| 100% detection	|
| 3		| 100% detection	| 100% detection	|
| 4		| 10^-4 undetected	| 100% detection	|
| 5		| 100% detection	| 100% detection	|
| 6		| 100% detection	| 100% detection	|
| 7		| 100% detection	| 100% detection	|
| 8		| 10^-8 undetected	| 10^-19 undetected	|

| Error count	| 8 x SECDED		| LDPC			|
| -----------	| -----------		| -----------		|
| 1		| no misrepair		| no misrepair		|
| 2		|			| no misrepair		|
| 3		|  1% misrepair		| no misrepair		|
| 4		|  4% misrepair		| 10^-5 misrepaired	|
| 5		|  8% misrepair		| 10^-4 misrepaired	|
| 6		| 10% misrepair		| 10^-3 misrepaired	|

Still significantly better than SECDED.


Margin
------

A non-obvious advantage of LDPC is that it introduces margin.  Errors aren't
entirely random, bits that have experienced errors in the past are more likely
to experience another error than the rest.  Therefore it is advisable to report
all repairs to the Operating System (OS) and avoid using the same memory in the
future.  Otherwise the chances of double errors can exceed tolerable limits.

With LDPC, there is no urgent need to report 1-bit errors or even 2-bit errors.
Even with 2 permanently stuck bits, we can still repair all 3-bit errors and
most 4-bit errors.  Therefore we can afford to silently repair 1-bit and 2-bit
errors without reporting.


Details
-------

My LDPC code uses a [Circulant](https://en.wikipedia.org/wiki/Circulant_matrix)
design and can be described by 8 64bit numbers.
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
Parity can be calculated using a function like this.  Essentially, we rotate
each number 64 times to generate 512 total rows in a 64x512 matrix.
```
static u64 syndrome(u64 data[8])
{
	u64 ecc=0;
	for (int i=0; i<8; i++) {
		u64 val = data[i];
		u64 m = mask[i];
		for (int b=0; b<64; b++) {
			if (val&1)
				ecc ^= rotl64(m, b);
			val >>= 1;
		}
	}
	return ecc;
}

```

Each number has 7 bits set.  Therefore each data bit influences 7 parity bits.
The number are chosen such that no two rows share more than 2 set bits.
Furthermore, this set of numbers guarantees a [Hamming
distance](https://en.wikipedia.org/wiki/Hamming_distance) of 10 in all but 512
cases.  The 512 exceptions with a Hamming distance of 8 are the obvious ones.
Change any data bit and the 7 associated parity bits and the error will go
undetected.

That last attribute doesn't increase the Hamming distance beyond 8.  But it is
still useful in reducing the number of unrepaired 4-bit errors.  With a 99.999%
chance of repairing 4-bit errors, a repair attempt is worth trying.  Everything
else equal, a higher success rate is clearly preferable.


Repair
------

Repairing 1-bit errors is easy.  We calculate the syndrome and count the bit
errors in it.  Given 1 syndrome error, the parity bit is wrong and we can XOR
the parity with the calculated syndrome.  Given 7 syndrome errors, a data bit is
wrong and we find the data bits that matches our 7 syndrome errors.

Repairing 2-bit errors is slightly harder.  If only parity bits are wrong, the
syndrome has two bits set.  But if one or two data bits are wrong, we get
interactions like these:

```
1000010001001011100 error 1 (5 syndrome bits wrong)
0100001000100101110 error 2 (5 syndrome bits wrong)
0010000100010010111 good (2 syndrome bits wrong)
1100011001101110010 syndrome
```

Since the parity for both errors can partially overlap, they syndrome errors can
cancel each other out.  So we have fewer than 7 syndrome bits wrong.  Both
errors can also overlap with good bits and those good bits have more than 0
syndrome bits wrong.

Thankfully, good bits will never have more than 4 syndrome bits wrong while
error bits have at least 5 syndrome bits wrong.  So finding the error bits is
still fairly easy.  Until we get to 3-bit errors.


Backtracking
------------

Repairing 3-bit errors can result in all errors partially overlapping.  In the
worst case, each error only has 3 syndrome bits wrong.  And some good bits have
up to 6 syndrome bits wrong.  So we appear to have an undecidable problem.

To my surprise (I swear!), it is still possible to repair all 3-bit errors.  We
just try repairing a bit and see what happens.  If we guessed right and did
repair an error, we now have a 2-bit error.  And we already know how to repair
all 2-bit errors.  If we guessed wrong, we undo the attempted repair and pick
another bit.

It is still a good idea to guess based on the number of syndrome bits that are
wrong.  Most of the time, the obvious answer is also the correct answer.  But in
the worst case we have to make a fair number of guesses.  Which means that
repair time is quite variable.


Performance
-----------

I have written a software implementation of both SECDED and LDPC.  The code
should be reasonably fast and fairly representative for a hardware
implementation.  If you have a faster implementation or are able to measure a
hardware implementation, I would like to hear about that.

| Error count	| 8 x SECDED	| LDPC		|
| -----------	| -----------	| -----------	|
| 1		| 170ns		|     140ns	|
| 2		| 190ns		|     320ns	|
| 3		| 210ns		|    1µs	|
| 4		| 230ns		|    6µs	|
| 5		| 250ns		|  130µs	|
| 6		| 270ns		| 8ms		|

LDPC is significantly slower than SECDED when dealing with multiple errors.
If you don't mind getting the wrong results, SECDED will give you the wrong
result much faster.

For a hardware implementation, I would expect a hybrid approach.  Repairing 1
and maybe 2 errors in hardware is easy.  Implementing backtracking in hardware
takes more effort, so it makes sense to fall back on microcode or even an
interrupt to handle it in the BIOS or OS.

Calculating the syndrome takes similar effort for both LDPC and SECDED.  With
LDPC we need 7 XOR per data bit, 448 in total.  With SECDED the XOR per data bit
vary between 3 and 7.  By my count we need 276 in total.  Both numbers seem
insignificant compared to other parts of a CPU.


Conclusion
----------

I think LDPC makes for a much better error correction code and should replace
SECDED in memory controllers.  Any hardware designers are hereby invited to
implement my code or some derivative of it.  If I am able to buy a CPU with
better error correction in about a decade, that would be a good outcome.

My [code](ldpc.c) is available.
