# inverse multipliers part 1: inversing multiplication

Inverse multipliers are a well-known trick in computing.  There are
two different kinds that are related and confusingly similar, but have
different purposes.  Here I want to discuss a way to invert
multiplications.

Multiplying any number with an odd multiplier can be reversed, at
least when operating modulo 2^n, as computers do.  For example, when
using 32bit integers, we get:

```
12345678 * 3 = 37037034
37037034 * 2863311531 = 12345678
```

You can replace 12345678 with other numbers and the pattern remains.
That means 2863311531 is an inverse multiplier for 3.  So, how does
this magic work?  Can we find a proof?  And can we find a convenient
way to generate inverse multipliers?


# proof by induction

For 1bit integers the proof is easy.  There is only one odd
multiplier, 1.  The inverse multiplier is also 1.  And since there are
only two possible numbers, we can try both of them:

```
0 * 1 = 0
1 * 1 = 1
```

We could also try all 2bit numbers and find that the inverse
multiplier for 1 is 1 and the inverse multiplier for 3 is 3.  The
important attribute is the product of multiplier and inverse
multiplier:

```
1 * 1 = 1
3 * 3 = 9
3 * 3 = 1 (mod 4)
```

When limiting the result to 2bit numbers, the product of multiplier
and inverse multiplier has to be 1.  Some higher bits outside our
range might be set as well, but the truncated 2bit representation of 9
is 1.  That is the key to finding inverse multipliers.


# induction step

Assuming we already have a known n bit multiplier `m` and inverse
multiplier `im`, we can create two new pairs of n+1 numbers `m0, im0`
and `m1, im1`.  `m0` is identical to `m`, with the new top bit set to
`0`.  `m1` is almost identical, the new top bit is set to `1`.

For example:
```
m = 3 = 11b
m0 = 3 = 011b
m1 = 7 = 111b
```

We now need to find the matching inverse multipliers for `m0` and
`m1`.  A reasonable guess for both is our old `im`.

```
m = 3 = 11b
im = 3 = 11b
m0 = 3 = 011b
im0 = 3 = 011b (guess)
m0 * im0 = 011b * 011b = 1001b = 001b (mod 8)
```

We found that our guess was correct, 011b is the inverse multiplier
for 011b.  How about `m1`?

```
m = 3 = 11b
im = 3 = 11b
m1 = 7 = 111b
im1 = 3 = 011b (guess)
m1 * im1 = 111b * 011b = 10101b = 101b (mod 8)
```

In this case our guess was incorrect.  The product is not 1.  However,
note that it is almost correct, only the top bit is wrong.  And we can
fix the top bit of the product by flipping the top bit of our guess
for `im1`.

```
m = 3 = 11b
im = 3 = 11b
m1 = 7 = 111b
im1 = 3 = 111b (corrected)
m1 * im1 = 111b * 111b = 110001b = 001b (mod 8)
```

With that we have a full proof.  We know that every odd 1bit number
has an inverse multiplier.  And we know that given an odd n bit number
and matching inverse multiplier, we can find both pairs of n+1 bit
number and inverse multiplier.

We can also write a trivial program to construct inverse multipliers
by starting with any number as a guess and incrementally flipping the
lowest bit positions where the product of `m*im` is different from 1.


# relevance

The main use I have for this is in hash functions.  Hash functions
should get constructed from invertible components.  We also want
components that stir the pot a lot.  Multiplication do a lot of
stirring and would be a useful component - as long as they are also
invertible.  We now have proof that they are.
