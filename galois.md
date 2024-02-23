A hacker's guide to Galois Field arithmetic
-------------------------------------------

It took me years to understand how [Galois Field
arithmetic](https://en.wikipedia.org/wiki/Finite_field_arithmetic) works.  They
are actually quite simple, most introductions are just unnecessarily
complicated, at least for a mind like mine.  So here is an attempt to explain
them to people with a programming background.

Galois Fields can be based on any prime number or power of a prime number.  We
will ignore most of them and only consider powers of 2, an obvious choice for
programmers.  But first we have to introduce carryless arithmetic as an
intermediate step.  Here we could also use many bases, but will limit us to
base2 arithmetic.


Carryless addition and substraction
-----------------------------------

Carryless addition works like regular addition with the carry bits ignored.
```
  0110
+ 1100
  1    carry
------
 10010
```
becomes
```
  0110
+ 1100
------
  1010
```

That means carryless addition is the same as XOR.  Carryless subtraction
similarly becomes XOR.  With interesting consequences.

```
4+1 = 5
4-1 = 5
```

Is 5 larger or smaller than 4?  Both?  Neither?  We will get back to this.


Carryless multiplication
------------------------

Carryless multiplication works like regular multiplication with the carry bits
ignored.

```
1101 * 1011

 1101
+ 0000
+  1101
+   1101
--------
 1111111
```

Carryless multiplication of two 4bit numbers has a 7bit result.  Regular
multiplication could have an 8bit result, but the 8th bit will only be set via
carry bits.  Carryless multiplication results are always one bit shorter.  X86
ow has a clmul instruction that multiplies two 64bit numbers to generate a
127bit result.


Carryless division and remainder
--------------------------------

For carryless division we need a working definition of larger/smaller/equal
numbers.  We declare that a 7bit number is greater than 6bit number, but all
6bit numbers are of equal size.  With that we can implement long division.

```
 1111111 / 1011
-1011    1
 -------
  100111
 -1011   1
  ------
    1011
  -0000  0
   -----
    1011
   -1011 1
    ----
       0
```
Otherwise this is the same as long division.  It takes a moment to get used to,
but isn't particularly hard.  The remainder of a carryless division is also
known as [CRC](https://en.wikipedia.org/wiki/Cyclic_redundancy_check).


Fast carryless division and remainder
-------------------------------------

Division is generally slower than multiplication.  Fast division is generally
implemented by finding an inverse of the divider and multiplying with the
inverse.

There is no inverse for integer numbers.  1/7 is a rational number.  But we can
usually cheat by multiplying everything with 2^64.  Dividing by 7 is the same as
multiplying with 2^64/7 and dividing the result by 2^64, aka using a shift.  We
introduce a rounding error here.  But for small numbers the result is precise.

The [Barret reduction](crc64.c#L157) in my crc64 implementation does division by
multiplying with the inverse.  It then calculates the remainder by subtracting
the clean multiple from the original number.  Sounds clunky, I think it makes
more sense in pseudocode.
```
b = clmul(a, inverse(divider)) >> 64;
c = clmul(b, divider);
a -= c;
```


Carryless primes
----------------

Carryless primes are similar to regular primes.  If a number is only divisible
(without remainder) by 1 or itself, it is a prime number.  Once you have
implemented carryless division, you can easily calculate them yourself.  Here
are the first ten in decimal and binary notation.
```
 2               10
 3               11
 7              111
11             1011
13             1101
19            10011
25            11001
31            11111
37           100101
41           101001
```

It is notable that we have fewer carryless prime numbers than regular prime
numbers, by roughly 3x.  For example `3*3=5`, so 5 is not a prime number in
carryless arithmetic.  Carryless primes are the last ingredient we need to work
with Galois Fields.


Galois sudoku
-------------

To get an overview, here is the multiplication table for GF(16).  The first two
columns and rows look familiar.  `0*x=0` and `1*x=x` hold true in Galois Field
arithmetic as well as regular arithmetic.  Beyond that things get a bit weird.
Sudoku players might notice that every row and column has every number appear
exactly once, ignoring column/row 0.  That is a useful attribute to have.
```
 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
 0 1 2 3 4 5 6 7 8 9 a b c d e f
 0 2 4 6 8 a c e 3 1 7 5 b 9 f d
 0 3 6 5 c f a 9 b 8 d e 7 4 1 2
 0 4 8 c 3 7 b f 6 2 e a 5 1 d 9
 0 5 a f 7 2 d 8 e b 4 1 9 c 3 6
 0 6 c a b d 7 1 5 3 9 f e 8 2 4
 0 7 e 9 f 8 1 6 d a 3 4 2 5 c b
 0 8 3 b 6 e 5 d c 4 f 7 a 2 9 1
 0 9 1 8 2 b 3 a 4 d 5 c 6 f 7 e
 0 a 7 d e 4 9 3 f 5 8 2 1 b 6 c
 0 b 5 e a 1 f 4 7 c 2 9 d 6 8 3
 0 c b 7 5 9 e 2 a 6 1 d f 3 4 8
 0 d 9 4 1 c 8 5 2 f b 6 3 e a 7
 0 e f 1 d 3 2 c 9 7 6 8 4 a b 5
 0 f d 2 9 6 4 b 1 e c 3 8 7 5 a
```

Galois addition and subtraction
-------------------------------

Galois addition is the same as carryless addition.  Moving on.


Galois multiplication
---------------------

Galois multiplication is almost the same as carryless multiplication.  In many
cases it is exactly the same.  But if the carryless multiplication result
exceeds the range of our field (larger than 15 for GF(16)), we calculate the
remainder of a carryless division.

Remainder of which division?  Pick any carryless prime with 5 digits for GF(16).
Or with `N+1` digits for GF(2^N).  For GF(16) we have three possible choices,
`19`, `25` or `31`.  Most people pick `19` and I agree that it is the best
choice.  But the other two primes work just as well.  So GF(16) is
underspecified.  GF(16,19) would be a better notation, but I have never seen it
anywhere.

Most people use the term [irreducible
polynomial](https://en.wikipedia.org/wiki/Irreducible_polynomial), instead of
carryless prime.  The two are the exact same thing.  And my brain always wants
to change browser tabs when I see carryless arithmetic done in terms of
polynomials.  So let's not use polynomials again.


Fast Galois multiplication
--------------------------

The best answer is to use the gfmul instruction.  Intel has added vgf2p8mulb
when introducing AVX512.  But that instruction only supports the Galois field
used for AES, GF(256,283).

Second best answer is to do carryless multiplication followed by one or more
additional multiplications.

```
static u8 gf16mul(u8 a, u8 b)
{
	u8 p = clmul(a, b);
	p ^= clmul(p>>4, 19);
	return p;
}
```
This is where the choice of prime number matters.  `19` or `10011` is an
excellent choice because we are done after two multiplications.  Any high bits
generated by the first clmul are cancelled out by the second clmul.  If we used
`31` or `11111`, we would need additional multiplications to guarantee a result
in the 0..15 range.

For GF(256) there is no good prime number.  Our best choices are `283` or `285`.
In both cases we need three clmul in total.  With `283` we might have hardware
support because that is the divider chosen by AES.


Galois division
---------------

There is no equivalent of long division.  We have three options.  We can
generate a large lookup table.  But that quickly gets out of hand.  Or we can
multiply with the inverse.

Inverses are the first example of why Galois arithmetic can be useful.  Every
multiplier has an inverse that is still part of our field.  If you look at the
multiplication table, you see that `2*9=1`.  That means `9` is the inverse of
`2` and vice versa.  So second option is to generate a table of inverses and
multiply by the inverse.  For the third option, we have to look at exponentials
first.


Galois exponentials
-------------------
If we calculate `2^0`, `2^1`, etc, we can generate an exponential table like
this:
```
1: 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1
2: 1 2 4 8 3 6 c b 5 a 7 e f d 9 1
3: 1 3 5 f 2 6 a d 4 c 7 9 8 b e 1
4: 1 4 3 c 5 7 f 9 2 8 6 b a e d 1
5: 1 5 2 a 4 7 8 e 3 f 6 d c 9 b 1
6: 1 6 7 1 6 7 1 6 7 1 6 7 1 6 7 1
7: 1 7 6 1 7 6 1 7 6 1 7 6 1 7 6 1
8: 1 8 c a f 1 8 c a f 1 8 c a f 1
9: 1 9 d f e 7 a 5 b c 6 3 8 4 2 1
a: 1 a 8 f c 1 a 8 f c 1 a 8 f c 1
b: 1 b 9 c d 6 f 3 e 8 7 4 a 2 5 1
c: 1 c f 8 a 1 c f 8 a 1 c f 8 a 1
d: 1 d e a b 6 8 2 9 f 7 5 c 3 4 1
e: 1 e b 8 9 7 c 4 d a 6 2 f 5 3 1
f: 1 f a c 8 1 f a c 8 1 f a c 8 1
```
We see familiar patterns.  `1^n=1` and `n^0=1` are true for any n except `0`.
But we also see unfamiliar behavior.  The most obvious is that `n^15=1`.  After
that the pattern repeats, `n^15+k = n^k`.  In a few rows we get repetition much
sooner, with a period of 3 or 5.  It is no accident that 3 and 5 are the prime
components of 15, but we don't have to dwell on that.

We can find our inverses for any `n` by calculating `n^14`.  Calculating
exponentials isn't particularly fast, but we can do it in 4 multiplications for
GF(16).  More generally we can do it in n multiplications for GF(2^n).

Which means we can so division without lookup tables.  Tables are still a better
choice if you can spare the storage and care about performance.  But you have a
reasonable fallback.


Galois field generators
-----------------------

As we saw above, all exponentials have a repeating sequence pattern.  Some of
those sequences cover all numbers except zero.  We call the base of those
sequences generators.  GF(16,19) above has the generators 2, 3, 4, 5, 9, b, d
and e.


Delayed modulus
---------------

Many applications of Galois fields calculate a large dot-product.  For RAID6,
one of the parity drives gets calculated like this:

```
Q = g0 · D0 + g1 · D1 + g2 · D2 + ... + gn−1 · Dn−1
```

If we do each multiplication in a Galois field, that requires several clmul.
All but one are just used to calculate the modulus.  Here is our gf16mul again:
```
static u8 gf16mul(u8 a, u8 b)
{
	u8 p = clmul(a, b);
	p ^= clmul(p>>4, 19); // p = p%19
	return p;
}
```

We can skip the modulus calculate for all intermediate values and instead do it
once at the end.  Result will be the same, for roughly half the compute cost.
Or a third in case of GF(256).

Since we have now moved the bulk of the work into carryless arithmetic and only
take the remainder in the end, you can view this as the CRC of a carryless dot
product.  After all, a CRC is the remainder of a carryless division.
