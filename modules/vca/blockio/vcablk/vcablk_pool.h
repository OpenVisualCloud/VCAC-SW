/*
 * Intel VCA Software Stack (VCASS)
 *
 * Copyright(c) 2016-2017 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Intel VCA Block IO driver.
 */
#ifndef __VCA_BLK_POOL_H__
#define __VCA_BLK_POOL_H__

#include <linux/vmalloc.h>

#define VCABLK_POOL_USED ((__u16)(~0))

struct vcablk_pool_head {
	__u16 next_free;
	char data[];
};

typedef struct {
	__u16 num;
	__u16 head;
	__u16 tail;

	size_t alloc_size;
	char alloc[];

} vcablk_pool_t;

#define get_pool_head_idx(pool, idx) \
		((struct vcablk_pool_head *) \
		((uintptr_t)(pool)->alloc + (idx) * (pool)->alloc_size))

#define get_pool_head_ptr(ptr) \
		((struct vcablk_pool_head *)((uintptr_t)(ptr) \
		- sizeof (struct vcablk_pool_head)))

/**
 * Loop all allocations
 * void * ptr - Pointer on allocation
 * vcablk_pool_t *pool - pool
 * int itr - iterator, int variable to internal use
 **/
#define vcablk_pool_foreach_all(ptr, pool, itr) \
	for(itr = 0; itr < pool->num; ++itr) \
	if((ptr = get_pool_head_idx(pool, itr)->data))

/* Loop all used allocations */
#define vcablk_pool_foreach_used(ptr, pool, itr) \
	for(itr = 0; itr < pool->num; ++itr) \
		if (get_pool_head_idx(pool, itr)->next_free == VCABLK_POOL_USED && \
		    (ptr = get_pool_head_idx(pool, itr)->data))

inline void vcablk_pool_clear(vcablk_pool_t *pool)
{
	__u16 i;
	memset (pool->alloc, 0, pool->alloc_size * pool->num);
	pool->head = 0;
	for (i = 0; i < pool->num - 1; ++i) {
		get_pool_head_idx(pool, i)->next_free = i + 1;
	}
	get_pool_head_idx(pool,  pool->num - 1)->next_free = 0;
	pool->tail = pool->num -1;
}

inline vcablk_pool_t *vcablk_pool_init(__u16 num, size_t size)
{
	size_t alloc_size = size + sizeof(struct vcablk_pool_head);
	vcablk_pool_t *pool;

	if (num >= VCABLK_POOL_USED || num +1 >= VCABLK_POOL_USED) {
		pr_err("%s: Number of elements too big: %u. Max is %u\n",
				__func__, num, VCABLK_POOL_USED -1);
		return NULL;
	}

	/* Add one element because one of elements in pool is always blocked */
	++num;

	pool = kmalloc(sizeof(vcablk_pool_t) + alloc_size * num, GFP_KERNEL);
	if (!pool)
		return NULL;
	memset (pool, 0, sizeof(vcablk_pool_t));
	pool->num = num;
	pool->alloc_size = alloc_size;
	vcablk_pool_clear(pool);
	return pool;
}

inline void vcablk_pool_deinit(vcablk_pool_t *pool)
{
	if (pool)
		kfree(pool);
}

inline void* vcablk_pool_get_by_id(vcablk_pool_t *pool, __u16 idx)
{
	void* ret = NULL;
	struct vcablk_pool_head *item = get_pool_head_idx(pool, idx);
	if (idx < pool->num && item->next_free == VCABLK_POOL_USED) {
		ret = item->data;
	} else {
		pr_err("%s: Incorrect element, not allocated or out of pool "
			"idx %i pool->num %i item->next_free %i item %p\n",
			__func__, idx, pool->num, item->next_free, item);
	}
	return ret;
}

inline bool vcablk_is_available(vcablk_pool_t *pool)
{
	return (pool->head != pool->tail);
}

/**
 * vcablk_pool_pop - get free element from pool
 * One element always left unusable. This is simple method to pop and push
 * elements in pool without locks.
 **/
inline void* vcablk_pool_pop(vcablk_pool_t *pool, __u16 *get_id)
{
	void* ret = NULL;
	if (pool->head != pool->tail) {
		struct vcablk_pool_head *item = get_pool_head_idx(pool, pool->head);
		if (get_id)
			*get_id = pool->head;
		pool->head = item->next_free;
		item->next_free = VCABLK_POOL_USED;
		ret = item->data;
	}
	return ret;
}

inline void  vcablk_pool_push(vcablk_pool_t *pool, void* ptr)
{
	__u16 idx;
	struct vcablk_pool_head *item;
	item = get_pool_head_ptr(ptr);
	idx = (__u16)(((uintptr_t)item - (uintptr_t)pool->alloc) / pool->alloc_size);

	if (!ptr || item->next_free != VCABLK_POOL_USED || pool->num <= idx) {
		pr_err("%s: Try push incorrect pointer!! pool->alloc_size %i "
				"ptr %p item %p idx %i pool->num %i item->next_free %i "
				"VCABLK_POOL_USED %i\n", __func__,
				(int)pool->alloc_size, ptr, item, idx, pool->num,
				item->next_free, VCABLK_POOL_USED);

		BUG_ON(!ptr);
		BUG_ON(item->next_free != VCABLK_POOL_USED);
		BUG_ON(pool->num <= idx);
	}

	item->next_free = 0;
	get_pool_head_idx(pool, pool->tail)->next_free = idx;
	pool->tail = idx;
}

#endif /* __VCA_BLK_POOL_H__ */
