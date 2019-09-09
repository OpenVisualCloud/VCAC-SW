/*
 * Intel VCA Software Stack (VCASS)
 *
 * Copyright(c) 2015-2017 Intel Corporation.
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
 * Intel PLX87XX VCA PCIe driver
 *
 * Implements ALM(A-LUT Manager).
 *
 */

#ifndef _PLX_ALM_H_
#define _PLX_ALM_H_

#include <linux/dmaengine.h>

/**
 * struct alm_arr_entry -  a single entry in A-LUT array.
 *
 * @value: value of translation bits
 * @counter: number of allocations that this entry contains.
 * @segments_count: number of continous segments that this entry contains ie.
	if segment size is 1MB and someone wants to allocate 4MB ,
	then the segment count would be 4
 * @start: id of first segment in this block
 */
struct plx_alm_arr_entry {
	u64 value;
	u16 counter;
	u16 segments_count;
	u16 start;
};

/**
 * struct plx_alm -  manager for handling A-LUT array.
 *
 * @segments_num: number of entries the A-LUT will manage.
 * @segment_size: size of a single segment in A-LUT
 * @entries: pointer to an array containing A-LUT entries
 */
struct plx_alm {
	u32 segments_num;
	u64 segment_size;
	struct plx_alm_arr_entry * entries;
};

int plx_alm_init(struct plx_alm*, struct pci_dev*, int segments_num, u64 aper_len);
void plx_alm_release(struct plx_alm*, struct pci_dev*);
void plx_alm_reset(struct plx_alm*, struct pci_dev*);

int plx_alm_add_entry(struct plx_alm*, struct pci_dev*, dma_addr_t addr,
					  size_t size, u32 *out_segment_id, u32 *out_segments_num);
void plx_alm_del_entry(struct plx_alm*, struct pci_dev*, u32 segment_id,
					  u32 * out_segment_id, u32 *out_segments_num);

#endif
