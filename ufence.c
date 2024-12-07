/*
 * ufence - userspace version of kfence allocator
 *
 * All allocations are backed by private pages with 2MiB of guard pages
 * around them.  Allocations are in the middle of a 16MiB-aligned region.
 *
 * |<------------------------- 16MiB region ------------------------->|
 * |<-  6MiB  ->|<- 2MiB  ->|<- 4kiB..6MiB ->|<- 2MiB  ->|<- ..6MiB ->|
 * |<- unused ->|<- guard ->|<- allocation ->|<- guard ->|<- unused ->|
 *
 * The entire allocation including guard pages has to fall within a single
 * 16MiB-aligned region to allow quick lookup for any associated address.
 * Large allocations up to 6MiB and aligned allocations up to 8MiB alignment
 * are supported, anything larger is not (without code changes).
 *
 * Allocations don't that fully occupy pages randomly get aligned to the
 * beginning or end of the allocation, with the obvious exception of memalign.
 *
 * Freed memory is kept it in quarantine for a minute.  After that it will
 * eventually get reused.
 *
 * Allocations have several backoff mechanism.  We randomly select a slot to
 * use.  If the slot is currently in use or still in quarantine, we return
 * NULL.  We also return NULL if a random number plus currently used memory
 * exceeds our quota.  Both of those mechanisms combined create a quadratic
 * backoff mechanism, so ufence allocations become less frequent as the quota
 * gets used up.  But we avoid a hard transition between frequent allocations
 * and no allocations at all.
 *
 *
 * To make ufence useful, you have to integrate five functions into an
 * existing allocator:
 * - ufence_malloc()
 * - ufence_memalign()
 * - ufence_free()
 * - ufence_init()
 * - ufence_segfault()
 *
 * Call ufence_malloc() from your regular allocator.  Avoid calling it from
 * the hot path, ufence_malloc() is slower than any regular allocator should
 * be.  One call every millisecond or so might be a good choice.  The same
 * goes for ufence_memalign().
 *
 * You have to mark ufence allocations in some way that allows to to call
 * ufence_free() whenever regular free() comes across a ufence object.  With
 * a dlmalloc-derived allocator, adding a header to the allocation will work,
 * but might hide buffer-underrun bugs from you.
 *
 * Call ufence_init() once to configure a fixed memory limit.  Ufence will
 * stay within that limit and slow down allocation frequence when you get
 * close.  There is currently no mechanism to change the limit at runtime.
 *
 * Finally, ufence turns memory corruption bugs in your code into segmentation
 * faults.  To get useful debug information, call ufence_segfault() from your
 * signal handler.  If the faulting address matches a ufence object, backtraces
 * for the allocation, free and fault get written to stderr.
 *
 *
 * You can compile and run this binary to get a general idea.  The main()
 * contains a number of memory corruption bugs, all of which trigger ufence
 * to print backtraces without crashing the program.
 */

#include <assert.h>
#include <execinfo.h>
#include <linux/futex.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

typedef unsigned __int128 u128;
typedef unsigned long long u64;
typedef unsigned u32;

#define ALIGN_DOWN(x, a)	((x) & ~((a)-1))
#define ALIGN_UP(x, a)		ALIGN_DOWN((x)+(a)-1, a)
#define PTR_ALIGN_DOWN(x, a)	((void *)ALIGN_DOWN((unsigned long)x, a))
#define PTR_ALIGN_UP(x, a)	((void *)ALIGN_UP((unsigned long)x, a))

#define ARRAY_SIZE(a)		(sizeof(a) / sizeof((a)[0]))

struct lock_pi {
	unsigned lock;
};

static inline pid_t gettid(void)
{
	return syscall(SYS_gettid);
}

static inline int futex(unsigned *uaddr, int futex_op, int val,
		const struct timespec *timeout,   /* or: uint32_t val2 */
		int *uaddr2, int val3)
{
	return syscall(SYS_futex, uaddr, futex_op, val, timeout, uaddr2, val3);
}

/* Returns 1 on success, 0 on error */
static int trylock_pi(struct lock_pi *l)
{
	pid_t tid = gettid();
	int old;

	old = __sync_val_compare_and_swap(&l->lock, 0, tid);
	if (old>FUTEX_TID_MASK)
		old = futex(&l->lock, FUTEX_TRYLOCK_PI, 0, NULL, NULL, 0);
	return !old;
}

static void lock_pi(struct lock_pi *l)
{
	pid_t tid = gettid();
	long old;

	old = __sync_val_compare_and_swap(&l->lock, 0, tid);
	while (old)
		old = futex(&l->lock, FUTEX_LOCK_PI, 0, NULL, NULL, 0);
}

static void unlock_pi(struct lock_pi *l)
{
	pid_t tid = gettid();
	int old;

	old = __sync_val_compare_and_swap(&l->lock, tid, 0);
	if (old != tid) {
		futex(&l->lock, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0);
	}
}


#define HARD_LIMIT	(0x40000000)		/*  1GiB */
#define REGION_SIZE	( 0x1000000)		/* 16MiB */
#define GUARD_SIZE	(  0x200000)		/*  2MiB */
#define MAX_ALIGN	(REGION_SIZE/2)		/*  8MiB */
#define MAX_ALLOC	(MAX_ALIGN-GUARD_SIZE)	/*  6MiB */
#define PAGE_SIZE	(    0x1000)		/*  4kiB (amd64) */
#define ADDRESS_SPACE	(1ull<<47)		/* 47bit (amd64) */
#define ADDR_MASK	(ADDRESS_SPACE-REGION_SIZE)

static inline void *guard_alloc(size_t size, u64 addr)
{
	void *p;
	if (addr) {
		p = mmap((void*)addr-GUARD_SIZE, size + 2*GUARD_SIZE, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE, -1, 0);
		if (p == MAP_FAILED)
			return NULL;
	} else
		p = mmap(NULL, size + 2*GUARD_SIZE, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	assert(p != MAP_FAILED);
	p = mmap(p+GUARD_SIZE, size, PROT_READ|PROT_WRITE, MAP_FIXED|MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	assert(p != MAP_FAILED);
	return p;
}

static inline void guard_free(void *p, size_t size)
{
	munmap(p - GUARD_SIZE, size + 2*GUARD_SIZE);
}

#define FL_USED		(1<<0)
#define FL_FRONTPAD	(1<<1)
#define FL_FAULT	(1<<2)
struct mapping {
	void *p;
	size_t size;
	size_t pad;
	size_t flags;
	u64 alloc_time;
	void *alloc_bt[29];
	u64 free_time;
	void *free_bt[29];
};

struct map {
	struct mapping m[128];
} map = {};

struct lock_pi ufence_lock;

struct ufence_hmap {
	u64 hmap_size; /* size of this structure, including list and htable */
	u64 mem_limit;
	u64 mem_used;
	u32 list_size;
	u32 htable_size;

	struct mapping *list;
	u32 *htable;
	u64 pad[2];
} *ufence_hmap;

static void ufence_init(u64 mem_limit)
{
	if (mem_limit < 2*PAGE_SIZE)
		mem_limit = 2*PAGE_SIZE;
	if (mem_limit > HARD_LIMIT)
		mem_limit = HARD_LIMIT;
	u64 list_size   = mem_limit / (PAGE_SIZE*2);
	u64 htable_size = mem_limit / (PAGE_SIZE/2);
	assert((u32)list_size == list_size);
	assert((u32)htable_size == htable_size);
	u64 hmap_size = sizeof(struct ufence_hmap)
		+ list_size*sizeof(struct mapping) + htable_size*sizeof(u32);
	assert(!ufence_hmap);
	ufence_hmap = guard_alloc(hmap_size, 0);
	ufence_hmap->hmap_size = hmap_size;
	ufence_hmap->mem_limit = mem_limit;
	ufence_hmap->mem_used = hmap_size;
	ufence_hmap->list_size = list_size;
	ufence_hmap->htable_size = htable_size;
	ufence_hmap->list   = (void *) (ufence_hmap+1);
	ufence_hmap->htable = (void *) (ufence_hmap->list+list_size);
}

static u64 get_monotonic(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

/* returns a random number in the range 0..n-1 (or 0 when n is 0) */
static u64 rand_n(u64 n)
{
	u64 r;
	getrandom(&r, sizeof(r), 0);
	u128 p = r;
	p *= n;
	return p>>64;
}

static u64 rand64()
{
	u64 r;
	getrandom(&r, sizeof(r), 0);
	return r;
}

static u64 lothash(u64 val, u64 limit)
{
	val = ALIGN_DOWN(val, REGION_SIZE);
	u64 m = 0x1da177e4c3f41524ull | 1 | 1ull<<63; /* Let it rip! */
	u128 p = val;
	p *= m;
	m = p ^ p>>64;
	p = m;
	p *= limit;
	return p>>64;
}

void *_ufence_hmap_alloc(size_t size, int frontpad)
{
	size_t outer = ALIGN_UP(size, PAGE_SIZE);
	void *ret = NULL;
	if (!trylock_pi(&ufence_lock))
		return NULL;
	/* backoff mechanism 1: pick a random slot and bail if it's in use */
	u32 lslot = rand_n(ufence_hmap->list_size);
	if (!lslot)
		goto out;
	struct mapping *m = &ufence_hmap->list[lslot];
	if (m->flags & FL_USED || (get_monotonic() - m->free_time < 60000000000ull))
		goto out;
	if (m->p) {
		/* permanently free memory in this slot */
		size_t outer = m->size + m->pad;
		void *p = m->p;
		if (m->flags & FL_FRONTPAD)
			p -= m->pad;
		u32 hslot = lothash((u64)p, ufence_hmap->htable_size);
		ufence_hmap->htable[hslot] = 0;
		m->p = 0;
		guard_free(p, outer);
		ufence_hmap->mem_used -= outer;
	}
	/* backoff mechanism 2: bail based on remaining free space */
	u64 n = rand_n(ufence_hmap->mem_limit);
	n += ufence_hmap->mem_used + outer;
	if (n > ufence_hmap->mem_limit)
		goto out;
	for (int i=0; i<10; i++) {
		/* Find a random address that hashes to a free slot */
		u64 addr = rand64() & ADDR_MASK;
		if (!addr)
			continue;
		addr += MAX_ALIGN; /* middle of region */
		u32 hslot = lothash(addr, ufence_hmap->htable_size);
		if (ufence_hmap->htable[hslot])
			continue;
		/* allocation with MAP_FIXED_NOREPLACE can fail */
		void *p = guard_alloc(outer, addr);
		if (!p)
			continue;
		if (p != (void*)addr) {
			/* Can happen with kernel < 4.17 */
			guard_free(p, outer);
			break;
		}
		ufence_hmap->htable[hslot] = lslot;

		m->alloc_time = get_monotonic();
		m->p = p;
		m->size = size;
		m->pad = outer-size;
		m->flags = FL_USED;
		if (m->pad && rand64()&1) {
			m->p += m->pad;
			m->flags |= FL_FRONTPAD;
		}
		backtrace(m->alloc_bt, ARRAY_SIZE(m->alloc_bt));
		ret = m->p;
		ufence_hmap->mem_used += outer;
		break;
	}
out:
	unlock_pi(&ufence_lock);
	return ret;
}

void ufence_free(void *p)
{
	lock_pi(&ufence_lock);
	u32 hslot = lothash((u64)p, ufence_hmap->htable_size);
	u32 lslot = ufence_hmap->htable[hslot];
	struct mapping *m = &ufence_hmap->list[lslot];

	m->free_time = get_monotonic();
	m->flags &= ~FL_USED;
	size_t outer = m->size + m->pad;
	assert(p == m->p);
	if (m->flags & FL_FRONTPAD)
		p -= m->pad;
	mprotect(p, outer, PROT_NONE);
	backtrace(m->free_bt, ARRAY_SIZE(m->free_bt));
	unlock_pi(&ufence_lock);
}

static char *number(u64 n)
{
	static char buf[30];
	buf[29] = 0;
	char *s = buf+29;
	while (n>=1000) {
		u32 frac = n%1000;
		s -= 4;
		s[0] = '_';
		s[1] = '0' + frac/100; frac %= 100;
		s[2] = '0' + frac/10;  frac %= 10;
		s[3] = '0' + frac;
		n /= 1000;
	}
	while (n) {
		*--s = '0' + n%10; n/=10;
	}
	return s;
}

/* returns 1 if the segfault was in ufence memory, 0 if unrelated */
int ufence_segfault(void *p)
{
	/* If possible, acquire the lock.  But the lock might already be held
	 * by the same thread, leading to a deadlock.  In such a situation, we
	 * work unlocked and hope for the best.  Given that we already received
	 * a SIGSEGV, the worst that could happen won't be worse than the
	 * situation we are already in. */
	int locked = trylock_pi(&ufence_lock);
	int ret = 0;

	u32 hslot = lothash((u64)p, ufence_hmap->htable_size);
	u32 lslot = ufence_hmap->htable[hslot];
	struct mapping *m = &ufence_hmap->list[lslot];
	if (PTR_ALIGN_DOWN(p, REGION_SIZE) != PTR_ALIGN_DOWN(m->p, REGION_SIZE))
		goto out;

	u64 now = get_monotonic();
	fprintf(stderr, "ufence fault at %p (offset %zd of %zd) flags=%zx\n", p, p-m->p, m->size, m->flags);
	{
		void *fault_bt[30];
		fprintf(stderr, "fault backtrace:\n");
		backtrace(fault_bt, ARRAY_SIZE(fault_bt));
		char **symbols = backtrace_symbols(fault_bt, ARRAY_SIZE(fault_bt));
		for (int s=0; s<ARRAY_SIZE(fault_bt) && fault_bt[s]; s++)
			fprintf(stderr, "%s\n", symbols[s]);
	}
	{
		fprintf(stderr, "alloc backtrace (%sns ago):\n", number(now - m->alloc_time));
		char **symbols = backtrace_symbols(m->alloc_bt, ARRAY_SIZE(m->alloc_bt));
		for (int s=0; s<ARRAY_SIZE(m->alloc_bt) && m->alloc_bt[s]; s++)
			fprintf(stderr, "%s\n", symbols[s]);
	}
	if (!(m->flags & FL_USED)) {
		fprintf(stderr, "free backtrace (%sns ago):\n", number(now - m->free_time));
		char **symbols = backtrace_symbols(m->free_bt, ARRAY_SIZE(m->free_bt));
		for (int s=0; s<ARRAY_SIZE(m->free_bt) && m->free_bt[s]; s++)
			fprintf(stderr, "%s\n", symbols[s]);
	}
	fprintf(stderr, "\n");

	if (m->p-GUARD_SIZE < p && p < m->p+m->size+GUARD_SIZE) {
		/* fixup permissions to handle fault gracefully */
		m->flags |= FL_FAULT;
		p = PTR_ALIGN_DOWN(p, PAGE_SIZE);
		mprotect(p, PAGE_SIZE, PROT_READ|PROT_WRITE);
		ret = 1;
	}
out:
	if (locked)
		unlock_pi(&ufence_lock);
	return ret;
}

void *ufence_malloc(size_t size)
{
	if (size>MAX_ALLOC)
		return NULL;
	int frontpad = 0;
	if (size%PAGE_SIZE && rand64()&1)
		frontpad = 1;
	return _ufence_hmap_alloc(size, frontpad);
}

void *ufence_memalign(size_t alignment, size_t size)
{
	if (size>MAX_ALLOC || alignment>MAX_ALIGN)
		return NULL;
	return _ufence_hmap_alloc(size, 0);
}

static void signal_handler(int signal, siginfo_t *info, void *addr)
{
	int kf = ufence_segfault(info->si_addr);
	if (kf)
		return; /* ufence handled the segfault */
	/* regular signal handler would follow... */
}

int main(void)
{
	ufence_init(1ull<<21);
	{
		/* set up signal handler */
		struct sigaction act = {};

		sigemptyset(&act.sa_mask);
		sigaddset(&act.sa_mask, SIGBUS);
		sigaddset(&act.sa_mask, SIGSEGV);
		act.sa_flags = SA_SIGINFO|SA_NODEFER|SA_ONSTACK;
		act.sa_sigaction = signal_handler;
		sigaction(SIGBUS, &act, NULL);
		sigaction(SIGSEGV, &act, NULL);
	}
	for (int i=0; i<100; i++) {
		/* trigger a bunch of memory corruptions */
		size_t size = (4096+rand64()) & 0xffff;
		void *p = ufence_malloc(size);
		if (!p)
			continue;
		fprintf(stderr, "---\n");
		memset(p, 0, size);
		memset(p-1, 1, 1);			/* buffer underrun */
		memset(p+size, 1, 1);			/* buffer overflow */
		memset(p-GUARD_SIZE+1, 1, 1);		/* buffer underrun */
		memset(p+size-1+GUARD_SIZE, 1, 1);	/* buffer overflow */
		ufence_free(p);
		memset(p, 1, 1);			/* use after free */
		memset(p+size-1, 1, 1);			/* use after free */
	}
	return 0;
}
