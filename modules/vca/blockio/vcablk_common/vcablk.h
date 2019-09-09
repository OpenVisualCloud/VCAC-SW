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
 * Intel VCA Block IO driver
 */
#ifndef __VCA_BLK_TYPE_H__
#define __VCA_BLK_TYPE_H__

#define VCA_BLK_VERSION  0x04

#if defined(_MSC_VER)
#define VCA_ALIGNED_PREFIX(_N)  __declspec(align(_N))
#define VCA_ALIGNED_POSTFIX(_N)
#elif defined(__GNUC__)
#define VCA_ALIGNED_PREFIX(_N)
#define VCA_ALIGNED_POSTFIX(_N) __attribute__((aligned(_N)))
#else
#error "Prepare new align macros"
#endif

/*
 * VCA_RB ring buffer size need be power of 2.
 */
#define VCA_RB_COUNTER_EQ(cnt1, cnt2, size) (((cnt1) & (((size) << 1) - 1)) \
                                          == ((cnt2) & (((size) << 1) - 1)))

#define VCA_RB_COUNTER_ADD(cnt, val, size) \
						(((cnt) + (val)) & (((size) << 1) - 1))

#define VCA_RB_COUNTER_TO_IDX(cnt, size) ((cnt) & ((size) - 1))

#define VCA_RB_BUFF_USED(head_cnt, tail_cnt, size) ((tail_cnt >= head_cnt)? \
		(tail_cnt - head_cnt):(tail_cnt + ((size) << 1) - head_cnt))

#define VCA_RB_BUFF_EMPTY(head_cnt, tail_cnt) ((head_cnt) == (tail_cnt))

#define VCA_RB_BUFF_FULL(head_cnt, tail_cnt, size) \
		(!VCA_RB_BUFF_EMPTY(head_cnt, tail_cnt) && \
		VCA_RB_COUNTER_TO_IDX(head_cnt, size) == VCA_RB_COUNTER_TO_IDX(tail_cnt, size))

#define VCABLK_RING_GET_OBJ(cnt, num_elems, elems, type) \
			( & ( (type) (elems) ) \
			[(VCA_RB_COUNTER_TO_IDX((cnt), num_elems))] )

#endif /* __VCA_BLK_TYPE_H__ */
