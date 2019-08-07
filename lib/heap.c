/*
 * SPDX-License-Identifier: MIT
 *
 * Derived from Xenomai heapmem support (https://xenomai.org/)
 * Copyright (C) 2018 Philippe Gerum <rpm@xenomai.org>
 * Relicensed by its author from LGPL 2.1 to MIT.
 * AVL support Copyright (c) 2015 Gilles Chanteperdrix
 */

#ifdef __OPTIMIZE__
#define NDEBUG 1
#endif

#include <sys/types.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <evl/atomic.h>
#include <evl/heap.h>
#include <evl/mutex.h>
#include <evl/clock.h>

static atomic_t heap_serial;

typedef int avlh_cmp_t(const struct avlh *const,
		const struct avlh *const);

typedef struct avlh *avl_search_t(const struct avl *,
				const struct avlh *, int *, int);
struct avl_searchops {
	avl_search_t *search;
	avlh_cmp_t *cmp;
};

#define AVL_LEFT	     -1
#define AVL_UP		      0
#define AVL_RIGHT	      1
#define avl_opposite(type)   (-(type))
#define avl_type2index(type) ((type)+1)

#define AVL_THR_LEFT  (1 << avl_type2index(AVL_LEFT))
#define AVL_THR_RIGHT (1 << avl_type2index(AVL_RIGHT))

#define avlh_link(avl, holder, dir) ((holder)->link[avl_type2index(dir)])
#define avl_end(avl, dir) ((avl)->end[avl_type2index(dir)])

#define avl_count(avl)	  ((avl)->count)
#define avl_height(avl)	  ((avl)->height)
#define avl_anchor(avl)	  (&(avl)->anchor)

#define avlh_up(avl, holder)	avlh_link((avl), (holder), AVL_UP)
#define avlh_left(avl, holder)	avlh_link((avl), (holder), AVL_LEFT)
#define avlh_right(avl, holder)	avlh_link((avl), (holder), AVL_RIGHT)

#define avl_top(avl)	  (avlh_right(avl, avl_anchor(avl)))
#define avl_head(avl)	  (avl_end((avl), AVL_LEFT))
#define avl_tail(avl)	  (avl_end((avl), AVL_RIGHT))

#define avlh_thr_tst(avl, holder, side)	  (avlh_link(avl, holder, side) == NULL)
#define avlh_child(avl, holder, side)     (avlh_link((avl),(holder),(side)))
#define avlh_has_child(avl, holder, side) (!avlh_thr_tst(avl, holder, side))

/*
 * From "Bit twiddling hacks", returns v < 0 ? -1 : (v > 0 ? 1 : 0)
 */
#define avl_sign(v)				\
	({					\
		typeof(v) _v = (v);		\
		((_v) > 0) - ((_v) < 0);	\
	})
/*
 * Variation on the same theme.
 */
#define avl_cmp_sign(l, r)			\
	({					\
		typeof(l) _l = (l);		\
		typeof(r) _r = (r);		\
		(_l > _r) - (_l < _r);		\
	})

#define DECLARE_AVL_SEARCH(__search_fn, __cmp)				\
	struct avlh *__search_fn(const struct avl *const avl,		\
				 const struct avlh *const node,		\
				 int *const pdelta, int dir)		\
	{								\
		int delta = AVL_RIGHT;					\
		struct avlh *holder = avl_top(avl), *next;		\
									\
		if (holder == NULL)					\
			goto done;					\
									\
		for (;;) {						\
			delta = __cmp(node, holder);			\
			/*						\
			 * Handle duplicates keys here, according to	\
			 * "dir", if dir is:				\
			 * - AVL_LEFT, the leftmost node is returned,	\
			 * - AVL_RIGHT, the rightmost node is returned,	\
			 * - 0, the first match is returned.		\
			 */						\
			if (!(delta ?: dir))				\
				break;					\
			next = avlh_child(avl, holder, delta ?: dir);	\
			if (next == NULL)				\
				break;					\
			holder = next;					\
		}							\
									\
	  done:								\
		*pdelta = delta;					\
		return holder;						\
	}

static inline unsigned int avlh_thr(const struct avl *const avl,
				    const struct avlh *h)
{
	unsigned int result = 0;

	if (avlh_link(avl, h, AVL_LEFT) == NULL)
		result |= AVL_THR_LEFT;
	if (avlh_link(avl, h, AVL_RIGHT) == NULL)
		result |= AVL_THR_RIGHT;

	return result;
}

static inline void
avlh_set_link(struct avl *const avl, struct avlh *lhs, int dir, struct avlh *rhs)
{
	avlh_link(avl, lhs, dir) = rhs;
}

static inline void
avlh_set_parent_link(struct avl *const avl,
		     struct avlh *lhs, struct avlh *rhs)
{
	avlh_set_link(avl, avlh_up(avl, lhs), lhs->type, rhs);
}

static inline void
avlh_set_left(struct avl *const avl, struct avlh *lhs,
	      struct avlh *rhs)
{
	avlh_set_link(avl, lhs, AVL_LEFT, rhs);
}

static inline void
avlh_set_up(struct avl *const avl, struct avlh *lhs,
	    struct avlh *rhs)
{
	avlh_set_link(avl, lhs, AVL_UP, rhs);
}

static inline void
avlh_set_right(struct avl *const avl, struct avlh* lhs,
	       struct avlh *rhs)
{
	avlh_set_link(avl, lhs, AVL_RIGHT, rhs);
}

static inline void
avl_set_end(struct avl *const avl, int dir, struct avlh *holder)
{
	avl_end(avl, dir) = holder;
}

static inline void avl_set_top(struct avl *const avl,
			       struct avlh *holder)
{
	avlh_set_link(avl, avl_anchor(avl), AVL_RIGHT, holder);
}

static inline void avl_set_head(struct avl *const avl,
				struct avlh *holder)
{
	avl_set_end(avl, AVL_LEFT, holder);
}

static inline void avl_set_tail(struct avl *const avl,
				struct avlh *holder)
{
	avl_set_end(avl, AVL_RIGHT, holder);
}

static inline void
avlh_link_child(struct avl *const avl,
		struct avlh *const oldh,
		struct avlh* const newh, const int side)
{
	struct avlh *const child = avlh_link(avl, oldh, side);

	avlh_set_link(avl, newh, side, child);
	if (avlh_has_child(avl, oldh, side))
		avlh_set_up(avl, child, newh);
}

static inline void
avlh_replace(struct avl *const avl,
	     struct avlh *const oldh,
	     struct avlh *const newh)
{
	newh->type = oldh->type;
	/* Do not update the balance, this has to be done by the caller. */

	avlh_set_up(avl, newh, avlh_up(avl, oldh));
	avlh_set_parent_link(avl, oldh, newh);

	avlh_link_child(avl, oldh, newh, AVL_LEFT);
	avlh_link_child(avl, oldh, newh, AVL_RIGHT);
}

static struct avlh *avl_inorder(const struct avl * const avl,
				struct avlh * holder,
				const int dir)
{
	struct avlh *next;

	/*
	 * If the current node is not right threaded, then go down
	 * left, starting from its right child.
	 */
	if (avlh_has_child(avl, holder, dir)) {
		const int opp_dir = avl_opposite(dir);
		holder = avlh_link(avl, holder, dir);
		while ((next = avlh_child(avl, holder, opp_dir)))
			holder = next;
		next = holder;
	} else {
		for (;;) {
			next = avlh_up(avl, holder);
			if (next == avl_anchor(avl))
				return NULL;
			if (holder->type != dir)
				break;
			holder = next;
		}
	}

	return next;
}

static inline
struct avlh *avl_prev(const struct avl *const avl,
		struct avlh *const holder)
{
	return avl_inorder(avl, holder, AVL_LEFT);
}

static inline struct avlh *
avl_search_nearest(const struct avl *const avl,
		const struct avlh *node, int dir,
		const struct avl_searchops *ops)
{
	struct avlh *holder;
	int delta;

	holder = ops->search(avl, node, &delta, 0);
	if (!holder || delta != dir)
		return holder;

	return avl_inorder(avl, holder, dir);
}

static inline
struct avlh *avl_search_le(const struct avl *const avl,
			const struct avlh *node,
			const struct avl_searchops *ops)
{
	return avl_search_nearest(avl, node, AVL_LEFT, ops);
}

static inline
struct avlh *avl_search_ge(const struct avl *const avl,
			const struct avlh *node,
			const struct avl_searchops *ops)
{
	return avl_search_nearest(avl, node, AVL_RIGHT, ops);
}

static inline
struct avlh *avl_next(const struct avl *const avl,
		struct avlh *const holder)
{
	return avl_inorder(avl, holder, AVL_RIGHT);
}

static void avl_delete_leaf(struct avl *const avl,
			    struct avlh *const node)
{
	/*
	 * Node has no child at all. It disappears and its father
	 * becomes threaded on the side id was.
	 */
	struct avlh* const new_node = avlh_up(avl, node);
	const int dir = node->type;

	/* Suppress node. */
	avlh_set_link(avl, new_node, dir, avlh_link(avl, node, dir));

	if (node == avl_end(avl, dir))
		avl_set_end(avl, dir, new_node);
}

static struct avlh *avl_delete_1child(struct avl *const avl,
				struct avlh *const node, const int dir)
{
	/*
	 * Node is threaded on one side and has a child on the other
	 * side. In this case, node is replaced by its child.
	 */
	struct avlh* const new_node = avlh_link(avl, node, dir);

	/*
	 * Change links as if new_node was suppressed before calling
	 * avlh_replace.
	 */
	avlh_set_link(avl, node, dir, avlh_link(avl, new_node, dir));
	avlh_replace(avl, node, new_node);

	if (node == avl_end(avl, avl_opposite(dir)))
		avl_set_end(avl, avl_opposite(dir), new_node);
	/* new_node->balance 0, which is correct. */
	return new_node;
}

static void avl_delete(struct avl *const avl, struct avlh *node);

static void avl_delete_2children(struct avl *const avl,
				struct avlh *const node)
{
	const int dir = node->balance ? node->balance : 1;
	struct avlh *const new_node = avl_inorder(avl, node, dir);

	avl_delete(avl, new_node);
	++avl_count(avl);
	avlh_replace(avl, node, new_node);
	new_node->balance = node->balance;
	if (avl_end(avl, dir) == node)
		avl_set_end(avl, dir, new_node);
}

static inline struct avlh *avlh_rotate(struct avl *const avl,
		    struct avlh *const holder, const int dir)
{
	const int opp_dir = avl_opposite(dir);
	struct avlh *const nexttop = avlh_link(avl, holder, opp_dir);
	struct avlh *const subtree = avlh_child(avl, nexttop, dir);

	if (subtree) {
		avlh_set_link(avl, holder, opp_dir, subtree);
		avlh_set_up(avl, subtree, holder);
		subtree->type = opp_dir;
	} else
		avlh_set_link(avl, holder, opp_dir, NULL);

	avlh_set_link(avl, nexttop, dir, holder);
	avlh_set_up(avl, nexttop, avlh_up(avl, holder));
	nexttop->type = holder->type;
	avlh_set_up(avl, holder, nexttop);
	holder->type = dir;

	avlh_set_parent_link(avl, nexttop, nexttop);

	return nexttop;
}

static inline
struct avlh *avlh_dbl_rotate(struct avl *const avl,
			struct avlh *const holder, const int dir)
{
	const int opp = avl_opposite(dir);

	avlh_rotate(avl, avlh_link(avl, holder, opp), opp);
	return avlh_rotate(avl, holder, dir);
}

static struct avlh *
avlh_rebalance(struct avl *const avl, struct avlh *holder,
	const int delta)
{

	int dir = delta;
	struct avlh *const heavy_side = avlh_link(avl, holder, dir);

	if (heavy_side->balance == -delta) {
		/* heavy_side->balance == -delta, double rotation needed. */
		holder = avlh_dbl_rotate(avl, holder, avl_opposite(dir));

		/*
		 * recompute balances, there are three nodes involved, two of
		 * which balances become null.
		 */
		dir = holder->balance ? : AVL_RIGHT;
		avlh_link(avl, holder, dir)->balance = 0;
		avlh_link(avl, holder, avl_opposite(dir))->balance
			= -holder->balance;
		holder->balance = 0;
	} else {
		/*
		 * heavy_side->balance == delta or 0, simple rotation needed.
		 * the case 0 occurs only when deleting, never when inserting.
		 */

		/* heavy_side becomes the new root. */
		avlh_rotate(avl, holder, avl_opposite(dir));

		/* recompute balances. */
		holder->balance -= heavy_side->balance;
		heavy_side->balance -= delta;

		holder = heavy_side;
	}
	return holder;
}

static inline
struct avlh *avlh_balance_add(struct avl *const avl,
			struct avlh *const holder, const int delta)
{
	if (holder->balance == delta)
		/* we need to rebalance the current subtree. */
		return avlh_rebalance(avl, holder, delta);

	/* the current subtree does not need rebalancing */
	holder->balance += delta;
	return holder;
}

static void avl_delete(struct avl *const avl, struct avlh *node)
{
	if (!--avl_count(avl))
		goto delete_last_and_ret;

	switch (avlh_thr(avl, node)) {
	case (AVL_THR_LEFT | AVL_THR_RIGHT):	/* thr is 5 */
		avl_delete_leaf(avl, node);
		break;

	case AVL_THR_LEFT:	/* only AVL_LEFT bit is on, thr is 1. */
		node = avl_delete_1child(avl, node, AVL_RIGHT);
		break;

	case AVL_THR_RIGHT:	/* only AVL_RIGHT bit is on, thr is 4. */
		node = avl_delete_1child(avl, node, AVL_LEFT);
		break;

	case 0:
		avl_delete_2children(avl, node);
		return;
	}

	/* node is the first node which needs to be rebalanced.
	   The tree is rebalanced, and contrarily to what happened for insertion,
	   the rebalancing stops when a node which is NOT balanced is met. */
	while (!node->balance) {
		const int delta = -node->type;
		node = avlh_up(avl, node);
		if (node == avl_anchor(avl))
			goto dec_height_and_ret;
		node = avlh_balance_add(avl, node, delta);
	}

	return;

delete_last_and_ret:
	avl_set_top(avl, NULL);
	avl_set_head(avl, NULL);
	avl_set_tail(avl, NULL);
dec_height_and_ret:
	--avl_height(avl);
}

static inline void
avlh_attach(struct avl *const avl,
	    struct avlh *const parent,
	    struct avlh *const child, const int side)
{
	avlh_set_left(avl, child, NULL);
	avlh_set_right(avl, child, NULL);
	avlh_set_up(avl, child, parent);
	avlh_set_link(avl, parent, side, child);
	child->type = side;
}

static inline void avl_insert_inner(struct avl *const avl,
				struct avlh *parent,
				struct avlh *const node,
				const int side)
{
	avlh_attach(avl, parent ? : avl_anchor(avl), node, side);
	++avl_count(avl);

	if (parent == NULL)
		goto insert_first_and_ret;	/* Get away from fast path */

	if (parent == avl_end(avl, side))
		avl_set_end(avl, side, node);

	parent->balance += side;

	while (parent->balance) {
		const int delta = parent->type;
		parent = avlh_up(avl, parent);
		if (parent == avl_anchor(avl))
			goto inc_height_and_ret;	/* Get away from fast path */
		parent = avlh_balance_add(avl, parent, delta);
	}

	return;

insert_first_and_ret:
	avl_set_head(avl, node);
	avl_set_tail(avl, node);
inc_height_and_ret:
	++avl_height(avl);
}

static void avl_insert(struct avl *const avl,
		struct avlh *const holder,
		const struct avl_searchops *ops)
{
	int delta;
	struct avlh *parent;

	parent = ops->search(avl, holder, &delta, 0);
	assert(delta != 0);	/* Should not be busy. */
	avl_insert_inner(avl, parent, holder, delta);
}

static void avl_insert_back(struct avl *const avl,
			struct avlh *const holder,
			const struct avl_searchops *ops)
{
	int delta;
	struct avlh *parent;

	parent = ops->search(avl, holder, &delta, AVL_RIGHT);

	avl_insert_inner(avl, parent, holder, delta ? : AVL_RIGHT);
}

static void avl_prepend(struct avl *const avl,
			struct avlh *const holder,
			const struct avl_searchops *ops)
{
	struct avlh *const parent = avl_head(avl);
	int type = parent == NULL ? AVL_RIGHT : AVL_LEFT;

	assert(parent == NULL || ops->cmp(holder, parent) < 0);
	avl_insert_inner(avl, parent, holder, type);
}

/*
 * Special case, when we know that replacing a node with another will
 * not change the avl, much faster than remove + add
 */
static void avl_replace(struct avl *avl, struct avlh *oldh,
			struct avlh *newh,
			const struct avl_searchops *ops)
{
	struct avlh *prev __maybe_unused, *next __maybe_unused;

	prev = avl_prev(avl, oldh);
	next = avl_next(avl, oldh);

	assert(!((prev && ops->cmp(newh, prev) < 0)
		|| (next && ops->cmp(newh, next) > 0)));

	avlh_replace(avl, oldh, newh);
	if (oldh == avl_head(avl))
		avl_set_head(avl, newh);
	if (oldh == avl_tail(avl))
		avl_set_tail(avl, newh);
	newh->balance = oldh->balance;
}

static inline void avlh_init(struct avlh *const holder)
{
	holder->balance = 0;
	holder->type = 0;
}

static void avl_init(struct avl *const avl)
{
	avlh_init(avl_anchor(avl));	/* this must be first. */
	avl_height(avl) = 0;
	avl_count(avl) = 0;
	avl_set_top(avl, NULL);
	avl_set_head(avl, NULL);
	avl_set_tail(avl, NULL);
}

enum evl_heap_pgtype {
	page_free =0,
	page_cont =1,
	page_list =2
};

static struct avl_searchops size_search_ops;
static struct avl_searchops addr_search_ops;

static inline uint32_t __attribute__ ((always_inline))
gen_block_mask(int log2size)
{
	return -1U >> (32 - (EVL_HEAP_PAGE_SIZE >> log2size));
}

static inline  __attribute__ ((always_inline))
int addr_to_pagenr(struct evl_heap_extent *ext, void *p)
{
	return ((void *)p - ext->membase) >> EVL_HEAP_PAGE_SHIFT;
}

static inline  __attribute__ ((always_inline))
void *pagenr_to_addr(struct evl_heap_extent *ext, int pg)
{
	return ext->membase + (pg << EVL_HEAP_PAGE_SHIFT);
}

#ifndef __OPTIMIZE__
/*
 * Setting page_cont/page_free in the page map is only required for
 * enabling full checking of the block address in free requests, which
 * may be extremely time-consuming when deallocating huge blocks
 * spanning thousands of pages. We only do such marking when running
 * in full debug mode.
 */
static inline bool
page_is_valid(struct evl_heap_extent *ext, int pg)
{
	switch (ext->pagemap[pg].type) {
	case page_free:
	case page_cont:
		return false;
	case page_list:
	default:
		return true;
	}
}

static void mark_pages(struct evl_heap_extent *ext,
		       int pg, int nrpages,
		       enum evl_heap_pgtype type)
{
	while (nrpages-- > 0)
		ext->pagemap[pg].type = type;
}

#else

static inline bool
page_is_valid(struct evl_heap_extent *ext, int pg)
{
	return true;
}

static void mark_pages(struct evl_heap_extent *ext,
		       int pg, int nrpages,
		       enum evl_heap_pgtype type)
{ }

#endif

ssize_t evl_check_block(struct evl_heap *heap, void *block)
{
	unsigned long pg, pgoff, boff;
	struct evl_heap_extent *ext;
	ssize_t ret = -EINVAL;
	size_t bsize;

	evl_lock_mutex(&heap->lock);

	/*
	 * Find the extent the checked block is originating from.
	 */
	list_for_each_entry(ext, &heap->extents, next) {
		if (block >= ext->membase &&
		    block < ext->memlim)
			goto found;
	}
	goto out;
found:
	/* Calculate the page number from the block address. */
	pgoff = block - ext->membase;
	pg = pgoff >> EVL_HEAP_PAGE_SHIFT;
	if (page_is_valid(ext, pg)) {
		if (ext->pagemap[pg].type == page_list)
			bsize = ext->pagemap[pg].bsize;
		else {
			bsize = (1 << ext->pagemap[pg].type);
			boff = pgoff & ~EVL_HEAP_PAGE_MASK;
			if ((boff & (bsize - 1)) != 0) /* Not at block start? */
				goto out;
		}
		ret = (ssize_t)bsize;
	}
out:
	evl_unlock_mutex(&heap->lock);

	return ret;
}

static inline struct evl_mem_range *
find_suitable_range(struct evl_heap_extent *ext, size_t size)
{
	struct evl_mem_range lookup;
	struct avlh *node;

	lookup.size = size;
	node = avl_search_ge(&ext->size_tree, &lookup.size_node,
			     &size_search_ops);
	if (node == NULL)
		return NULL;

	return container_of(node, struct evl_mem_range, size_node);
}

static int reserve_page_range(struct evl_heap_extent *ext, size_t size)
{
	struct evl_mem_range *new, *splitr;

	new = find_suitable_range(ext, size);
	if (new == NULL)
		return -1;

	avl_delete(&ext->size_tree, &new->size_node);
	if (new->size == size) {
		avl_delete(&ext->addr_tree, &new->addr_node);
		return addr_to_pagenr(ext, new);
	}

	/*
	 * The free range fetched is larger than what we need: split
	 * it in two, the upper part goes to the user, the lower part
	 * is returned to the free list, which makes reindexing by
	 * address pointless.
	 */
	splitr = new;
	splitr->size -= size;
	new = (struct evl_mem_range *)((void *)new + splitr->size);
	avlh_init(&splitr->size_node);
	avl_insert_back(&ext->size_tree, &splitr->size_node,
			&size_search_ops);

	return addr_to_pagenr(ext, new);
}

static inline struct evl_mem_range *
find_left_neighbour(struct evl_heap_extent *ext, struct evl_mem_range *r)
{
	struct avlh *node;

	node = avl_search_le(&ext->addr_tree, &r->addr_node,
			     &addr_search_ops);
	if (node == NULL)
		return NULL;

	return container_of(node, struct evl_mem_range, addr_node);
}

static inline struct evl_mem_range *
find_right_neighbour(struct evl_heap_extent *ext, struct evl_mem_range *r)
{
	struct avlh *node;

	node = avl_search_ge(&ext->addr_tree, &r->addr_node,
			     &addr_search_ops);
	if (node == NULL)
		return NULL;

	return container_of(node, struct evl_mem_range, addr_node);
}

static inline struct evl_mem_range *
find_next_neighbour(struct evl_heap_extent *ext, struct evl_mem_range *r)
{
	struct avlh *node;

	node = avl_next(&ext->addr_tree, &r->addr_node);
	if (node == NULL)
		return NULL;

	return container_of(node, struct evl_mem_range, addr_node);
}

static inline bool
ranges_mergeable(struct evl_mem_range *left, struct evl_mem_range *right)
{
	return (void *)left + left->size == (void *)right;
}

static void release_page_range(struct evl_heap_extent *ext,
			       void *page, size_t size)
{
	struct evl_mem_range *freed = page, *left, *right;
	bool addr_linked = false;

	freed->size = size;

	left = find_left_neighbour(ext, freed);
	if (left && ranges_mergeable(left, freed)) {
		avl_delete(&ext->size_tree, &left->size_node);
		left->size += freed->size;
		freed = left;
		addr_linked = true;
		right = find_next_neighbour(ext, freed);
	} else
		right = find_right_neighbour(ext, freed);

	if (right && ranges_mergeable(freed, right)) {
		avl_delete(&ext->size_tree, &right->size_node);
		freed->size += right->size;
		if (addr_linked)
			avl_delete(&ext->addr_tree, &right->addr_node);
		else
			avl_replace(&ext->addr_tree, &right->addr_node,
				    &freed->addr_node, &addr_search_ops);
	} else if (!addr_linked) {
		avlh_init(&freed->addr_node);
		if (left)
			avl_insert(&ext->addr_tree, &freed->addr_node,
				   &addr_search_ops);
		else
			avl_prepend(&ext->addr_tree, &freed->addr_node,
				    &addr_search_ops);
	}

	avlh_init(&freed->size_node);
	avl_insert_back(&ext->size_tree, &freed->size_node,
			&size_search_ops);
	mark_pages(ext, addr_to_pagenr(ext, page),
		   size >> EVL_HEAP_PAGE_SHIFT, page_free);
}

static void add_page_front(struct evl_heap *heap,
			   struct evl_heap_extent *ext,
			   int pg, int log2size)
{
	struct evl_heap_pgentry *new, *head, *next;
	int ilog;

	/* Insert page at front of the per-bucket page list. */

	ilog = log2size - EVL_HEAP_MIN_LOG2;
	new = &ext->pagemap[pg];
	if (heap->buckets[ilog] == -1U) {
		heap->buckets[ilog] = pg;
		new->prev = new->next = pg;
	} else {
		head = &ext->pagemap[heap->buckets[ilog]];
		new->prev = heap->buckets[ilog];
		new->next = head->next;
		next = &ext->pagemap[new->next];
		next->prev = pg;
		head->next = pg;
		heap->buckets[ilog] = pg;
	}
}

static void remove_page(struct evl_heap *heap,
			struct evl_heap_extent *ext,
			int pg, int log2size)
{
	struct evl_heap_pgentry *old, *prev, *next;
	int ilog = log2size - EVL_HEAP_MIN_LOG2;

	/* Remove page from the per-bucket page list. */

	old = &ext->pagemap[pg];
	if (pg == old->next)
		heap->buckets[ilog] = -1U;
	else {
		if (pg == heap->buckets[ilog])
			heap->buckets[ilog] = old->next;
		prev = &ext->pagemap[old->prev];
		prev->next = old->next;
		next = &ext->pagemap[old->next];
		next->prev = old->prev;
	}
}

static void move_page_front(struct evl_heap *heap,
			    struct evl_heap_extent *ext,
			    int pg, int log2size)
{
	int ilog = log2size - EVL_HEAP_MIN_LOG2;

	/* Move page at front of the per-bucket page list. */

	if (heap->buckets[ilog] == pg)
		return;	 /* Already at front, no move. */

	remove_page(heap, ext, pg, log2size);
	add_page_front(heap, ext, pg, log2size);
}

static void move_page_back(struct evl_heap *heap,
			   struct evl_heap_extent *ext,
			   int pg, int log2size)
{
	struct evl_heap_pgentry *old, *last, *head, *next;
	int ilog;

	/* Move page at end of the per-bucket page list. */

	old = &ext->pagemap[pg];
	if (pg == old->next) /* Singleton, no move. */
		return;

	remove_page(heap, ext, pg, log2size);

	ilog = log2size - EVL_HEAP_MIN_LOG2;
	head = &ext->pagemap[heap->buckets[ilog]];
	last = &ext->pagemap[head->prev];
	old->prev = head->prev;
	old->next = last->next;
	next = &ext->pagemap[old->next];
	next->prev = pg;
	last->next = pg;
}

static void *add_free_range(struct evl_heap *heap, size_t bsize, int log2size)
{
	struct evl_heap_extent *ext;
	size_t rsize;
	int pg;

	/*
	 * Scanning each extent, search for a range of contiguous
	 * pages in the extent. The range must be at least @bsize
	 * long. @pg is the heading page number on success.
	 */
	rsize =__align_to(bsize, EVL_HEAP_PAGE_SIZE);
	list_for_each_entry(ext, &heap->extents, next) {
		pg = reserve_page_range(ext, rsize);
		if (pg >= 0)
			goto found;
	}

	return NULL;

found:
	/*
	 * Update the page entry.  If @log2size is non-zero
	 * (i.e. bsize < EVL_HEAP_PAGE_SIZE), bsize is (1 << log2Size)
	 * between 2^EVL_HEAP_MIN_LOG2 and 2^(EVL_HEAP_PAGE_SHIFT - 1).
	 * Save the log2 power into entry.type, then update the
	 * per-page allocation bitmap to reserve the first block.
	 *
	 * Otherwise, we have a larger block which may span multiple
	 * pages: set entry.type to page_list, indicating the start of
	 * the page range, and entry.bsize to the overall block size.
	 */
	if (log2size) {
		ext->pagemap[pg].type = log2size;
		/*
		 * Mark the first object slot (#0) as busy, along with
		 * the leftmost bits we won't use for this log2 size.
		 */
		ext->pagemap[pg].map = ~gen_block_mask(log2size) | 1;
		/*
		 * Insert the new page at front of the per-bucket page
		 * list, enforcing the assumption that pages with free
		 * space live close to the head of this list.
		 */
		add_page_front(heap, ext, pg, log2size);
	} else {
		ext->pagemap[pg].type = page_list;
		ext->pagemap[pg].bsize = (uint32_t)bsize;
		mark_pages(ext, pg + 1,
			   (bsize >> EVL_HEAP_PAGE_SHIFT) - 1, page_cont);
	}

	heap->used_size += bsize;

	return pagenr_to_addr(ext, pg);
}

void *evl_alloc_block(struct evl_heap *heap, size_t size)
{
	struct evl_heap_extent *ext;
	int log2size, ilog, pg, b;
	uint32_t bmask;
	size_t bsize;
	void *block;

	if (size == 0)
		return NULL;

	if (size < EVL_HEAP_MIN_ALIGN) {
		bsize = size = EVL_HEAP_MIN_ALIGN;
		log2size = EVL_HEAP_MIN_LOG2;
	} else {
		log2size = sizeof(size) * CHAR_BIT - 1 -
			__lzcount(size);
		if (log2size < EVL_HEAP_PAGE_SHIFT) {
			if (size & (size - 1))
				log2size++;
			bsize = 1 << log2size;
		} else
			bsize = __align_to(size, EVL_HEAP_PAGE_SIZE);
	}

	/*
	 * Allocate entire pages directly from the pool whenever the
	 * block is larger or equal to EVL_HEAP_PAGE_SIZE.  Otherwise,
	 * use bucketed memory.
	 *
	 * NOTE: Fully busy pages from bucketed memory are moved back
	 * at the end of the per-bucket page list, so that we may
	 * always assume that either the heading page has some room
	 * available, or no room is available from any page linked to
	 * this list, in which case we should immediately add a fresh
	 * page.
	 */
	if (bsize < EVL_HEAP_PAGE_SIZE) {
		ilog = log2size - EVL_HEAP_MIN_LOG2;
		assert(ilog >= 0 && ilog < EVL_HEAP_MAX_BUCKETS);

		evl_lock_mutex(&heap->lock);

		list_for_each_entry(ext, &heap->extents, next) {
			pg = heap->buckets[ilog];
			if (pg < 0) /* Empty page list? */
				continue;

			/*
			 * Find a block in the heading page. If there
			 * is none, there won't be any down the list:
			 * add a new page right away.
			 */
			bmask = ext->pagemap[pg].map;
			if (bmask == -1U)
				break;
			b = __tzcount(~bmask);

			/*
			 * Got one block from the heading per-bucket
			 * page, tag it as busy in the per-page
			 * allocation map.
			 */
			ext->pagemap[pg].map |= (1U << b);
			heap->used_size += bsize;
			block = ext->membase +
				(pg << EVL_HEAP_PAGE_SHIFT) +
				(b << log2size);
			if (ext->pagemap[pg].map == -1U)
				move_page_back(heap, ext, pg, log2size);
			goto out;
		}

		/* No free block in bucketed memory, add one page. */
		block = add_free_range(heap, bsize, log2size);
	} else {
		evl_lock_mutex(&heap->lock);
		/* Add a range of contiguous free pages. */
		block = add_free_range(heap, bsize, 0);
	}
out:
	evl_unlock_mutex(&heap->lock);

	return block;
}

int evl_free_block(struct evl_heap *heap, void *block)
{
	int log2size, ret = 0, pg, n;
	struct evl_heap_extent *ext;
	unsigned long pgoff, boff;
	uint32_t oldmap;
	size_t bsize;

	evl_lock_mutex(&heap->lock);

	/*
	 * Find the extent from which the returned block is
	 * originating from.
	 */
	list_for_each_entry(ext, &heap->extents, next) {
		if (block >= ext->membase && block < ext->memlim)
			goto found;
	}

	goto bad;
found:
	/* Compute the heading page number in the page map. */
	pgoff = block - ext->membase;
	pg = pgoff >> EVL_HEAP_PAGE_SHIFT;
	if (!page_is_valid(ext, pg))
		goto bad;

	switch (ext->pagemap[pg].type) {
	case page_list:
		bsize = ext->pagemap[pg].bsize;
		assert((bsize & (EVL_HEAP_PAGE_SIZE - 1)) == 0);
		release_page_range(ext, pagenr_to_addr(ext, pg), bsize);
		break;

	default:
		log2size = ext->pagemap[pg].type;
		bsize = (1 << log2size);
		assert(bsize < EVL_HEAP_PAGE_SIZE);
		boff = pgoff & ~EVL_HEAP_PAGE_MASK;
		if ((boff & (bsize - 1)) != 0) /* Not at block start? */
			goto bad;

		n = boff >> log2size; /* Block position in page. */
		oldmap = ext->pagemap[pg].map;
		ext->pagemap[pg].map &= ~(1U << n);

		/*
		 * If the page the block was sitting on is fully idle,
		 * return it to the pool. Otherwise, check whether
		 * that page is transitioning from fully busy to
		 * partially busy state, in which case it should move
		 * toward the front of the per-bucket page list.
		 */
		if (ext->pagemap[pg].map == ~gen_block_mask(log2size)) {
			remove_page(heap, ext, pg, log2size);
			release_page_range(ext, pagenr_to_addr(ext, pg),
					   EVL_HEAP_PAGE_SIZE);
		} else if (oldmap == -1U)
			move_page_front(heap, ext, pg, log2size);
	}

	heap->used_size -= bsize;
out:
	evl_unlock_mutex(&heap->lock);

	return ret;
bad:
	ret = -EINVAL;
	goto out;
}

static inline int compare_range_by_size(const struct avlh *l, const struct avlh *r)
{
	struct evl_mem_range *rl = container_of(l, typeof(*rl), size_node);
	struct evl_mem_range *rr = container_of(r, typeof(*rl), size_node);

	return avl_sign((long)(rl->size - rr->size));
}
static DECLARE_AVL_SEARCH(search_range_by_size, compare_range_by_size);

static struct avl_searchops size_search_ops = {
	.search = search_range_by_size,
	.cmp = compare_range_by_size,
};

static inline int compare_range_by_addr(const struct avlh *l, const struct avlh *r)
{
	uintptr_t al = (uintptr_t)l, ar = (uintptr_t)r;

	return avl_cmp_sign(al, ar);
}
static DECLARE_AVL_SEARCH(search_range_by_addr, compare_range_by_addr);

static struct avl_searchops addr_search_ops = {
	.search = search_range_by_addr,
	.cmp = compare_range_by_addr,
};

static ssize_t add_extent(void *mem, size_t size)
{
	struct evl_heap_extent *ext;
	size_t user_size, overhead;
	int nrpages;

	/*
	 * @size must include the overhead memory we need for storing
	 * our meta data as calculated by EVL_HEAP_ARENA_SIZE(), find
	 * this amount back.
	 *
	 * o = overhead
	 * e = sizeof(evl_heap_extent)
	 * p = EVL_HEAP_PAGE_SIZE
	 * m = EVL_HEAP_PGMAP_BYTES
	 *
	 * o = align_to(((a * m + e * p) / (p + m)), minlog2)
	 */
	overhead = __align_to((size * EVL_HEAP_PGMAP_BYTES +
			       sizeof(*ext) * EVL_HEAP_PAGE_SIZE) /
			      (EVL_HEAP_PAGE_SIZE + EVL_HEAP_PGMAP_BYTES),
			      EVL_HEAP_MIN_ALIGN);

	user_size = size - overhead;
	if (user_size & ~EVL_HEAP_PAGE_MASK)
		return -EINVAL;

	if (user_size < EVL_HEAP_PAGE_SIZE ||
	    user_size > EVL_HEAP_MAX_EXTSZ)
		return -EINVAL;

	/*
	 * Setup an extent covering user_size bytes of user memory
	 * starting at @mem. user_size must be a multiple of
	 * EVL_HEAP_PAGE_SIZE.  The extent starts with a descriptor,
	 * followed by the array of page entries.
	 *
	 * Page entries contain per-page metadata for managing the
	 * page pool.
	 *
	 * +-------------------+ <= mem
	 * | extent descriptor |
	 * /...................\
	 * \...page entries[]../
	 * /...................\
	 * +-------------------+ <= extent->membase
	 * |                   |
	 * |                   |
	 * |    (page pool)    |
	 * |                   |
	 * |                   |
	 * +-------------------+
	 *                       <= extent->memlim == mem + size
	 */
	nrpages = user_size >> EVL_HEAP_PAGE_SHIFT;
	ext = mem;
	ext->membase = mem + overhead;
	ext->memlim = mem + size;

	memset(ext->pagemap, 0, nrpages * sizeof(struct evl_heap_pgentry));
	/*
	 * The free page pool is maintained as a set of ranges of
	 * contiguous pages indexed by address and size in AVL
	 * trees. Initially, we have a single range in those trees
	 * covering the whole user memory we have been given for the
	 * extent. Over time, that range will be split then possibly
	 * re-merged back as allocations and deallocations take place.
	 */
	avl_init(&ext->size_tree);
	avl_init(&ext->addr_tree);
	release_page_range(ext, ext->membase, user_size);

	return (ssize_t)user_size;
}

int evl_init_heap(struct evl_heap *heap, void *mem, size_t size)
{
	struct evl_heap_extent *ext = mem;
	ssize_t ret;
	int n;

	list_init(&heap->extents);

	ret = evl_new_mutex(&heap->lock, EVL_CLOCK_MONOTONIC, "heap:%.3d",
			atomic_add_return(&heap_serial, 1));
	if (ret < 0)
		return ret;

	/* Reset bucket page lists, all empty. */
	for (n = 0; n < EVL_HEAP_MAX_BUCKETS; n++)
		heap->buckets[n] = -1U;

	ret = add_extent(mem, size);
	if (ret < 0) {
		evl_close_mutex(&heap->lock);
		return ret;
	}

	list_append(&ext->next, &heap->extents);
	heap->arena_size = size;
	heap->usable_size = ret;
	heap->used_size = 0;

	return 0;
}

int evl_extend_heap(struct evl_heap *heap, void *mem, size_t size)
{
	struct evl_heap_extent *ext = mem;
	ssize_t ret;

	ret = add_extent(mem, size);
	if (ret < 0)
		return ret;

	evl_lock_mutex(&heap->lock);
	list_append(&ext->next, &heap->extents);
	heap->arena_size += size;
	heap->usable_size += ret;
	evl_unlock_mutex(&heap->lock);

	return 0;
}

void evl_destroy_heap(struct evl_heap *heap)
{
	evl_close_mutex(&heap->lock);
}
