# MLSV notation

Here is a new model for thinking about code I recently developed.
Several people I mentioned this to liked it and said I should publish
it.  Here ya go!

Motivation is that the big-O notation is mostly useless for my work.
Pretty much everything I spend time on is O(n).  And yet I spend a lot
of time to get another 2x or sometimes 10x improvement that big-O
notation casually ignores.  MLSV is designed to give better contrast
between different flavors of O(n) algorithms.


## 1S

S is for *s*erial *s*weep or *s*calar, whichever you prefer.  If you have a
simple loop that touches every element of input data once, your effort
is `1S`.  If you need a second sweep, it is 2S.  `3S` for three sweeps,
etc.

I still ignore constant factors, which makes the distinction between
`1S` and `3S` slightly silly.  But I like the implicit documentation
for the number of sweeps and describing something as `1S` instead of
`S` has a slightly better ring to it - according to my taste.


## 1M

M is for branch *m*isprediction or cache *m*iss.  If you are likely to
hit those for every input element, your effort is `1M` instead of
`1S`.  `1M` is very roughly 10x more expensive than `1S`.  Precise
cost depends on the hardware and algorithm details, but a rough 10x
cost increase should be enough to catch your attention.


## 1L

L is for *l*inear dependency.  Modern CPUs can do a lot of work in
parallel.  They are capable of starting with loop iteration 10 long
before finishing iteration 1 - as long as there are no dependencies.
If there are, performance that otherwise could be throughput limited
becomes latency-limited.

Cost increase for L is typically a bit lower than for M, closer to 5x
instead of 10x.


## 1V

V is for *v*ector instructions.  If you can vectorize some loop, you
get a decent performance improvement, somewhere around 10x again.


## Putting it together

When dealing with a composite algorithm with multiple components, you
can annotate each component using this notation.  The point is that
you will immediately see where you should spent your attention.  The
`1V` components typically don't matter.  Maybe you can improve them
and double their speed, but they are already too fast to matter.

Any remaining `1M` or `1L` is what you should focus on.

A real-world example where this model would have helped me was an LZ
matchfinder I wrote.  A colleague (hello Curtis!) pointed out the
function doing hash table lookups being a hotspot in benchmarks.  The
hashtable was too large for L1 cache, so most lookups hit L2, with
higher latency and frequently enough to hit L2 bandwidth limit.

Had I used MLVS notation, this function would have been `1M` instead
of `1S` and the problem should have been obvious to me.  But the
notation is something I came up with in 2026, so instead I shipped a
`1M` component into production until somebody else noticed.
