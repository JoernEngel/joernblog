ufence - a userspace clone of kfence
------------------------------------

Some people know about [efence](https://en.wikipedia.org/wiki/Electric_Fence),
far fewer know about [kfence](https://docs.kernel.org/dev-tools/kfence.html).


kfence
------

Kfence can be viewed as a kernel port of efence, but that would be
missing the most important aspects of it.  Efence is almost unusable
in practice, it has a dramatic impact on both runtime and memory
footprint.  Kfence, apart from being a kernel port with the typical
k-prefix to the name, only does sampling.

One allocation in a million or even billion goes to kfence and is
subjected to expensive instrumentation.  Most get serviced by the
regular allocator and remain cheap and fast.  If you are careful, you
can tune kfence to be as aggressive as possible while keeping overhead
below some threshold like 0.1%.

Also importantly, kfence does not crash the kernel.  If a memory
corruption is detected, debug information is printed, but the system
continues to run as if nothing happened.  Some bugs consistently write
beyond the allocated buffer.  But with allocator padding, the
overflows are harmless up to a point.  They are definitely bugs, but
would never cause any harm with the existing allocator padding.  It is
an important design choice for kfence not to introduce new crashes
when the system would have continued running without kfence.

In my experience, kfence can find all kernel memory corruption bugs.
It makes a huge difference.


ufence
------

Having experienced some userspace memory corruptions after fixing all
kernel memory corruptions, I wanted to get the same functionality in
userspace.  The basic idea isn't hard, you ensure all allocations are
surrounded by guard pages to catch overflows/underruns and use
mprotect() when the object is freed to catch use-after-free.

[Here](ufence.c) is my version.  It has all the functionality I want
from a ufence library.  What it doesn't have is integration into
existing memory allocators.  That is left as an exercise to the
reader.

Integrating ufence into standard allocators is problematic anyway.
Ufence depends on catching segmentation faults and signal handlers are
typically the domain of the application, not the memory allocator.  We
also currently have half a dozen standard allocators to choose from
and I don't want to touch them all.  So consider this a flatpack
debugger.  Hexkey not included.
