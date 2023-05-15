CRC musings
-----------

I happened to need a checksum with the following attributes:
1. Fast/cheap in hardware,
2. Fast/cheap in software,
3. Good quality.

The hardware requirement directly translates to "use a CRC".
Implementing a fast CRC in software is a bit tricky, but can be done
as well.  My [current implementation](crc64.c) reaches 30B/c (bytes
per cycle) on Sapphire Rapids and 8B/c on 10-year-old hardware.  Good
enough.

Quality of CRC is where we run into black magic.  Most people simply
copy an existing implementation.  And those existing implementations
often don't explain their design decision.  So this is an attempt to
lift the veil a bit.

CRC math
--------

A CRC is the remainder of a carry-less division.  If you are new to
this, let's start with carry-less additions.  In regular math, we know
that 6+7=13 and 16+17=33.  In both cases we add 6+7 and the result is
3, plus a carry of 1.  Carry-less additions simply ignore the carry.
So in carry-less math, the equivalent equations are 6+7=3 and
16+17=23.

Switching from decimal to binary numbers, we get 10000+10001=00001.
Again, the carry is dropped.  That means a carry-less addition is an
XOR.  A carry-less subtraction is also an XOR, so in binary carry-less
math, addition and subtraction are the same thing.

Multiplication works similar to regular math.
```
	  11011⋅101
	  11011
	(11011)
	11011
	-------
	1110111
```
We shift one argument left a number of times.  The bits in the second
argument decide which shifted version to use or to ignore.  Then we
add all the shifted versions we use.  Regular additions give regular
multiplication, carry-less additions give carry-less multiplication.

Division is the inverse of multiplication.  Conceptually we shift the
divisor left until the left-most bit of the dividend lines up with the
left-most bit of the divider, then subtract the shifted divider.
```
	 1110111/101
	-101
	 0100111
	- 101
	  001111
	-   101
	    0101
	-    101
	     000
```
Result is all the bit-positions where did subtractions.

One of the weirdest attributes of carry-less math is the concept of
smaller/greater.  Since subtraction and addition are the same
operation, there is no clear ordering and 111 can be both larger and
smaller than 100.  Not particularly useful.  But we can re-define
smaller/greater to compare the highest bit set.  Basically, any number
with N+1 bits is greater than any number with only N bits.

Using this definition, we see that the remainder of carry-less
division is always smaller than the divider.

```
	 111/101
	-101
	 010
```
If the remainder had the same number of bits as the divider, we would
do one more subtraction.  Once the remainder is smaller, we have to
stop.  That remainder is our CRC.  Which explains why CRC32 uses a
33-bit divider, CRC64 uses a 65-bit divider, etc.

The dividers are commonly called polynomial.  You can use polynomial
math to come to the exact same result as we did here, which imo is an
example of unnecessary complexity.  If your brain works like mine,
just substitute polynomial with divider whenever you read it.

More CRC math
-------------

Now that we understand the basic math, the question is which dividers
to choose.  Circling back to the goal of "good quality", we want our
CRC to catch data corruptions with high probability.  We would also
like to catch all common data corruptions with 100% probability.

Single-bit errors are simple.  Any polynomial with at least two bits
set will catch every single-bit error.  In classical tradition, I will
leave the proof as an exercise to the reader.  It isn't too hard to
work out.

Completely random data is also simple.  If you want to improve your
odds of catching random corruptions, use a larger polynomial.  CRC4
will miss one corruption every 16 messages on average.  CRC32 will
miss one every 2^32 messages.  CRC64 will miss one every 2^64
messages.  I would suggest you choose CRC64 for this reason.

N-bit errors are somewhat trickier, and we need some additional
math.

```
CRC(a^b) = CRC(a)^CRC(b)
CRC(message^error) = CRC(message)^CRC(error)
CRC(message^error) = CRC(error)
```
With CRC, the message itself doesn't matter.  We always deal with
messages that have a remainder of zero.  So for any message, we will
only miss corruptions if the bits flipped in the message evenly divide
by the polynomial.  Put differently, the error is some multiple of the
polynomial.

That leads to a very simple observation.  You will miss every error
that is the polynomial or a shifted version of the polynomial.  So if
your polynomial only as 5 bits, you will miss a lot of 5-bit errors.
Which immediately explains [why David T. Jones found problems with
CRC-64-ISO](http://www0.cs.ucl.ac.uk/staff/d.jones/crcnote.pdf).

The ISO-polynomial is 0x1B plus the implicit bit 64.  It is actually
worse than most 5-bit polynomials, since you only need to corrupt two
bytes.  Completely corrupting single bytes is more common in practice
than would be expected if all bit-corruptions were independent events.

We learn that we want a polynomial with a lot of bits set.  So what
would happen if we went all-in and set every single bit?
```
	 111111111⋅11
	 111111111
	111111111
	----------
	1000000001

```
Oops!  Such a polynomial would miss all 2-bit errors N bits apart,
with N being the size of our polynomial.  It would also miss all 2-bit
errors 2N, 3N, 4N, etc bits apart.  Which means that your chance of
missing random 2-bit errors is 1/N.  Not a desirable attribute.

Instead of setting as many bits as possible, we should probably set
only about half the bits in a random pattern.

Testing all possible 2-bit errors
---------------------------------

Finally it's time for some brute force.  If we know that all messages
will be shorter than 1518 bytes or 1MB or any other limit, we can try
every possible 2-bit corruption within that limit.  We can simplify
that process by requiring that the first message bit be corrupted.  If
we find a missed 2-bit error this way, we know that the same 2-bit
error shifted to any other position will also be missed.

One way is to calculate the CRC or every possible 1-bit error and
comparing it to the CRC of the first bit being corrupted.  Or we can
corrupt the first bit, then iteratively calculate the CRC of that
message shifted to any possible position.  If at any iteration the CRC
has a single bit set, we have found a missed 2-bit error.

Using that approach I have found a missed 2-bit error for the ECMA
polynomial (0x42f0e1eba9ea3693).  The two corrupted bits have a
distance of 8589606850 bits or 1023.96 megabytes.  If you want to
protect gigabyte-sized messages with a single CRC, this might be a
problem to be concerned about.

Testing all possible N-bit errors
---------------------------------

Going beyond 2-bit errors is hard.  We can turn the search for 2-bit
errors into a linear search by hard-coding the position of one of
those errors.  3-bit errors and beyond are a multi-dimensional problem
and runtime quickly explodes.

The best approach I have found is to calculate every possible 1-bit,
2-bit, ..., N-bit error up to some bit position.  Once you have all
1-bit errors, you can calculate the 2-bit errors using our old friend
```
CRC(a^b) = CRC(a)^CRC(b)
```
Xor the 1-bit errors for both bit positions and you get the 2-bit
error.  That part is quick.  You then need to store all the calculated
errors.  I limited myself to 4GB of storage to keep things reasonable.

Next step is to sort all the calculated CRC-values.  If any two
neighboring CRCs are identical, that means you found an error that
would be missed.  Assuming the two CRCs correspond to an N-bit and
M-bit errors respectively, you now have an N+M-bit error that would
result in a CRC of zero and go unnoticed.

Similar to the search for 2-bit errors, you can require one of the
error bits to be at position 0.  That helps a little bit, but the
required effort still quickly explodes.  So effectively you either
have to limit the N-bit errors to small values of N or require the
error bits to be relatively close to each other for the search to be
practical.

Results
-------

Using this approach, I tested all the common CRC64-polynomials plus
two I randomly picked myself:
```
	0x42f0e1eba9ea3693ull, /* crc64-ecma */
	0xad93d23594c935a9ull, /* crc64-jones */
	0xad93d23594c93659ull, /* crc64-nvme */
	0x000000000000001bull, /* crc64-iso */
	0x1da177e4c3f41525ull, /* crc64-linus */
	0x73508687c215724full, /* crc64-random */
```
Once you test for 5-bit errors, crc64-iso falls apart.  Nobody should
be surprised by that.  And nobody should use crc64-iso for anything,
really.

Crc64-ecma missed a 2-bit error almost 1GB apart, as mentioned before.

For N-bit errors, all polynomials other than crc64-iso worked well.  I
couldn't find any N-bit errors within my memory constraints up to
N=12.  If you are using one of them, you made a good choice.

For those wondering about 0x1da177e4c3f41525, it is the hash of the
first git commit plus 1.  You want the lowest bit of your polynomial
to be set and 0x1da177e4c3f41524 didn't meet that requirement.
