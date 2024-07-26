# ssh remote root exploit

Openssh has an impressive track record.  It is the obvious attack vector for
everyone.  On many systems it is the open service reachable from the internet.
And yet, we very rarely hear of any issues.  Openssh is near-flawless.

A rare exception happened this year with
["leap of faith"](https://www.qualys.com/2024/07/01/cve-2024-6387/regresshion.txt).
If you want to blame the Openssh developers, they had a signal handler using
functions that were not signal-safe.  Which functions are those?  The answer is
malloc() and free().  Technically it was some other function, so the dependency
on malloc was indirect.  And maybe malloc was not the only problem with
signal-safety.  But it certainly is one of the dominating problems, as so many
other functions use malloc internally.

Which reminded me of a
[discussion](https://marc.info/?l=glibc-alpha&m=145383008619140) I had with
glibc folks eight years ago.  Glibc folks (specifically Szabolcs Nagy) were
arguing that having a signal-safe implementation of malloc would be a bad thing.
I would argue that not having a signal-safe implementation of malloc is directly
responsible for the ssh remote root exploit.

As is often the case, two mistakes were required here.  If Openssh developers
hadn't used a signal-unsafe function, the exploit would have been prevented.  Or
if the function in question had been signal-safe, the exploit would also have
been prevented.  My general rule of thumb in such cases is to fix both problems
and not make any excuses trying to shift responsibility on the other party.


# malloc signal-safety status

It has been a few years, but I think I found glibc malloc, jemalloc and tcmalloc
all failed my signal testcase, the only known signal-safe malloc back then was
mine.  I just retested and tcmalloc passed the test.  Well, mostly.  Running the
test in a loop, tcmalloc hit a deadlock on the 12th iteration.  Better than
most, not quite good enough.

Glibc malloc hit a segfault, jemalloc hit a deadlock in the first test.  The
test is creating 16 threads and we hit the deadlock after creating just one
thread, so almost immediately.

If you are working on a malloc implementation, I would implore you to add a test
for signal safety.  And then fix the code until you consistently pass that test.
If you know someone that is working on a malloc implementation, please convince
them that a signal-safe implementation is both possible and worth having.


# making malloc signal-safe

There is not a lot of rocket science required.  I assume your malloc is already
thread-safe and has locks on some data structure (slabs, heaps, whatever).  You
are already mostly done.  There are typically two additional problems.

Thread caches are used by most implementations for performance.  You need a lock
on those.  However, you don't need the lock to be atomic.  You only have to
protect against the same thread recursing and racing with itself via signal
handler.  On x86, atomic generally cost 20+ cycles, while conditional branches
are often free in practice.  My locking primitive for the thread cache is this:

```
/* Use only for thread cache - doesn't protect against other CPUs/threads */
static inline int trylock_unsafe(struct lock *l)
{
	int ret = l->lock;
	if (!l->lock) {
		l->lock = 1;
		__atomic_signal_fence(__ATOMIC_ACQUIRE);
	}
	return ret;
}
```

I deliberately named the function "unsafe" to discourage use.  It is almost
always the wrong thing to do, with one known exception.  For the thread cache it
is the right thing to do.

Second problem is a little more subtle and may not apply to your implementation.
Ptmalloc and derivatives (including glibc malloc) need an arena to allocate
memory.  If all arenas are currently locked, a new arena can be created.  But
the code creating a new arena is protected by a lock, the `list_lock`.  If you
are holding the `list_lock` when the signal happens, your signal handler cannot
create new arenas.  Waiting for the `list_lock` would be a deadlock.

You can have a similar problem for any other lock in your implementation.  So
you have to be careful and either have multiple locks.  Each arena/heap
typically has a lock and you can avoid locked arenas/heaps by using a different
one.  Or you need a lock-free fallback mechanism.  Freeing memory often involves
taking a specific lock, so you need some atomic fallback that allows you to take
the lock at a later point.


# the glibc malloc problem

And finally, it is hard to avoid mentioning glibc malloc in particular.  I don't
know a polite way of describing the trainwreck that it is.  It is the default
malloc for everything and should receive all imaginable improvements.  But in
practice it is not just the slowest malloc in any benchmark, you often have to
adjust the scale to make it fit.

I see two fundamental problems.  One is that glibc is a GNU project and GNU acts
more like a political organization than a technical one.  If political concerns
trump technical concerns, it is simply unrealistic to expect technical
excellence.  The pool of capable developers willing to work within those rules
is too small.

The second problem is emacs.  I cannot claim to fully understand the situation,
but emacs appears to be playing games with core dumps.  Apparently the emacs
binary you are running was created by starting a rather different emacs binary
and creating a coredump after all initialization was done.  Your emacs is simply
loading the coredump back into memory.  Details are almost certainly wrong, but
the consequences are the same either way.

If you are loading new shared libraries to then operate on existing data
structures from the core dump, the new libraries cannot have incompatible data
structures.  In particular, malloc cannot change its data structures and hope to
run emacs.  My malloc started out as a fork of glibc malloc, but I quickly
decided I had to change data structures.  Guess what happened when collegues
tried to use it with emacs.

So whatever it is that emacs is doing, I consider it a problem with emacs.  If
the choice is between breaking emacs or holding back malloc and all other
programs using malloc, I would definitely choose to break emacs.  In fact I did.
Glibc folks apparently decided otherwise and it shows.

There are more problems with glibc malloc, but addressing any of those is
hopeless until technical merit trumps political merit and breaking emacs is no
longer seen as a problem.


# Conclusion

Please make your malloc signal-safe!
