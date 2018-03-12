Fast filtering of sensor noise
------------------------------

Around 2000 I had to deal with noisy hardware sensors.  The
conflicting goals were to remove the noise as much as possible and be
as responsive as possible.  A similar problem would be [Keyboard
debouncing](https://en.wikipedia.org/wiki/Keyboard_technology#Debouncing),
where too much filtering results in an unresponsive keyboard.

My solution back then was ad-hoc, but seemed to work well.  Basically
you take three samples, discard the outlier and average the remaining
two.

Examples:
| Raw			| Filtered		|
| 0, 0, 9, 0, 0, 0	| 0, 0, 0, 0, 0, 0	|
| 0, 0, 1, 1, 1, 1	| 0, 0, 0, 1, 1, 1	|
| 0, 0, 2, 4, 4, 4	| 0, 0, 1, 3, 4, 4	|
| 0, 0, 9, 0, 0, 7	| 0, 0, 0, 0, 0, 0	|
| 0, 0, 9, 0, 7, 0	| 0, 0, 0, 8, 0, 0	|

If you have a single sample that is wildly off, it will be the outlier
and get discarded.  A step function will be delayed by one sample
interval.  That is the latency cost we pay for the filter.  Ramps get
smeared a bit, but essentially are also delayed by one sample
interval.  And given two noisy inputs within the same 3-sample window,
we finally see the filter break down.

Nearly two decades later I still haven't come across some paper
describing my idea.  So in case it is novel, here you go.

[Median filters](https://en.wikipedia.org/wiki/Median_filter) are
pretty similar.  Taking the median is the equivalent of discarding two
outliers.  Given the examples above a median filter appears to work
better than my ad-hoc solution.  It responds quicker to a ramp and
remains closer to the signal with two noise samples inside the window.
Median filter should also consume less CPU, in case it makes a
difference.

As a side note, it is generally better to have a high sample rate and
aggressive filter than a low sample rate.  Sampling at 1kHz with
median-of-5 gives you an average latency of 2.5ms.  Sampling at 100Hz
with no filter gives you an average latency of 5ms and likely a lot
more noise.

As a second side note, it is probably a good idea to have a dedicated
8-bit CPU or something like that to do the filtering.  A tiny chip
running at 1MHz is more than enough in most cases and can dramatically
reduce the interrupt rate to your main CPU.  There is a reason
keyboard have a controller.
