/*
 * SPDX-License-Identifier: MIT
 *
 * Derived from Xenomai heapmem support (https://xenomai.org/)
 * Copyright (C) 2019 Philippe Gerum <rpm@xenomai.org>
 * Relicensed by its author from LGPL 2.1 to MIT.
 */

#ifndef _EVL_HEAP_H
#define _EVL_HEAP_H

#include <sys/types.h>
#include <stdint.h>
#include <limits.h>
#include <evl/list.h>
#include <evl/mutex.h>

#define EVL_HEAP_PAGE_SHIFT	9 /* 2^9 => 512 bytes */
#define EVL_HEAP_PAGE_SIZE	(1UL << EVL_HEAP_PAGE_SHIFT)
#define EVL_HEAP_PAGE_MASK	(~(EVL_HEAP_PAGE_SIZE - 1))
#define EVL_HEAP_MIN_LOG2	4 /* 16 bytes */
/*
 * Use bucketed memory for sizes between 2^EVL_HEAP_MIN_LOG2 and
 * 2^(EVL_HEAP_PAGE_SHIFT-1).
 */
#define EVL_HEAP_MAX_BUCKETS	(EVL_HEAP_PAGE_SHIFT - EVL_HEAP_MIN_LOG2)
#define EVL_HEAP_MIN_ALIGN	(1U << EVL_HEAP_MIN_LOG2)
/* Max size of an extent (4Gb - EVL_HEAP_PAGE_SIZE). */
#define EVL_HEAP_MAX_EXTSZ	(4294967295U - EVL_HEAP_PAGE_SIZE + 1)
/* Bits we need for encoding a page # */
#define EVL_HEAP_PGENT_BITS      (32 - EVL_HEAP_PAGE_SHIFT)

/* Each page is represented by a page map entry. */
#define EVL_HEAP_PGMAP_BYTES	sizeof(struct evl_heap_pgentry)

struct avlh {
	unsigned int flags : 28;
	int type : 2;
	int balance : 2;
	struct avlh *link[3];
};

struct avl {
	struct avlh anchor;
	struct avlh *end[3];
	unsigned int count;
	unsigned int height;
};

struct evl_heap_pgentry {
	/* Linkage in bucket list. */
	unsigned int prev : EVL_HEAP_PGENT_BITS;
	unsigned int next : EVL_HEAP_PGENT_BITS;
	/*  page_list or log2. */
	unsigned int type : 6;
	/*
	 * We hold either a spatial map of busy blocks within the page
	 * for bucketed memory (up to 32 blocks per page), or the
	 * overall size of the multi-page block if entry.type ==
	 * page_list.
	 */
	union {
		uint32_t map;
		uint32_t bsize;
	};
};

/*
 * A range descriptor is stored at the beginning of the first page of
 * a range of free pages. mem_range.size is nrpages *
 * EVL_HEAP_PAGE_SIZE. Ranges are indexed by address and size in AVL
 * trees.
 */
struct evl_mem_range {
	struct avlh addr_node;
	struct avlh size_node;
	size_t size;
};

struct evl_heap_extent {
	struct list_head next;
	void *membase;		/* Base of page array */
	void *memlim;		/* Limit of page array */
	struct avl addr_tree;
	struct avl size_tree;
	struct evl_heap_pgentry pagemap[0]; /* Start of page entries[] */
};

struct evl_heap {
	struct evl_mutex lock;
	struct list_head extents;
	size_t raw_size;
	size_t usable_size;
	size_t used_size;
	/* Heads of page lists for log2-sized blocks. */
	uint32_t buckets[EVL_HEAP_MAX_BUCKETS];
};

#define __EVL_HEAP_MAP_SIZE(__nrpages)					\
	((__nrpages) * EVL_HEAP_PGMAP_BYTES)

#define __EVL_HEAP_RAW_SIZE(__size)					\
	(__size +							\
	 __align_to(sizeof(struct evl_heap_extent) +			\
		    __EVL_HEAP_MAP_SIZE((__size) >> EVL_HEAP_PAGE_SHIFT),\
		    EVL_HEAP_MIN_ALIGN))

/*
 * Calculate the size of the memory area needed to contain a heap of
 * __user_size bytes, including our meta-data for managing it.  Usable
 * at build time if __user_size is constant.
 */
#define EVL_HEAP_RAW_SIZE(__user_size)	\
	__EVL_HEAP_RAW_SIZE(__align_to(__user_size, EVL_HEAP_PAGE_SIZE))

#ifdef __cplusplus
extern "C" {
#endif

int evl_init_heap(struct evl_heap *heap,
		void *mem, size_t size);

int evl_extend_heap(struct evl_heap *heap,
		   void *mem, size_t size);

void evl_destroy_heap(struct evl_heap *heap);

void *evl_alloc_block(struct evl_heap *heap,
		size_t size) __alloc_size(2);

int evl_free_block(struct evl_heap *heap,
		void *block);

ssize_t evl_check_block(struct evl_heap *heap,
			void *block);

static inline
size_t evl_heap_raw_size(const struct evl_heap *heap)
{
	return heap->raw_size;
}

static inline
size_t evl_heap_size(const struct evl_heap *heap)
{
	return heap->usable_size;
}

static inline
size_t evl_heap_used(const struct evl_heap *heap)
{
	return heap->used_size;
}

#ifdef __cplusplus
}
#endif

#endif /* _EVL_HEAP_H */
