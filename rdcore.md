# Measuring core cycles

Here is something that has annoyed me for years.  When doing
performance work, I often want to measure how many cycles a particular
function takes.

Amateurs measure speed, professionals measure time.  And they measure
time in cycles, not nanoseconds.  As programmers we have relatively
little control over CPU frequency, but we have a lot of control over
the number of cycles our code spends to do some work.  And when trying
to improve that measure, we want to carefully measure cycles.

On x86 the well-known default solution is `rdtsc`, often wrapped like
this:
```
static inline u64 rdtsc(void)
{
	unsigned int low, high;

	asm volatile ("rdtsc":"=a" (low), "=d"(high));
	return low | ((u64) high) << 32;
}
```

Afaics the instruction used to measure real cycles.  But with
multicore CPUs and in particular with dynamic frequency scaling that
became problematic.  By now it returns `nominal cycles`.  It reads a
cycle counter in the `uncore` that isn't affected by each core's
frequency scaling.

Which means that `rdtsc` measurements need a conversion factor to get
translated into core cycles.  In the past it was relatively easy to
control CPU frequency from userspace, but I haven't had that ability
for quite a while now.  Instead we have to measure CPU frequency.


# Measuring frequency

One option is to read `/proc/cpuinfo` and parse the output.  A
slightly more convenient option is to read
`/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq`.  But opening
files and parsing base10 integers from C code is still rather
annoying.  So yet another option is to look at the kernel code that
makes the measurement.

And the result is... quite underwhelming.  We run a delay loop and
measure how long that delay loop took.  Add a bit of math and you get
the core frequency.  Some of the time, at least.  In some cases the
kernel appears to not even measure and just claim what it thinks the
frequency ought to be.

While not exactly inspirational, at least we can replicate things in
userspace.  And I finally did.

```
static inline u64 loop16(void)
{
	u64 t = rdtsc();
	u64 rcx = 1ull<<16;
	asm volatile ("1: sub $1, %%rcx; jg 1b" : "+c" (rcx));
	t = rdtsc() - t;
	return t;
}

static u64 rdcore(u64 start_tsc)
{
	static u64 last;
	static u64 div;
	u64 now = rdtsc();
	if (now-last > 1<<26) {
		div = loop16();
		last = now;
	}
	u128 c = now - start_tsc;
	c <<= 16;
	return c/div;
}
```

`loop16` is my delay loop that should run for 65536 cycles, plus a
little overhead.  `rdcore` uses the delay loop to turn nominal cycles
into core cycles.  Usage looks like this:

```
	u64 t = rdtsc();
	f(); /* the function you want to measure */
	t = rdcore(t);
	printf("f took %8lld cycles\n", t);
```

# caveats

As always when benchmarking, you ought to be suspicious and mistrust
results.  If the core frequency changes, `rdcore` will only take a new
measurement after 64M nominal cycles.  Measurements are expensive, so
we don't want to do it for every conversion.  Which means that
sometimes we use a stale measurement and report the wrong result.

So far I have only used this in single-threaded measurements.  In
principle you can make the two static variable per-thread and
everythign should be fine.  Maybe it even is.
```
	static __thread u64 last;
	static __thread u64 div;
```

Otherwise, this is finally a solution to an old problem that has
constantly annoyed me but was never urgent enough to deal with.
