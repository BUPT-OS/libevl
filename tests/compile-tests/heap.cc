/*
 * SPDX-License-Identifier: MIT
 *
 * COMPILE-TESTING ONLY.
 */

#include <evl/heap.h>

int main(int argc, char *argv[])
{
	struct evl_heap heap;
	void *ptr;
	size_t n;

	evl_init_heap(&heap, NULL, 0);
	evl_extend_heap(&heap, NULL, 0);
	evl_destroy_heap(&heap);
	ptr = evl_alloc_block(&heap, 16);
	evl_free_block(&heap, ptr);
	evl_check_block(&heap, ptr);
	n = evl_heap_raw_size(&heap);
	n += evl_heap_size(&heap);
	n += evl_heap_used(&heap);
	
	return n ?: 0;
}
