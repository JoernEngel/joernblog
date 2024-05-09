# An atomic queue

For some reason I seem to find the need for an atomic queue every few months.
Often in code I only review and often it can get away with some sort of hack
that is good enough for this specific use-case.  But once you start to think
"we need a proper atomic queue", you see the need almost everywhere.

So I've written one.  Design goal was to encapsulate as much of the complexity
as possible:
- Multiple producers are fine.
- Multiple consumers are fine.
- If you get the size wrong, the queue will automatically grow.

Queue design is essentially a ring-buffer with head- and tail-counters.  Atomic
dequeue is simple enough, you read the tail-counter, read the corresponding
entry, then cmpxchg the tail-counter.  If it has changed, you try again.

Enqueue requires writing both the entry and the head-counter.  Making those two
operations atomic isn't possible, unless you cheat a bit.  My cheat is that the
head-counter in the header is just a hint.  Queue entries are count/value pairs
and the true head is the entry with the highest count.  So you scan the array to
find the true head, then use cmpxchg16b to atomically update both the count and
the value of the new entry.


# Interface

There are currently only three functions, enqueue, dequeue and a constructor.
A destructor would make sense, I just haven't written it yet.

```
struct atomic_queue *alloc_queue(u64 initial_size, u64 max_size);
/* returns 0 on empty queue, 1 on dequeue */
int dequeue(struct atomic_queue *q, u64 *retval);
void enqueue(struct atomic_queue *q, u64 val);
```

Enqueue never returns an error.  If the queue is full, it will automatically
grow.  Growing a queue isn't actually atomic anymore, it takes a lock.  That
should be a rare operation and the lock does priority inheritance, so I don't
feel too bad about it.  You can completely prevent growing the queue by
specifing the right initial size.  But if you don't know the right initial size
or the choice made years ago no longer reflects current reality, growing the
queue imo is the right answer.

To catch blatant bugs you can also specify a maximum size.  The queue will not
grow larger than the maximum size.  Instead it will crash the process.  So only
specify a limit to catch bugs, not to handle normal conditions.

Also note that the queue may need to be a bit larger than you think.  You cannot
atomically read both the head-counter and the tail-counter.  So with concurrent
operations from many threads, one of the two will often be somewhat stale and
the queue will appear full when it actually isn't.  That's the cost of
concurrency.


# RCU

After growing the queue we have two ringbuffers.  Producers should use the new
ringbuffer, called subqueue.  But some producers can still use the old subqueue
for a while, so consumers have to check the old queue first.  To eventually stop
checking the old subqueue we need a guarantee that all producers have switched
over.  That's an RCU problem.

Similarly we cannot free the old subqueue until we can guarantee that all
producers and consumers have stopped looking at it.  Yet another RCU problem.
I couldn't find a small userspace implementation of RCU, so I wrote my own.  It
is a bit non-standard, but should work for my purposes.


# Portability and bugs

I mostly care about Linux on x86.  If you need a queue on a different platform,
you either have to port it yourself or use something different.  Any 64bit
machine with a 16B cmpxchg should work.

Similarly, there may still be bugs.  Lock-free code is notoriously tricky to get
right.  I have tested my queue and tried to find/fix all bugs.  But I cannot
give you any guarantees.

If you are ok with those caveats, feel free to use [my code](atomic_queue.c).
Or read it for inspiration before writing your own.
