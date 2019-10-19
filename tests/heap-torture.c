/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2018 Philippe Gerum <rpm@xenomai.org>
 * Derived from Xenomai heapmem smokey test (https://xenomai.org/)
 */

#include <sys/types.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <error.h>
#include <pthread.h>
#include <sched.h>
#include <evl/clock.h>
#include <evl/thread.h>
#include <evl/heap.h>
#include "helpers.h"

#define do_trace(__fmt, __args...)			\
	do {						\
		if (verbose)				\
			printf(__fmt "\n", ##__args);	\
	} while (0)

#define do_warn(__fmt, __args...)	\
	fprintf(stderr, __fmt "\n", ##__args)

static struct evl_heap heap;

#define MIN_HEAP_SIZE		8192
#define MAX_HEAP_SIZE		(1024 * 32) /* or -s <size> */
#define RANDOM_ROUNDS		3	    /* or -r <count> */
#define PATTERN_HEAP_SIZE	(1024 * 32) /* or -p <size> */
#define PATTERN_ROUNDS		1	    /* or -c <count> */

static size_t seq_min_heap_size = MIN_HEAP_SIZE;
static size_t seq_max_heap_size = MAX_HEAP_SIZE;
static int random_rounds = RANDOM_ROUNDS;
static size_t pattern_heap_size = PATTERN_HEAP_SIZE;
static int pattern_rounds = PATTERN_ROUNDS;
static int verbose;

#define MEMCHECK_ZEROOVRD   1
#define MEMCHECK_SHUFFLE    2
#define MEMCHECK_PATTERN    4
#define MEMCHECK_HOT        8

struct run_statistics {
	size_t heap_size;
	size_t user_size;
	size_t block_size;
	size_t maximum_free;
	size_t largest_free;
	int nrblocks;
	long alloc_avg_ns;
	long alloc_max_ns;
	long free_avg_ns;
	long free_max_ns;
	int flags;
	struct run_statistics *next;
};

enum pattern {
	alphabet_series,
	digit_series,
	binary_series,
};

struct chunk {
	void *ptr;
	enum pattern pattern;
};

static struct run_statistics *statistics;

static int nrstats;

static int max_results = 4;

static inline void breathe(int loops)
{
	/*
	 * We need to leave some cycles free for the inband activity
	 * to progress, let's nap a bit during the test.
	 */
	if ((loops % 1000) == 0)
		evl_udelay(300);
}

static inline long diff_ts(struct timespec *left, struct timespec *right)
{
	return (long long)(left->tv_sec - right->tv_sec) * 1000000000
		+ left->tv_nsec - right->tv_nsec;
}

static inline void swap(void *left, void *right, const size_t size)
{
	char trans[size];

	memcpy(trans, left, size);
	memcpy(left, right, size);
	memcpy(right, trans, size);
}

static void random_shuffle(void *vbase, size_t nmemb, const size_t size)
{
	struct {
		char x[size];
	} __attribute__((packed)) *base = vbase;
	unsigned int j, k;
	double u;

	for(j = nmemb; j > 0; j--) {
		breathe(j);
		u = (double)random() / RAND_MAX;
		k = (unsigned int)(j * u) + 1;
		if (j == k)
			continue;
		swap(&base[j - 1], &base[k - 1], size);
	}
}

/* Reverse sort, high values first. */

#define compare_values(l, r)			\
	({					\
		typeof(l) _l = (l);		\
		typeof(r) _r = (r);		\
		(_l > _r) - (_l < _r);		\
	})

static int sort_by_heap_size(const void *l, const void *r)
{
	const struct run_statistics *ls = l, *rs = r;

	return compare_values(rs->heap_size, ls->heap_size);
}

static int sort_by_alloc_time(const void *l, const void *r)
{
	const struct run_statistics *ls = l, *rs = r;

	return compare_values(rs->alloc_max_ns, ls->alloc_max_ns);
}

static int sort_by_free_time(const void *l, const void *r)
{
	const struct run_statistics *ls = l, *rs = r;

	return compare_values(rs->free_max_ns, ls->free_max_ns);
}

static int sort_by_frag(const void *l, const void *r)
{
	const struct run_statistics *ls = l, *rs = r;

	return compare_values(rs->maximum_free - rs->largest_free,
			      ls->maximum_free - ls->largest_free);
}

static int sort_by_overhead(const void *l, const void *r)
{
	const struct run_statistics *ls = l, *rs = r;

	return compare_values(rs->heap_size - rs->user_size,
			      ls->heap_size - ls->user_size);
}

static inline const char *get_debug_state(void)
{
#ifndef __OPTIMIZE__
	return "\n(CAUTION: optimizer is disabled, heap sanity checks on)";
#else
	return "";
#endif
}

static void memcheck_log_stat(struct run_statistics *st)
{
	st->next = statistics;
	statistics = st;
	nrstats++;
}

static void __dump_stats(struct run_statistics *stats,
			 int (*sortfn)(const void *l, const void *r),
			 int nr, const char *key)
{
	struct run_statistics *p;
	int n;

	qsort(stats, nrstats, sizeof(*p), sortfn);

	do_trace("\nsorted by: max %s\n%8s  %7s  %7s  %5s  %5s  %5s  %5s   %5s  %5s  %s",
		     key, "HEAPSZ", "BLOCKSZ", "NRBLKS", "AVG-A",
		     "AVG-F", "MAX-A", "MAX-F", "OVRH%", "FRAG%", "FLAGS");

	for (n = 0; n < nr; n++) {
		p = stats + n;
		do_trace("%7zuk  %7zu%s  %6d  %5.1f  %5.1f  %5.1f  %5.1f   %4.1f  %5.1f   %s%s%s",
			     p->heap_size / 1024,
			     p->block_size < 1024 ? p->block_size : p->block_size / 1024,
			     p->block_size < 1024 ? " " : "k",
			     p->nrblocks,
			     (double)p->alloc_avg_ns/1000.0,
			     (double)p->free_avg_ns/1000.0,
			     (double)p->alloc_max_ns/1000.0,
			     (double)p->free_max_ns/1000.0,
			     100.0 - (p->user_size * 100.0 / p->heap_size),
			     (1.0 - ((double)p->largest_free / p->maximum_free)) * 100.0,
			     p->alloc_avg_ns == 0 && p->free_avg_ns == 0 ? "FAILED " : "",
			     p->flags & MEMCHECK_SHUFFLE ? "+shuffle " : "",
			     p->flags & MEMCHECK_HOT ? "+hot" : "");
	}

	if (nr < nrstats)
		do_trace("  ... (%d results following) ...", nrstats - nr);
}

static int dump_stats(const char *title)
{
	long worst_alloc_max = 0, worst_free_max = 0;
	double overhead_sum = 0.0, frag_sum = 0.0;
	long max_alloc_sum = 0, max_free_sum = 0;
	long avg_alloc_sum = 0, avg_free_sum = 0;
	struct run_statistics *stats, *p, *next;
	int n;

	stats = malloc(sizeof(*p) * nrstats);
	if (stats == NULL) {
		do_warn("failed allocating memory");
		return -ENOMEM;
	}

	for (n = 0, p = statistics; n < nrstats; n++, p = p->next)
		stats[n] = *p;

	do_trace("\n[%s] %s\n", title, get_debug_state());

	do_trace("HEAPSZ	test heap size");
	do_trace("BLOCKSZ	tested block size");
	do_trace("NRBLKS	number of blocks allocatable in heap");
	do_trace("AVG-A	average time to allocate block (us)");
	do_trace("AVG-F	average time to free block (us)");
	do_trace("MAX-A	max time to allocate block (us)");
	do_trace("MAX-F	max time to free block (us)");
	do_trace("OVRH%%	overhead");
	do_trace("FRAG%%	external fragmentation");
	do_trace("FLAGS%%	+shuffle: randomized free");
	do_trace("    	+hot: measure after initial alloc/free pass (hot heap)");

	if (max_results > 0) {
		if (max_results > nrstats)
			max_results = nrstats;
		__dump_stats(stats, sort_by_alloc_time, max_results, "alloc time");
		__dump_stats(stats, sort_by_free_time, max_results, "free time");
		__dump_stats(stats, sort_by_overhead, max_results, "overhead");
		__dump_stats(stats, sort_by_frag, max_results, "fragmentation");
	} else if (max_results < 0)
		__dump_stats(stats, sort_by_heap_size, nrstats, "heap size");

	free(stats);

	for (p = statistics; p; p = next) {
		max_alloc_sum += p->alloc_max_ns;
		max_free_sum += p->free_max_ns;
		avg_alloc_sum += p->alloc_avg_ns;
		avg_free_sum += p->free_avg_ns;
		overhead_sum += 100.0 - (p->user_size * 100.0 / p->heap_size);
		frag_sum += (1.0 - ((double)p->largest_free / p->maximum_free)) * 100.0;
		if (p->alloc_max_ns > worst_alloc_max)
			worst_alloc_max = p->alloc_max_ns;
		if (p->free_max_ns > worst_free_max)
			worst_free_max = p->free_max_ns;
		next = p->next;
		free(p);
	}

	do_trace("\noverall:");
	do_trace("  worst alloc time: %.1f (us)",
		     (double)worst_alloc_max / 1000.0);
	do_trace("  worst free time: %.1f (us)",
		     (double)worst_free_max / 1000.0);
	do_trace("  average of max. alloc times: %.1f (us)",
		     (double)max_alloc_sum / nrstats / 1000.0);
	do_trace("  average of max. free times: %.1f (us)",
		     (double)max_free_sum / nrstats / 1000.0);
	do_trace("  average alloc time: %.1f (us)",
		     (double)avg_alloc_sum / nrstats / 1000.0);
	do_trace("  average free time: %.1f (us)",
		     (double)avg_free_sum / nrstats / 1000.0);
	do_trace("  average overhead: %.1f%%",
		     (double)overhead_sum / nrstats);
	do_trace("  average fragmentation: %.1f%%",
		     (double)frag_sum / nrstats);

	statistics = NULL;
	nrstats = 0;

	return 0;
}

static void fill_pattern(char *p, size_t size, enum pattern pat)
{
	unsigned int val, count;

	switch (pat) {
	case alphabet_series:
		val = 'a';
		count = 26;
		break;
	case digit_series:
		val = '0';
		count = 10;
		break;
	default:
		val = 0;
		count = 255;
		break;
	}

	while (size-- > 0) {
		*p++ = (char)(val % count);
		val++;
	}
}

static int check_pattern(const char *p, size_t size, enum pattern pat)
{
	unsigned int val, count;

	switch (pat) {
	case alphabet_series:
		val = 'a';
		count = 26;
		break;
	case digit_series:
		val = '0';
		count = 10;
		break;
	default:
		val = 0;
		count = 255;
		break;
	}

	while (size-- > 0) {
		if (*p++ != (char)(val % count))
			return 0;
		val++;
	}

	return 1;
}

static size_t find_largest_free(size_t free_size, size_t block_size)
{
	void *p;

	for (;;) {
		p = evl_alloc_block(&heap, free_size);
		if (p) {
			evl_free_block(&heap, p);
			break;
		}
		if (free_size <= block_size)
			break;
		free_size -= block_size;
	}

	return free_size;
}

static int test_seq(size_t heap_size, size_t block_size, int flags)
{
	size_t arena_size, user_size, largest_free, maximum_free, freed;
	long alloc_sum_ns, alloc_avg_ns, free_sum_ns, free_avg_ns,
		alloc_max_ns, free_max_ns, d;
	int ret, n, k, maxblocks, nrblocks;
	struct timespec start, end;
	struct run_statistics *st;
	struct sched_param param;
	struct chunk *chunks;
	bool done_frag;
	void *mem, *p;

	param.sched_priority = 1;
	pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
	ret = evl_attach_self("heap-torture:%d", getpid());

	arena_size = EVL_HEAP_ARENA_SIZE(heap_size);
	mem = malloc(arena_size);
	if (mem == NULL)
		return -ENOMEM;

	maxblocks = heap_size / block_size;

	ret = evl_init_heap(&heap, mem, arena_size);
	if (ret) {
		do_trace("cannot init heap with arena size %zu",
			     arena_size);
		goto out;
	}

	chunks = calloc(sizeof(*chunks), maxblocks);
	if (chunks == NULL) {
		ret = -ENOMEM;
		goto no_chunks;
	}

	if (evl_heap_size(&heap) != heap_size) {
		do_trace("memory size inconsistency (%zu / %zu bytes)",
			     heap_size, evl_heap_size(&heap));
		goto bad;
	}

	user_size = 0;
	alloc_avg_ns = 0;
	free_avg_ns = 0;
	alloc_max_ns = 0;
	free_max_ns = 0;
	largest_free = 0;
	maximum_free = 0;

	/*
	 * Make sure to run out-of-band before the first allocation
	 * call takes place, not to charge any switch time to the
	 * allocator.
	 */
	evl_switch_oob();

	for (n = 0, alloc_sum_ns = 0; ; n++) {
		evl_read_clock(EVL_CLOCK_MONOTONIC, &start);
		p = evl_alloc_block(&heap, block_size);
		evl_read_clock(EVL_CLOCK_MONOTONIC, &end);
		d = diff_ts(&end, &start);
		if (d > alloc_max_ns)
			alloc_max_ns = d;
		alloc_sum_ns += d;
		if (p == NULL)
			break;
		user_size += block_size;
		if (n >= maxblocks) {
			do_trace("too many blocks fetched"
				     " (heap=%zu, block=%zu, "
				     "got more than %d blocks)",
				     heap_size, block_size, maxblocks);
			goto bad;
		}
		chunks[n].ptr = p;
		if (flags & MEMCHECK_PATTERN) {
			chunks[n].pattern = (enum pattern)(random() % 3);
			fill_pattern(chunks[n].ptr, block_size, chunks[n].pattern);
		}
		breathe(n);
	}

	nrblocks = n;
	if (nrblocks == 0)
		goto do_stats;

	if ((flags & MEMCHECK_ZEROOVRD) && nrblocks != maxblocks) {
		do_trace("too few blocks fetched, unexpected overhead"
			     " (heap=%zu, block=%zu, "
			     "got %d, less than %d blocks)",
			     heap_size, block_size, nrblocks, maxblocks);
		goto bad;
	}

	breathe(0);

	/* Make sure we did not trash any busy block while allocating. */
	if (flags & MEMCHECK_PATTERN) {
		for (n = 0; n < nrblocks; n++) {
			if (!check_pattern(chunks[n].ptr, block_size,
					   chunks[n].pattern)) {
				do_trace("corrupted block #%d on alloc"
					     " sequence (pattern %d)",
					     n, chunks[n].pattern);
				goto bad;
			}
			breathe(n);
		}
	}

	if (flags & MEMCHECK_SHUFFLE)
		random_shuffle(chunks, nrblocks, sizeof(*chunks));

	evl_switch_oob();

	/*
	 * Release all blocks.
	 */
	for (n = 0, free_sum_ns = 0, freed = 0, done_frag = false;
	     n < nrblocks; n++) {
		evl_read_clock(EVL_CLOCK_MONOTONIC, &start);
		ret = evl_free_block(&heap, chunks[n].ptr);
		evl_read_clock(EVL_CLOCK_MONOTONIC, &end);
		if (ret) {
			do_trace("failed to free block %p "
				     "(heap=%zu, block=%zu)",
				     chunks[n].ptr, heap_size, block_size);
			goto bad;
		}
		d = diff_ts(&end, &start);
		if (d > free_max_ns)
			free_max_ns = d;
		free_sum_ns += d;
		chunks[n].ptr = NULL;
		/* Make sure we did not trash busy blocks while freeing. */
		if (flags & MEMCHECK_PATTERN) {
			for (k = 0; k < nrblocks; k++) {
				if (chunks[k].ptr &&
				    !check_pattern(chunks[k].ptr, block_size,
						   chunks[k].pattern)) {
					do_trace("corrupted block #%d on release"
						     " sequence (pattern %d)",
						     k, chunks[k].pattern);
					goto bad;
				}
				breathe(k);
			}
		}
		freed += block_size;
		/*
		 * Get a sense of the fragmentation for the tested
		 * allocation pattern, heap and block sizes when half
		 * of the usable heap size should be available to us.
		 * NOTE: user_size excludes the overhead, this is
		 * actually what we managed to get from the current
		 * heap out of the allocation loop.
		 */
		if (!done_frag && freed >= user_size / 2) {
			/* Calculate the external fragmentation. */
			largest_free = find_largest_free(freed, block_size);
			maximum_free = freed;
			done_frag = true;
		}
		breathe(n);
	}

	/*
	 * If the deallocation mechanism is broken, we might not be
	 * able to reproduce the same allocation pattern with the same
	 * outcome, check this.
	 */
	if (flags & MEMCHECK_HOT) {
		for (n = 0, alloc_max_ns = alloc_sum_ns = 0; ; n++) {
			evl_read_clock(EVL_CLOCK_MONOTONIC, &start);
			p = evl_alloc_block(&heap, block_size);
			evl_read_clock(EVL_CLOCK_MONOTONIC, &end);
			d = diff_ts(&end, &start);
			if (d > alloc_max_ns)
				alloc_max_ns = d;
			alloc_sum_ns += d;
			if (p == NULL)
				break;
			if (n >= maxblocks) {
				do_trace("too many blocks fetched during hot pass"
					     " (heap=%zu, block=%zu, "
					     "got more than %d blocks)",
					     heap_size, block_size, maxblocks);
				goto bad;
			}
			chunks[n].ptr = p;
			breathe(n);
		}
		if (n != nrblocks) {
			do_trace("inconsistent block count fetched during hot pass"
				     " (heap=%zu, block=%zu, "
				     "got %d blocks vs %d during alloc)",
				     heap_size, block_size, n, nrblocks);
			goto bad;
		}
		for (n = 0, free_max_ns = free_sum_ns = 0; n < nrblocks; n++) {
			evl_read_clock(EVL_CLOCK_MONOTONIC, &start);
			ret = evl_free_block(&heap, chunks[n].ptr);
			evl_read_clock(EVL_CLOCK_MONOTONIC, &end);
			if (ret) {
				do_trace("failed to free block %p during hot pass"
					     "(heap=%zu, block=%zu)",
					     chunks[n].ptr, heap_size, block_size);
				goto bad;
			}
			d = diff_ts(&end, &start);
			if (d > free_max_ns)
				free_max_ns = d;
			free_sum_ns += d;
			breathe(n);
		}
	}

	alloc_avg_ns = alloc_sum_ns / nrblocks;
	free_avg_ns = free_sum_ns / nrblocks;

	if ((flags & MEMCHECK_ZEROOVRD) && heap_size != user_size) {
		do_trace("unexpected overhead reported");
		goto bad;
	}

	if (evl_heap_used(&heap) > 0) {
		do_trace("memory leakage reported: %zu bytes missing",
			evl_heap_used(&heap));
		goto bad;
	}

	/*
	 * Don't report stats when running a pattern check, timings
	 * are affected.
	 */
do_stats:
	breathe(0);
	ret = 0;
	if (!(flags & MEMCHECK_PATTERN)) {
		st = malloc(sizeof(*st));
		if (st == NULL) {
			do_warn("failed allocating memory");
			ret = -ENOMEM;
			goto oom;
		}
		st->heap_size = heap_size;
		st->user_size = user_size;
		st->block_size = block_size;
		st->nrblocks = nrblocks;
		st->alloc_avg_ns = alloc_avg_ns;
		st->alloc_max_ns = alloc_max_ns;
		st->free_avg_ns = free_avg_ns;
		st->free_max_ns = free_max_ns;
		st->largest_free = largest_free;
		st->maximum_free = maximum_free;
		st->flags = flags;
		memcheck_log_stat(st);
	}

done:
	free(chunks);
no_chunks:
	evl_destroy_heap(&heap);
out:
	if (ret)
		do_trace("** FAILED(overhead %s, %sshuffle, %scheck, %shot): heapsz=%zuk, "
			     "blocksz=%zu, overhead=%zu (%.1f%%)",
			     flags & MEMCHECK_ZEROOVRD ? "disallowed" : "allowed",
			     flags & MEMCHECK_SHUFFLE ? "" : "no ",
			     flags & MEMCHECK_PATTERN ? "" : "no ",
			     flags & MEMCHECK_HOT ? "" : "no ",
			     heap_size / 1024, block_size,
			     arena_size - heap_size,
			     (arena_size * 100.0 / heap_size) - 100.0);
oom:
	free(mem);

	param.sched_priority = 0;
	pthread_setschedparam(pthread_self(), SCHED_OTHER, &param);

	return ret;
bad:
	ret = -EPROTO;
	goto done;
}

static void usage(void)
{
        fprintf(stderr, "usage: heap-torture [options]:\n");
        fprintf(stderr, "-s --sequential-test-size    heap size for sequential access tests\n");
        fprintf(stderr, "-p --pattern-test-size       heap size for pattern tests\n");
        fprintf(stderr, "-c --pattern-check-rounds    number of pattern tests\n");
        fprintf(stderr, "-r --random-check-rounds     number of random allocation tests\n");
        fprintf(stderr, "-v --verbose                 turn on verbosity\n");
}

#define short_optlist "s:p:c:r:v"

static const struct option options[] = {
	{
		.name = "sequential-test-size",
		.has_arg = required_argument,
		.val = 's',
	},
	{
		.name = "pattern-test-size",
		.has_arg = required_argument,
		.val = 'p',
	},
	{
		.name = "pattern-check-rounds",
		.has_arg = required_argument,
		.val = 'c',
	},
	{
		.name = "random-check-rounds",
		.has_arg = required_argument,
		.val = 'r',
	},
	{
		.name = "verbose",
		.has_arg = no_argument,
		.val = 'v',
	},
	{ /* Sentinel */ }
};

int main(int argc, char *argv[])
{
	size_t heap_size, block_size;
	cpu_set_t affinity;
	unsigned long seed;
	int ret, runs, c;
	time_t now;
	void *p;

	for (;;) {
		c = getopt_long(argc, argv, short_optlist, options, NULL);
		if (c == EOF)
			break;

		switch (c) {
		case 0:
			break;
		case 's':
			seq_max_heap_size = atoi(optarg);
			if (seq_max_heap_size <= 0)
				error(1, EINVAL, "invalid heap size for sequential test "
					"(<= 0)");
			break;
		case 'p':
			pattern_heap_size = atoi(optarg);
			if (pattern_heap_size <= 0)
				error(1, EINVAL, "invalid heap size for pattern test "
					"(<= 0)");
			break;
		case 'c':
			pattern_rounds = atoi(optarg);
			if (pattern_rounds <= 0)
				error(1, EINVAL, "invalid round count for pattern test "
					"(<= 0)");
			break;
		case 'r':
			random_rounds = atoi(optarg);
			if (random_rounds <= 0)
				error(1, EINVAL, "invalid round count for random test "
					"(<= 0)");
			break;
		case 'v':
			verbose = 1;
			break;
		case '?':
		default:
			usage();
			return 1;
		}
	}

	if (optind < argc) {
		usage();
		return 1;
	}

	/* Populate the malloc arena early. */
	p = malloc(2 * 1024 * 1024);
	free(p);

	now = time(NULL);
	seed = (unsigned long)now * getpid();
	srandom(seed);

	do_trace("== heap-torture started at %s", ctime(&now));
	do_trace("     seq_heap_size=%zuk", seq_max_heap_size / 1024);
	do_trace("     random_alloc_rounds=%d", random_rounds);
	do_trace("     pattern_heap_size=%zuk", pattern_heap_size / 1024);
	do_trace("     pattern_check_rounds=%d", pattern_rounds);

	CPU_ZERO(&affinity);
	CPU_SET(0, &affinity);
	ret = sched_setaffinity(0, sizeof(affinity), &affinity);
	if (ret) {
		do_warn("failed setting CPU affinity");
		return ret;
	}

	/*
	 * Create a series of heaps of increasing size, allocating
	 * then freeing all blocks sequentially from them, ^2 block
	 * sizes up to half of the heap size. Test multiple patterns:
	 *
	 * - alloc -> free_in_alloc_order
	 * - alloc -> free_in_alloc_order -> (re)alloc
	 * - alloc -> free_in_random_order
	 * - alloc -> free_in_random_order -> (re)alloc
	 */
	for (heap_size = seq_min_heap_size;
	     heap_size < seq_max_heap_size; heap_size <<= 1) {
		for (block_size = 16;
		     block_size < heap_size / 2; block_size <<= 1) {
			ret = test_seq(heap_size, block_size,
				MEMCHECK_ZEROOVRD);
			if (ret) {
				do_trace("failed with %zuk heap, "
					     "%zu-byte block (pow2)",
					     heap_size / 1024, block_size);
				return -ret;
			}
		}
		for (block_size = 16;
		     block_size < heap_size / 2; block_size <<= 1) {
			ret = test_seq(heap_size, block_size,
				MEMCHECK_ZEROOVRD|MEMCHECK_HOT);
			if (ret) {
				do_trace("failed with %zuk heap, "
					     "%zu-byte block (pow2, hot)",
					     heap_size / 1024, block_size);
				return -ret;
			}
		}
		for (block_size = 16;
		     block_size < heap_size / 2; block_size <<= 1) {
			ret = test_seq(heap_size, block_size,
				MEMCHECK_ZEROOVRD|MEMCHECK_SHUFFLE);
			if (ret) {
				do_trace("failed with %zuk heap, "
					     "%zu-byte block (pow2, shuffle)",
					     heap_size / 1024, block_size);
				return -ret;
			}
		}
		for (block_size = 16;
		     block_size < heap_size / 2; block_size <<= 1) {
			ret = test_seq(heap_size, block_size,
			       MEMCHECK_ZEROOVRD|MEMCHECK_HOT|MEMCHECK_SHUFFLE);
			if (ret) {
				do_trace("failed with %zuk heap, "
					     "%zu-byte block (pow2, shuffle, hot)",
					     heap_size / 1024, block_size);
				return -ret;
			}
		}
	}

	ret = dump_stats("SEQUENTIAL ALLOC->FREE, ^2 BLOCK SIZES");
	if (ret)
		return ret;

	/*
	 * Create a series of heaps of increasing size, allocating
	 * then freeing all blocks sequentially from them, random
	 * block sizes. Test multiple patterns as previously with ^2
	 * block sizes.
	 */
	for (heap_size = seq_min_heap_size;
	     heap_size < seq_max_heap_size; heap_size <<= 1) {
		for (runs = 0; runs < random_rounds; runs++) {
			block_size = (random() % (heap_size / 2)) ?: 1;
			ret = test_seq(heap_size, block_size, 0);
			if (ret) {
				do_trace("failed with %zuk heap, "
					     "%zu-byte block (random)",
					     heap_size / 1024, block_size);
				return -ret;
			}
		}
	}

	for (heap_size = seq_min_heap_size;
	     heap_size < seq_max_heap_size; heap_size <<= 1) {
		for (runs = 0; runs < random_rounds; runs++) {
			block_size = (random() % (heap_size / 2)) ?: 1;
			ret = test_seq(heap_size, block_size,
				MEMCHECK_HOT);
			if (ret) {
				do_trace("failed with %zuk heap, "
					     "%zu-byte block (random, hot)",
					     heap_size / 1024, block_size);
				return -ret;
			}
		}
	}

	for (heap_size = seq_min_heap_size;
	     heap_size < seq_max_heap_size; heap_size <<= 1) {
		for (runs = 0; runs < random_rounds; runs++) {
			block_size = (random() % (heap_size / 2)) ?: 1;
			ret = test_seq(heap_size, block_size,
				       MEMCHECK_SHUFFLE);
			if (ret) {
				do_trace("failed with %zuk heap, "
					     "%zu-byte block (random, shuffle)",
					     heap_size / 1024, block_size);
				return ret;
			}
		}
	}

	for (heap_size = seq_min_heap_size;
	     heap_size < seq_max_heap_size; heap_size <<= 1) {
		for (runs = 0; runs < random_rounds; runs++) {
			block_size = (random() % (heap_size / 2)) ?: 1;
			ret = test_seq(heap_size, block_size,
			       MEMCHECK_HOT|MEMCHECK_SHUFFLE);
			if (ret) {
				do_trace("failed with %zuk heap, "
					     "%zu-byte block (random, shuffle, hot)",
					     heap_size / 1024, block_size);
				return ret;
			}
		}
	}

	ret = dump_stats("SEQUENTIAL ALLOC->FREE, RANDOM BLOCK SIZES");
	if (ret)
		return ret;

	do_trace("\n(running the pattern check test"
		     " -- this may take some time)");

	for (runs = 0; runs < pattern_rounds; runs++) {
		block_size = (random() % (pattern_heap_size / 2)) ?: 1;
		ret = test_seq(pattern_heap_size, block_size,
			       MEMCHECK_SHUFFLE|MEMCHECK_PATTERN);
		if (ret) {
			do_trace("failed with %zuk heap, "
				     "%zu-byte block (random, shuffle, check)",
				     pattern_heap_size / 1024, block_size);
			return ret;
		}
	}

	now = time(NULL);
	do_trace("\n== heap-torture finished at %s", ctime(&now));

	return ret;
}
