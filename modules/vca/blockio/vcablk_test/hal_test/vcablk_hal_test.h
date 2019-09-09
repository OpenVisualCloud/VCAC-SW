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
#ifndef __VCA_BLK_IMP_LOCAL_COMMON_H__
#define __VCA_BLK_IMP_LOCAL_COMMON_H__


struct vcablk_impl_local_cbs
{
	struct vcablk_fe_callbacks {
		void (*irq_handler)(int db);
	} fe_callbacks;

	struct vcablk_be_callbacks {
		void (*irq_handler)(int id);
	} be_callbacks;
} __attribute__((aligned(8)));


#endif /* __VCA_BLK_IMP_LOCAL_COMMON_H__ */
