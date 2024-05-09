#include <assert.h>
#include <linux/futex.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>

typedef unsigned int u32;
typedef unsigned long long u64;
typedef long long s64;
typedef unsigned __int128 u128;
#define READ_ONCE(x)		(*(const volatile __typeof(x) *)&(x))
#define WRITE_ONCE(x, val)						\
do {									\
	*(volatile typeof(x)*)&(x) = (val);				\
} while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define atomic_add(p, n)	__sync_fetch_and_add(p, n)
#define atomic_inc(p)		__sync_fetch_and_add(p, 1)
#define popcount64(arg)		__builtin_popcountll(arg)
#define barrier()		asm volatile("": : :"memory")

/* See linux kernel commit b6c7347fffa6 */
#if (defined(__amd64__) || defined(__x86_64__))
#define mb()			barrier()
#define rmb()			barrier()
#define wmb()			barrier()
#endif


struct entry {
	union {
		struct {
			u64 ctr;
			u64 val;
		};
		u128 tupel;
	};
};

static inline int cmpxchg64(u64 *p, u64 old, u64 new)
{
	return __sync_bool_compare_and_swap(p, old, new);
}

static inline int cmpxchg_p(void **p, void *old, void *new)
{
	return __sync_bool_compare_and_swap(p, old, new);
}

static inline int cmpxchg_entry(volatile struct entry *entry, struct entry old, struct entry new)
{
	return __sync_bool_compare_and_swap(&entry->tupel, old.tupel, new.tupel);
}

#define CL_ALIGNED __attribute__((aligned(64)))
struct subqueue {
	/* generic */
	u64 size CL_ALIGNED;
	u64 mask;
	u64 max_size;
	u64 rcu_dequeue_count;
	u64 rcu_free_count;
	struct subqueue *next;
	u64 ancestor_count; /* sum of enqueue/dequeue pairs of previous queues */
	/* consumer-owned */
	u64 tail CL_ALIGNED;
	/* producer-owned */
	u64 head_copy CL_ALIGNED;
	u64 tail_copy;
	/* consumer-stats */
	u64 enqueue_collisions CL_ALIGNED;
	/* producer-stats */
	u64 dequeue_collisions CL_ALIGNED;
	/* finally - the ringbuffer */
	struct entry q[0] CL_ALIGNED;
};

struct lock_pi {
	unsigned lock;
};

/*
 * Mostly-atomic queue.  Each subqueue is atomic, but also fixed-size.  If the
 * initial size wasn't quite right, we automatically grow by creating a new
 * subqueue.  Growing the queue and freeing the old subqueue requires locking,
 * so the queue is only truly atomic if the initial size was large enough.
 * Lock uses PI to avoid really nasty worst-case latencies.
 */
struct atomic_queue {
	struct subqueue *enq;
	struct subqueue *deq;
	struct subqueue *freeq;
	struct lock_pi lock;
};

inline pid_t gettid(void)
{
	return syscall(SYS_gettid);
}

static inline int futex(unsigned *uaddr, int futex_op, int val,
		const struct timespec *timeout,   /* or: uint32_t val2 */
		int *uaddr2, int val3)
{
	return syscall(SYS_futex, uaddr, futex_op, val, timeout, uaddr2, val3);
}

/*
 * returns 1 on success, 0 on error
 * Linux kernel has the reverse return codes
 *
 * trylock_pi() is special.  It needs to actually wait for priority
 * inheritance to work.  Without waiting the thread holding the lock
 * will stay at the same priority and continue to not make progress.
 * But we still don't want to wait indefinitely.  Good timeout values
 * are around a scheduler timeslice, 1ms.
 */
static inline void lock_pi(struct lock_pi *l)
{
	pid_t tid = gettid();

	for (;;) {
		int old = __sync_val_compare_and_swap(&l->lock, 0, tid);
		if (!old)
			return;
		int err = futex(&l->lock, FUTEX_LOCK_PI, 0, NULL, NULL, 0);
		if (!err)
			return;
	}
}

static inline void unlock_pi(struct lock_pi *l)
{
	pid_t tid = gettid();
	int old;

	old = __sync_val_compare_and_swap(&l->lock, tid, 0);
	if (old != tid) {
		int err = futex(&l->lock, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0);
		assert(!err);
	}
}

/*
 * global_rcu is an array of rcu_counters, one for each thread.
 * Counters are initially 0.  New threads claim a counter by setting
 * it to RCU_UNLOCKED.  Exiting threads allow reuse by setting the
 * counter to RCU_UNUSED.  Critical sections protected by rcu_lock()
 * set the counter to the current global_rcu_count, until rcu_unlock()
 * sets it back to RCU_UNLOCKED.
 */
#define RCU_UNLOCKED	(-2ull)
#define RCU_UNUSED	(-1ull)
static __thread u64 *local_rcu;
static u64 global_rcu[1<<20];
static u64 global_rcu_count=1;

static void rcu_lock(void)
{
	if (!local_rcu) {
		/* register our thread */
		for (int i=0; ; i++) {
			assert(i<ARRAY_SIZE(global_rcu));
			u64 old = READ_ONCE(global_rcu[i]);
			if (old && old!=RCU_UNUSED)
				continue;
			int err = !cmpxchg64(&global_rcu[i], old, RCU_UNLOCKED);
			if (err)
				continue;
			local_rcu = &global_rcu[i];
			break;
		}
	}

	assert(*local_rcu == RCU_UNLOCKED);
	WRITE_ONCE(*local_rcu, global_rcu_count);
	mb();
}

static void rcu_unlock(void)
{
	assert(*local_rcu <= global_rcu_count);
	mb();
	WRITE_ONCE(*local_rcu, RCU_UNLOCKED);
}

static u64 rcu_register(void)
{
	return atomic_inc(&global_rcu_count);
}

static int rcu_safe(u64 count)
{
	static u64 global_rcu_safe;
	assert(count < global_rcu_count);
	/* optimization - we can avoid the loop for old counts */
	if (count <= global_rcu_safe)
		return 1;
	for (int i=0; i<ARRAY_SIZE(global_rcu); i++) {
		if (global_rcu[i] <= count) {
			if (!global_rcu[i])
				break;
			return 0;
		}
	}
	global_rcu_safe = count;
	return 1;
}

static void rcu_destroy(void *unused)
{
	*local_rcu = RCU_UNUSED;
}

static void rcu_init(void)
{
	static struct lock_pi init_lock;
	static int is_initialized;

	if (is_initialized)
		return;
	lock_pi(&init_lock);
	if (!is_initialized) {
		static pthread_key_t unused;
		pthread_key_create(&unused, rcu_destroy);
		/* TODO */
	}
	unlock_pi(&init_lock);
}

static struct subqueue *alloc_subqueue(u64 initial_size, u64 max_size)
{
	struct subqueue *subq = NULL;

	rcu_init();
	if (!initial_size)
		initial_size = 32;
	if (!max_size)
		max_size = 1ull<<59;
	/* Lots of sanity checks */
	assert(popcount64(initial_size) == 1);
	assert(popcount64(max_size) == 1);
	u64 q_size = sizeof(struct subqueue) + initial_size*sizeof(struct entry);
	u64 max_q_size = sizeof(struct subqueue) + max_size*sizeof(struct entry);
	assert((u64)&subq->q[initial_size] == q_size);
	assert((u64)&subq->q[max_size] == max_q_size);

	subq = calloc(q_size, 1);
	subq->size = initial_size;
	subq->mask = initial_size-1;
	subq->max_size = max_size;
	return subq;
}

struct atomic_queue *alloc_queue(u64 initial_size, u64 max_size)
{
	//rcu_init();
	struct subqueue *subq = alloc_subqueue(initial_size, max_size);

	struct atomic_queue *aq = malloc(sizeof(*aq));
	aq->enq = subq;
	aq->deq = subq;
	return aq;
}

/* returns 0 on empty queue, 1 on dequeue */
static int subdequeue(struct subqueue *q, u64 *retval)
{
	u64 slot;
	u64 ctr;
	u64 val;
	u64 counter;
	u64 retries=-1;
	do {
		retries++;
		counter = READ_ONCE(q->tail) + 1;
		slot = counter & q->mask;
		ctr = READ_ONCE(q->q[slot].ctr);
		rmb();
		val = READ_ONCE(q->q[slot].val);
		if (ctr < counter)
			return 0; /* queue empty */
	} while (!cmpxchg64(&q->tail, counter-1, counter));
	assert(slot == (ctr & q->mask));
	if (retries)
		atomic_add(&q->dequeue_collisions, retries);
	*retval = val;
	return 1;
}

/* returns 0 on empty queue, 1 on dequeue */
int dequeue(struct atomic_queue *q, u64 *retval)
{
	rcu_lock();
	struct subqueue *subq = READ_ONCE(q->deq);
	int ret=0;
	do {
		ret = subdequeue(subq, retval);
		if (ret) {
			rcu_unlock();
			return ret;
		}
		/* Have all consumers forgotten about an old queue? */
		if (q->freeq && rcu_safe(subq->rcu_free_count)) {
			lock_pi(&q->lock);
			if (q->freeq) {
				free(q->freeq);
				q->freeq = NULL;
			}
			unlock_pi(&q->lock);
		}
		/* Have all producers forgotten about this queue? */
		struct subqueue *next = subq->next;
		if (next && !q->freeq && rcu_safe(subq->rcu_dequeue_count)) {
			lock_pi(&q->lock);
			if (!q->freeq && q->deq==subq) {
				q->freeq = subq;
				q->deq = next;
				subq->rcu_free_count = rcu_register();
				/* carry over statistics */
				atomic_add(&next->enqueue_collisions, subq->enqueue_collisions);
				atomic_add(&next->dequeue_collisions, subq->dequeue_collisions);
				next->ancestor_count = subq->ancestor_count + subq->tail;
			}
			unlock_pi(&q->lock);
		}
		subq = next;
	} while (subq);
	rcu_unlock();
	return 0;
}

static s64 _enqueue(struct subqueue *q, u64 val, u64 *head, u64 tail)
{
	u64 tries=0;
	struct entry old={}, new={}, *entry;
	do {
retry:
		(*head)++;
		u64 slot = *head & q->mask;
		entry = &q->q[slot];

		old.val = READ_ONCE(entry->val);
		rmb();
		old.ctr = READ_ONCE(entry->ctr);
		if (old.ctr >= *head)
			goto retry;
		if (old.ctr > tail) {
			(*head)--;
			return ~tries;
		}
		tries++;
		new.ctr = *head;
		new.val = val;
	} while (!cmpxchg_entry(entry, old, new));
	return tries;
}

static int subenqueue(struct subqueue *q, u64 val)
{
	rcu_lock();
	u64 head = READ_ONCE(q->head_copy);
	u64 tail = READ_ONCE(q->tail_copy);
	for (int i=0; i<4; i++) {
		s64 tries = _enqueue(q, val, &head, tail);
		assert(tries);
		if (tries<0) {
			tries = ~tries;
			if (tries>1)
				atomic_add(&q->enqueue_collisions, tries-1);
			/* queue appeared full */
			u64 new_tail = READ_ONCE(q->tail);
			assert(new_tail >= tail);
			if (new_tail <= tail) {
				rcu_unlock();
				return 0;
			}
			tail = new_tail;
			if (new_tail > READ_ONCE(q->tail_copy))
				q->tail_copy = new_tail;
			continue;
		}
		if (tries>1)
			atomic_add(&q->enqueue_collisions, tries-1);
		q->head_copy = head; /* unconditional write, might occasionally go backwards. */
		rcu_unlock();
		return 1;
	}
	rcu_unlock();
	return 0;
}

void enqueue(struct atomic_queue *q, u64 val)
{
retry:;
	struct subqueue *subq = READ_ONCE(q->enq);
	int queue_full = !subenqueue(subq, val);
	if (queue_full) {
		/* handle full queue - this is where things get interesting. */
		lock_pi(&q->lock);
		if (subq->next) {
			/* another thread has already created a bigger queue */
			assert(subq != READ_ONCE(q->enq));
		} else {
			u64 size = 2 * subq->size;
			assert(size > subq->size);
			assert(size <= subq->max_size);
			subq->next = alloc_subqueue(size, subq->max_size);
			q->enq = subq->next;
			mb();
			subq->rcu_dequeue_count = rcu_register();
		}
		unlock_pi(&q->lock);
		goto retry;
	}
}
