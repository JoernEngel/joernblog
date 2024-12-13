collision-free hashtables
-------------------------

One detail hidden inside my [ufence](ufence.c) implementation is a
collision-free hashtable.  In theory, hashtables are awesome.  They
provide O(1) performance, unless you hit collisions.

Handing collisions introduces a lot of complexity and different
implementation choices.  Linked lists vs. open addressing?  Two
choices?  Cuckoo?  Robin Hood?  Vector instructions?  Whatever
solution you pick, doing a lookup in less than 10 cycles will rather
ambitious.

For a memory allocator like ufence, there is a cheat code.  You simply
don't use any keys that would result in collisions.  As of Linux 4.17
there is a new mmap flag:
[MAP\_FIXED\_NOREPLACE](https://www.man7.org/linux/man-pages/man2/mmap.2.html).

When allocating memory from the kernel, ufence picks a random address
and checks whether the corresponding slot in the hash table is
available.  If not, it retries with a different random address.  Then
it asks the kernel whether the address range in question is available,
using MAP\_FIXED\_NOREPLACE.  If not, it also retries.

The result is that lookup only has to check a single hashtable slot.
Either it finds the correct object or an empty slot.
